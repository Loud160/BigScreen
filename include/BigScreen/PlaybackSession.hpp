#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ScreenSurface.hpp"
#include "BigScreen/ShowcaseSurfaceGroup.hpp"

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
        double peakDecodeMilliseconds = 0.0;
        double decoderCpuMilliseconds = 0.0;
        int automaticReductions = 0;
        std::string decoderBackend;
        std::string decoderRuntime;
        std::string codec;
    };

    /// Immutable snapshot used by the results-screen presentation. Keeping the
    /// values structured avoids scraping the human-readable log line back into
    /// fields and lets the UI change without changing the append-only log.
    struct PlaybackResultsData {
        PlaybackDiagnostics video;
        double minimumGameplayFps = 0.0;
        double averageGameplayFps = 0.0;
        double maximumGameplayFps = 0.0;
        std::uint64_t sampledGameplayFrames = 0;
        std::uint64_t missedVideoFrames = 0;
        double averageVideoFps = 0.0;
        double missedVideoFramePercent = 0.0;
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
        /// Supplies the characteristic/difficulty selected for the upcoming
        /// gameplay transition. BeatmapLevel alone cannot distinguish the
        /// Lawless showcase chart from the map's normal Expert+ chart.
        void ConfigureGameplayBeatmap(
            const std::string& characteristic,
            int difficulty);
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
        /// Applies a new user FPS cap without reopening FFmpeg or requiring
        /// preview pause/resume. It also starts a clean comparison window.
        void RefreshPlaybackFpsLimitLive();
        void Stop();

        bool HasPreparedVideo() const { return config_.has_value(); }
        /// True only when Allow Chroma Override is enabled, the map actually
        /// uses Chroma, and its video metadata authors custom screen geometry.
        /// Only this predicate may suppress the player's canvas controls.
        bool MapperScreenPresentationActive() const;
        /// True when Chroma or Cinema environment metadata should retain
        /// ownership of the gameplay scene. This intentionally does not imply
        /// ownership of the video canvas.
        bool MapperEnvironmentPresentationActive() const;
        bool IsMenuPreviewActive() const { return context_ == PlaybackContext::MenuPreview; }
        bool IsLibraryPreviewActive() const { return context_ == PlaybackContext::LibraryPreview; }
        /// True after a decoded image from the active session has reached the
        /// Unity texture. Menu audio uses this as the same pre-start readiness
        /// boundary that gameplay receives from its scene-transition prewarm.
        bool FirstFrameUploaded() const { return started_ && firstFrameUploaded_; }
        /// True when menu audio may safely start at the requested song time.
        /// Visible video positions require an uploaded picture; an intentional
        /// negative lead-in is already ready because its correct presentation
        /// is the configured black or transparent background, not frame zero.
        bool SynchronizedAudioReady(double songTimeSeconds) const;
        /// Starts the measured Library-preview interval after its untimed
        /// decoder prewarm. Gameplay gets this same boundary from Start(),
        /// which runs after PrewarmGameplay during the scene transition.
        void BeginLibraryPreviewMeasurement(double songTimeSeconds);
        bool IsGameplayActive() const {
            return started_ && context_ == PlaybackContext::Gameplay;
        }
        /// Identifies the exact developer-demo chart after its gameplay
        /// characteristic and difficulty have been resolved.
        bool ShowcaseActive() const { return showcaseEligible_; }
        const std::optional<MapVideoConfig>& PreparedConfig() const { return config_; }
        const std::optional<MapVideoConfig>& PreparedBaseConfig() const { return baseConfig_; }
        const std::optional<std::string>& RequestedEnvironment() const;
        PlaybackDiagnostics Diagnostics() const;
        /// Supplies the last playable note time so headset-FPS statistics stop
        /// before Beat Saber's results transition begins. A missing value keeps
        /// the safe fallback of sampling until gameplay ends.
        void SetGameplayLastNoteTime(double songTimeSeconds);
        /// Freezes the current statistics and keeps them visible beside the
        /// video during Beat Saber's failed-level overlay.
        void FinalizeDiagnosticsDisplay();
        const std::string& LastDiagnosticsSummary() const {
            return lastDiagnosticsSummary_;
        }
        const std::optional<PlaybackResultsData>& LastResultsData() const {
            return lastResultsData_;
        }

    private:
        PlaybackSession() = default;
        void RebuildEffectiveConfig(
            PlaybackContext intendedContext = PlaybackContext::None);
        bool OpenDecoder(std::string& error);
        bool ApplyAutomaticPerformanceReduction(double mediaTimeSeconds);
        bool ApplyAutomaticPerformanceRecovery(double mediaTimeSeconds);
        bool ApplyAutomaticPerformanceTier(
            int nextFps,
            int nextResolution,
            double mediaTimeSeconds,
            const char* failureOperation);
        void CaptureDiagnosticsSummary();

        std::filesystem::path levelDirectory_;
        // Captured while the BeatmapLevel is known and retained through scene
        // teardown so the append-only performance record identifies the map.
        std::string preparedLevelId_;
        std::string preparedSongName_;
        std::string preparedSongArtist_;
        std::string preparedCharacteristic_;
        int preparedDifficulty_ = -1;
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
        ShowcaseSurfaceGroup showcase_;
        bool showcaseEligible_ = false;
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
        // Presentation statistics are derived only from timestamps of images
        // that successfully reached Unity. Thread scheduling latency is not a
        // missed frame; a gap in delivered media timestamps is.
        std::uint64_t deliveredPresentedFrames_ = 0;
        std::uint64_t missedPresentedFrames_ = 0;
        std::uint64_t windowDeliveredPresentedFrames_ = 0;
        std::uint64_t windowMissedPresentedFrames_ = 0;
        double performanceWindowStartSongTime_ = 0.0;
        std::uint64_t diagnosticsWindowDeliveredPresentedFrames_ = 0;
        std::uint64_t diagnosticsWindowMissedPresentedFrames_ = 0;
        double diagnosticsWindowStartSongTime_ = 0.0;
        std::optional<double> lastUploadedPresentationSeconds_;
        double lastUploadedDurationSeconds_ = 0.0;
        // Unity frame timing is sampled only while the active playback clock
        // advances. The same session-local counters feed the live Video
        // Library overlay and the final gameplay log, but only gameplay
        // sessions are ever persisted.
        double minimumFrameSeconds_ = 0.0;
        double maximumFrameSeconds_ = 0.0;
        double totalFrameSeconds_ = 0.0;
        double lastFpsSongTime_ = 0.0;
        std::uint64_t sampledFrames_ = 0;
        int automaticReductions_ = 0;
        // Automatic Performance may reopen the decoder when the output
        // resolution changes. Preserve completed worker CPU time across those
        // decoder instances so one map still has one meaningful total.
        double accumulatedDecoderCpuMilliseconds_ = 0.0;
        double decoderCpuBaselineMilliseconds_ = 0.0;
        // Each successful reduction stores the exact tier it replaced. A
        // healthy user-selected response window pops one entry, which
        // guarantees that resolution/FPS are restored in the reverse order
        // they were lowered. The controller continues evaluating throughout
        // the map, so later pressure can lower quality again.
        CoreLogic::AutomaticPerformanceHistory automaticPerformanceHistory_;
        int diagnosticsFrameCounter_ = 0;
        bool diagnosticsVisible_ = false;
        // Quest Chroma applies difficulty environment data from an end-of-frame
        // coroutine. Delay Cinema's separate environment array a few gameplay
        // updates so it remains the final mapper-authored scene pass.
        int mapperEnvironmentApplyCountdown_ = 0;
        std::string lastDiagnosticsSummary_;
        std::optional<PlaybackResultsData> lastResultsData_;
        std::optional<double> gameplayLastNoteTime_;
        bool gameplayFrameSamplingFinished_ = false;
        PlaybackContext context_ = PlaybackContext::None;
    };
}
