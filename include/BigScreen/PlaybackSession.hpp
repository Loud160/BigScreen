// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/CinemaScreenGroup.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ScreenSurface.hpp"
#include "BigScreen/ShowcaseSurfaceGroup.hpp"

namespace GlobalNamespace {
    class BeatmapLevel;
}

namespace BigScreen {
    struct PlaybackDiagnostics {
        /// Presentation-oriented native dimensions after container rotation.
        int videoWidth = 0;
        int videoHeight = 0;
        double sourceFps = 0.0;
        int outputFpsLimit = 0;
        std::uint64_t requestedFrames = 0;
        std::uint64_t expectedFrames = 0;
        std::uint64_t presentedFrames = 0;
        /// Cumulative output deadlines missed during this playback session.
        /// Unlike expected-presented backlog, this value is monotonic.
        std::uint64_t missedFrames = 0;
        std::uint64_t rgbaBufferAllocations = 0;
        double averageDecodeMilliseconds = 0.0;
        double peakDecodeMilliseconds = 0.0;
        double decoderOpenMilliseconds = 0.0;
        double decoderCpuMilliseconds = 0.0;
        double averagePresentationMilliseconds = 0.0;
        double peakPresentationMilliseconds = 0.0;
        int automaticReductions = 0;
        /// Hardware/software decode method. The separately selectable FFmpeg
        /// 4.4/9 implementation is reported by decoderRuntime.
        std::string decodeMethod;
        std::string decoderRuntime;
        std::string codec;
        std::string presentationMethod;
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
        /// Clears a preview-owned prepared configuration only when it still
        /// belongs to the supplied level. Leaving Big Screen's menu uses this
        /// to prevent Campaign—which has its own transition type—from
        /// accidentally inheriting the last editor preview. A newly prepared
        /// gameplay level with a different ID is never disturbed.
        void ClearPreparedPreviewForLevel(std::string_view levelId);

