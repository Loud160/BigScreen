#pragma once

#include <cstdint>
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
    struct PlaybackDiagnostics {
        int sourceWidth = 0;
        int sourceHeight = 0;
        int outputWidth = 0;
        int outputHeight = 0;
        double sourceFps = 0.0;
        int outputFpsLimit = 0;
        std::uint64_t requestedFrames = 0;
        std::uint64_t presentedFrames = 0;
        double averageDecodeMilliseconds = 0.0;
        int automaticReductions = 0;
    };

    enum class PlaybackContext {
        None,
        MenuPreview,
        LibraryPreview,
        Gameplay
    };

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
        /// Rebuilds the selected map's effective display configuration from
        /// mapper-authored metadata and the latest global settings.
        void RefreshDisplaySettings();
        void Start(PlaybackContext context);
        void Tick(double songTimeSeconds);
        void Stop();

        bool HasPreparedVideo() const { return config_.has_value(); }
        bool IsMenuPreviewActive() const { return context_ == PlaybackContext::MenuPreview; }
        bool IsLibraryPreviewActive() const { return context_ == PlaybackContext::LibraryPreview; }
        const std::optional<MapVideoConfig>& PreparedConfig() const { return config_; }
        const std::optional<MapVideoConfig>& PreparedBaseConfig() const { return baseConfig_; }
        const std::optional<std::string>& RequestedEnvironment() const;
        PlaybackDiagnostics Diagnostics() const;
        /// Freezes the current statistics and keeps them visible beside the
        /// video during Beat Saber's failed-level overlay.
        void FinalizeDiagnosticsDisplay();
        const std::string& LastDiagnosticsSummary() const {
            return lastDiagnosticsSummary_;
        }

    private:
        PlaybackSession() = default;
        bool ApplyAutomaticPerformanceReduction(double mediaTimeSeconds);
        void CaptureDiagnosticsSummary();

        std::filesystem::path levelDirectory_;
        // Keep the mapper-authored configuration immutable. Reapplying global
        // offsets to an already-adjusted config would accumulate scale and
        // placement changes, especially after Reset to Defaults.
        std::optional<MapVideoConfig> baseConfig_;
        std::optional<MapVideoConfig> config_;
        FrameDecoder decoder_;
        ScreenSurface surface_;
        double menuPreviewStartSongTime_ = 0.0;
        bool started_ = false;
        bool firstFrameUploaded_ = false;
        // The presentation limiter lives above FFmpeg so decoder timestamps
        // remain untouched. Native-rate gating still occurs inside FrameDecoder.
        std::optional<std::int64_t> lastPresentationSlot_;
        double lastTickSongTime_ = 0.0;
        int effectiveFpsLimit_ = 30;
        int effectiveResolutionHeight_ = 720;
        std::uint64_t requestedFrames_ = 0;
        std::uint64_t presentedFrames_ = 0;
        std::uint64_t windowRequestedFrames_ = 0;
        std::uint64_t windowPresentedFrames_ = 0;
        double performanceWindowStartSongTime_ = 0.0;
        int automaticReductions_ = 0;
        int diagnosticsFrameCounter_ = 0;
        std::string lastDiagnosticsSummary_;
        PlaybackContext context_ = PlaybackContext::None;
    };
}
