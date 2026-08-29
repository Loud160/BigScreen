// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
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
            "allowCustomPlatform":false,
            "mergePropGroups":true,
            "colorBlending":false,
            "colorCorrection":{"brightness":1.25,"hue":45,"gamma":1.5},
            "vignette":{"type":"oval","radius":0.8,"softness":0.1},
            "additionalScreens":[{
                "position":{"x":4,"y":8,"z":12},
                "rotation":{"x":0,"y":90,"z":0},
                "scale":{"x":0.5,"y":0.5,"z":0.5}
            }],
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
        Expect(config->hasMapperScreenGeometry,
               "authored screen placement and size should claim screen geometry");
        Expect(config->hasMapperEnvironmentPresentation,
               "authored environment changes should claim environment presentation");
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
        Expect(!config->opaqueScreenBody,
               "Cinema transparency should remove the opaque light-blocking body");
        Expect(Near(config->videoOpacity, 1.0f),
               "Cinema transparency must not alter decoded picture opacity");
        Expect(config->allowCustomPlatform && !*config->allowCustomPlatform,
               "explicit custom-platform suppression should parse");
        Expect(config->mergePropGroups,
               "mergePropGroups should parse");
        Expect(config->colorBlending && !*config->colorBlending,
               "explicit color-blending disable should parse");
        Expect(config->colorCorrection &&
               Near(config->colorCorrection->brightness, 1.25f) &&
               Near(config->colorCorrection->hue, 45.0f) &&
               Near(config->colorCorrection->gamma, 1.5f),
               "Cinema color correction should parse and preserve defaults");
        Expect(config->vignette &&
               config->vignette->type == "elliptical" &&
               Near(config->vignette->radius, 0.8f) &&
               Near(config->vignette->softness, 0.1f),
               "Cinema vignette aliases and values should parse");
        Expect(config->additionalScreens.size() == 1 &&
               config->additionalScreens.front().position &&
               Near(config->additionalScreens.front().position->z, 12.0f),
               "Cinema additional screens should parse");
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
               !userLayoutBase.mapperTransparency &&
               !userLayoutBase.colorCorrection &&
               !userLayoutBase.vignette &&
               userLayoutBase.additionalScreens.empty(),
               "mapper-only presentation fields should not leak into a user layout");
        Expect(userLayoutBase.colorBlending && !*userLayoutBase.colorBlending,
               "presentation reset should explicitly suppress Cinema's default color blending");
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
    Expect(timingOnly && !timingOnly->hasMapperPresentation &&
           !timingOnly->hasMapperScreenGeometry,
           "timing-only metadata should keep the user screen layout");

    // The combined Quest test map stores each ten-second phase in its own
    // Cinema file. Explicit files must travel through the same validation and
    // normalization path as the conventional cinema-video.json candidate.
    const auto explicitPhase = root / "cinema-cycle-02.json";
    {
        std::ofstream output(explicitPhase, std::ios::trunc);
        output << R"({
            "videoFile":"cinema-cycle-video.mp4",
            "screenPosition":{"x":7.5,"y":11.0,"z":42.0},
            "screenHeight":18.0
        })";
    }
    {
        std::ofstream video(root / "cinema-cycle-video.mp4", std::ios::trunc);
        video << "fixture";
    }
    const auto cyclePhase = BigScreen::MapVideoConfig::LoadDefinitionFromFile(
        root, explicitPhase, error);
    Expect(cyclePhase && error.empty(),
           "an explicit Cinema cycle phase should parse through the normal loader");
    Expect(cyclePhase && Near(cyclePhase->screenPosition.x, 7.5f) &&
           Near(cyclePhase->screenPosition.z, 42.0f) &&
           Near(cyclePhase->screenHeight, 18.0f),
           "an explicit Cinema cycle phase should preserve mapper geometry");
    Expect(cyclePhase && cyclePhase->hasMapperScreenGeometry &&
           !cyclePhase->cinemaCurvatureDegrees &&
           Near(cyclePhase->screenCurvature, 0.0f) &&
           !cyclePhase->maintainAspectRatioWhenCurved,
           "an authored Cinema canvas without curvature should use the flat Cinema default");

    // Environment-only Cinema data can preserve a Chroma scene without
    // disabling the player's screen layout controls.
    {
        std::ofstream output(metadata, std::ios::trunc);
        output << R"({
            "videoID":"TcHvEFxk_78",
            "environmentName":"BigMirrorEnvironment"
        })";
    }
    const auto environmentOnly =
        BigScreen::MapVideoConfig::LoadDefinitionFromLevel(root, error);
    Expect(environmentOnly && environmentOnly->hasMapperPresentation,
           "environment metadata should remain presentation data");
    Expect(environmentOnly &&
           environmentOnly->hasMapperEnvironmentPresentation,
           "environment metadata should retain environment ownership");
    Expect(environmentOnly && !environmentOnly->hasMapperScreenGeometry,
           "environment metadata must not claim video screen geometry");

    // Cinema explicitly supports an environment-only file when the mapper
    // opts into forceEnvironmentModifications. This must parse without a
    // synthetic video URL or local media file.
    {
        std::ofstream output(metadata, std::ios::trunc);
        output << R"({
            "forceEnvironmentModifications":true,
            "environment":[{"name":"Floor","active":false}]
        })";
    }
    const auto forcedEnvironmentOnly =
        BigScreen::MapVideoConfig::LoadDefinitionFromLevel(root, error);
    Expect(forcedEnvironmentOnly &&
           forcedEnvironmentOnly->forceEnvironmentModifications &&
           forcedEnvironmentOnly->videoPath.empty(),
           "forced environment-only Cinema metadata should parse without media");

    // Cinema metadata—including a requested environment—and a Cinema-only
    // suggestion do not mean the beatmap uses Chroma. Playback uses this
    // distinction to keep Big Screen's environment settings authoritative for
    // maps such as V8C while still honoring the video's media/timing fields.
    const auto info = root / "Info.dat";
    {
        std::ofstream output(info, std::ios::trunc);
        output << R"({"_customData":{"_suggestions":["Cinema"]}})";
    }
    std::string chromaReason;
    Expect(!BigScreen::ChromaMapDetector::UsesChroma(root, chromaReason),
           "Cinema-only metadata must not be mistaken for Chroma use");

    {
        std::ofstream output(metadata, std::ios::trunc);
        output << R"({"videoUrl":"https://youtube.com.example.invalid/video"})";
    }
    const auto untrustedUrl = BigScreen::MapVideoConfig::LoadDefinitionFromLevel(
        root, error);
    Expect(!untrustedUrl && error.find("HTTPS YouTube") != std::string::npos,
           "mapper metadata must not turn the downloader into an arbitrary URL fetcher");

    // Older PC Cinema maps sometimes shipped a trailing comma that strict
    // JSON rejects. Recover only the complete document using RapidJSON's
    // explicit compatibility flag and retain an actionable warning.
    {
        std::ofstream output(metadata, std::ios::trunc);
        output << R"({
            "videoID":"6swmTBVI83k",
            "offset":1100,
            "vignette":{"type":"elliptical","radius":0.5,}
        })";
    }
    const auto recovered = BigScreen::MapVideoConfig::LoadDefinitionFromLevel(
        root, error);
    Expect(recovered && recovered->videoId &&
           *recovered->videoId == "6swmTBVI83k" &&
           recovered->vignette && Near(recovered->vignette->radius, 0.5f),
           "a complete Cinema object with a trailing comma should recover its supported fields");
    Expect(!error.empty() &&
           error.find("non-standard syntax") != std::string::npos,
           "recovered Cinema JSON should retain a user-visible warning");

    // Recovery must not invent fields across genuinely truncated JSON.
    {
        std::ofstream output(metadata, std::ios::trunc);
        output << R"({"videoID":"6swmTBVI83k","vignette":{"radius":0.5)";
    }
    const auto truncated = BigScreen::MapVideoConfig::LoadDefinitionFromLevel(
        root, error);
    Expect(!truncated && error.find("Invalid Cinema JSON at byte") != std::string::npos,
           "truncated Cinema JSON must remain rejected with its parse location");

    // A user-downloaded or locally assigned video has no reason to modify the
    // map files. Detect Chroma independently so those videos still yield the
    // environment to Chroma when Allow Chroma Override is enabled.
    std::filesystem::remove(info);
    const auto chromaInfo = root / "ChromaInfo.dat";
    {
        std::ofstream output(chromaInfo, std::ios::trunc);
        output << R"({"_customData":{"_suggestions":["Chroma"]}})";
    }
    Expect(BigScreen::ChromaMapDetector::UsesChroma(root, chromaReason),
           "Info.dat Chroma suggestion should activate map-wide detection");
    Expect(!chromaReason.empty(),
           "Chroma detection should explain which declaration matched");

    std::filesystem::remove(chromaInfo);
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

    // PC Cinema creates one canonical CinemaScreen. Chroma maps commonly park
    // that base at z=-200, duplicate it, and place the visible copies through
    // the selected difficulty's environment data. Menu preview has no active
    // Chroma beatmap controller, so the compatibility bridge must reproduce
    // those initial clone transforms itself without moving the parked base.
    std::filesystem::remove(difficulty);
    {
        std::ofstream output(info, std::ios::trunc);
        output << R"({
            "_difficultyBeatmapSets":[{
                "_beatmapCharacteristicName":"Standard",
                "_difficultyBeatmaps":[{
                    "_difficulty":"Expert",
                    "_beatmapFilename":"ExpertStandard.dat"
                }]
            }]
        })";
    }
    const auto cinemaDifficulty = root / "ExpertStandard.dat";
    {
        std::ofstream output(cinemaDifficulty, std::ios::trunc);
        output << R"({
            "customData":{"environment":[
                {"id":"CinemaScreen$","lookupMethod":"Regex","track":"cinema",
                 "duplicate":1,"scale":[1,1,1],"position":[0,-1.9,20],
                 "rotation":[45,0,0]},
                {"id":"CinemaScreen$","lookupMethod":"Regex","track":"cL",
                 "duplicate":1,"position":[-3,2,15],"rotation":[0,-25,0]},
                {"id":"CinemaScreen$","lookupMethod":"Regex","track":"cR",
                 "duplicate":1,"position":[3,2,15],"rotation":[0,25,0]},
                {"id":"EnvironmentLight$","lookupMethod":"Regex",
                 "duplicate":1,"position":[0,0,0]}
            ]}
        })";
    }
    BigScreen::MapVideoConfig chromaPreview;
    chromaPreview.screenPosition = {0.0f, 5.0f, -200.0f};
    chromaPreview.screenRotation = {45.0f, 0.0f, 0.0f};
    chromaPreview.screenHeight = 5.0f;
    std::string previewReason;
    Expect(BigScreen::ChromaMapDetector::ApplyCinemaScreenPreview(
               root, "Standard", 3, chromaPreview, previewReason),
           "selected-difficulty Chroma CinemaScreen data should resolve for menu preview");
    Expect(Near(chromaPreview.screenPosition.z, -200.0f),
           "Chroma duplicate instructions must not move Cinema's parked base screen");
    Expect(chromaPreview.additionalScreens.size() == 3,
           "three Chroma CinemaScreen duplicates should become three shared preview screens");
    if(chromaPreview.additionalScreens.size() == 3)
    {
        Expect(chromaPreview.additionalScreens[0].position &&
               Near(chromaPreview.additionalScreens[0].position->y, -1.9f) &&
               Near(chromaPreview.additionalScreens[0].position->z, 20.0f),
               "center Chroma duplicate should retain its initial placement");
        Expect(chromaPreview.additionalScreens[1].rotation &&
               Near(chromaPreview.additionalScreens[1].rotation->y, -25.0f) &&
               chromaPreview.additionalScreens[2].rotation &&
               Near(chromaPreview.additionalScreens[2].rotation->y, 25.0f),
               "left and right Chroma duplicate rotations should remain exact");
    }
    Expect(chromaPreview.hasMapperScreenGeometry &&
           previewReason.find("ExpertStandard.dat") != std::string::npos,
           "resolved Chroma screens should claim mapper geometry and identify their source file");

    // A non-duplicate instruction mutates the canonical screen itself. Scale
    // is a transform property in Chroma rather than a Cinema screenHeight, so
    // preserve it independently for the menu surface root.
    {
        std::ofstream output(cinemaDifficulty, std::ios::trunc);
        output << R"({"customData":{"environment":[{
            "id":"CinemaScreen","lookupMethod":"Exact",
            "position":[7,8,9],"rotation":[10,20,30],"scale":[2,3,1]
        }]}})";
    }
    BigScreen::MapVideoConfig directPreview;
    Expect(BigScreen::ChromaMapDetector::ApplyCinemaScreenPreview(
               root, "Standard", 3, directPreview, previewReason),
           "direct Chroma CinemaScreen instructions should resolve");
    Expect(Near(directPreview.screenPosition.x, 7.0f) &&
           Near(directPreview.screenRotation.y, 20.0f) &&
           Near(directPreview.screenScale.x, 2.0f) &&
           Near(directPreview.screenScale.y, 3.0f),
           "direct screen position, rotation, and transform scale should all apply");

    std::filesystem::remove_all(root);
    if(failures == 0)
        std::cout << "All Big Screen map configuration tests passed.\n";
    return failures == 0 ? 0 : 1;
}
