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
        return showcaseEligible_ ||
               (Settings::Instance().AllowChromaOverride() &&
                baseConfig_ &&
                chromaMapDetected_ &&
                baseConfig_->hasMapperScreenGeometry);
    }

    bool PlaybackSession::MapperEnvironmentPresentationActive() const
    {
        return showcaseEligible_ ||
               (Settings::Instance().AllowChromaOverride() &&
                baseConfig_ &&
                (chromaMapDetected_ ||
                 baseConfig_->hasMapperEnvironmentPresentation));
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
        return {
            decoder_.Width(), decoder_.Height(),
            decoder_.SourceFramesPerSecond(), effectiveFpsLimit_,
            requestedFrames_,
            deliveredPresentedFrames_ + missedPresentedFrames_,
            deliveredPresentedFrames_,
            decoder_.BufferAllocations(),
            decoder_.AverageDecodeMilliseconds(),
            decoder_.PeakDecodeMilliseconds(),
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
        chromaMapDetected_ = false;
        menuPreviewStartSongTime_ = 0.0;

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
        baseConfig_ = VideoLibrary::Instance().ResolvePlayback(level);

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
            RefreshDisplaySettings();

            PaperLogger.info(
                "Prepared video '{}' from '{}'",
                config_->videoPath.filename().string(),
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
        const auto applyUserVideoControls = [&settings, this]()
        {
            // Chroma/Cinema may own the canvas transform, but Big Screen's
            // Video Controls describe how the decoded picture is composed
            // inside that canvas. Keeping these independent lets a mapper
            // place/size/curve the screen without silently disabling the
            // user's rotation, zoom, pan, tilt, or stretch controls.
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
        // Mapper screen geometry wins only when the map both uses Chroma and
        // explicitly authors the video canvas. Chroma environment ownership
        // is evaluated separately; merely using Chroma must not make the
        // player's Screen Canvas controls inert.
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
            config_->letterboxTransparent =
                settings.AdvancedOptionsEnabled() &&
                settings.LetterboxTransparencyEnabled();
            if(!config_->cinemaCurvatureDegrees)
            {
                config_->screenCurvature = settings.CurvedScreenEnabled()
                    ? settings.ScreenCurvature() : 0.0f;
                config_->maintainAspectRatioWhenCurved =
                    settings.CurvedScreenEnabled() &&
                    settings.MaintainCurveAspectRatio();
            }
            applyUserVideoControls();
            PaperLogger.info(
                "Allow Chroma Override is yielding custom screen geometry to this Chroma map");
            return;
        }

        // Turning mapper control off means returning to Big Screen's neutral
        // back-wall canvas, not merely applying user offsets to the mapper's
        // authored X/Y/Z and scale. The latter left Chroma screens at their
        // custom location even after the visible size changed.
        if(config_->hasMapperPresentation)
            config_->ResetPresentationToDefaults();

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
        config_->letterboxTransparent = settings.AdvancedOptionsEnabled() &&
            settings.LetterboxTransparencyEnabled();
        applyUserVideoControls();
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
            showcase_.SetVisible(false);
            showcase_.SetMediaReady(false);
            lastPresentationSlot_.reset();
        }
        else
        {
            surface_.SetVisible(false);
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
            }
        }

        started_ = true;
        playbackFailed_ = false;
        gameplayScreenEnabled_ = true;
        firstFrameUploaded_ = false;
        lastPresentationSlot_.reset();
        lastTickSongTime_ = 0.0;
        context_ = context;
        requestedFrames_ = 0;
        deliveredPresentedFrames_ = 0;
        missedPresentedFrames_ = 0;
        windowDeliveredPresentedFrames_ = 0;
        windowMissedPresentedFrames_ = 0;
        performanceWindowStartSongTime_ = 0.0;
        diagnosticsWindowDeliveredPresentedFrames_ = 0;
        diagnosticsWindowMissedPresentedFrames_ = 0;
        diagnosticsWindowStartSongTime_ = 0.0;
        lastUploadedPresentationSeconds_.reset();
        lastUploadedDurationSeconds_ = 0.0;
        minimumFrameSeconds_ = 0.0;
        maximumFrameSeconds_ = 0.0;
        totalFrameSeconds_ = 0.0;
        lastFpsSongTime_ = 0.0;
        sampledFrames_ = 0;
        gameplayFrameSamplingFinished_ = false;
        automaticReductions_ = 0;
        // Gameplay prewarming deliberately runs before Beat Saber's song
        // clock. Exclude that setup work from the map benchmark so video-on
        // and video-off runs cover the same measured interval.
        decoderCpuBaselineMilliseconds_ = decoder_.WorkerCpuMilliseconds();
        automaticPerformanceHistory_.Reset();
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
        missedPresentedFrames_ = 0;
        windowDeliveredPresentedFrames_ = 0;
        windowMissedPresentedFrames_ = 0;
        performanceWindowStartSongTime_ = songTimeSeconds;
        diagnosticsWindowDeliveredPresentedFrames_ = 0;
        diagnosticsWindowMissedPresentedFrames_ = 0;
        diagnosticsWindowStartSongTime_ = songTimeSeconds;
        lastUploadedPresentationSeconds_.reset();
        lastUploadedDurationSeconds_ = 0.0;
        minimumFrameSeconds_ = 0.0;
        maximumFrameSeconds_ = 0.0;
        totalFrameSeconds_ = 0.0;
        lastFpsSongTime_ = songTimeSeconds;
        sampledFrames_ = 0;
        automaticReductions_ = 0;
        decoderCpuBaselineMilliseconds_ = decoder_.WorkerCpuMilliseconds();
        automaticPerformanceHistory_.Reset();
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

    bool PlaybackSession::OpenDecoder(std::string& error)
    {
        effectiveFpsLimit_ = Settings::Instance().PlaybackFpsLimit();
        return decoder_.Open(
            config_->videoPath,
            UncappedOutputHeight,
            error);
    }

    void PlaybackSession::PrewarmGameplay()
    {
        if(!Settings::Instance().ModEnabled() || !config_ || started_)
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
                PaperLogger.error(
                    "Up & Down showcase update failed; restored ordinary video playback");
            }
            else if(showcase_.TimelineActive())
            {
                surface_.SetVisible(false);
            }
            else
            {
                surface_.SetVisible(firstFrameUploaded_);
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
            return;
        }
        if(config_->stopAtVideoSecond && mediaTime > *config_->stopAtVideoSecond)
        {
            surface_.SetVisible(false);
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
            windowMissedPresentedFrames_ = 0;
            performanceWindowStartSongTime_ = songTimeSeconds;
            diagnosticsWindowDeliveredPresentedFrames_ = 0;
            diagnosticsWindowMissedPresentedFrames_ = 0;
            diagnosticsWindowStartSongTime_ = songTimeSeconds;
            lastUploadedPresentationSeconds_.reset();
            lastUploadedDurationSeconds_ = 0.0;
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
                diagnosticsWindowDeliveredPresentedFrames_ +
                diagnosticsWindowMissedPresentedFrames_;
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
                diagnosticsWindowMissedPresentedFrames_ = 0;
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
                missedPresentedFrames_,
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
            if(performanceWindowStartSongTime_ <= 0.0)
                performanceWindowStartSongTime_ = songTimeSeconds;
            else if(songTimeSeconds - performanceWindowStartSongTime_ >=
                    Settings::Instance().AutomaticPerformanceResponseSeconds())
            {
                const double missedPercent = CoreLogic::MissedFramePercent(
                    windowDeliveredPresentedFrames_ +
                        windowMissedPresentedFrames_,
                    windowDeliveredPresentedFrames_);
                if(missedPercent >= Settings::Instance().AutomaticPerformanceThreshold())
                    ApplyAutomaticPerformanceReduction();
                else
                    ApplyAutomaticPerformanceRecovery();
                windowDeliveredPresentedFrames_ = 0;
                windowMissedPresentedFrames_ = 0;
                performanceWindowStartSongTime_ = songTimeSeconds;
            }
        }

        VideoFrame frame;
        if(!decoder_.TryTake(frame))
            return;

        if(surface_.Upload(frame))
        {
            // Count every distinct image that actually reached Unity. The
            // timestamp comparison below accounts only for media pictures
            // skipped between this upload and the preceding upload. Keeping
            // those as separate monotonic totals makes the live panel, the
            // results screen, and the persisted log mathematically identical.
            ++deliveredPresentedFrames_;
            ++windowDeliveredPresentedFrames_;
            ++diagnosticsWindowDeliveredPresentedFrames_;
            if(lastUploadedPresentationSeconds_)
            {
                const auto intervals = CoreLogic::PresentedFrameIntervals(
                    *lastUploadedPresentationSeconds_,
                    lastUploadedDurationSeconds_,
                    frame.presentationSeconds,
                    config_->playbackRate,
                    effectiveFpsLimit_);
                const auto missed = intervals > 1 ? intervals - 1 : 0;
                missedPresentedFrames_ += missed;
                windowMissedPresentedFrames_ += missed;
                diagnosticsWindowMissedPresentedFrames_ += missed;
            }
            lastUploadedPresentationSeconds_ = frame.presentationSeconds;
            lastUploadedDurationSeconds_ = frame.durationSeconds;
            firstFrameUploaded_ = true;
            if(showcase_.IsCreated() && showcase_.TimelineActive())
            {
                showcase_.SetMediaReady(true);
                // Apply once more after the first upload so the panels become
                // visible in this same Unity frame instead of one update late.
                if(!showcase_.Apply(songTimeSeconds))
                {
                    showcase_.Destroy();
                    surface_.SetVisible(true);
                    PaperLogger.error(
                        "Up & Down showcase activation failed after first frame; restored ordinary playback");
                }
                else
                {
                    surface_.SetVisible(false);
                }
            }
            else
            {
                surface_.SetVisible(true);
            }
        }
        decoder_.Recycle(std::move(frame));
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
        automaticPerformanceHistory_.Reset();
        lastPresentationSlot_.reset();
        // A new cap defines a new experiment. Retaining expected/presented
        // counts from the previous cap made the live percentage slow to react
        // and produced misleading comparisons between 15, 30, and 60 FPS.
        // Keep the session totals intact. Changing the cap starts a new live
        // comparison window, but Total Missed represents the entire video run.
        diagnosticsWindowDeliveredPresentedFrames_ = 0;
        diagnosticsWindowMissedPresentedFrames_ = 0;
        diagnosticsWindowStartSongTime_ = lastTickSongTime_;
        windowDeliveredPresentedFrames_ = 0;
        windowMissedPresentedFrames_ = 0;
        performanceWindowStartSongTime_ = lastTickSongTime_;
        lastUploadedPresentationSeconds_.reset();
        lastUploadedDurationSeconds_ = 0.0;
        diagnosticsFrameCounter_ = 29;
        PaperLogger.info(
            "Applied live video frame-rate cap: {} FPS",
            effectiveFpsLimit_);
    }

    void PlaybackSession::Stop()
    {
        const bool gameplaySession =
            started_ && context_ == PlaybackContext::Gameplay;
        const bool recordGameplayPerformance =
            gameplaySession &&
            Settings::Instance().PerformanceDiagnosticsEnabled();
        if(started_)
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
        surface_.Destroy();
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
        lastPresentationSlot_.reset();
        lastTickSongTime_ = 0.0;
        context_ = PlaybackContext::None;
        mapperEnvironmentApplyCountdown_ = 0;
        gameplayLastNoteTime_.reset();
        gameplayFrameSamplingFinished_ = false;
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
        const auto [changed, nextFps] = CoreLogic::NextPerformanceFpsLimit(
            effectiveFpsLimit_,
            decoder_.SourceFramesPerSecond(),
            config_ ? config_->playbackRate : 1.0);
        if(!changed)
            return false;

        const int previousFps = effectiveFpsLimit_;
        if(!automaticPerformanceHistory_.RecordReduction(previousFps))
        {
            // A 60-to-15 ladder contains exactly nine reductions. Refuse an
            // unrecorded tenth step if a future policy changes that invariant;
            // exact reverse recovery is more important than one extra drop.
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
        const auto target = automaticPerformanceHistory_.RecoveryTarget();
        if(!target)
            return false;

        ApplyAutomaticPerformanceFpsLimit(*target);
        automaticPerformanceHistory_.CommitRecovery();
        PaperLogger.info(
            "Automatic Performance restored the video frame-rate limit to {} FPS after {:.1f} seconds below the missed-frame threshold",
            effectiveFpsLimit_,
            Settings::Instance().AutomaticPerformanceResponseSeconds());
        return true;
    }

    void PlaybackSession::ApplyAutomaticPerformanceFpsLimit(int nextFps)
    {
        effectiveFpsLimit_ = std::max(15, nextFps);
        lastPresentationSlot_.reset();
        // A cap boundary starts clean controller and live diagnostic samples.
        // Session-wide delivered/missed totals remain monotonic, while the
        // next response window evaluates only the new presentation cadence.
        windowDeliveredPresentedFrames_ = 0;
        windowMissedPresentedFrames_ = 0;
        diagnosticsWindowDeliveredPresentedFrames_ = 0;
        diagnosticsWindowMissedPresentedFrames_ = 0;
        diagnosticsWindowStartSongTime_ = lastTickSongTime_;
        lastUploadedPresentationSeconds_.reset();
        lastUploadedDurationSeconds_ = 0.0;
    }

    void PlaybackSession::CaptureDiagnosticsSummary()
    {
        const auto diagnostics = Diagnostics();
        const auto missedFrames =
            diagnostics.expectedFrames > diagnostics.presentedFrames
                ? diagnostics.expectedFrames - diagnostics.presentedFrames
                : 0;
        const double missedPercent = CoreLogic::MissedFramePercent(
            diagnostics.expectedFrames, diagnostics.presentedFrames);
        const double expectedVideoFps = CoreLogic::ExpectedPresentationRate(
            diagnostics.sourceFps,
            config_ ? config_->playbackRate : 1.0,
            diagnostics.outputFpsLimit);
        const double averageVideoFps = diagnostics.expectedFrames > 0
            ? expectedVideoFps *
                static_cast<double>(diagnostics.presentedFrames) /
                static_cast<double>(diagnostics.expectedFrames)
            : 0.0;
        std::ostringstream text;
        text << diagnostics.videoWidth << 'x' << diagnostics.videoHeight
             << " @ " << std::fixed << std::setprecision(1)
             << diagnostics.sourceFps << " FPS video  |  "
             << "Codec " << diagnostics.codec << "  |  "
             << "Presentation limit " << diagnostics.outputFpsLimit << " FPS\n"
             << "Delivered " << diagnostics.presentedFrames << " / "
             << diagnostics.expectedFrames << " expected pictures  |  "
             << "Missed Frames " << missedFrames << " ("
             << std::setprecision(2) << missedPercent << "%)  |  "
             << "Video Average " << std::setprecision(1)
             << averageVideoFps << " FPS  |  Decode "
             << std::setprecision(2) << diagnostics.averageDecodeMilliseconds
             << " ms average / " << diagnostics.peakDecodeMilliseconds
             << " ms peak  |  RGBA allocations "
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
                static_cast<double>(diagnostics.presentedFrames) /
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
            diagnostics.expectedFrames > diagnostics.presentedFrames
                ? diagnostics.expectedFrames - diagnostics.presentedFrames
                : 0,
            diagnostics.expectedFrames,
            diagnostics.expectedFrames > diagnostics.presentedFrames
                ? diagnostics.expectedFrames - diagnostics.presentedFrames
                : 0,
            averageVideoFps,
            CoreLogic::MissedFramePercent(
                diagnostics.expectedFrames,
                diagnostics.presentedFrames),
            diagnostics.averageDecodeMilliseconds,
            diagnostics.peakDecodeMilliseconds,
            diagnostics.decodeMethod,
            diagnostics.decoderRuntime,
            diagnostics.codec});
        diagnosticsVisible_ = true;
    }
}
