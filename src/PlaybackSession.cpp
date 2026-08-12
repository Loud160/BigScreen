#include "BigScreen/PlaybackSession.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "BigScreen/Settings.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/CinemaEnvironment.hpp"
#include "BigScreen/ChromaMapDetector.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
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

    bool PlaybackSession::MapperPresentationActive() const
    {
        return Settings::Instance().AllowChromaOverride() &&
               baseConfig_ &&
               (baseConfig_->hasMapperPresentation || chromaMapDetected_);
    }

    PlaybackDiagnostics PlaybackSession::Diagnostics() const
    {
        return {
            decoder_.SourceWidth(), decoder_.SourceHeight(),
            decoder_.Width(), decoder_.Height(),
            decoder_.SourceFramesPerSecond(), effectiveFpsLimit_,
            requestedFrames_, presentedFrames_,
            decoder_.AverageDecodeMilliseconds(), automaticReductions_};
    }

    void PlaybackSession::Prepare(GlobalNamespace::BeatmapLevel* level)
    {
        Stop();
        baseConfig_.reset();
        config_.reset();
        levelDirectory_.clear();
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

    void PlaybackSession::RefreshDisplaySettings()
    {
        // A surface must never outlive the effective configuration that
        // created it. Menu setting changes normally arrive after the preview
        // stopped playback, but this guard keeps future callers safe as well.
        if(started_)
            Stop();

        RebuildEffectiveConfig();
    }

    void PlaybackSession::RebuildEffectiveConfig()
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
        if(MapperPresentationActive())
        {
            // Cinema/Chroma compatibility is deliberately all-or-nothing for
            // geometry. Mixing a mapper's close, angled screen with a user's
            // back-wall offsets is the exact failure this opt-in prevents.
            // Transparency remains a user preference only when the mapper did
            // not explicitly choose it, matching PC Cinema's nullable field.
            config_->transparent = config_->mapperTransparency.value_or(
                settings.TransparencyEnabled());
            if(!config_->cinemaCurvatureDegrees)
            {
                config_->screenCurvature = settings.CurvedScreenEnabled()
                    ? settings.ScreenCurvature() : 0.0f;
                config_->maintainAspectRatioWhenCurved =
                    settings.CurvedScreenEnabled() &&
                    settings.MaintainCurveAspectRatio();
            }
            PaperLogger.info(
                "Allow Chroma Override is yielding screen and environment presentation to the mapper/Chroma");
            return;
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
        config_->transparent = settings.TransparencyEnabled();
        if(settings.AdvancedOptionsEnabled())
        {
            config_->videoRotation = settings.VideoRotation();
            config_->videoZoom = settings.VideoZoom();
            config_->videoOffsetX = settings.VideoOffsetX();
            config_->videoOffsetY = settings.VideoOffsetY();
            config_->videoTilt = settings.VideoTilt();
            config_->stretchVideoToFit = settings.StretchVideoToFit();
        }
    }

    bool PlaybackSession::ApplyActiveScreenLayoutLive()
    {
        // Mapper/Chroma presentation intentionally ignores user layouts. The
        // pause control is hidden for that case, but keep this guard here so a
        // stale callback can never override the mapper's live screen.
        if(!IsGameplayActive() || !baseConfig_ || MapperPresentationActive())
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
            lastPresentationSlot_.reset();
        }
        else
        {
            surface_.SetVisible(false);
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

        std::string error;
        effectiveFpsLimit_ = Settings::Instance().PlaybackFpsLimit();
        effectiveResolutionHeight_ = Settings::Instance().ResolutionHeight();
        if(!decoder_.Open(
               config_->videoPath,
               effectiveResolutionHeight_,
               error))
        {
            PaperLogger.error("Could not start video playback: {}", error);
            ErrorManager::Instance().ReportUserVisible(
                "Video playback error",
                "Big Screen could not open this video's H.264 stream. " + error);
            return;
        }

        if(!surface_.Create(*config_, decoder_.Width(), decoder_.Height()))
        {
            PaperLogger.error("Could not create the Unity video screen");
            ErrorManager::Instance().ReportInternal(
                "creating video screen", "Unity could not create the screen surface");
            decoder_.Close();
            return;
        }

        started_ = true;
        playbackFailed_ = false;
        gameplayScreenEnabled_ = true;
        firstFrameUploaded_ = false;
        lastPresentationSlot_.reset();
        lastTickSongTime_ = 0.0;
        context_ = context;
        requestedFrames_ = 0;
        presentedFrames_ = 0;
        windowRequestedFrames_ = 0;
        windowPresentedFrames_ = 0;
        performanceWindowStartSongTime_ = 0.0;
        automaticReductions_ = 0;
        diagnosticsFrameCounter_ = 0;
        mapperEnvironmentApplyCountdown_ =
            context == PlaybackContext::Gameplay && MapperPresentationActive()
                ? 3 : 0;

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
            ++windowRequestedFrames_;
            lastPresentationSlot_ = static_cast<std::int64_t>(std::floor(
                initialSongTime * fpsLimit + 0.000001));
            lastTickSongTime_ = initialSongTime;
        }
        PaperLogger.info(
            "Started {}x{} {} video screen at no more than {} FPS (duration {:.3f}s)",
            decoder_.Width(),
            decoder_.Height(),
            context == PlaybackContext::MenuPreview ? "song-menu" :
                context == PlaybackContext::LibraryPreview ? "library-preview" : "gameplay",
            effectiveFpsLimit_,
            decoder_.DurationSeconds());
    }

    void PlaybackSession::Tick(double songTimeSeconds)
    {
        if(!Settings::Instance().ModEnabled() || !started_ || !config_)
            return;

        if(playbackFailed_)
            return;

        if(auto decoderError = decoder_.TakeError())
        {
            playbackFailed_ = true;
            surface_.SetVisible(false);
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
            lastTickSongTime_ = songTimeSeconds;
            return;
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
        if(!lastPresentationSlot_ ||
           clockDiscontinuity ||
           presentationSlot != *lastPresentationSlot_)
        {
            decoder_.Request(mediaTime);
            ++requestedFrames_;
            ++windowRequestedFrames_;
            lastPresentationSlot_ = presentationSlot;
        }
        lastTickSongTime_ = songTimeSeconds;

        if(context_ == PlaybackContext::Gameplay &&
           Settings::Instance().PerformanceDiagnosticsEnabled() &&
           ++diagnosticsFrameCounter_ >= 30)
        {
            diagnosticsFrameCounter_ = 0;
            const auto d = Diagnostics();
            const auto missed = d.requestedFrames > d.presentedFrames
                ? d.requestedFrames - d.presentedFrames : 0;
            const double missedPercent = d.requestedFrames == 0
                ? 0.0 : 100.0 * missed / static_cast<double>(d.requestedFrames);
            std::ostringstream text;
            text << d.sourceWidth << 'x' << d.sourceHeight << " @ "
                 << std::fixed << std::setprecision(1) << d.sourceFps
                 << " FPS  ->  " << d.outputWidth << 'x' << d.outputHeight
                 << " @ " << d.outputFpsLimit << " FPS\nMissed "
                 << std::setprecision(1) << missedPercent << "%  |  Decode "
                 << std::setprecision(2) << d.averageDecodeMilliseconds << " ms";
            surface_.SetDiagnosticsText(text.str());
        }
        else if(context_ == PlaybackContext::Gameplay &&
                !Settings::Instance().PerformanceDiagnosticsEnabled() &&
                diagnosticsFrameCounter_ != 0)
        {
            diagnosticsFrameCounter_ = 0;
            surface_.SetDiagnosticsText("");
        }

        if(context_ == PlaybackContext::Gameplay &&
           Settings::Instance().AutomaticPerformanceEnabled())
        {
            if(performanceWindowStartSongTime_ <= 0.0)
                performanceWindowStartSongTime_ = songTimeSeconds;
            else if(songTimeSeconds - performanceWindowStartSongTime_ >= 5.0)
            {
                const auto missed = windowRequestedFrames_ > windowPresentedFrames_
                    ? windowRequestedFrames_ - windowPresentedFrames_ : 0;
                const double missedPercent = windowRequestedFrames_ == 0
                    ? 0.0
                    : 100.0 * missed / static_cast<double>(windowRequestedFrames_);
                if(missedPercent >= Settings::Instance().AutomaticPerformanceThreshold())
                    ApplyAutomaticPerformanceReduction(mediaTime);
                windowRequestedFrames_ = 0;
                windowPresentedFrames_ = 0;
                performanceWindowStartSongTime_ = songTimeSeconds;
            }
        }

        VideoFrame frame;
        if(!decoder_.TryTake(frame))
            return;

        ++presentedFrames_;
        ++windowPresentedFrames_;

        if(surface_.Upload(frame))
        {
            firstFrameUploaded_ = true;
            surface_.SetVisible(true);
        }
    }

    void PlaybackSession::Stop()
    {
        if(started_)
            CaptureDiagnosticsSummary();
        // Unity objects must be destroyed before closing the decoder because
        // this function is called by a main-thread scene-transition hook. The
        // decoder close then joins its worker so no FFmpeg state survives into
        // the menu or the next level.
        surface_.Destroy();
        decoder_.Close();
        started_ = false;
        playbackFailed_ = false;
        gameplayScreenEnabled_ = true;
        firstFrameUploaded_ = false;
        lastPresentationSlot_.reset();
        lastTickSongTime_ = 0.0;
        context_ = PlaybackContext::None;
        mapperEnvironmentApplyCountdown_ = 0;
    }

    bool PlaybackSession::ApplyAutomaticPerformanceReduction(double mediaTimeSeconds)
    {
        const auto [changed, tier] = CoreLogic::NextPerformanceTier(
            effectiveFpsLimit_, effectiveResolutionHeight_);
        if(!changed)
            return false;
        const auto [nextFps, nextResolution] = tier;

        if(nextResolution != effectiveResolutionHeight_)
        {
            // A texture cannot change dimensions in place. Reopen only Big
            // Screen's decoder/surface at the current audio-derived timestamp;
            // Beat Saber's map clock and gameplay scene continue uninterrupted.
            surface_.Destroy();
            decoder_.Close();
            std::string error;
            if(!decoder_.Open(config_->videoPath, nextResolution, error) ||
               !surface_.Create(*config_, decoder_.Width(), decoder_.Height()))
            {
                ErrorManager::Instance().ReportInternal(
                    "applying automatic performance reduction",
                    error.empty() ? "Could not recreate the lower-resolution screen" : error);
                return false;
            }
            decoder_.Request(mediaTimeSeconds);
            firstFrameUploaded_ = false;
            effectiveResolutionHeight_ = nextResolution;
        }
        effectiveFpsLimit_ = nextFps;
        lastPresentationSlot_.reset();
        ++automaticReductions_;
        PaperLogger.warn(
            "Automatic Performance reduced video output to {}p / {} FPS",
            effectiveResolutionHeight_, effectiveFpsLimit_);
        return true;
    }

    void PlaybackSession::CaptureDiagnosticsSummary()
    {
        const auto diagnostics = Diagnostics();
        const auto missed = diagnostics.requestedFrames > diagnostics.presentedFrames
            ? diagnostics.requestedFrames - diagnostics.presentedFrames : 0;
        const double missedPercent = diagnostics.requestedFrames == 0
            ? 0.0
            : 100.0 * missed / static_cast<double>(diagnostics.requestedFrames);
        std::ostringstream text;
        text << diagnostics.sourceWidth << 'x' << diagnostics.sourceHeight
             << " @ " << std::fixed << std::setprecision(1)
             << diagnostics.sourceFps << " FPS source  |  "
             << diagnostics.outputWidth << 'x' << diagnostics.outputHeight
             << " @ " << diagnostics.outputFpsLimit << " FPS output\n"
             << "Presented " << diagnostics.presentedFrames << " / "
             << diagnostics.requestedFrames << " frames  |  Missed "
             << std::setprecision(1) << missedPercent << "%  |  Decode "
             << std::setprecision(2) << diagnostics.averageDecodeMilliseconds
             << " ms";
        if(diagnostics.automaticReductions > 0)
            text << "  |  Automatic reductions " << diagnostics.automaticReductions;
        lastDiagnosticsSummary_ = text.str();
    }

    void PlaybackSession::FinalizeDiagnosticsDisplay()
    {
        if(!started_ || !Settings::Instance().PerformanceDiagnosticsEnabled())
            return;
        CaptureDiagnosticsSummary();
        surface_.SetDiagnosticsText(lastDiagnosticsSummary_);
    }
}
