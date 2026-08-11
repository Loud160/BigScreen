#include "BigScreen/PlaybackSession.hpp"

#include <string>

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
        config_.reset();
        levelDirectory_.clear();

        if(!level || !level->levelID)
            return;

        // SongCore is the public owner of custom-level paths on Quest. Looking
        // up by Beat Saber's level ID avoids guessing installation directories
        // and works for both CustomLevels and CustomWIPLevels.
        const std::string levelId(level->levelID);
        auto* customLevel = SongCore::API::Loading::GetLevelByLevelID(levelId);
        if(!customLevel)
        {
            PaperLogger.debug("Selected level is not a SongCore custom level: {}", levelId);
            return;
        }

        levelDirectory_ = std::filesystem::path(customLevel->get_customLevelPath());
        std::string error;
        config_ = MapVideoConfig::LoadFromLevel(levelDirectory_, error);
        if(!error.empty())
        {
            PaperLogger.error("Big Screen metadata rejected for '{}': {}", levelDirectory_.string(), error);
            config_.reset();
            return;
        }

        if(config_)
        {
            PaperLogger.info(
                "Prepared video '{}' from '{}'",
                config_->videoPath.filename().string(),
                config_->metadataPath.filename().string());
        }
        else
        {
            PaperLogger.debug("No supported video metadata in '{}'", levelDirectory_.string());
        }
    }

    void PlaybackSession::Start(PlaybackContext context)
    {
        if(!config_ || started_ || context == PlaybackContext::None)
            return;

        std::string error;
        if(!decoder_.Open(config_->videoPath, error))
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
        if(!started_ || !config_)
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