        bool HasPreparedVideo() const {
            return config_.has_value() && !environmentOnlySession_;
        }
        bool HasPreparedPresentation() const { return config_.has_value(); }
        /// True when the showcase owns its authored canvas or Respect Mapper
        /// Settings is enabled and Cinema metadata authors screen geometry.
        bool MapperScreenPresentationActive() const;
        /// True when Cinema's explicit environment data is respected or an
        /// actually detected Chroma map is allowed to retain its environment.
        /// This intentionally does not imply ownership of the video canvas.
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
        /// Begins a new Library-preview pass without rebuilding the Unity
        /// screen. Readiness is invalidated until a picture produced after
        /// this restart reaches the texture, preventing audio from running
        /// ahead of a decoder that was previously drained at EOF.
        bool RestartLibraryPreview(double songTimeSeconds);
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
        static FrameVisualEffects VisualEffectsFor(
            const MapVideoConfig& config);
        bool OpenDecoder(std::string& error);
        bool LoadCinemaCompatibilityCycle();
        bool ApplyCinemaCompatibilityCyclePhase(
            std::size_t phaseIndex,
            double songTimeSeconds);
        bool ApplyAutomaticPerformanceReduction();
        bool ApplyAutomaticPerformanceRecovery();
        void ApplyAutomaticPerformanceFpsLimit(int nextFps);
        void ResetAutomaticPerformanceWindow(double songTimeSeconds);
        void ResetAutomaticPerformanceController(double songTimeSeconds);
        void CaptureDiagnosticsSummary();
        bool ReopenLibraryPreviewDecoder(double mediaTime);

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
        CinemaScreenGroup cinemaScreens_;
        ShowcaseSurfaceGroup showcase_;
        bool showcaseEligible_ = false;
        // The bundled no-note WIP cycle is a private validation fixture, not a
        // general mapper timeline. Every phase is loaded from an ordinary
        // Cinema JSON object and applied through the production screen,
        // decoder, additional-screen and environment paths.
        bool cinemaCompatibilityCycleEligible_ = false;
        bool cinemaCompatibilityCycleScreenHidden_ = false;
        std::size_t cinemaCompatibilityCyclePhase_ = 0;
        std::vector<MapVideoConfig> cinemaCompatibilityCycleConfigs_;
        double menuPreviewStartSongTime_ = 0.0;
        bool started_ = false;
        bool gameplayDecoderPrewarmed_ = false;
        // A failed prewarm already queued a safe user-visible error. Remember
        // it through Start() so the same transition does not immediately retry
        // the identical open and duplicate both work and error reporting.
        bool gameplayPrewarmFailed_ = false;
        std::string gameplayPrewarmError_;
        bool firstFrameUploaded_ = false;
        // An EOF loop has a stricter readiness contract than an ordinary
        // seek: only a picture decoded after Restart may release the audio.
        // A single bounded reopen handles Android MediaCodec implementations
        // that do not resume after avcodec_flush_buffers at drained EOF.
        bool libraryPreviewRestartPending_ = false;
        bool libraryPreviewRestartReopenAttempted_ = false;
        std::uint64_t libraryPreviewRestartGeneration_ = 0;
        std::chrono::steady_clock::time_point libraryPreviewRestartStarted_{};
        bool gameplayScreenEnabled_ = true;
        // A decoder failure hides only this session's screen. Beat Saber keeps
        // playing and ErrorManager postpones the explanation until it is safe.
        bool playbackFailed_ = false;
        // Cinema permits an environment-only configuration when
        // forceEnvironmentModifications is true. Such a session participates
        // in scene ownership and cleanup but never opens FFmpeg or constructs
        // a video surface.
        bool environmentOnlySession_ = false;
        // Cinema fades the picture over its final second. Cache the last
        // applied multiplier so steady playback performs no material writes.
        float appliedMapperEndFade_ = 1.0f;
        // The presentation limiter lives above FFmpeg so decoder timestamps
        // remain untouched. Native-rate gating still occurs inside FrameDecoder.
        std::optional<std::int64_t> lastPresentationSlot_;
        double lastTickSongTime_ = 0.0;
        int effectiveFpsLimit_ = 30;
        std::uint64_t requestedFrames_ = 0;
        // Presentation loss compares pictures that successfully reached Unity
        // with source-aware output deadlines advanced by Beat Saber's song
        // clock. Fractional deadlines must survive between Unity updates or a
        // 60 FPS source sampled by a 72 Hz headset would be undercounted.
        std::uint64_t deliveredPresentedFrames_ = 0;
        std::uint64_t expectedPresentationDeadlines_ = 0;
        double expectedPresentationFraction_ = 0.0;
        // The live backlog may decrease when FFmpeg catches up. Keep a
        // separate deadline-outcome accumulator for the user-facing total so
        // "Frames Skipped" never changes from eight back to seven.
        CoreLogic::PresentationMissAccumulator presentationMisses_;
        std::uint64_t windowDeliveredPresentedFrames_ = 0;
        std::uint64_t windowExpectedPresentationDeadlines_ = 0;
        double windowExpectedPresentationFraction_ = 0.0;
        double performanceWindowStartSongTime_ = 0.0;
        std::uint64_t diagnosticsWindowDeliveredPresentedFrames_ = 0;
        std::uint64_t diagnosticsWindowExpectedPresentationDeadlines_ = 0;
        double diagnosticsWindowExpectedPresentationFraction_ = 0.0;
        double diagnosticsWindowStartSongTime_ = 0.0;
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
        // The facade retains worker CPU time across hardware-to-software
        // recovery. This baseline excludes pre-map/pre-audio warmup work.
        double decoderCpuBaselineMilliseconds_ = 0.0;
        // Each successful reduction stores the exact tier it replaced. A
        // healthy user-selected response window pops one entry, which
        // guarantees that FPS limits are restored in the reverse order they
        // were lowered. The controller continues evaluating throughout the
        // map, so later pressure can lower and recover repeatedly.
        CoreLogic::AutomaticPerformanceHistory automaticPerformanceHistory_;
        // A failed recovery is the exact low->high->low pair that causes FPS
        // flapping. Once the configurable number of failures is reached, only
        // further reductions are allowed for the rest of this playback session.
        std::optional<int> lastAutomaticRecoveryLowFps_;
        std::optional<int> lastAutomaticRecoveryHighFps_;
        int automaticPerformanceFailedRecoveries_ = 0;
        bool automaticPerformanceRecoveryPinned_ = false;
        int diagnosticsFrameCounter_ = 0;
        bool diagnosticsVisible_ = false;
        double averagePresentationMilliseconds_ = 0.0;
        double peakPresentationMilliseconds_ = 0.0;
        std::string presentationMethod_ = "CPU RGBA";
        bool gpuConversionFallbackLogged_ = false;
        bool gpuConversionDisabledForSession_ = false;
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
