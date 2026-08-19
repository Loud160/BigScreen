// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/PlaybackSession.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "BigScreen/Settings.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/PerformancePanel.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/CinemaEnvironment.hpp"
#include "BigScreen/ChromaMapDetector.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "BigScreen/UpDownShowcaseTimeline.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Vector3.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr std::string_view CinemaCycleSongName =
            "Big Screen Cinema Effects Cycle";
        constexpr std::string_view CinemaCycleFolderName =
            "Big Screen Cinema Test 00 - Quest Effects Cycle";
        constexpr double CinemaCyclePhaseSeconds = 10.0;
        constexpr std::size_t CinemaCyclePhaseCount = 14;
    }

    PlaybackSession& PlaybackSession::Instance()
    {
        // The mod is driven entirely by main-thread hooks, so one process-wide
        // session is enough. FrameDecoder owns the only background activity and
        // synchronizes its request/output mailboxes internally.
        static PlaybackSession session;
        return session;
    }

    const std::optional<std::string>& PlaybackSession::RequestedEnvironment() const
    {
        // Return a stable empty optional instead of exposing the entire parsed
        // configuration to the transition hook.
        static const std::optional<std::string> noEnvironment;
        return config_ ? config_->requestedEnvironment : noEnvironment;
    }

    bool PlaybackSession::MapperScreenPresentationActive() const
    {
        // The developer showcase is authored specifically around the complete
        // Chroma/Noodle environment. Treat it as mapper-owned presentation
        // even if the user's global Chroma override preference is off, so Big
        // Screen cannot force Big Mirror or remove lighting/side geometry.
        return showcaseEligible_ || cinemaCompatibilityCycleEligible_ ||
               (Settings::Instance().RespectMapperSettings() &&
                baseConfig_ &&
                baseConfig_->hasMapperScreenGeometry);
    }

    bool PlaybackSession::MapperEnvironmentPresentationActive() const
    {
        // Cinema environment instructions and Chroma environment ownership
        // are separate interoperability decisions. Respect Mapper Settings
        // applies Cinema's explicit environmentName/environment array even on
        // a Cinema-only map. Allow Chroma Override yields the broader scene to
        // Chroma only when map-wide detection confirms actual Chroma use.
        return showcaseEligible_ || cinemaCompatibilityCycleEligible_ ||
               (baseConfig_ &&
                ((Settings::Instance().RespectMapperSettings() &&
                  baseConfig_->hasMapperEnvironmentPresentation) ||
                 (Settings::Instance().AllowChromaOverride() &&
                  chromaMapDetected_)));
    }

    PlaybackDiagnostics PlaybackSession::Diagnostics() const
    {
        // Diagnostics belong to the currently active decoder session. Prepare()
        // calls Stop() before a new map, but FrameDecoder deliberately retains
        // its final worker CPU clock long enough for the just-finished results
        // and power benchmark to consume it. Returning that retained state for
        // a map with no video made the video-off control row inherit the prior
        // video's decoder CPU and frame totals. An inactive session therefore
        // has an explicitly empty diagnostic record.
        if(!started_)
            return {};

        const double activeDecoderCpu = std::max(
            0.0,
            decoder_.WorkerCpuMilliseconds() - decoderCpuBaselineMilliseconds_);
        const auto expectedFrames = CoreLogic::ReportablePresentationDeadlines(
            expectedPresentationDeadlines_,
            deliveredPresentedFrames_);
        return {
            decoder_.Width(), decoder_.Height(),
            decoder_.SourceFramesPerSecond(), effectiveFpsLimit_,
            requestedFrames_,
            expectedFrames,
            deliveredPresentedFrames_,
            presentationMisses_.MissedDeadlines(),
            decoder_.BufferAllocations(),
            decoder_.AverageDecodeMilliseconds(),
            decoder_.PeakDecodeMilliseconds(),
            decoder_.OpenMilliseconds(),
            activeDecoderCpu,
            automaticReductions_,
            decoder_.DecodeMethodName(),
            decoder_.RuntimeVersion(),
            decoder_.CodecName()};
    }

    void PlaybackSession::Prepare(GlobalNamespace::BeatmapLevel* level)
    {
        Stop();
        lastResultsData_.reset();
        baseConfig_.reset();
        config_.reset();
        levelDirectory_.clear();
        preparedLevelId_.clear();
        preparedSongName_.clear();
        preparedSongArtist_.clear();
        preparedCharacteristic_.clear();
        preparedDifficulty_ = -1;
        showcaseEligible_ = false;
        cinemaCompatibilityCycleEligible_ = false;
        cinemaCompatibilityCycleScreenHidden_ = false;
        cinemaCompatibilityCyclePhase_ = 0;
        cinemaCompatibilityCycleConfigs_.clear();
        chromaMapDetected_ = false;
        menuPreviewStartSongTime_ = 0.0;
        environmentOnlySession_ = false;

        // Hooks remain installed for the lifetime of the process, but the
        // master switch makes every entry point inert. Keeping this guard here
        // as well as in callers prevents future code from accidentally parsing
        // map files while Big Screen is disabled.
        if(!Settings::Instance().ModEnabled())
            return;

        if(!level || !level->levelID)
            return;

        const std::string levelId(level->levelID);
        preparedLevelId_ = levelId;
        preparedSongName_ = level->songName
            ? std::string(level->songName)
            : std::string("Unknown song");
        preparedSongArtist_ = level->songAuthorName
            ? std::string(level->songAuthorName)
            : std::string("Unknown artist");
        menuPreviewStartSongTime_ = std::max(
            0.0,
            static_cast<double>(level->previewStartTime));
        const auto descriptor = VideoLibrary::Instance().Describe(level);
        baseConfig_ = descriptor.playableConfig;
        if(!baseConfig_ && descriptor.mapperDefinition &&
           descriptor.mapperDefinition->forceEnvironmentModifications &&
           descriptor.mapperDefinition->hasMapperEnvironmentPresentation)
        {
            // PC Cinema deliberately permits environment-only mapper files.
            // Keep them out of FFmpeg while still allowing the gameplay scene
            // transition and delayed environment pass to honor the metadata.
            baseConfig_ = descriptor.mapperDefinition;
            environmentOnlySession_ = true;
        }

        if(baseConfig_)
        {
            // Resolve the real map folder only after confirming this level has
            // a playable video. Maps without videos must remain completely
            // unaffected by every Big Screen environment decision.
            if(auto* custom = SongCore::API::Loading::GetLevelByLevelID(levelId))
            {
                levelDirectory_ = std::filesystem::path(
                    custom->get_customLevelPath());
                std::string chromaReason;
                chromaMapDetected_ = ChromaMapDetector::UsesChroma(
                    levelDirectory_, chromaReason);
                if(chromaMapDetected_)
                {
                    PaperLogger.info(
                        "Allow Chroma Override detected map-wide Chroma use: {}",
                        chromaReason);
                }
            }
            cinemaCompatibilityCycleEligible_ =
                preparedSongName_ == CinemaCycleSongName &&
                levelDirectory_.filename().string() == CinemaCycleFolderName &&
                LoadCinemaCompatibilityCycle();
            RefreshDisplaySettings();

            const std::string preparedKind = environmentOnlySession_
                ? "Cinema environment-only presentation"
                : "video '" + config_->videoPath.filename().string() + "'";
            PaperLogger.info(
                "Prepared {} from '{}'",
                preparedKind,
                config_->metadataPath.filename().string());
        }
        else
        {
            PaperLogger.debug("No playable video for selected level '{}'", levelId);
        }
    }

    void PlaybackSession::ConfigureGameplayBeatmap(
        const std::string& characteristic,
        int difficulty)
    {
        preparedCharacteristic_ = characteristic;
        preparedDifficulty_ = difficulty;
        showcaseEligible_ = baseConfig_ && UpDownShowcase::MatchesTarget(
            preparedSongName_,
            preparedSongArtist_,
            levelDirectory_.filename().string(),
            preparedCharacteristic_,
            preparedDifficulty_);
        if(showcaseEligible_)
        {
            PaperLogger.info(
                "Activated private Up & Down showcase for level '{}', characteristic '{}', difficulty {}",
                preparedLevelId_,
                preparedCharacteristic_,
                preparedDifficulty_);
        }
    }

    void PlaybackSession::RefreshDisplaySettings()
    {
        // A surface must never outlive the effective configuration that
        // created it. Menu setting changes normally arrive after the preview
        // stopped playback, but this guard keeps future callers safe as well.
        if(started_)
            Stop();

        RebuildEffectiveConfig();
    }

    bool PlaybackSession::ApplyLibraryPreviewEditorDisplay(
        const MapVideoConfig& displayConfig,
        bool rebuildGeometry)
    {
        if(!started_ || context_ != PlaybackContext::LibraryPreview)
            return false;

        if(rebuildGeometry && !surface_.UpdateGeometry(displayConfig))
            return false;
        if(!rebuildGeometry)
        {
            surface_.SetWorldTransform(
                {
                    displayConfig.screenPosition.x,
                    displayConfig.screenPosition.y,
                    displayConfig.screenPosition.z},
                UnityEngine::Quaternion::Euler({
                    displayConfig.screenRotation.x,
                    displayConfig.screenRotation.y,
                    displayConfig.screenRotation.z}));
        }
        return true;
    }

    bool PlaybackSession::RestoreLibraryPreviewDisplay(
        bool useLatestSettings)
    {
        if(!started_ || context_ != PlaybackContext::LibraryPreview)
            return false;
        if(useLatestSettings)
            RebuildEffectiveConfig(PlaybackContext::LibraryPreview);
        return config_ && surface_.UpdateGeometry(*config_);
    }

    void PlaybackSession::RebuildEffectiveConfig(
        PlaybackContext intendedContext)
    {

        if(!baseConfig_)
        {
            config_.reset();
            return;
        }

        config_ = *baseConfig_;

        // Always derive from the immutable mapper baseline. This makes Reset
        // to Defaults deterministic and prevents repeated X/Y/Z, tilt, or
        // scale adjustments from accumulating on the selected song.
        const auto& settings = Settings::Instance();
        const auto applyUserVideoControls = [&settings, this](bool applyOpacity)
        {
            // Chroma/Cinema may own the canvas transform, but Big Screen's
            // Video Controls describe how the decoded picture is composed
            // inside that canvas. Keeping these independent lets a mapper
            // place/size/curve the screen without silently disabling the
            // user's rotation, zoom, pan, tilt, or stretch controls.
            if(applyOpacity)
                config_->videoOpacity = settings.VideoOpacity();
            if(settings.AdvancedOptionsEnabled())
            {
                config_->videoRotation = settings.VideoRotation();
                config_->videoZoom = settings.VideoZoom();
                config_->videoOffsetX = settings.VideoOffsetX();
                config_->videoOffsetY = settings.VideoOffsetY();
                config_->videoTilt = settings.VideoTilt();
                config_->stretchVideoToFit = settings.StretchVideoToFit();
            }
        };
        // Cinema screen ownership and Chroma environment ownership are
        // independent. A mapper-authored Cinema canvas does not require
        // Chroma, and merely using Chroma must not make the player's Screen
        // Canvas controls inert.
        (void)intendedContext;
        if(MapperScreenPresentationActive())
        {
            // Cinema/Chroma compatibility is deliberately all-or-nothing for
            // geometry. Mixing a mapper's close, angled screen with a user's
            // back-wall offsets is the exact failure this opt-in prevents.
            // Mapper ownership is limited to the canvas and environment.
            // Letterbox Transparency, Video Opacity, and the transforms under
            // Video Controls remain player-owned, including when Cinema's
            // legacy `transparency` field is present. Treating that field as a
            // forced black background made the visible control lie and left
            // black bars around videos in mapper-positioned screens.
            if(config_->mapperTransparency)
            {
                config_->opaqueScreenBody = !*config_->mapperTransparency;
            }
            config_->letterboxTransparent =
                (config_->mapperTransparency.has_value() &&
                 *config_->mapperTransparency) ||
                config_->vignette.has_value() ||
                (settings.AdvancedOptionsEnabled() &&
                 settings.LetterboxTransparencyEnabled());
            if(!config_->cinemaCurvatureDegrees)
            {
                config_->screenCurvature = settings.CurvedScreenEnabled()
                    ? settings.ScreenCurvature() : 0.0f;
                config_->maintainAspectRatioWhenCurved =
                    settings.CurvedScreenEnabled() &&
                    settings.MaintainCurveAspectRatio();
            }
            applyUserVideoControls(true);
            PaperLogger.info(
                "Respect Mapper Settings is applying this map's Cinema screen presentation");
            return;
        }

        // Turning mapper control off means returning to Big Screen's neutral
        // back-wall canvas, not merely applying user offsets to the mapper's
        // authored X/Y/Z and scale. The latter left Chroma screens at their
        // custom location even after the visible size changed.
        if(config_->hasMapperPresentation)
        {
            config_->ResetScreenGeometryToDefaults();
            if(!settings.RespectMapperSettings())
                config_->ResetMapperVisualEffects();
        }

        const auto& layout = settings.ActiveLayout();
        if(settings.AdvancedOptionsEnabled() && layout.undocked)
        {
            config_->screenPosition = {
                layout.undockedPositionX,
                layout.undockedPositionY,
                layout.undockedPositionZ};
            config_->screenRotation = {
                layout.undockedRotationX,
                layout.undockedRotationY,
                layout.undockedRotationZ};
            config_->screenHeight = layout.undockedHeight;
            config_->screenWidthOverride = layout.undockedWidth;
        }
        else
        {
            config_->screenPosition.x += settings.ScreenHorizontalOffset();
            config_->screenPosition.y += settings.ScreenVerticalOffset();
            config_->screenPosition.z += settings.ScreenDistanceOffset();
            config_->screenRotation.x += settings.ScreenTiltOffset();
            if(settings.AdvancedOptionsEnabled())
                config_->screenRotation.z += settings.ScreenRoll();
            config_->screenHeight *= settings.ScreenScale();
            config_->screenWidthOverride.reset();
        }
        config_->screenCurvature = settings.CurvedScreenEnabled()
            ? settings.ScreenCurvature()
            : 0.0f;
        config_->maintainAspectRatioWhenCurved =
            settings.CurvedScreenEnabled() &&
            settings.MaintainCurveAspectRatio();
        // Letterbox transparency is advanced; picture opacity is deliberately
        // a basic per-layout control and therefore applies in both modes.
        if(settings.RespectMapperSettings() && config_->mapperTransparency)
        {
            config_->opaqueScreenBody = !*config_->mapperTransparency;
            config_->letterboxTransparent =
                *config_->mapperTransparency ||
                config_->vignette.has_value() ||
                (settings.AdvancedOptionsEnabled() &&
                 settings.LetterboxTransparencyEnabled());
            applyUserVideoControls(true);
        }
        else
        {
            config_->letterboxTransparent = settings.AdvancedOptionsEnabled() &&
                settings.LetterboxTransparencyEnabled();
            applyUserVideoControls(true);
        }
    }

    bool PlaybackSession::ApplyActiveScreenLayoutLive()
    {
        // Mapper/Chroma presentation intentionally ignores user layouts. The
        // pause control is hidden for that case, but keep this guard here so a
        // stale callback can never override the mapper's live screen.
        if(!IsGameplayActive() || !baseConfig_ ||
           MapperScreenPresentationActive())
            return false;

        const auto previousConfig = config_;
        RebuildEffectiveConfig();
        if(!config_ || !surface_.UpdateGeometry(*config_))
        {
            config_ = previousConfig;
            ErrorManager::Instance().ReportInternal(
                "switching the paused screen layout",
                "Unity could not rebuild the video screen geometry");
            return false;
        }

        PaperLogger.info(
            "Applied Screen Layout {} to the paused gameplay video without restarting playback",
            Settings::Instance().ActiveScreenLayout() + 1);
        return true;
    }

    void PlaybackSession::SetGameplayScreenEnabled(bool enabled)
    {
        if(!IsGameplayActive())
            return;

        gameplayScreenEnabled_ = enabled;
        if(enabled)
        {
            // Do not briefly reveal the last frame from before the screen was
            // hidden. Keep the surface invisible until the decoder supplies a
            // frame for Beat Saber's current song time.
            firstFrameUploaded_ = false;
            surface_.SetVisible(false);
            cinemaScreens_.SetVisible(false);
            showcase_.SetVisible(false);
            showcase_.SetMediaReady(false);
            lastPresentationSlot_.reset();
        }
        else
        {
            surface_.SetVisible(false);
            cinemaScreens_.SetVisible(false);
            showcase_.SetVisible(false);
        }
        PaperLogger.info(
            "Paused gameplay Video Screen changed to {} for the current map",
            enabled ? "On" : "Off");
    }

    void PlaybackSession::Start(PlaybackContext context)
    {
        if(!Settings::Instance().ModEnabled() ||
           !config_ ||
           started_ ||
           context == PlaybackContext::None)
            return;

        // Prepare() cannot know which preview owns the upcoming surface. In
        // the Video Library, rebuild once with that context so the decoded
        // picture is placed on the editable user canvas rather than on a
        // mapper-authored back-wall screen.
        RebuildEffectiveConfig(context);
        if(!config_)
            return;

        if(environmentOnlySession_)
        {
            // Environment-only metadata is meaningful during gameplay only.
            // Menu previews have no gameplay environment to modify and must
            // remain an inert no-video selection.
            if(context != PlaybackContext::Gameplay)
                return;
            if(Settings::Instance().RespectMapperSettings() &&
               !config_->environmentModifications.empty())
                CinemaEnvironment::Prepare(*config_);
            started_ = true;
            context_ = context;
            mapperEnvironmentApplyCountdown_ =
                MapperEnvironmentPresentationActive() ? 3 : 0;
            PaperLogger.info(
                "Started Cinema environment-only gameplay presentation");
            return;
        }

        std::string error;
        if(context == PlaybackContext::Gameplay && gameplayPrewarmFailed_)
        {
            gameplayPrewarmFailed_ = false;
            ErrorManager::Instance().ReportUserVisible(
                "Video playback error",
                "Big Screen could not prepare this video's stream. " +
                    gameplayPrewarmError_);
            gameplayPrewarmError_.clear();
            return;
        }
        if(!(context == PlaybackContext::Gameplay && gameplayDecoderPrewarmed_) &&
           !OpenDecoder(error))
        {
            PaperLogger.error("Could not start video playback: {}", error);
            ErrorManager::Instance().ReportUserVisible(
                "Video playback error",
                "Big Screen could not open this video's stream. " + error);
            return;
        }
        gameplayDecoderPrewarmed_ = false;
        gameplayPrewarmFailed_ = false;
        gameplayPrewarmError_.clear();

        if(!surface_.Create(*config_, decoder_.Width(), decoder_.Height()))
        {
            PaperLogger.error("Could not create the Unity video screen");
            ErrorManager::Instance().ReportInternal(
                "creating video screen", "Unity could not create the screen surface");
            decoder_.Close();
            return;
        }

        if(MapperScreenPresentationActive() &&
           !config_->additionalScreens.empty() &&
           !cinemaScreens_.Create(
               *config_,
               decoder_.Width(),
               decoder_.Height(),
               surface_.Texture()))
        {
            // Additional panels are presentation enhancement, not a reason to
            // interrupt a playable map. The primary screen remains available
            // and the failure is retained in the diagnostic log.
            PaperLogger.error(
                "Cinema additional screens could not be created; continuing with the primary screen");
        }

        if(context == PlaybackContext::Gameplay &&
           Settings::Instance().RespectMapperSettings() &&
           !config_->environmentModifications.empty())
        {
            // Create clones before the delayed Chroma environment pass. Their
            // temporary position controls whether Chroma merges their prop
            // groups; CinemaEnvironment::Apply restores final placement.
            CinemaEnvironment::Prepare(*config_);
        }

        if(context == PlaybackContext::Gameplay && showcaseEligible_)
        {
            if(!showcase_.Create(
                   *config_,
                   decoder_.Width(),
                   decoder_.Height(),
                   surface_.Texture()))
            {
                // This optional spectacle must never cost the player a map.
                // Ordinary playback remains live on the primary surface.
                PaperLogger.error(
                    "Up & Down showcase panel creation failed; continuing with the ordinary single screen");
            }
            else
            {
                surface_.SetVisible(false);
                cinemaScreens_.SetVisible(false);
            }
        }

        started_ = true;
        playbackFailed_ = false;
        gameplayScreenEnabled_ = true;
        firstFrameUploaded_ = false;
        libraryPreviewRestartPending_ = false;
        libraryPreviewRestartReopenAttempted_ = false;
        libraryPreviewRestartGeneration_ = 0;
        appliedMapperEndFade_ = 1.0f;
        lastPresentationSlot_.reset();
        lastTickSongTime_ = 0.0;
        context_ = context;
        requestedFrames_ = 0;
        deliveredPresentedFrames_ = 0;
        expectedPresentationDeadlines_ = 0;
        expectedPresentationFraction_ = 0.0;
        presentationMisses_.Reset();
        windowDeliveredPresentedFrames_ = 0;
        windowExpectedPresentationDeadlines_ = 0;
        windowExpectedPresentationFraction_ = 0.0;
        performanceWindowStartSongTime_ = 0.0;
        diagnosticsWindowDeliveredPresentedFrames_ = 0;
        diagnosticsWindowExpectedPresentationDeadlines_ = 0;
        diagnosticsWindowExpectedPresentationFraction_ = 0.0;
        diagnosticsWindowStartSongTime_ = 0.0;
        minimumFrameSeconds_ = 0.0;
        maximumFrameSeconds_ = 0.0;
        totalFrameSeconds_ = 0.0;
        lastFpsSongTime_ = 0.0;
        sampledFrames_ = 0;
        gameplayFrameSamplingFinished_ = false;
        automaticReductions_ = 0;
        cinemaCompatibilityCyclePhase_ = 0;
        cinemaCompatibilityCycleScreenHidden_ = false;
        // Gameplay prewarming deliberately runs before Beat Saber's song
        // clock. Exclude that setup work from the map benchmark so video-on
        // and video-off runs cover the same measured interval.
        decoderCpuBaselineMilliseconds_ = decoder_.WorkerCpuMilliseconds();
        diagnosticsFrameCounter_ = 0;
        diagnosticsVisible_ = false;
        mapperEnvironmentApplyCountdown_ =
            context == PlaybackContext::Gameplay &&
            MapperEnvironmentPresentationActive()
                ? 3 : 0;

        if(context == PlaybackContext::Gameplay &&
           Settings::Instance().PerformanceDiagnosticsEnabled())
            PerformancePanel::Instance().ActivateGameplay();

        // SongPreviewPlayer begins custom-song previews at BeatmapLevel's
        // previewStartTime rather than zero. Start the decoder worker toward
        // that exact media position immediately, before the first audio Update
        // reaches Big Screen. This is especially important for downloaded
        // YouTube streams whose previous H.264 keyframe may be several seconds
        // before the requested preview point.
        const double initialSongTime = context == PlaybackContext::MenuPreview
            ? menuPreviewStartSongTime_
            : 0.0;
        ResetAutomaticPerformanceController(initialSongTime);
        const double initialMediaTime = config_->MediaTimeForSong(
            initialSongTime,
            decoder_.DurationSeconds());
        const bool initialTimeIsVisible =
            initialMediaTime >= 0.0 &&
            (!config_->stopAtVideoSecond ||
             initialMediaTime <= *config_->stopAtVideoSecond);
        if(initialTimeIsVisible)
        {
            const int fpsLimit = std::max(
                1,
                effectiveFpsLimit_);
            decoder_.Request(initialMediaTime);
            ++requestedFrames_;
            lastPresentationSlot_ = static_cast<std::int64_t>(std::floor(
                initialSongTime * fpsLimit + 0.000001));
            lastTickSongTime_ = initialSongTime;
        }
        PaperLogger.info(
            "Started {}x{} {} video screen at no more than {} FPS (duration {:.3f}s, FFmpeg {})",
            decoder_.Width(),
            decoder_.Height(),
            context == PlaybackContext::MenuPreview ? "song-menu" :
                context == PlaybackContext::LibraryPreview ? "library-preview" : "gameplay",
            effectiveFpsLimit_,
            decoder_.DurationSeconds(),
            decoder_.RuntimeVersion());
        if(context == PlaybackContext::LibraryPreview &&
           Settings::Instance().PerformanceDiagnosticsEnabled())
            PerformancePanel::Instance().ShowWaitingMessage();
    }

    void PlaybackSession::BeginLibraryPreviewMeasurement(double songTimeSeconds)
    {
        if(!started_ || context_ != PlaybackContext::LibraryPreview ||
           !firstFrameUploaded_)
            return;

        // Library preview opens the decoder and uploads a stationary first
        // picture before its audio clock starts. Discard that untimed setup
        // from every panel counter, exactly as gameplay excludes work performed
        // by PrewarmGameplay before Start() establishes its measured session.
        requestedFrames_ = 0;
        deliveredPresentedFrames_ = 0;
        expectedPresentationDeadlines_ = 0;
        expectedPresentationFraction_ = 0.0;
        presentationMisses_.Reset();
        windowDeliveredPresentedFrames_ = 0;
        windowExpectedPresentationDeadlines_ = 0;
        windowExpectedPresentationFraction_ = 0.0;
        performanceWindowStartSongTime_ = songTimeSeconds;
        diagnosticsWindowDeliveredPresentedFrames_ = 0;
        diagnosticsWindowExpectedPresentationDeadlines_ = 0;
        diagnosticsWindowExpectedPresentationFraction_ = 0.0;
        diagnosticsWindowStartSongTime_ = songTimeSeconds;
        minimumFrameSeconds_ = 0.0;
        maximumFrameSeconds_ = 0.0;
        totalFrameSeconds_ = 0.0;
        lastFpsSongTime_ = songTimeSeconds;
        sampledFrames_ = 0;
        automaticReductions_ = 0;
        decoderCpuBaselineMilliseconds_ = decoder_.WorkerCpuMilliseconds();
        ResetAutomaticPerformanceController(songTimeSeconds);
        diagnosticsFrameCounter_ = 0;
        decoder_.ResetPeakDecodeMilliseconds();
        PaperLogger.info(
            "Started Video Library performance measurement after decoder prewarm");
    }

    bool PlaybackSession::SynchronizedAudioReady(double songTimeSeconds) const
    {
        if(!started_ || !config_)
            return false;
        const double mediaTime = config_->MediaTimeForSong(
            songTimeSeconds,
            decoder_.DurationSeconds());
        const bool mediaPastConfiguredEnd = config_->stopAtVideoSecond &&
            mediaTime >= *config_->stopAtVideoSecond;
        return CoreLogic::SynchronizedPreviewReady(
            mediaTime,
            mediaPastConfiguredEnd,
            firstFrameUploaded_);
    }

    bool PlaybackSession::RestartLibraryPreview(double songTimeSeconds)
    {
        if(!started_ || context_ != PlaybackContext::LibraryPreview ||
           !config_ || playbackFailed_ || !decoder_.IsOpen())
            return false;

        const double mediaTime = config_->MediaTimeForSong(
            songTimeSeconds,
            decoder_.DurationSeconds());

        // This invalidates the exact state StartPreviewAudio consults. Merely
        // sending a backwards clock request left firstFrameUploaded_ true from
        // the completed pass, allowing the song to restart while MediaCodec
        // was still drained at EOF.
        firstFrameUploaded_ = false;
        libraryPreviewRestartPending_ = mediaTime >= 0.0;
        libraryPreviewRestartReopenAttempted_ = false;
        libraryPreviewRestartStarted_ = std::chrono::steady_clock::now();
        lastPresentationSlot_.reset();
        lastTickSongTime_ = songTimeSeconds;
        expectedPresentationFraction_ = 0.0;
        ResetAutomaticPerformanceWindow(songTimeSeconds);
        ResetAutomaticPerformanceController(songTimeSeconds);
        libraryPreviewRestartGeneration_ = decoder_.Restart(
            std::max(0.0, mediaTime));
        PaperLogger.info(
            "Restarting Video Library decoder at media time {:.3f}; audio will wait for the new opening frame",
            std::max(0.0, mediaTime));
        return true;
    }

    bool PlaybackSession::ReopenLibraryPreviewDecoder(double mediaTime)
    {
        std::string error;
        if(!OpenDecoder(error))
        {
            playbackFailed_ = true;
            surface_.SetVisible(false);
            cinemaScreens_.SetVisible(false);
            PaperLogger.error(
                "Video Library decoder could not recover after EOF: {}",
                error);
            ErrorManager::Instance().ReportUserVisible(
                "Video preview could not restart",
                "Big Screen could not reopen this video's decoder. Leave and reopen the song to try again. " +
                    error);
            return false;
        }

        // Keep the existing Unity screen and texture. Only the codec state is
        // replaced, which avoids material churn during a normal preview loop
        // and isolates MediaCodec implementations that cannot resume after a
        // drained EOF flush.
        libraryPreviewRestartGeneration_ = decoder_.Restart(
            std::max(0.0, mediaTime));
        libraryPreviewRestartStarted_ = std::chrono::steady_clock::now();
        libraryPreviewRestartReopenAttempted_ = true;
        PaperLogger.warn(
            "Reopened the Video Library decoder after its EOF seek produced no frame");
        return true;
    }

    bool PlaybackSession::OpenDecoder(std::string& error)
    {
        effectiveFpsLimit_ = Settings::Instance().PlaybackFpsLimit();
        return decoder_.Open(
            config_->videoPath,
            UncappedOutputHeight,
            VisualEffectsFor(*config_),
            error);
    }

    FrameVisualEffects PlaybackSession::VisualEffectsFor(
        const MapVideoConfig& config)
    {
        FrameVisualEffects effects;
        if(config.colorCorrection)
        {
            const auto& correction = *config.colorCorrection;
            effects.brightness = correction.brightness;
            effects.contrast = correction.contrast;
            effects.saturation = correction.saturation;
            effects.hue = correction.hue;
            effects.exposure = correction.exposure;
            effects.gamma = correction.gamma;
            // A mapper may include an all-default colorCorrection object. Do
            // not turn that harmless metadata into a full RGBA per-pixel pass
            // on every decoded picture.
            constexpr float EffectEpsilon = 0.0001f;
            effects.enabled =
                std::abs(correction.brightness - 1.0f) > EffectEpsilon ||
                std::abs(correction.contrast - 1.0f) > EffectEpsilon ||
                std::abs(correction.saturation - 1.0f) > EffectEpsilon ||
                std::abs(correction.hue) > EffectEpsilon ||
                std::abs(correction.exposure - 1.0f) > EffectEpsilon ||
                std::abs(correction.gamma - 1.0f) > EffectEpsilon;
        }
        if(config.vignette)
        {
            effects.enabled = true;
            effects.vignetteEnabled = true;
            effects.vignetteElliptical =
                config.vignette->type == "elliptical";
            effects.vignetteRadius = config.vignette->radius;
            effects.vignetteSoftness = config.vignette->softness;
        }
        return effects;
    }

    bool PlaybackSession::LoadCinemaCompatibilityCycle()
    {
        cinemaCompatibilityCycleConfigs_.clear();
        cinemaCompatibilityCycleConfigs_.reserve(CinemaCyclePhaseCount);
        for(std::size_t index = 0; index < CinemaCyclePhaseCount; ++index)
        {
            std::ostringstream fileName;
            fileName << "cinema-cycle-" << std::setw(2) << std::setfill('0')
                     << index + 1 << ".json";
            std::string error;
            auto phase = MapVideoConfig::LoadDefinitionFromFile(
                levelDirectory_, levelDirectory_ / fileName.str(), error);
            if(!phase || !phase->HasLocalVideo())
            {
                PaperLogger.error(
                    "Cinema compatibility cycle phase {} could not be loaded: {}",
                    index + 1,
                    error.empty() ? "its local MP4 was missing" : error);
                cinemaCompatibilityCycleConfigs_.clear();
                return false;
            }
            cinemaCompatibilityCycleConfigs_.push_back(std::move(*phase));
        }
        PaperLogger.info(
            "Prepared {} Cinema compatibility phases at {:.0f} seconds each",
            cinemaCompatibilityCycleConfigs_.size(),
            CinemaCyclePhaseSeconds);
        return true;
    }

    bool PlaybackSession::ApplyCinemaCompatibilityCyclePhase(
        std::size_t phaseIndex,
        double songTimeSeconds)
    {
        if(phaseIndex >= cinemaCompatibilityCycleConfigs_.size())
            return false;

        cinemaCompatibilityCyclePhase_ = phaseIndex;
        config_ = cinemaCompatibilityCycleConfigs_[phaseIndex];
        decoder_.UpdateVisualEffects(VisualEffectsFor(*config_));
        appliedMapperEndFade_ = 1.0f;
        lastPresentationSlot_.reset();
        firstFrameUploaded_ = false;

        // Every environment phase starts from the scene's original state.
        // Cleanup now restores non-clone transforms/visibility in addition to
        // destroying clones, so the ten-second tests cannot contaminate one
        // another.
        CinemaEnvironment::Cleanup();
        if(!config_->environmentModifications.empty())
        {
            CinemaEnvironment::Prepare(*config_);
            CinemaEnvironment::Apply(*config_);
        }

        cinemaScreens_.Destroy();
        if(phaseIndex + 1 == CinemaCyclePhaseCount)
        {
            // Test 14 is intentionally environment-only. Keep the valid
            // decoder warm but issue no requests/uploads during this final
            // phase, exactly matching its expected absence of a screen.
            cinemaCompatibilityCycleScreenHidden_ = true;
            surface_.SetVisible(false);
            PaperLogger.info(
                "Cinema compatibility phase {}/{}: environment-only screen hidden",
                phaseIndex + 1,
                CinemaCyclePhaseCount);
            return true;
        }

        cinemaCompatibilityCycleScreenHidden_ = false;
        surface_.Destroy();
        if(!surface_.Create(*config_, decoder_.Width(), decoder_.Height()))
        {
            PaperLogger.error(
                "Cinema compatibility phase {} could not rebuild the primary screen",
                phaseIndex + 1);
            return false;
        }
        if(!config_->additionalScreens.empty() &&
           !cinemaScreens_.Create(
               *config_, decoder_.Width(), decoder_.Height(), surface_.Texture()))
        {
            PaperLogger.error(
                "Cinema compatibility phase {} could not create its additional screens",
                phaseIndex + 1);
        }
        decoder_.Request(std::max(0.0, songTimeSeconds));
        PaperLogger.info(
            "Cinema compatibility phase {}/{} applied from '{}'",
            phaseIndex + 1,
            CinemaCyclePhaseCount,
            config_->metadataPath.filename().string());
        return true;
    }

    void PlaybackSession::PrewarmGameplay()
    {
        if(!Settings::Instance().ModEnabled() || !config_ || started_ ||
           environmentOnlySession_)
            return;
        std::string error;
        gameplayDecoderPrewarmed_ = OpenDecoder(error);
        if(!gameplayDecoderPrewarmed_)
        {
            gameplayPrewarmFailed_ = true;
            gameplayPrewarmError_ = error;
            PaperLogger.error("Could not prewarm gameplay video: {}", error);
            // Start() reports this after AudioTimeSyncController has marked
            // gameplay active. ErrorManager can then defer the dialog until
            // the map ends instead of interrupting the scene transition.
            return;
        }
        const double initialMediaTime = config_->MediaTimeForSong(
            0.0, decoder_.DurationSeconds());
        if(initialMediaTime >= 0.0)
            decoder_.Request(initialMediaTime);
        PaperLogger.info("Prewarmed gameplay video decoder before scene activation");
    }

    void PlaybackSession::Tick(double songTimeSeconds)
    {
        if(!Settings::Instance().ModEnabled() || !started_ || !config_)
            return;

        if(cinemaCompatibilityCycleEligible_ &&
           context_ == PlaybackContext::Gameplay &&
           !cinemaCompatibilityCycleConfigs_.empty())
        {
            const auto requestedPhase = std::min(
                static_cast<std::size_t>(std::max(
                    0.0,
                    std::floor(songTimeSeconds / CinemaCyclePhaseSeconds))),
                cinemaCompatibilityCycleConfigs_.size() - 1);
            if(requestedPhase != cinemaCompatibilityCyclePhase_)
            {
                if(!ApplyCinemaCompatibilityCyclePhase(
                       requestedPhase, songTimeSeconds))
                {
                    playbackFailed_ = true;
                    surface_.SetVisible(false);
                    cinemaScreens_.SetVisible(false);
                    ErrorManager::Instance().ReportInternal(
                        "changing the Cinema compatibility test phase",
                        "The test screen could not be rebuilt. Beat Saber will continue without video.");
                    return;
                }
            }
            if(cinemaCompatibilityCycleScreenHidden_)
            {
                lastTickSongTime_ = songTimeSeconds;
                return;
            }
        }

        if(environmentOnlySession_)
        {
            if(mapperEnvironmentApplyCountdown_ > 0 &&
               --mapperEnvironmentApplyCountdown_ == 0)
                CinemaEnvironment::Apply(*config_);
            return;
        }

        const bool diagnosticsContext =
            context_ == PlaybackContext::Gameplay ||
            context_ == PlaybackContext::LibraryPreview;
        if(diagnosticsContext &&
           Settings::Instance().PerformanceDiagnosticsEnabled())
        {
            // Both live contexts use the same ten-second warmup boundary so
            // their headset-FPS figures describe comparable steady playback.
            // Gameplay additionally stops after the final playable note;
            // Library preview has no beatmap-note end boundary.
            if(context_ == PlaybackContext::Gameplay &&
               gameplayLastNoteTime_ &&
               songTimeSeconds > *gameplayLastNoteTime_)
                gameplayFrameSamplingFinished_ = true;
            const bool maySamplePlaybackFrame =
                CoreLogic::ShouldSampleGameplayFrame(
                    songTimeSeconds,
                    context_ == PlaybackContext::Gameplay
                        ? gameplayLastNoteTime_
                        : std::nullopt,
                    context_ == PlaybackContext::Gameplay &&
                        gameplayFrameSamplingFinished_);
            const double frameSeconds = UnityEngine::Time::get_unscaledDeltaTime();
            const double songAdvance =
                songTimeSeconds - lastFpsSongTime_;
            lastFpsSongTime_ = songTimeSeconds;
            // A focused Quest frame is normally well below 100 ms. Excluding
            // longer gaps and frames where Beat Saber's song clock did not
            // advance prevents pauses, headset sleep, and the system menu from
            // being mislabeled as active gameplay or menu-preview FPS.
            if(maySamplePlaybackFrame &&
               frameSeconds > 0.0001 && frameSeconds <= 0.10 &&
               songAdvance > 0.000001 && songAdvance <= 0.10)
            {
                minimumFrameSeconds_ =
                    sampledFrames_ == 0
                        ? frameSeconds
                        : std::min(minimumFrameSeconds_, frameSeconds);
                maximumFrameSeconds_ =
                    std::max(maximumFrameSeconds_, frameSeconds);
                totalFrameSeconds_ += frameSeconds;
                ++sampledFrames_;
            }
        }

        if(playbackFailed_)
            return;

        if(auto decoderError = decoder_.TakeError())
        {
            playbackFailed_ = true;
            surface_.SetVisible(false);
            cinemaScreens_.SetVisible(false);
            showcase_.SetVisible(false);
            // The worker has already published its terminal error. Join it and
            // release FFmpeg contexts/RGBA buffers now instead of retaining
            // several megabytes for the remainder of a map whose video can no
            // longer recover. The Unity/gameplay clock remains untouched.
            decoder_.Close();
            PaperLogger.error("Video decoder stopped safely: {}", *decoderError);
            ErrorManager::Instance().ReportUserVisible(
                "Video playback stopped",
                "Big Screen stopped this video's screen, but the map will continue normally. " +
                    *decoderError);
            return;
        }

        if(mapperEnvironmentApplyCountdown_ > 0 &&
           --mapperEnvironmentApplyCountdown_ == 0)
        {
            // The surface already exists under the CinemaScreen-compatible
            // name, so Quest Chroma can include it in its delayed difficulty
            // pass. Apply Cinema's independent environment array afterward.
            CinemaEnvironment::Apply(*config_);
        }

        // The pause-menu switch is intentionally session-local. Keep the
        // decoder open for a fast restart, but stop issuing frame requests and
        // uploading textures while the player has hidden this map's screen.
        if(context_ == PlaybackContext::Gameplay && !gameplayScreenEnabled_)
        {
            surface_.SetVisible(false);
            cinemaScreens_.SetVisible(false);
            showcase_.SetVisible(false);
            lastTickSongTime_ = songTimeSeconds;
            return;
        }

        if(showcase_.IsCreated())
        {
            showcase_.SetVisible(true);
            if(!showcase_.Apply(songTimeSeconds))
            {
                showcase_.Destroy();
                surface_.SetVisible(firstFrameUploaded_);
                cinemaScreens_.SetVisible(firstFrameUploaded_);
                PaperLogger.error(
                    "Up & Down showcase update failed; restored ordinary video playback");
            }
            else if(showcase_.TimelineActive())
            {
                surface_.SetVisible(false);
                cinemaScreens_.SetVisible(false);
            }
            else
            {
                surface_.SetVisible(firstFrameUploaded_);
                cinemaScreens_.SetVisible(firstFrameUploaded_);
            }
        }

        const double mediaTime = config_->MediaTimeForSong(
            songTimeSeconds,
            decoder_.DurationSeconds());

        // Negative media time is deliberate lead-in created by a negative
        // Start Offset. The per-video choice either shows an intentional solid
        // black screen or removes the surface completely until frame zero.
        if(mediaTime < 0.0)
        {
            surface_.ShowLeadIn(config_->blackDuringLeadIn);
            cinemaScreens_.ShowLeadIn(config_->blackDuringLeadIn);
            // Lead-in intentionally has no video deadlines. Rebase the song
            // clock every tick so frame zero does not inherit the elapsed
            // transparent/black interval as an artificial burst of misses.
            lastTickSongTime_ = songTimeSeconds;
            return;
        }

        // PC Cinema fades mapper video brightness during the final second.
        // Big Screen uses the equivalent alpha transition on the picture
        // material, including all additional screens sharing that picture.
        // Looping media has no final frame, so its natural duration does not
        // trigger this effect; an explicit endVideoAt still does.
        const double configuredEnd = config_->stopAtVideoSecond.value_or(
            config_->loop ? 0.0 : decoder_.DurationSeconds());
        const float endFade = configuredEnd > 0.0 &&
                              mediaTime >= configuredEnd - 1.0
            ? std::clamp(
                static_cast<float>(configuredEnd - mediaTime), 0.0f, 1.0f)
            : 1.0f;
        if(std::abs(endFade - appliedMapperEndFade_) >= 0.001f)
        {
            const float opacity = config_->videoOpacity * endFade;
            if(!surface_.SetOpacity(opacity) ||
               !cinemaScreens_.SetOpacity(opacity))
            {
                PaperLogger.error(
                    "Cinema end fade could not update the video material");
            }
            appliedMapperEndFade_ = endFade;
        }
        if(config_->stopAtVideoSecond && mediaTime > *config_->stopAtVideoSecond)
        {
            surface_.SetVisible(false);
            cinemaScreens_.SetVisible(false);
            lastTickSongTime_ = songTimeSeconds;
            return;
        }

        // Limit how often the main thread asks for a presented image without
        // altering the timestamp FFmpeg decodes toward. Quantizing the decoder
        // target itself caused irregular source-frame boundaries and visible
        // stalls. A song-clock slot simply skips redundant requests; the
        // selected request retains its exact audio-synchronized media time.
        const int fpsLimit = std::max(1, effectiveFpsLimit_);
        const auto presentationSlot = static_cast<std::int64_t>(std::floor(
            std::max(0.0, songTimeSeconds) * fpsLimit + 0.000001));
        const bool clockDiscontinuity =
            lastPresentationSlot_.has_value() &&
            (songTimeSeconds + 0.01 < lastTickSongTime_ ||
             songTimeSeconds - lastTickSongTime_ > 0.75);
        if(clockDiscontinuity)
        {
            // A practice/replay seek starts a fresh automatic-performance
            // sample. Mixing frames from before and after a large clock jump
            // could create an artificial miss spike and lower quality.
            windowDeliveredPresentedFrames_ = 0;
            windowExpectedPresentationDeadlines_ = 0;
            windowExpectedPresentationFraction_ = 0.0;
            performanceWindowStartSongTime_ = songTimeSeconds;
            diagnosticsWindowDeliveredPresentedFrames_ = 0;
            diagnosticsWindowExpectedPresentationDeadlines_ = 0;
            diagnosticsWindowExpectedPresentationFraction_ = 0.0;
            diagnosticsWindowStartSongTime_ = songTimeSeconds;
            // Preserve the session totals across a Replay/practice seek, but
            // discard a partial deadline that belonged to the old clock span.
            expectedPresentationFraction_ = 0.0;
        }
        else
        {
            const double elapsedSongSeconds = std::max(
                0.0,
                songTimeSeconds - lastTickSongTime_);
            const double sourceFps = decoder_.SourceFramesPerSecond();
            const double playbackRate = config_->playbackRate;
            expectedPresentationDeadlines_ +=
                CoreLogic::AccumulatePresentationDeadlines(
                    elapsedSongSeconds,
                    sourceFps,
                    playbackRate,
                    effectiveFpsLimit_,
                    expectedPresentationFraction_);
            windowExpectedPresentationDeadlines_ +=
                CoreLogic::AccumulatePresentationDeadlines(
                    elapsedSongSeconds,
                    sourceFps,
                    playbackRate,
                    effectiveFpsLimit_,
                    windowExpectedPresentationFraction_);
            diagnosticsWindowExpectedPresentationDeadlines_ +=
                CoreLogic::AccumulatePresentationDeadlines(
                    elapsedSongSeconds,
                    sourceFps,
                    playbackRate,
                    effectiveFpsLimit_,
                    diagnosticsWindowExpectedPresentationFraction_);
        }
        if(!lastPresentationSlot_ ||
           clockDiscontinuity ||
           presentationSlot != *lastPresentationSlot_)
        {
            decoder_.Request(mediaTime);
            ++requestedFrames_;
            lastPresentationSlot_ = presentationSlot;
        }
        lastTickSongTime_ = songTimeSeconds;

        if(diagnosticsContext &&
           Settings::Instance().PerformanceDiagnosticsEnabled() &&
           ++diagnosticsFrameCounter_ >= 30)
        {
            diagnosticsFrameCounter_ = 0;
            const auto d = Diagnostics();
            // Snapshot this completed/current sample before rolling its
            // counters. The percentage and count displayed together must
            // always describe the same set of delivered pictures.
            const auto currentWindowExpected =
                CoreLogic::ReportablePresentationDeadlines(
                    diagnosticsWindowExpectedPresentationDeadlines_,
                    diagnosticsWindowDeliveredPresentedFrames_);
            const auto currentWindowDelivered =
                diagnosticsWindowDeliveredPresentedFrames_;
            // The UI setting is a ceiling, not a target. Scale the source-
            // aware expected cadence by the delivered/expected ratio so a
            // healthy 24 FPS video under a 30 FPS cap reads 24 FPS—not 30.
            // Fit-to-Song's resolved playbackRate is already included here.
            const double expectedWindowVideoFps =
                CoreLogic::ExpectedPresentationRate(
                    d.sourceFps,
                    config_->playbackRate,
                    d.outputFpsLimit);
            const double currentWindowVideoFps = currentWindowExpected > 0
                ? expectedWindowVideoFps *
                    static_cast<double>(currentWindowDelivered) /
                    static_cast<double>(currentWindowExpected)
                : 0.0;
            const double currentWindowMissed = CoreLogic::MissedFramePercent(
                currentWindowExpected,
                currentWindowDelivered);
            if(diagnosticsWindowStartSongTime_ <= 0.0)
                diagnosticsWindowStartSongTime_ = songTimeSeconds;
            bool completedDiagnosticsWindow = false;
            if(songTimeSeconds - diagnosticsWindowStartSongTime_ >= 5.0)
            {
                diagnosticsWindowDeliveredPresentedFrames_ = 0;
                diagnosticsWindowExpectedPresentationDeadlines_ = 0;
                diagnosticsWindowExpectedPresentationFraction_ = 0.0;
                diagnosticsWindowStartSongTime_ = songTimeSeconds;
                completedDiagnosticsWindow = true;
            }
            const auto frameRate = CoreLogic::SummarizeFrameRate(
                minimumFrameSeconds_,
                maximumFrameSeconds_,
                totalFrameSeconds_,
                sampledFrames_);
            PerformancePanel::Instance().SetStatistics({
                context_ == PlaybackContext::Gameplay,
                frameRate.minimumFps,
                frameRate.averageFps,
                frameRate.maximumFps,
                frameRate.sampledFrames,
                d.videoWidth,
                d.videoHeight,
                d.sourceFps,
                d.outputFpsLimit,
                currentWindowExpected > currentWindowDelivered
                    ? currentWindowExpected - currentWindowDelivered
                    : 0,
                currentWindowExpected,
                d.missedFrames,
                currentWindowVideoFps,
                currentWindowMissed,
                d.averageDecodeMilliseconds,
                d.peakDecodeMilliseconds,
                d.decodeMethod,
                d.decoderRuntime,
                d.codec});
            if(completedDiagnosticsWindow)
                decoder_.ResetPeakDecodeMilliseconds();
            diagnosticsVisible_ = true;
        }
        else if(diagnosticsContext &&
                !Settings::Instance().PerformanceDiagnosticsEnabled() &&
                diagnosticsVisible_)
        {
            diagnosticsFrameCounter_ = 0;
            PerformancePanel::Instance().SetEnabled(false);
            diagnosticsVisible_ = false;
        }

        if((context_ == PlaybackContext::Gameplay ||
            context_ == PlaybackContext::LibraryPreview) &&
           Settings::Instance().AutomaticPerformanceEnabled())
        {
            const auto& settings = Settings::Instance();
            if(!settings.AutomaticPerformanceOscillationPreventionEnabled())
            {
                automaticPerformanceRecoveryPinned_ = false;
                automaticPerformanceFailedRecoveries_ = 0;
                lastAutomaticRecoveryLowFps_.reset();
                lastAutomaticRecoveryHighFps_.reset();
            }

            const auto expectedWindowFrames =
                CoreLogic::ReportablePresentationDeadlines(
                    windowExpectedPresentationDeadlines_,
                    windowDeliveredPresentedFrames_);
            const double missedPercent = CoreLogic::MissedFramePercent(
                expectedWindowFrames,
                windowDeliveredPresentedFrames_);
            // Do not classify an empty startup/lead-in interval as healthy.
            // As soon as real source deadlines exist, the applicable attack
            // or release duration is measured from this bounded window.
            if(expectedWindowFrames > 0)
            {
                const auto decision =
                    CoreLogic::EvaluateAutomaticPerformanceWindow(
                        songTimeSeconds - performanceWindowStartSongTime_,
                        missedPercent,
                        static_cast<double>(
                            settings.AutomaticPerformanceThreshold()),
                        settings.AutomaticPerformanceAttackSeconds(),
                        settings.AutomaticPerformanceReleaseSeconds());
                if(decision !=
                   CoreLogic::AutomaticPerformanceDecision::Wait)
                {
                    if(decision ==
                       CoreLogic::AutomaticPerformanceDecision::Reduce)
                        ApplyAutomaticPerformanceReduction();
                    else
                        ApplyAutomaticPerformanceRecovery();
                    ResetAutomaticPerformanceWindow(songTimeSeconds);
                }
            }
        }
        else if(windowExpectedPresentationDeadlines_ > 0 ||
                windowDeliveredPresentedFrames_ > 0)
        {
            ResetAutomaticPerformanceWindow(songTimeSeconds);
        }

        VideoFrame frame;
        if(decoder_.TryTake(frame))
        {
            bool uploadFailed = false;
            const bool staleRestartFrame =
                libraryPreviewRestartPending_ &&
                frame.generation != libraryPreviewRestartGeneration_;
            if(staleRestartFrame)
            {
                // The worker may have finished one old-pass conversion while
                // the main thread posted Restart. Its generation cannot unlock
                // synchronized audio or replace the opening frame requested
                // for the new loop.
                PaperLogger.debug(
                    "Discarded stale Video Library frame from generation {}; waiting for generation {}",
                    frame.generation,
                    libraryPreviewRestartGeneration_);
            }
            else if(surface_.Upload(frame))
            {
                // Count every distinct picture that actually reached Unity.
                // The song-clock deadline accumulators above provide the
                // independent expectation; media timestamp gaps are
                // deliberately irrelevant because a cap may intentionally
                // skip source pictures.
                ++deliveredPresentedFrames_;
                ++windowDeliveredPresentedFrames_;
                ++diagnosticsWindowDeliveredPresentedFrames_;
                firstFrameUploaded_ = true;
                if(libraryPreviewRestartPending_)
                {
                    libraryPreviewRestartPending_ = false;
                    PaperLogger.info(
                        "Video Library restart frame reached the Unity texture");
                }
                if(showcase_.IsCreated() && showcase_.TimelineActive())
                {
                    showcase_.SetMediaReady(true);
                    // Apply once more after the first upload so the panels
                    // become visible in this same Unity frame instead of one
                    // update late.
                    if(!showcase_.Apply(songTimeSeconds))
                    {
                        showcase_.Destroy();
                        surface_.SetVisible(true);
                        cinemaScreens_.SetVisible(true);
                        PaperLogger.error(
                            "Up & Down showcase activation failed after first frame; restored ordinary playback");
                    }
                    else
                    {
                        surface_.SetVisible(false);
                        cinemaScreens_.SetVisible(false);
                    }
                }
                else
                {
                    surface_.SetVisible(true);
                    cinemaScreens_.SetVisible(true);
                }
            }
            else
            {
                // An upload failure means the Unity surface was destroyed by
                // a scene transition/restart or no longer matches the decoder.
                // Stop consuming frames immediately. Repeatedly invoking a
                // stale Texture2D is unsafe on IL2CPP and can crash the entire
                // Beat Saber process instead of throwing a managed exception.
                playbackFailed_ = true;
                uploadFailed = true;
                PaperLogger.error(
                    "Video frame upload stopped because the Unity screen is no longer valid");
                ErrorManager::Instance().ReportInternal(
                    "uploading a decoded video frame",
                    "The gameplay screen was no longer available. The map will continue without video.");
            }
            decoder_.Recycle(std::move(frame));
            if(uploadFailed)
                decoder_.Close();
        }

        if(libraryPreviewRestartPending_ && !firstFrameUploaded_ &&
           !libraryPreviewRestartReopenAttempted_)
        {
            constexpr auto RestartFrameTimeout =
                std::chrono::milliseconds(1000);
            if(std::chrono::steady_clock::now() -
                   libraryPreviewRestartStarted_ >= RestartFrameTimeout)
            {
                // avcodec_flush_buffers normally restarts both software and
                // MediaCodec backends. Some Android codecs remain drained
                // after EOF, however. One bounded reopen is the defined
                // recovery—not an unbounded retry loop—and retains the
                // already-created screen/material.
                const double restartMediaTime = config_->MediaTimeForSong(
                    songTimeSeconds,
                    decoder_.DurationSeconds());
                ReopenLibraryPreviewDecoder(restartMediaTime);
            }
        }

        // Evaluate deadline outcomes after this tick's possible Unity upload.
        // This keeps a picture delivered on the current update from being
        // classified as late merely because Tick() checks the decoder near the
        // end of the function. Automatic Performance continues using its own
        // bounded, recoverable backlog window above.
        presentationMisses_.Observe(
            CoreLogic::ReportablePresentationDeadlines(
                expectedPresentationDeadlines_,
                deliveredPresentedFrames_),
            deliveredPresentedFrames_);
    }

    void PlaybackSession::RefreshPlaybackFpsLimitLive()
    {
        if(!started_)
            return;
        const int requestedLimit = Settings::Instance().PlaybackFpsLimit();
        if(requestedLimit == effectiveFpsLimit_)
            return;

        effectiveFpsLimit_ = requestedLimit;
        // A manual preference change is a new quality baseline. Old automatic
        // tiers no longer describe a reversible path from the current value.
        ResetAutomaticPerformanceController(lastTickSongTime_);
        lastPresentationSlot_.reset();
        // A new cap defines a new experiment. Retaining expected/presented
        // counts from the previous cap made the live percentage slow to react
        // and produced misleading comparisons between 15, 30, and 60 FPS.
        // Keep the session totals intact. Changing the cap starts a new live
        // comparison window, but Total Missed represents the entire video run.
        diagnosticsWindowDeliveredPresentedFrames_ = 0;
        diagnosticsWindowExpectedPresentationDeadlines_ = 0;
        diagnosticsWindowExpectedPresentationFraction_ = 0.0;
        diagnosticsWindowStartSongTime_ = lastTickSongTime_;
        windowDeliveredPresentedFrames_ = 0;
        windowExpectedPresentationDeadlines_ = 0;
        windowExpectedPresentationFraction_ = 0.0;
        performanceWindowStartSongTime_ = lastTickSongTime_;
        // The session accumulator continues at the new rate, but a fractional
        // deadline from the prior cap must not leak across that boundary.
        expectedPresentationFraction_ = 0.0;
        diagnosticsFrameCounter_ = 29;
        PaperLogger.info(
            "Applied live video frame-rate cap: {} FPS",
            effectiveFpsLimit_);
    }

    void PlaybackSession::Stop()
    {
        const bool gameplaySession =
            started_ && context_ == PlaybackContext::Gameplay &&
            !environmentOnlySession_;
        const bool recordGameplayPerformance =
            gameplaySession &&
            Settings::Instance().PerformanceDiagnosticsEnabled();
        if(started_ && !environmentOnlySession_)
            CaptureDiagnosticsSummary();
        if(recordGameplayPerformance)
        {
            // Append only after gameplay has ended or been exited. No file IO
            // occurs in Tick(), so enabling diagnostics does not add storage
            // latency to Beat Saber's real-time frame loop.
            ErrorManager::Instance().RecordPerformance(
                preparedSongName_ + " - " + preparedSongArtist_ +
                    " [" + preparedLevelId_ + "]",
                lastDiagnosticsSummary_);
        }
        else if(started_ && context_ == PlaybackContext::LibraryPreview &&
                Settings::Instance().PerformanceDiagnosticsEnabled())
        {
            PerformancePanel::Instance().ShowWaitingMessage();
        }
        // Unity objects must be destroyed before closing the decoder because
        // this function is called by a main-thread scene-transition hook. The
        // decoder close then joins its worker so no FFmpeg state survives into
        // the menu or the next level.
        // Shared panels release their materials before the primary owner
        // destroys the one decoded texture they all reference.
        showcase_.Destroy();
        cinemaScreens_.Destroy();
        surface_.Destroy();
        CinemaEnvironment::Cleanup();
        if(context_ == PlaybackContext::Gameplay)
            PerformancePanel::Instance().SuspendGameplay();
        decoder_.Close();
        // Close joins the worker and preserves its final CPU clock. Replace the
        // pre-join snapshot so a last in-flight decode is not lost from the
        // post-map power benchmark.
        if(gameplaySession && lastResultsData_)
        {
            lastResultsData_->video.decoderCpuMilliseconds = std::max(
                0.0,
                decoder_.WorkerCpuMilliseconds() -
                    decoderCpuBaselineMilliseconds_);
        }
        gameplayDecoderPrewarmed_ = false;
        gameplayPrewarmFailed_ = false;
        gameplayPrewarmError_.clear();
        started_ = false;
        playbackFailed_ = false;
        gameplayScreenEnabled_ = true;
        firstFrameUploaded_ = false;
        libraryPreviewRestartPending_ = false;
        libraryPreviewRestartReopenAttempted_ = false;
        libraryPreviewRestartGeneration_ = 0;
        appliedMapperEndFade_ = 1.0f;
        lastPresentationSlot_.reset();
        lastTickSongTime_ = 0.0;
        context_ = PlaybackContext::None;
        mapperEnvironmentApplyCountdown_ = 0;
        gameplayLastNoteTime_.reset();
        gameplayFrameSamplingFinished_ = false;
    }

    void PlaybackSession::ClearPreparedPreviewForLevel(std::string_view levelId)
    {
        if(levelId.empty() || preparedLevelId_ != levelId || IsGameplayActive())
            return;

        // Prepare(nullptr) performs the complete metadata reset in one place
        // after Stop has joined any active decoder worker and destroyed its
        // Unity surface. Do not duplicate only part of that state here.
        Prepare(nullptr);
        PaperLogger.info(
            "Cleared Video Library preview state while leaving Big Screen");
    }

    void PlaybackSession::SetGameplayLastNoteTime(double songTimeSeconds)
    {
        if(songTimeSeconds < 0.0)
            return;
        gameplayLastNoteTime_ = songTimeSeconds;
        gameplayFrameSamplingFinished_ = false;
        PaperLogger.info(
            "Gameplay FPS sampling will stop after the final note at {:.3f} seconds",
            songTimeSeconds);
    }

    bool PlaybackSession::ApplyAutomaticPerformanceReduction()
    {
        const auto& settings = Settings::Instance();
        const double sourceFps = decoder_.SourceFramesPerSecond();
        const double playbackRate = config_ ? config_->playbackRate : 1.0;
        const auto [changed, nextFps] = CoreLogic::NextPerformanceFpsLimit(
            effectiveFpsLimit_,
            sourceFps,
            playbackRate,
            settings.AutomaticPerformanceFpsStep());
        if(!changed)
            return false;

        const int previousFps = effectiveFpsLimit_;
        const int recoveryTarget = CoreLogic::EffectivePerformanceFpsLimit(
            previousFps,
            sourceFps,
            playbackRate);

        if(settings.AutomaticPerformanceOscillationPreventionEnabled())
        {
            const bool failedLastRecovery =
                lastAutomaticRecoveryLowFps_ == nextFps &&
                lastAutomaticRecoveryHighFps_ == previousFps;
            if(failedLastRecovery)
            {
                ++automaticPerformanceFailedRecoveries_;
                if(automaticPerformanceFailedRecoveries_ >=
                   settings.AutomaticPerformanceOscillationLimit())
                {
                    automaticPerformanceRecoveryPinned_ = true;
                    PaperLogger.warn(
                        "Automatic Performance pinned recovery at {} FPS after {} failed {}<->{} FPS cycles",
                        nextFps,
                        automaticPerformanceFailedRecoveries_,
                        nextFps,
                        previousFps);
                }
            }
            else if(lastAutomaticRecoveryLowFps_ ||
                    lastAutomaticRecoveryHighFps_)
            {
                // A different pair starts a different oscillation sequence.
                automaticPerformanceFailedRecoveries_ = 0;
            }
            lastAutomaticRecoveryLowFps_.reset();
            lastAutomaticRecoveryHighFps_.reset();
        }

        if(!automaticPerformanceHistory_.RecordReduction(recoveryTarget))
        {
            // A one-FPS ladder can contain forty-five reductions. Refuse an
            // unrecorded extra step if a future policy exceeds that invariant;
            // exact reverse recovery is more important than one more drop.
            PaperLogger.error(
                "Automatic Performance reduction history is full at {} FPS",
                effectiveFpsLimit_);
            return false;
        }
        ApplyAutomaticPerformanceFpsLimit(nextFps);
        ++automaticReductions_;
        PaperLogger.warn(
            "Automatic Performance reduced the video frame-rate limit to {} FPS",
            effectiveFpsLimit_);
        return true;
    }

    bool PlaybackSession::ApplyAutomaticPerformanceRecovery()
    {
        const auto& settings = Settings::Instance();
        if(settings.AutomaticPerformanceOscillationPreventionEnabled() &&
           automaticPerformanceRecoveryPinned_)
            return false;

        const auto target = automaticPerformanceHistory_.RecoveryTarget();
        if(!target)
            return false;

        const int lowerFps = effectiveFpsLimit_;
        ApplyAutomaticPerformanceFpsLimit(*target);
        automaticPerformanceHistory_.CommitRecovery();
        if(settings.AutomaticPerformanceOscillationPreventionEnabled())
        {
            lastAutomaticRecoveryLowFps_ = lowerFps;
            lastAutomaticRecoveryHighFps_ = effectiveFpsLimit_;
        }
        PaperLogger.info(
            "Automatic Performance restored the video frame-rate limit to {} FPS after {:.1f} seconds below the missed-frame threshold",
            effectiveFpsLimit_,
            settings.AutomaticPerformanceReleaseSeconds());
        return true;
    }

    void PlaybackSession::ApplyAutomaticPerformanceFpsLimit(int nextFps)
    {
        effectiveFpsLimit_ = std::max(15, nextFps);
        lastPresentationSlot_.reset();
        // A cap boundary starts clean controller and live diagnostic samples.
        // Session-wide deadline/upload totals remain monotonic, while the
        // next response window evaluates only the new presentation cadence.
        ResetAutomaticPerformanceWindow(lastTickSongTime_);
        diagnosticsWindowDeliveredPresentedFrames_ = 0;
        diagnosticsWindowExpectedPresentationDeadlines_ = 0;
        diagnosticsWindowExpectedPresentationFraction_ = 0.0;
        diagnosticsWindowStartSongTime_ = lastTickSongTime_;
        expectedPresentationFraction_ = 0.0;
    }

    void PlaybackSession::ResetAutomaticPerformanceWindow(double songTimeSeconds)
    {
        windowDeliveredPresentedFrames_ = 0;
        windowExpectedPresentationDeadlines_ = 0;
        windowExpectedPresentationFraction_ = 0.0;
        performanceWindowStartSongTime_ = songTimeSeconds;
    }

    void PlaybackSession::ResetAutomaticPerformanceController(double songTimeSeconds)
    {
        automaticPerformanceHistory_.Reset();
        lastAutomaticRecoveryLowFps_.reset();
        lastAutomaticRecoveryHighFps_.reset();
        automaticPerformanceFailedRecoveries_ = 0;
        automaticPerformanceRecoveryPinned_ = false;
        ResetAutomaticPerformanceWindow(songTimeSeconds);
    }

    void PlaybackSession::CaptureDiagnosticsSummary()
    {
        const auto diagnostics = Diagnostics();
        const auto missedFrames = diagnostics.missedFrames;
        const double missedPercent = diagnostics.expectedFrames > 0
            ? 100.0 * static_cast<double>(missedFrames) /
                static_cast<double>(diagnostics.expectedFrames)
            : 0.0;
        const double expectedVideoFps = CoreLogic::ExpectedPresentationRate(
            diagnostics.sourceFps,
            config_ ? config_->playbackRate : 1.0,
            diagnostics.outputFpsLimit);
        const double averageVideoFps = diagnostics.expectedFrames > 0
            ? expectedVideoFps *
                static_cast<double>(
                    diagnostics.expectedFrames -
                    std::min(diagnostics.expectedFrames, missedFrames)) /
                static_cast<double>(diagnostics.expectedFrames)
            : 0.0;
        std::ostringstream text;
        text << diagnostics.videoWidth << 'x' << diagnostics.videoHeight
             << " @ " << std::fixed << std::setprecision(1)
             << diagnostics.sourceFps << " FPS video  |  "
             << "Codec " << diagnostics.codec << "  |  "
             << "Presentation limit " << diagnostics.outputFpsLimit << " FPS\n"
             << "Uploaded " << diagnostics.presentedFrames << " pictures  |  "
             << diagnostics.expectedFrames << " presentation deadlines  |  "
             << "Missed Frames " << missedFrames << " ("
             << std::setprecision(2) << missedPercent << "%)  |  "
             << "Video Average " << std::setprecision(1)
             << averageVideoFps << " FPS  |  Decode "
             << std::setprecision(2) << diagnostics.averageDecodeMilliseconds
             << " ms average / " << diagnostics.peakDecodeMilliseconds
             << " ms peak  |  Decoder startup "
             << diagnostics.decoderOpenMilliseconds
             << " ms  |  RGBA allocations "
              << diagnostics.rgbaBufferAllocations << "  |  FFmpeg "
              << diagnostics.decoderRuntime << "  |  Decoder "
              << diagnostics.decodeMethod;
        if(diagnostics.automaticReductions > 0)
            text << "  |  Automatic reductions " << diagnostics.automaticReductions;
        const auto frameRate = CoreLogic::SummarizeFrameRate(
            minimumFrameSeconds_,
            maximumFrameSeconds_,
            totalFrameSeconds_,
            sampledFrames_);
        lastResultsData_ = PlaybackResultsData{
            diagnostics,
            frameRate.minimumFps,
            frameRate.averageFps,
            frameRate.maximumFps,
            frameRate.sampledFrames,
            missedFrames,
            averageVideoFps,
            missedPercent};
        if(frameRate.sampledFrames > 0)
        {
            text << '\n'
                 << (context_ == PlaybackContext::Gameplay
                         ? "Gameplay"
                         : "Menu preview")
                 << " FPS min / avg / max "
                 << std::setprecision(1) << frameRate.minimumFps << " / "
                 << frameRate.averageFps << " / "
                 << frameRate.maximumFps << "  |  Samples "
                 << frameRate.sampledFrames;
        }
        lastDiagnosticsSummary_ = text.str();
    }

    void PlaybackSession::FinalizeDiagnosticsDisplay()
    {
        if(!started_ || !Settings::Instance().PerformanceDiagnosticsEnabled())
            return;
        CaptureDiagnosticsSummary();
        const auto diagnostics = Diagnostics();
        const auto frameRate = CoreLogic::SummarizeFrameRate(
            minimumFrameSeconds_,
            maximumFrameSeconds_,
            totalFrameSeconds_,
            sampledFrames_);
        const double expectedVideoFps = CoreLogic::ExpectedPresentationRate(
            diagnostics.sourceFps,
            config_ ? config_->playbackRate : 1.0,
            diagnostics.outputFpsLimit);
        const double averageVideoFps = diagnostics.expectedFrames > 0
            ? expectedVideoFps *
                static_cast<double>(
                    diagnostics.expectedFrames -
                    std::min(
                        diagnostics.expectedFrames,
                        diagnostics.missedFrames)) /
                static_cast<double>(diagnostics.expectedFrames)
            : 0.0;
        PerformancePanel::Instance().SetStatistics({
            context_ == PlaybackContext::Gameplay,
            frameRate.minimumFps,
            frameRate.averageFps,
            frameRate.maximumFps,
            frameRate.sampledFrames,
            diagnostics.videoWidth,
            diagnostics.videoHeight,
            diagnostics.sourceFps,
            diagnostics.outputFpsLimit,
            diagnostics.missedFrames,
            diagnostics.expectedFrames,
            diagnostics.missedFrames,
            averageVideoFps,
            diagnostics.expectedFrames > 0
                ? 100.0 * static_cast<double>(diagnostics.missedFrames) /
                    static_cast<double>(diagnostics.expectedFrames)
                : 0.0,
            diagnostics.averageDecodeMilliseconds,
            diagnostics.peakDecodeMilliseconds,
            diagnostics.decodeMethod,
            diagnostics.decoderRuntime,
            diagnostics.codec});
        diagnosticsVisible_ = true;
    }
}
