#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace BigScreen::CoreLogic {
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
