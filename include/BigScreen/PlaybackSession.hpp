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
        std::uint64_t expectedFrames = 0;
        std::uint64_t presentedFrames = 0;
        std::uint64_t rgbaBufferAllocations = 0;
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
        /// Temporarily applies the free-position editor's unsaved geometry to
        /// an active Video Library preview without restarting its decoder or
        /// audio. Returns false when the library preview does not own a live
        /// video surface.
        bool ApplyLibraryPreviewEditorDisplay(
            const MapVideoConfig& displayConfig,
            bool rebuildGeometry);
        /// Restores an active library preview after positioning. Saving first
        /// rebuilds from the newly persisted layout; cancelling reuses the
        /// unchanged effective configuration from before editing began.
        bool RestoreLibraryPreviewDisplay(bool useLatestSettings);
        /// Applies the currently selected user layout to a running gameplay
        /// surface without reopening FFmpeg or changing the playback clock.
        bool ApplyActiveScreenLayoutLive();
        /// Shows or hides only the current gameplay screen. This session-local
        /// override deliberately leaves the user's global Video In Map setting
        /// unchanged, so the next map still follows the saved preference.
        void SetGameplayScreenEnabled(bool enabled);
        bool GameplayScreenEnabled() const { return gameplayScreenEnabled_; }
        void Start(PlaybackContext context);
        /// Opens FFmpeg and starts decoding before Beat Saber's gameplay audio
        /// clock begins. Unity geometry is intentionally deferred until the
        /// gameplay scene exists.
        void PrewarmGameplay();
        void Tick(double songTimeSeconds);
        void Stop();

        bool HasPreparedVideo() const { return config_.has_value(); }
        /// True only when both the global opt-in and actual mapper-authored
        /// presentation data are present. Environment hooks use the same
        /// predicate as screen rendering so the two halves cannot disagree.
        bool MapperPresentationActive() const;
        bool IsMenuPreviewActive() const { return context_ == PlaybackContext::MenuPreview; }
        bool IsLibraryPreviewActive() const { return context_ == PlaybackContext::LibraryPreview; }
        bool IsGameplayActive() const {
            return started_ && context_ == PlaybackContext::Gameplay;
        }
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
        void RebuildEffectiveConfig(
            PlaybackContext intendedContext = PlaybackContext::None);
        bool OpenDecoder(std::string& error);
        bool ApplyAutomaticPerformanceReduction(double mediaTimeSeconds);
        void CaptureDiagnosticsSummary();

        std::filesystem::path levelDirectory_;
        // This flag is independent of cinema-video.json. A user-assigned video
        // can make any Chroma map a Big Screen map, and Chroma still needs
        // ownership of that map's environment even when Cinema metadata is
        // absent.
        bool chromaMapDetected_ = false;
        // Keep the mapper-authored configuration immutable. Reapplying global
        // offsets to an already-adjusted config would accumulate scale and
        // placement changes, especially after Reset to Defaults.
        std::optional<MapVideoConfig> baseConfig_;
        std::optional<MapVideoConfig> config_;
        FrameDecoder decoder_;
        ScreenSurface surface_;
        double menuPreviewStartSongTime_ = 0.0;
        bool started_ = false;
        bool gameplayDecoderPrewarmed_ = false;
        // A failed prewarm already queued a safe user-visible error. Remember
        // it through Start() so the same transition does not immediately retry
        // the identical open and duplicate both work and error reporting.
        bool gameplayPrewarmFailed_ = false;
        std::string gameplayPrewarmError_;
        bool firstFrameUploaded_ = false;
        bool gameplayScreenEnabled_ = true;
        // A decoder failure hides only this session's screen. Beat Saber keeps
        // playing and ErrorManager postpones the explanation until it is safe.
        bool playbackFailed_ = false;
        // The presentation limiter lives above FFmpeg so decoder timestamps
        // remain untouched. Native-rate gating still occurs inside FrameDecoder.
        std::optional<std::int64_t> lastPresentationSlot_;
        double lastTickSongTime_ = 0.0;
        int effectiveFpsLimit_ = 30;
        int effectiveResolutionHeight_ = 720;
        std::uint64_t requestedFrames_ = 0;
        double expectedFrameAccumulator_ = 0.0;
        std::uint64_t presentedFrames_ = 0;
        double windowExpectedFrameAccumulator_ = 0.0;
        std::uint64_t windowPresentedFrames_ = 0;
        double performanceWindowStartSongTime_ = 0.0;
        int automaticReductions_ = 0;
        int diagnosticsFrameCounter_ = 0;
        // Quest Chroma applies difficulty environment data from an end-of-frame
        // coroutine. Delay Cinema's separate environment array a few gameplay
        // updates so it remains the final mapper-authored scene pass.
        int mapperEnvironmentApplyCountdown_ = 0;
        std::string lastDiagnosticsSummary_;
        PlaybackContext context_ = PlaybackContext::None;
    };
}
