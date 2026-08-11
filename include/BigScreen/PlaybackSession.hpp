#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ScreenSurface.hpp"

namespace GlobalNamespace {
    class BeatmapLevel;
}

namespace BigScreen {
    /// Coordinates one selected map, one decoder, and one Unity screen.
    ///
    /// The transition hook prepares ordinary file metadata. The gameplay clock
    /// hook then creates Unity/FFmpeg resources and drives them using Beat
    /// Saber's song position. Splitting those phases prevents a menu selection
    /// from accidentally creating scene objects in the wrong Unity scene.
    class PlaybackSession final {
    public:
        static PlaybackSession& Instance();

        void Prepare(GlobalNamespace::BeatmapLevel* level);
        void Start();
        void Tick(double songTimeSeconds);
        void Stop();

        bool HasPreparedVideo() const { return config_.has_value(); }
        const std::optional<std::string>& RequestedEnvironment() const;

    private:
        PlaybackSession() = default;

        std::filesystem::path levelDirectory_;
        std::optional<MapVideoConfig> config_;
        FrameDecoder decoder_;
        ScreenSurface surface_;
        bool started_ = false;
        bool firstFrameUploaded_ = false;
    };
}
