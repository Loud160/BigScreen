#include "BigScreen/MapVideoConfig.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string_view>

#include "rapidjson/document.h"

namespace BigScreen {
    namespace {
        using JsonValue = rapidjson::Value;

        const JsonValue* Member(const JsonValue& object, std::string_view name)
        {
            if(!object.IsObject())
                return nullptr;

            const auto iterator = object.FindMember(
                rapidjson::StringRef(name.data(), static_cast<rapidjson::SizeType>(name.size())));
            return iterator == object.MemberEnd() ? nullptr : &iterator->value;
        }

        double NumberOr(const JsonValue& object, std::string_view name, double fallback)
        {
            const JsonValue* value = Member(object, name);
            return value && value->IsNumber() ? value->GetDouble() : fallback;
        }

        bool BoolOr(const JsonValue& object, std::string_view name, bool fallback)
        {
            const JsonValue* value = Member(object, name);
            return value && value->IsBool() ? value->GetBool() : fallback;
        }

        Float3 VectorOr(const JsonValue& object, std::string_view name, Float3 fallback)
        {
            const JsonValue* value = Member(object, name);
            if(!value || !value->IsObject())
                return fallback;

            return {
                static_cast<float>(NumberOr(*value, "x", fallback.x)),
                static_cast<float>(NumberOr(*value, "y", fallback.y)),
                static_cast<float>(NumberOr(*value, "z", fallback.z))
            };
        }

        std::optional<std::string> OptionalString(const JsonValue& object, std::string_view name)
        {
            const JsonValue* value = Member(object, name);
            if(!value || !value->IsString() || value->GetStringLength() == 0)
                return std::nullopt;
            return std::string(value->GetString(), value->GetStringLength());
        }

        bool IsPathInside(const std::filesystem::path& child, const std::filesystem::path& parent)
        {
            // Video filenames are map-authored input. Compare normalized path
            // components instead of using a string prefix, which would mistake
            // sibling directories such as "Song" and "Song Backup".
            const auto normalizedChild = std::filesystem::absolute(child).lexically_normal();
            const auto normalizedParent = std::filesystem::absolute(parent).lexically_normal();
            auto childPart = normalizedChild.begin();
            for(auto parentPart = normalizedParent.begin(); parentPart != normalizedParent.end(); ++parentPart, ++childPart)
            {
                if(childPart == normalizedChild.end() || *childPart != *parentPart)
                    return false;
            }
            return true;
        }
    }

    std::optional<MapVideoConfig> MapVideoConfig::LoadFromLevel(
        const std::filesystem::path& levelDirectory,
        std::string& error)
    {
        error.clear();

        // The native name allows future maps to target Big Screen explicitly.
        // The other names preserve file-format interoperability for maps that
        // already carry local MP4 metadata.
        constexpr std::string_view candidates[] = {
            "bigscreen.json",
            "cinema-video.json",
            "video.json"
        };

        std::filesystem::path metadata;
        for(const auto name : candidates)
        {
            const auto path = levelDirectory / name;
            if(std::filesystem::is_regular_file(path))
            {
                metadata = path;
                break;
            }
        }

        if(metadata.empty())
            return std::nullopt;

        std::ifstream stream(metadata, std::ios::binary);
        if(!stream)
        {
            error = "Could not open " + metadata.string();
            return std::nullopt;
        }

        const std::string json{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        rapidjson::Document document;
        document.Parse(json.data(), json.size());
        if(document.HasParseError() || !document.IsObject())
        {
            error = "Invalid JSON in " + metadata.string();
            return std::nullopt;
        }

        const auto videoFile = OptionalString(document, "videoFile");
        if(!videoFile)
        {
            error = "The map video metadata does not specify videoFile";
            return std::nullopt;
        }

        const std::filesystem::path relativeVideo(*videoFile);
        if(relativeVideo.is_absolute())
        {
            error = "videoFile must be relative to the custom level directory";
            return std::nullopt;
        }

        const auto resolvedVideo = (levelDirectory / relativeVideo).lexically_normal();
        if(!IsPathInside(resolvedVideo, levelDirectory))
        {
            error = "videoFile resolves outside the custom level directory";
            return std::nullopt;
        }
        if(!std::filesystem::is_regular_file(resolvedVideo))
        {
            error = "The configured MP4 does not exist: " + resolvedVideo.string();
            return std::nullopt;
        }

        MapVideoConfig config;
        config.metadataPath = metadata;
        config.videoPath = resolvedVideo;
        config.offsetSeconds = NumberOr(document, "offset", 0.0) / 1000.0;
        config.playbackRate = std::clamp(NumberOr(document, "playbackSpeed", 1.0), 0.05, 8.0);
        config.declaredDurationSeconds = std::max(0.0, NumberOr(document, "duration", 0.0));
        config.loop = BoolOr(document, "loop", false);
        config.screenPosition = VectorOr(document, "screenPosition", config.screenPosition);
        config.screenRotation = VectorOr(document, "screenRotation", config.screenRotation);
        config.screenHeight = std::clamp(
            static_cast<float>(NumberOr(document, "screenHeight", config.screenHeight)),
            0.5f,
            200.0f);
        config.screenCurvature = std::clamp(
            static_cast<float>(NumberOr(document, "screenCurvature", 0.0)),
            -1.0f,
            1.0f);
        config.screenSegments = std::clamp(
            static_cast<int>(NumberOr(document, "screenSubsurfaces", 32.0)),
            1,
            128);
        config.transparent = BoolOr(document, "transparency", false);
        config.requestedEnvironment = OptionalString(document, "environmentName");

        const double stopAt = NumberOr(document, "endVideoAt", 0.0);
        if(stopAt > 0.0)
            config.stopAtVideoSecond = stopAt;

        return config;
    }

    double MapVideoConfig::MediaTimeForSong(double songTimeSeconds, double decodedDurationSeconds) const
    {
        double mediaTime = songTimeSeconds * playbackRate + offsetSeconds;
        if(mediaTime < 0.0)
            return mediaTime;

        const double duration = decodedDurationSeconds > 0.0
            ? decodedDurationSeconds
            : declaredDurationSeconds;
        if(loop && duration > 0.0)
            mediaTime = std::fmod(mediaTime, duration);

        return mediaTime;
    }
}
