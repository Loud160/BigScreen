#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace BigScreen::CoreLogic {
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

    inline bool IsRepeatedWithin(
        const std::string& previous,
        const std::string& current,
        std::chrono::steady_clock::duration elapsed,
        std::chrono::steady_clock::duration window = std::chrono::minutes(3))
    {
        return !current.empty() && previous == current &&
               elapsed >= std::chrono::steady_clock::duration::zero() &&
               elapsed <= window;
    }
}
