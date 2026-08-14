#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace BigScreen::CoreLogic {
    /// Advances a menu-preview clock smoothly between the coarse updates that
    /// Unity exposes through AudioSource.time. Large differences are real
    /// seeks/restarts and re-anchor immediately; ordinary buffer quantization
    /// is corrected gradually so a 60 FPS video does not skip presentation
    /// slots merely because the audio property advances in larger steps.
    inline double AdvanceSmoothedPreviewClock(
        double smoothedSongSeconds,
        double rawAudioSongSeconds,
        double realSecondsElapsed)
    {
        if(!std::isfinite(rawAudioSongSeconds) || rawAudioSongSeconds < 0.0)
            return std::max(0.0, smoothedSongSeconds);
        if(!std::isfinite(smoothedSongSeconds) || smoothedSongSeconds < 0.0 ||
           !std::isfinite(realSecondsElapsed) || realSecondsElapsed < 0.0)
            return rawAudioSongSeconds;

        const double predicted = smoothedSongSeconds +
            std::clamp(realSecondsElapsed, 0.0, 0.10);
        const double error = rawAudioSongSeconds - predicted;
        if(std::abs(error) > 0.075)
            return rawAudioSongSeconds;
        return std::max(
            0.0,
            predicted + std::clamp(error * 0.08, -0.0015, 0.0015));
    }

    /// Converts CPU milliseconds accumulated by one process or thread into the
    /// conventional percentage of one fully occupied core. Values above 100
    /// are valid for a multi-threaded process.
    inline double CpuPercentOfOneCore(
        double cpuMilliseconds,
        double wallSeconds)
    {
        if(cpuMilliseconds <= 0.0 || wallSeconds <= 0.0)
            return 0.0;
        return cpuMilliseconds / (wallSeconds * 10.0);
    }

    inline double EquivalentCpuCores(
        double cpuMilliseconds,
        double wallSeconds)
    {
        return CpuPercentOfOneCore(cpuMilliseconds, wallSeconds) / 100.0;
    }

    /// Converts a fuel-gauge charge-counter decrease to an hourly drain rate.
    /// A rising counter means the headset was charging and is intentionally
    /// reported as zero consumption rather than a negative battery drain.
    inline double ChargeConsumedMicroampHours(
        std::int64_t startMicroampHours,
        std::int64_t endMicroampHours)
    {
        return static_cast<double>(std::max<std::int64_t>(
            0,
            startMicroampHours - endMicroampHours));
    }

    inline double ConsumptionRateMahPerHour(
        double consumedMicroampHours,
        double wallSeconds)
    {
        if(consumedMicroampHours <= 0.0 || wallSeconds <= 0.0)
            return 0.0;
        return (consumedMicroampHours / 1000.0) /
               (wallSeconds / 3600.0);
    }
    inline constexpr float MinimumScreenScale = 0.5f;
    inline constexpr float CurvedScreenMaximumScale = 2.5f;
    inline constexpr float FlatScreenMaximumScale = 4.0f;

    /// Curved geometry generates many more vertices and grows much wider as
    /// scale increases, so it retains the tested 2.5x ceiling. A flat quad can
    /// safely use the larger 4x presentation requested by the player.
    inline constexpr float ScreenScaleMaximum(bool curved)
    {
        return curved ? CurvedScreenMaximumScale : FlatScreenMaximumScale;
    }

    inline float NormalizeScreenScale(float value, bool curved)
    {
        return std::clamp(
            value,
            MinimumScreenScale,
            ScreenScaleMaximum(curved));
    }

    struct VideoContentSize {
        float width;
        float height;
    };

    struct FrameRateStats {
        double minimumFps = 0.0;
        double averageFps = 0.0;
        double maximumFps = 0.0;
        std::uint64_t sampledFrames = 0;
    };

    /// Gameplay statistics describe only the playable body of a map. The
    /// caller latches samplingFinished after crossing the final note so replay
    /// seeks or results animations cannot reopen the measurement window.
    inline bool ShouldSampleGameplayFrame(
        double songTimeSeconds,
        std::optional<double> lastNoteTimeSeconds,
        bool samplingFinished)
    {
        return !samplingFinished &&
               songTimeSeconds >= 10.0 &&
               (!lastNoteTimeSeconds || songTimeSeconds <= *lastNoteTimeSeconds);
    }

    /// Summarizes accepted Unity frame durations. The average is derived from
    /// total frames / total elapsed time rather than averaging instantaneous
    /// rates, which would over-weight short fast frames and hide stutters.
    inline FrameRateStats SummarizeFrameRate(
        double minimumFrameSeconds,
        double maximumFrameSeconds,
        double totalFrameSeconds,
        std::uint64_t sampledFrames)
    {
        if(sampledFrames == 0 || minimumFrameSeconds <= 0.0 ||
           maximumFrameSeconds <= 0.0 || totalFrameSeconds <= 0.0)
            return {};
        return {
            1.0 / maximumFrameSeconds,
            static_cast<double>(sampledFrames) / totalFrameSeconds,
            1.0 / minimumFrameSeconds,
            sampledFrames};
    }

    /// Calculates the picture's uncropped size inside a fixed screen frame.
    /// Rotation is intentionally absent: rotating media must never swap or
    /// mutate the saved frame dimensions. Clipping happens after this result.
    inline VideoContentSize FitVideoContent(
        float frameWidth,
        float frameHeight,
        float sourceAspectRatio,
        bool stretchToFit,
        float zoom)
    {
        frameWidth = std::max(frameWidth, 0.0001f);
        frameHeight = std::max(frameHeight, 0.0001f);
        sourceAspectRatio = std::max(sourceAspectRatio, 0.0001f);
        zoom = std::clamp(zoom, 0.5f, 3.0f);
        float width = frameWidth;
        float height = frameHeight;
        if(!stretchToFit)
        {
            const float frameAspect = frameWidth / frameHeight;
            if(sourceAspectRatio > frameAspect)
                height = frameWidth / sourceAspectRatio;
            else
                width = frameHeight * sourceAspectRatio;
        }
        return {width * zoom, height * zoom};
    }

    /// The full-frame background supplies black letterboxing only for opaque
    /// layouts. Transparent layouts remove that renderer altogether so shader
    /// fallback behavior cannot leave black bars around a smaller picture.
    /// A requested black lead-in is the sole deliberate exception.
    inline constexpr bool ScreenBackgroundVisible(
        bool transparent,
        bool blackLeadInActive)
    {
        return !transparent || blackLeadInActive;
    }

    /// Deterministic filesystem key. FNV-1a is not used for security; the full
    /// level ID remains the authoritative manifest key.
    inline std::string StableVideoKey(const std::string& levelId)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for(const unsigned char value : levelId)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        }
        std::ostringstream text;
        text << std::hex << std::setw(16) << std::setfill('0') << hash;
        return text.str();
    }

    inline bool SafeSingleFilename(const std::string& value)
    {
        if(value.empty() || value == "." || value == "..") return false;
        return value.find('/') == std::string::npos &&
               value.find('\\') == std::string::npos &&
               value.find(':') == std::string::npos;
    }

    /// Accept only HTTPS YouTube addresses. Map metadata is untrusted input,
    /// so the downloader must never become a general-purpose URL fetcher.
    inline bool IsSupportedYouTubeUrl(std::string_view value)
    {
        while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.remove_prefix(1);
        while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.remove_suffix(1);

        constexpr std::string_view scheme = "https://";
        if(value.size() <= scheme.size())
            return false;
        for(std::size_t i = 0; i < scheme.size(); ++i)
        {
            if(std::tolower(static_cast<unsigned char>(value[i])) != scheme[i])
                return false;
        }

        value.remove_prefix(scheme.size());
        const auto hostEnd = value.find_first_of("/?#");
        auto host = value.substr(0, hostEnd);
        if(host.empty() || host.find('@') != std::string_view::npos ||
           host.find(':') != std::string_view::npos)
            return false;

        std::string lowercaseHost(host);
        std::transform(lowercaseHost.begin(), lowercaseHost.end(), lowercaseHost.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

        const auto isHostOrSubdomain = [&lowercaseHost](std::string_view domain)
        {
            return lowercaseHost == domain ||
                (lowercaseHost.size() > domain.size() &&
                 lowercaseHost.ends_with(std::string(".") + std::string(domain)));
        };
        return isHostOrSubdomain("youtube.com") ||
               isHostOrSubdomain("youtu.be") ||
               isHostOrSubdomain("youtube-nocookie.com");
    }

    inline bool IsValidYouTubeVideoId(std::string_view value)
    {
        if(value.size() != 11)
            return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char character)
        {
            return std::isalnum(character) || character == '-' || character == '_';
        });
    }

    /// Returns the next temporary FPS/resolution tier. The boolean is false
    /// when the session is already at the lowest supported output.
    inline std::pair<bool, std::pair<int, int>> NextPerformanceTier(
        int fps, int resolution)
    {
        if(fps > 30) return {true, {30, resolution}};
        if(fps > 15) return {true, {15, resolution}};
        if(resolution > 720) return {true, {fps, 720}};
        if(resolution > 480) return {true, {fps, 480}};
        return {false, {fps, resolution}};
    }

    /// Records the exact path Automatic Performance used while lowering
    /// quality. Recovery reads this small stack backwards instead of trying to
    /// infer the user's previous setting from the current tier. The supported
    /// 60/30/15 FPS and 1080/720/480p ladder has at most four reductions, so a
    /// fixed array avoids allocating memory in Beat Saber's gameplay loop.
    class AutomaticPerformanceHistory final {
    public:
        void Reset() noexcept { size_ = 0; }
        bool Empty() const noexcept { return size_ == 0; }
        std::size_t Size() const noexcept { return size_; }

        bool RecordReduction(int previousFps, int previousResolution) noexcept
        {
            if(size_ >= tiers_.size())
                return false;
            tiers_[size_++] = {previousFps, previousResolution};
            return true;
        }

        std::optional<std::pair<int, int>> RecoveryTarget() const noexcept
        {
            if(Empty())
                return std::nullopt;
            return tiers_[size_ - 1];
        }

        bool CommitRecovery() noexcept
        {
            if(Empty())
                return false;
            --size_;
            return true;
        }

    private:
        std::array<std::pair<int, int>, 4> tiers_{};
        std::size_t size_ = 0;
    };

    /// Estimates how many distinct source images should have been displayed
    /// during an interval. Output requests above the media's effective cadence
    /// are intentional frame reuse, not decoder misses. playbackRate converts
    /// source cadence into Beat Saber's song-clock domain.
    inline double ExpectedPresentationRate(
        double sourceFramesPerSecond,
        double playbackRate,
        int outputFramesPerSecond)
    {
        if(sourceFramesPerSecond <= 0.0 || playbackRate <= 0.0 ||
           outputFramesPerSecond <= 0)
            return 0.0;
        const double effectiveSourceRate = sourceFramesPerSecond * playbackRate;
        return std::min(
            effectiveSourceRate,
            static_cast<double>(outputFramesPerSecond));
    }

    inline std::uint64_t ExpectedPresentedFrames(
        double activeSongSeconds,
        double sourceFramesPerSecond,
        double playbackRate,
        int outputFramesPerSecond)
    {
        if(activeSongSeconds <= 0.0)
            return 0;
        const double expectedRate = ExpectedPresentationRate(
            sourceFramesPerSecond, playbackRate, outputFramesPerSecond);
        return static_cast<std::uint64_t>(std::floor(
            activeSongSeconds * expectedRate + 0.000001));
    }

    inline double MissedFramePercent(
        std::uint64_t expectedFrames,
        std::uint64_t presentedFrames)
    {
        if(expectedFrames == 0 || presentedFrames >= expectedFrames)
            return 0.0;
        return 100.0 * static_cast<double>(expectedFrames - presentedFrames) /
               static_cast<double>(expectedFrames);
    }

    /// Returns how many visible output-frame intervals separate two images
    /// that actually reached Unity. A value above one means intermediate video
    /// content was skipped. This deliberately uses media timestamps instead of
    /// worker-thread completion timing: a frame that arrives a few milliseconds
    /// late but is still displayed is not a visible miss.
    inline std::uint64_t PresentedFrameIntervals(
        double previousPresentationSeconds,
        double previousDurationSeconds,
        double currentPresentationSeconds,
        double playbackRate,
        int outputFramesPerSecond)
    {
        if(!std::isfinite(previousPresentationSeconds) ||
           !std::isfinite(currentPresentationSeconds) ||
           currentPresentationSeconds <= previousPresentationSeconds ||
           playbackRate <= 0.0 || outputFramesPerSecond <= 0)
            return 1;

        // Requests are limited in Beat Saber's song-clock domain. Convert one
        // output interval into media time, then respect a longer source-frame
        // duration (including variable-frame-rate samples) so intentional
        // holds are never classified as missing pictures.
        const double cappedMediaInterval =
            playbackRate / static_cast<double>(outputFramesPerSecond);
        const double sourceInterval =
            std::isfinite(previousDurationSeconds) &&
            previousDurationSeconds > 0.0
                ? previousDurationSeconds
                : 0.0;
        const double expectedInterval = std::max(
            cappedMediaInterval,
            sourceInterval);
        if(expectedInterval <= 0.0)
            return 1;

        const double elapsed =
            currentPresentationSeconds - previousPresentationSeconds;
        // Nearest-interval rounding tolerates ordinary MP4 time-base error
        // (for example 29.97 FPS) without hiding a complete skipped image.
        return std::max<std::uint64_t>(
            1,
            static_cast<std::uint64_t>(std::llround(elapsed / expectedInterval)));
    }

    /// Uses the shorter of Beat Saber's song duration and the loaded audio
    /// clip. Some custom levels report slightly different values for those two
    /// clocks, so waiting only for the map duration can leave a completed
    /// AudioSource in a terminal state. A small tolerance lets the menu loop
    /// before Unity tears down the channel at its exact final sample.
    inline bool PreviewReachedLoopBoundary(
        double songPositionSeconds,
        double songDurationSeconds,
        double audioClipDurationSeconds,
        double toleranceSeconds = 0.03)
    {
        const double songEnd = songDurationSeconds > 0.0
            ? songDurationSeconds
            : audioClipDurationSeconds;
        const double clipEnd = audioClipDurationSeconds > 0.0
            ? audioClipDurationSeconds
            : songDurationSeconds;
        const double playableEnd = std::min(songEnd, clipEnd);
        if(playableEnd <= 0.0)
            return false;
        return songPositionSeconds >=
            std::max(0.0, playableEnd - std::max(0.0, toleranceSeconds));
    }

    /// The circuit breaker counts internal failures, not error text. Two
    /// different failures close together are still evidence that the mod is
    /// unstable and should stop interacting with Beat Saber.
    inline bool IsSecondFailureWithin(
        std::chrono::steady_clock::duration elapsed,
        std::chrono::steady_clock::duration window = std::chrono::minutes(3))
    {
        return elapsed >= std::chrono::steady_clock::duration::zero() &&
               elapsed <= window;
    }

    /// Converts yt-dlp byte counters into a UI-safe progress fraction. The
    /// total can be unknown while YouTube is preparing a stream, and some
    /// servers briefly report estimates smaller than the bytes already read.
    inline float DownloadProgressFraction(
        std::uint64_t downloadedBytes,
        std::uint64_t totalBytes)
    {
        if(totalBytes == 0)
            return 0.0f;
        return std::clamp(
            static_cast<float>(downloadedBytes) /
                static_cast<float>(totalBytes),
            0.0f,
            1.0f);
    }
}
