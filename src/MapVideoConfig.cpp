// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/Utility.hpp"
#include "BigScreen/CoreLogic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string_view>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

namespace BigScreen {
    namespace {
        using JsonValue = rapidjson::Value;

        bool IsRegularFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::is_regular_file(path, error) && !error;
        }

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

        std::optional<Float3> OptionalVector(
            const JsonValue& object,
            std::string_view name)
        {
            const JsonValue* value = Member(object, name);
            if(!value || !value->IsObject())
                return std::nullopt;
            return Float3{
                static_cast<float>(NumberOr(*value, "x", 0.0)),
                static_cast<float>(NumberOr(*value, "y", 0.0)),
                static_cast<float>(NumberOr(*value, "z", 0.0))};
        }

        std::optional<bool> OptionalBool(
            const JsonValue& object,
            std::string_view name)
        {
            const JsonValue* value = Member(object, name);
            return value && value->IsBool()
                ? std::optional<bool>{value->GetBool()}
                : std::nullopt;
        }

        std::optional<float> OptionalClampedFloat(
            const JsonValue& object,
            std::string_view name,
            float minimum,
            float maximum)
        {
            const JsonValue* value = Member(object, name);
            if(!value || !value->IsNumber())
                return std::nullopt;
            return std::clamp(value->GetFloat(), minimum, maximum);
        }

