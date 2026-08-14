#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ChromaMapDetector.hpp"

namespace {
    int failures = 0;

    void Expect(bool condition, const char* description)
    {
        if(condition)
            return;
        ++failures;
        std::cerr << "FAILED: " << description << '\n';
    }

    bool Near(float left, float right)
    {
        return std::abs(left - right) < 0.0001f;
    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        "bigscreen-map-config-tests";
    std::filesystem::create_directories(root);
    const auto metadata = root / "cinema-video.json";
    {
        std::ofstream output(metadata, std::ios::trunc);
        output << R"({
            "videoID":"TcHvEFxk_78",
            "configByMapper":true,
            "screenPosition":{"x":-2.5,"y":0.5,"z":3.0},
            "screenRotation":{"x":10,"y":-45,"z":0},
            "screenHeight":2,
            "screenCurvature":45,
            "screenSubsurfaces":64,
            "transparency":true,
            "disableDefaultModifications":true,
            "environment":[{
                "name":"Tube",
                "parentName":"Environment",
                "position":{"x":20,"y":0,"z":42},
                "active":false
            }]
        })";
    }

    std::string error;
    const auto config = BigScreen::MapVideoConfig::LoadDefinitionFromLevel(
        root, error);
    Expect(config.has_value(), "Cinema metadata should parse");
    Expect(error.empty(), "valid Cinema metadata should not report an error");
    if(config)
    {
        Expect(config->hasMapperPresentation,
               "screen/environment fields claim mapper presentation");
        Expect(config->disableDefaultModifications,
               "disableDefaultModifications should be retained");
        Expect(Near(config->screenPosition.x, -2.5f) &&
               Near(config->screenPosition.y, 0.5f) &&
               Near(config->screenPosition.z, 3.0f),
               "mapper screen position should remain exact");
        Expect(config->cinemaCurvatureDegrees &&
               Near(*config->cinemaCurvatureDegrees, 45.0f),
               "Cinema curvature must stay in arc degrees");
        Expect(config->mapperTransparency.value_or(false),
               "explicit mapper transparency should remain distinguishable");
        Expect(config->letterboxTransparent,
               "mapper transparency should remove the letterbox background");
        Expect(Near(config->videoOpacity, 0.75f),
               "legacy Cinema transparency should retain its 75 percent picture opacity");
        Expect(config->environmentModifications.size() == 1,
               "environment array should parse");
        if(!config->environmentModifications.empty())
        {
            const auto& item = config->environmentModifications.front();
            Expect(item.name == "Tube", "environment object name should parse");
            Expect(item.parentName && *item.parentName == "Environment",
                   "environment parent filter should parse");
            Expect(item.active && !*item.active,
                   "explicit false active state should not be lost");
        }


        auto userLayoutBase = *config;
        userLayoutBase.ResetPresentationToDefaults();
        Expect(Near(userLayoutBase.screenPosition.x, 0.0f) &&
               Near(userLayoutBase.screenPosition.y, 12.0f) &&
               Near(userLayoutBase.screenPosition.z, 60.0f),
               "disabling mapper presentation should restore the neutral back-wall position");
        Expect(Near(userLayoutBase.screenRotation.x, -8.0f) &&
               Near(userLayoutBase.screenHeight, 25.0f),
               "disabling mapper presentation should restore neutral rotation and size");
        Expect(!userLayoutBase.cinemaCurvatureDegrees &&
               !userLayoutBase.mapperTransparency,
               "mapper-only presentation fields should not leak into a user layout");
        Expect(userLayoutBase.videoId == config->videoId &&
               userLayoutBase.offsetSeconds == config->offsetSeconds &&
               userLayoutBase.hasMapperPresentation,
               "presentation reset must preserve media, timing, and mapper ownership metadata");
    }

    // A normal Cinema download/timing declaration is not permission to throw
    // away the player's selected Big Screen layout.
    {
        std::ofstream output(metadata, std::ios::trunc);
        output << R"({"videoID":"TcHvEFxk_78","configByMapper":true,"offset":-1000})";
    }
    const auto timingOnly = BigScreen::MapVideoConfig::LoadDefinitionFromLevel(
        root, error);
    Expect(timingOnly && !timingOnly->hasMapperPresentation,
           "timing-only metadata should keep the user screen layout");

    {
        std::ofstream output(metadata, std::ios::trunc);
        output << R"({"videoUrl":"https://youtube.com.example.invalid/video"})";
    }
    const auto untrustedUrl = BigScreen::MapVideoConfig::LoadDefinitionFromLevel(
        root, error);
    Expect(!untrustedUrl && error.find("HTTPS YouTube") != std::string::npos,
           "mapper metadata must not turn the downloader into an arbitrary URL fetcher");

    // A user-downloaded or locally assigned video has no reason to modify the
    // map files. Detect Chroma independently so those videos still yield the
    // environment to Chroma when Allow Chroma Override is enabled.
    const auto info = root / "Info.dat";
    {
        std::ofstream output(info, std::ios::trunc);
        output << R"({"_customData":{"_suggestions":["Chroma"]}})";
    }
    std::string chromaReason;
    Expect(BigScreen::ChromaMapDetector::UsesChroma(root, chromaReason),
           "Info.dat Chroma suggestion should activate map-wide detection");
    Expect(!chromaReason.empty(),
           "Chroma detection should explain which declaration matched");

    std::filesystem::remove(info);
    const auto difficulty = root / "ExpertPlus.dat";
    {
        std::ofstream output(difficulty, std::ios::trunc);
        output << R"({"customData":{"environment":[{"id":"CinemaScreen$"}]}})";
    }
    Expect(BigScreen::ChromaMapDetector::UsesChroma(root, chromaReason),
           "difficulty Chroma environment data should activate detection without a declaration");

    {
        std::ofstream output(difficulty, std::ios::trunc);
        output << R"({"customData":{"environment":[]}})";
    }
    Expect(!BigScreen::ChromaMapDetector::UsesChroma(root, chromaReason),
           "an empty environment array should not claim Chroma ownership");

    std::filesystem::remove_all(root);
    if(failures == 0)
        std::cout << "All Big Screen map configuration tests passed.\n";
    return failures == 0 ? 0 : 1;
}
