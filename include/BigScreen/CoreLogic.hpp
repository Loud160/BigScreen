#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
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

    struct VideoContentSize {
        float width;
        float height;
    };

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
