#include "BigScreen/PlaybackSession.hpp"

#include <string>

#include "BigScreen/Settings.hpp"
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

    void PlaybackSession::Prepare(GlobalNamespace::BeatmapLevel* level)
    {
        Stop();
        baseConfig_.reset();
        config_.reset();
        levelDirectory_.clear();

        // Hooks remain installed for the lifetime of the process, but the
        // master switch makes every entry point inert. Keeping this guard here
        // as well as in callers prevents future code from accidentally parsing
        // map files while Big Screen is disabled.
        if(!Settings::Instance().ModEnabled())
            return;

        if(!level || !level->levelID)
            return;

        const std::string levelId(level->levelID);
        baseConfig_ = VideoLibrary::Instance().ResolvePlayback(level);

        if(baseConfig_)
        {
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
        config_->screenPosition.x += settings.ScreenHorizontalOffset();
        config_->screenPosition.y += settings.ScreenVerticalOffset();
        config_->screenPosition.z += settings.ScreenDistanceOffset();
        config_->screenRotation.x += settings.ScreenTiltOffset();
        config_->screenHeight *= settings.ScreenScale();
        config_->screenCurvature = settings.CurvedScreenEnabled()
            ? settings.ScreenCurvature()
            : 0.0f;
        config_->transparent = settings.TransparencyEnabled();
    }

    void PlaybackSession::Start(PlaybackContext context)
    {
        if(!Settings::Instance().ModEnabled() ||
           !config_ ||
           started_ ||
           context == PlaybackContext::None)
            return;

        std::string error;
        if(!decoder_.Open(
               config_->videoPath,
               Settings::Instance().ResolutionHeight(),
               error))
        {
            PaperLogger.error("Could not start video playback: {}", error);
            return;
        }

        if(!surface_.Create(*config_, decoder_.Width(), decoder_.Height()))
        {
            PaperLogger.error("Could not create the Unity video screen");
            decoder_.Close();
            return;
        }

        started_ = true;
        firstFrameUploaded_ = false;
        context_ = context;
        PaperLogger.info(
            "Started {}x{} {} video screen (duration {:.3f}s)",
            decoder_.Width(),
            decoder_.Height(),
            context == PlaybackContext::MenuPreview ? "menu-preview" : "gameplay",
            decoder_.DurationSeconds());
    }

    void PlaybackSession::Tick(double songTimeSeconds)
    {
        if(!Settings::Instance().ModEnabled() || !started_ || !config_)
            return;

        const double mediaTime = config_->MediaTimeForSong(
            songTimeSeconds,
            decoder_.DurationSeconds());

        // Negative media time means a mapper intentionally delayed the video.
        // Keeping the last texture hidden avoids displaying frame zero early.
        const bool withinConfiguredRange =
            mediaTime >= 0.0 &&
            (!config_->stopAtVideoSecond || mediaTime <= *config_->stopAtVideoSecond);
        if(!withinConfiguredRange)
        {
            surface_.SetVisible(false);
            return;
        }

        decoder_.Request(mediaTime);

        VideoFrame frame;
        if(!decoder_.TryTake(frame))
            return;

        if(surface_.Upload(frame))
        {
            firstFrameUploaded_ = true;
            surface_.SetVisible(true);
        }
    }

    void PlaybackSession::Stop()
    {
        // Unity objects must be destroyed before closing the decoder because
        // this function is called by a main-thread scene-transition hook. The
        // decoder close then joins its worker so no FFmpeg state survives into
        // the menu or the next level.
        surface_.Destroy();
        decoder_.Close();
        started_ = false;
        firstFrameUploaded_ = false;
        context_ = PlaybackContext::None;
    }
}