        std::optional<std::string> OptionalString(const JsonValue& object, std::string_view name)
        {
            const JsonValue* value = Member(object, name);
            if(!value || !value->IsString() || value->GetStringLength() == 0 ||
               value->GetStringLength() > 2048)
                return std::nullopt;
            return std::string(value->GetString(), value->GetStringLength());
        }

    }

    std::optional<MapVideoConfig> MapVideoConfig::LoadFromLevel(
        const std::filesystem::path& levelDirectory,
        std::string& error)
    {
        auto definition = LoadDefinitionFromLevel(levelDirectory, error);
        if(!definition || !definition->HasLocalVideo())
            return std::nullopt;
        return definition;
    }

    void MapVideoConfig::ResetPresentationToDefaults()
    {
        ResetScreenGeometryToDefaults();
        ResetMapperVisualEffects();
    }

    void MapVideoConfig::ResetScreenGeometryToDefaults()
    {
        const MapVideoConfig defaults;
        screenPosition = defaults.screenPosition;
        screenRotation = defaults.screenRotation;
        screenScale = defaults.screenScale;
        screenHeight = defaults.screenHeight;
        screenWidthOverride = defaults.screenWidthOverride;
        screenCurvature = defaults.screenCurvature;
        cinemaCurvatureDegrees.reset();
        cinemaCurveYAxis = defaults.cinemaCurveYAxis;
        maintainAspectRatioWhenCurved = defaults.maintainAspectRatioWhenCurved;
        screenSegments = defaults.screenSegments;
        videoRotation = defaults.videoRotation;
        videoZoom = defaults.videoZoom;
        videoOffsetX = defaults.videoOffsetX;
        videoOffsetY = defaults.videoOffsetY;
        videoTilt = defaults.videoTilt;
        stretchVideoToFit = defaults.stretchVideoToFit;
    }

    void MapVideoConfig::ResetMapperVisualEffects()
    {
        const MapVideoConfig defaults;
        letterboxTransparent = defaults.letterboxTransparent;
        opaqueScreenBody = defaults.opaqueScreenBody;
        videoOpacity = defaults.videoOpacity;
        mapperTransparency.reset();
        colorCorrection.reset();
        vignette.reset();
        // An absent colorBlending field means "use Cinema's default" while
        // an explicit false means "do not use mapper blending." Preserve that
        // distinction when Respect Mapper Settings is disabled; resetting the
        // optional entirely would cause ScreenSurface to re-enable Cinema's
        // soft-additive default merely because the original file contained
        // some other mapper presentation field.
        colorBlending = false;
        // Disabling mapper visual effects returns the glow strength to
        // Cinema's neutral default rather than keeping an authored value.
        bloomIntensity = defaults.bloomIntensity;
        additionalScreens.clear();
    }

    std::optional<MapVideoConfig> MapVideoConfig::LoadDefinitionFromLevel(
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
            if(IsRegularFile(path))
            {
                metadata = path;
                break;
            }
        }

        if(metadata.empty())
            return std::nullopt;

        return LoadDefinitionFromFile(levelDirectory, metadata, error);
    }

    std::optional<MapVideoConfig> MapVideoConfig::LoadDefinitionFromFile(
        const std::filesystem::path& levelDirectory,
        const std::filesystem::path& metadata,
        std::string& error)
    {
        error.clear();
        if(!IsRegularFile(metadata))
        {
            error = "The requested map video metadata file does not exist";
            return std::nullopt;
        }

        const auto normalizedMetadata = metadata.lexically_normal();
        const auto normalizedLevel = levelDirectory.lexically_normal();
        if(!Utility::IsPathInside(normalizedMetadata, normalizedLevel))
        {
            error = "The requested map video metadata resolves outside the custom level directory";
            return std::nullopt;
        }

        constexpr std::uintmax_t MaximumMetadataBytes = 1024 * 1024;
        std::error_code sizeError;
        const auto metadataSize = std::filesystem::file_size(
            normalizedMetadata, sizeError);
        if(sizeError || metadataSize > MaximumMetadataBytes)
        {
            error = sizeError
                ? "Could not determine the map video metadata size"
                : "The map video metadata is larger than the 1 MB safety limit";
            return std::nullopt;
        }

        std::ifstream stream(normalizedMetadata, std::ios::binary);
        if(!stream)
        {
            error = "Could not open " + normalizedMetadata.string();
            return std::nullopt;
        }

        const std::string json{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        rapidjson::Document document;
        document.Parse(json.data(), json.size());
        if(document.HasParseError())
        {
            const auto strictError = document.GetParseError();
            const auto strictOffset = document.GetErrorOffset();

            // Several older PC Cinema maps were published with harmless JSON
            // extensions accepted by desktop serializers, most commonly a
            // trailing comma before `}`. Reparse only with RapidJSON's two
            // explicit compatibility flags. This recovers a complete object;
            // it never guesses across truncated strings, missing braces, or
            // arbitrary damaged data.
            document.Parse<
                rapidjson::kParseCommentsFlag |
                rapidjson::kParseTrailingCommasFlag>(
                    json.data(), json.size());
            if(document.HasParseError() || !document.IsObject())
            {
                error = "Invalid Cinema JSON at byte " +
                    std::to_string(strictOffset) + ": " +
                    rapidjson::GetParseError_En(strictError) + " (" +
                    normalizedMetadata.string() + ")";
                return std::nullopt;
            }

            error = "Cinema JSON uses non-standard syntax at byte " +
                std::to_string(strictOffset) + ": " +
                rapidjson::GetParseError_En(strictError) +
                ". Big Screen recovered the supported fields, but the map "
                "author should correct " + normalizedMetadata.string();
        }
        else if(!document.IsObject())
        {
            error = "Cinema JSON must contain one object (" +
                normalizedMetadata.string() + ")";
            return std::nullopt;
        }

        const auto videoFile = OptionalString(document, "videoFile");
        const auto videoId = OptionalString(document, "videoID");
        const auto videoUrl = OptionalString(document, "videoUrl");
        const bool forceEnvironmentModifications = BoolOr(
            document, "forceEnvironmentModifications", false);
        if(!videoFile && !videoId && !videoUrl &&
           !forceEnvironmentModifications)
        {
            error = "The map video metadata has no videoFile, videoID, or videoUrl";
            return std::nullopt;
        }
        if(videoId && !CoreLogic::IsValidYouTubeVideoId(*videoId))
        {
            error = "The map videoID is not a valid YouTube video ID";
            return std::nullopt;
        }
        if(videoUrl && !CoreLogic::IsSupportedYouTubeUrl(*videoUrl))
        {
            error = "The map videoUrl must be an HTTPS YouTube address";
            return std::nullopt;
        }

        std::filesystem::path resolvedVideo;
        std::filesystem::path relativeVideo;
        if(videoFile)
        {
            relativeVideo = std::filesystem::path(*videoFile);
            if(relativeVideo.is_absolute())
            {
                error = "videoFile must be relative to the custom level directory";
                return std::nullopt;
            }

            resolvedVideo = (levelDirectory / relativeVideo).lexically_normal();
            if(!Utility::IsPathInside(resolvedVideo, levelDirectory))
            {
                error = "videoFile resolves outside the custom level directory";
                return std::nullopt;
            }
        }

        MapVideoConfig config;
        config.metadataPath = normalizedMetadata;
        config.declaredVideoPath = resolvedVideo;
        if(!resolvedVideo.empty() && IsRegularFile(resolvedVideo))
            config.videoPath = resolvedVideo;
        config.videoId = videoId;
        config.videoUrl = videoUrl;
        config.title = OptionalString(document, "title");
        config.author = OptionalString(document, "author");
        config.configByMapper = BoolOr(document, "configByMapper", false);
        config.disableDefaultModifications = BoolOr(
            document, "disableDefaultModifications", false);
        config.forceEnvironmentModifications = forceEnvironmentModifications;
        config.allowCustomPlatform = OptionalBool(document, "allowCustomPlatform");
        config.mergePropGroups = BoolOr(document, "mergePropGroups", false);
        config.colorBlending = OptionalBool(document, "colorBlending");
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
        if(const auto* curvature = Member(document, "screenCurvature");
           curvature && curvature->IsNumber())
        {
            config.cinemaCurvatureDegrees = std::clamp(
                curvature->GetFloat(), 0.0f, 180.0f);
        }
        config.cinemaCurveYAxis = BoolOr(document, "curveYAxis", false);
        config.screenSegments = std::clamp(
            static_cast<int>(NumberOr(document, "screenSubsurfaces", 32.0)),
            1,
            256);
        config.mapperTransparency = OptionalBool(document, "transparency");
        config.opaqueScreenBody = config.mapperTransparency.has_value() &&
            !*config.mapperTransparency;
        // Cinema's mapper `bloom` field. 1.0 is Cinema's default when the
        // field is absent; CoreLogic::CinemaBloomIntensity clamps authored
        // values to Cinema's documented 0..2 range at use time.
        config.bloomIntensity =
            static_cast<float>(NumberOr(document, "bloom", 1.0));
        config.requestedEnvironment = OptionalString(document, "environmentName");

        if(const auto* correction = Member(document, "colorCorrection");
           correction && correction->IsObject())
        {
            CinemaColorCorrection parsed;
            parsed.brightness = OptionalClampedFloat(
                *correction, "brightness", 0.0f, 2.0f).value_or(1.0f);
            parsed.contrast = OptionalClampedFloat(
                *correction, "contrast", 0.0f, 5.0f).value_or(1.0f);
            parsed.saturation = OptionalClampedFloat(
                *correction, "saturation", 0.0f, 5.0f).value_or(1.0f);
            parsed.hue = OptionalClampedFloat(
                *correction, "hue", -360.0f, 360.0f).value_or(0.0f);
            parsed.exposure = OptionalClampedFloat(
                *correction, "exposure", 0.0f, 5.0f).value_or(1.0f);
            parsed.gamma = OptionalClampedFloat(
                *correction, "gamma", 0.0f, 5.0f).value_or(1.0f);
            config.colorCorrection = parsed;
        }

        if(const auto* vignette = Member(document, "vignette");
           vignette && vignette->IsObject())
        {
            CinemaVignette parsed;
            if(auto type = OptionalString(*vignette, "type"))
            {
                std::transform(type->begin(), type->end(), type->begin(),
                    [](unsigned char value) {
                        return static_cast<char>(std::tolower(value));
                    });
                if(*type == "oval" || *type == "elliptical" || *type == "ellipse")
                    parsed.type = "elliptical";
            }
            parsed.radius = OptionalClampedFloat(
                *vignette, "radius", 0.0f, 1.0f).value_or(1.0f);
            parsed.softness = OptionalClampedFloat(
                *vignette, "softness", 0.0f, 1.0f).value_or(0.005f);
            config.vignette = parsed;
        }

        if(const auto* screens = Member(document, "additionalScreens");
           screens && screens->IsArray())
        {
            constexpr rapidjson::SizeType MaximumAdditionalScreens = 32;
            config.additionalScreens.reserve(
                std::min(screens->Size(), MaximumAdditionalScreens));
            rapidjson::SizeType processed = 0;
            for(const auto& item : screens->GetArray())
            {
                if(processed++ >= MaximumAdditionalScreens)
                    break;
                if(!item.IsObject())
                    continue;
                CinemaAdditionalScreen screen;
                screen.position = OptionalVector(item, "position");
                screen.rotation = OptionalVector(item, "rotation");
                screen.scale = OptionalVector(item, "scale");
                config.additionalScreens.push_back(std::move(screen));
            }
        }

        if(const auto* environment = Member(document, "environment");
           environment && environment->IsArray())
        {
            constexpr rapidjson::SizeType MaximumEnvironmentEntries = 256;
            config.environmentModifications.reserve(
                std::min(environment->Size(), MaximumEnvironmentEntries));
            rapidjson::SizeType processed = 0;
            for(const auto& item : environment->GetArray())
            {
                if(processed++ >= MaximumEnvironmentEntries)
                    break;
                if(!item.IsObject())
                    continue;
                auto name = OptionalString(item, "name");
                if(!name)
                    continue;
                EnvironmentModification modification;
                modification.name = std::move(*name);
                modification.parentName = OptionalString(item, "parentName");
                modification.cloneFrom = OptionalString(item, "cloneFrom");
                modification.active = OptionalBool(item, "active");
                modification.position = OptionalVector(item, "position");
                modification.rotation = OptionalVector(item, "rotation");
                modification.scale = OptionalVector(item, "scale");
                config.environmentModifications.push_back(std::move(modification));
            }
        }

        // Do not treat configByMapper by itself as permission to ignore the
        // user's layout: most Cinema configs use it only to protect timing.
        // Presentation ownership begins only when a mapper supplied an actual
        // screen/environment field that PC Cinema would honor.
        config.hasMapperScreenGeometry =
            Member(document, "screenPosition") ||
            Member(document, "screenRotation") ||
            Member(document, "screenHeight") ||
            Member(document, "screenCurvature") ||
            !config.additionalScreens.empty();
        config.hasMapperEnvironmentPresentation =
            Member(document, "environmentName") ||
            Member(document, "disableDefaultModifications") ||
            Member(document, "forceEnvironmentModifications") ||
            !config.environmentModifications.empty();
        config.hasMapperPresentation =
            config.hasMapperScreenGeometry ||
            config.hasMapperEnvironmentPresentation ||
            Member(document, "screenSubsurfaces") ||
            Member(document, "curveYAxis") ||
            Member(document, "transparency") ||
            config.mapperTransparency.has_value() ||
            config.colorCorrection.has_value() ||
            config.vignette.has_value() ||
            config.colorBlending.has_value();

        const double stopAt = NumberOr(document, "endVideoAt", 0.0);
        if(stopAt > 0.0)
            config.stopAtVideoSecond = stopAt;

        return config;
    }

    std::optional<std::string> MapVideoConfig::DownloadUrl() const
    {
        if(videoUrl && CoreLogic::IsSupportedYouTubeUrl(*videoUrl))
            return videoUrl;
        if(videoId && CoreLogic::IsValidYouTubeVideoId(*videoId))
            return "https://www.youtube.com/watch?v=" + *videoId;
        return std::nullopt;
    }

    bool MapVideoConfig::HasLocalVideo() const
    {
        return !videoPath.empty() && IsRegularFile(videoPath);
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
