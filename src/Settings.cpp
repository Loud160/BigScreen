// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/Settings.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/DiagnosticSessionLogger.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "beatsaber-hook/shared/config/config-utils.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr float MaximumScreenCurvature = 7.0f;
        constexpr float InitialUndockedY = 1.8f;
        constexpr float InitialUndockedZ = 3.2f;
        constexpr float InitialUndockedWidth = 2.4f;
        constexpr float InitialUndockedHeight = 1.35f;
        // This version belongs to the persisted settings document, not the mod
        // release. Increment it only when an upgrade must deliberately replace
        // an already-saved value. Each migration runs once, after which the
        // user's new choice is respected normally.
        constexpr int CurrentSettingsMigrationVersion = 2;

        bool NearlyEqual(float left, float right)
        {
            return std::abs(left - right) < 0.0005f;
        }

        Configuration& GetConfiguration()
        {
            // Configuration keys files by mod ID. Keeping this object private
            // to Settings gives every caller one normalization and write path.
            static modloader::ModInfo settingsModInfo{MOD_ID, VERSION, 0};
            static Configuration configuration(settingsModInfo);
            return configuration;
        }

        bool ReadBool(
            const rapidjson::Document& document,
            const char* name,
            bool fallback)
        {
            const auto member = document.FindMember(name);
            return member != document.MemberEnd() && member->value.IsBool()
                ? member->value.GetBool()
                : fallback;
        }

        float ReadFloat(
            const rapidjson::Document& document,
            const char* name,
            float fallback)
        {
            const auto member = document.FindMember(name);
            return member != document.MemberEnd() && member->value.IsNumber()
                ? member->value.GetFloat()
                : fallback;
        }

        int ReadInt(
            const rapidjson::Document& document,
            const char* name,
            int fallback)
        {
            const auto member = document.FindMember(name);
            return member != document.MemberEnd() && member->value.IsInt()
                ? member->value.GetInt()
                : fallback;
        }

        template<typename T>
        void Replace(
            rapidjson::Document& document,
            const char* name,
            T value)
        {
            document.RemoveMember(name);
            rapidjson::Value key(name, document.GetAllocator());
            document.AddMember(key.Move(), value, document.GetAllocator());
        }

        void ApplySettingsMigrations(rapidjson::Document& document)
        {
            // A missing version identifies every settings file written before
            // the migration ledger existed. Keep these steps ordered and
            // append-only so a user can upgrade across several releases in one
            // launch without resetting unrelated preferences.
            int version = std::max(
                ReadInt(document, "settingsMigrationVersion", 0),
                0);
            while(version < CurrentSettingsMigrationVersion)
            {
                switch(version)
                {
                    case 0:
                        // GPU conversion/read-ahead has completed its initial
                        // Quest 2 validation. Promote it for both fresh installs
                        // and existing users even when an experimental build
                        // previously persisted the old false default. Once this
                        // migration is recorded, a user may turn it off again.
                        Replace(document, "gpuVideoConversionEnabled", true);
                        version = 1;
                        BigScreen::BigScreenLogger.info(
                            "Applied settings migration 1: enabled GPU Video Conversion");
                        break;
                    case 1:
                        // The embedded shader is now the preferred visible
                        // video-material path. Promote existing settings once,
                        // including the false value written by its former
                        // experimental default, without permanently overriding
                        // a user's later choice.
                        Replace(document, "embeddedVideoShaderEnabled", true);
                        version = 2;
                        BigScreen::BigScreenLogger.info(
                            "Applied settings migration 2: enabled Embedded Video Shader");
                        break;
                    default:
                        // The loop is intentionally guarded even though every
                        // known version has an explicit case. Never risk a
                        // startup loop if a future edit increments the current
                        // version before adding its migration body.
                        BigScreen::BigScreenLogger.error(
                            "No migration exists for Big Screen settings version {}",
                            version);
                        version = CurrentSettingsMigrationVersion;
                        break;
                }
            }

            // Preserve a newer version if a settings file is later read by an
            // older build. Unknown keys are likewise retained by WriteNow().
            Replace(
                document,
                "settingsMigrationVersion",
                std::max(
                    version,
                    ReadInt(document, "settingsMigrationVersion", 0)));
        }

        int NormalizePlaybackFps(int value)
        {
            // These are presentation ceilings, not forced rates. A 24 FPS
            // source remains 24 FPS even when the selected ceiling is 30 or 60.
            return value == 15 || value == 30 || value == 60 ? value : 30;
        }

        int NormalizeGpuReadAheadMemoryMiB(int value)
        {
            constexpr int MinimumMiB = 32;
            constexpr int MaximumMiB = 256;
            constexpr int StepMiB = 16;
            const int clamped = std::clamp(value, MinimumMiB, MaximumMiB);
            const int stepped = MinimumMiB +
                static_cast<int>(std::lround(
                    static_cast<double>(clamped - MinimumMiB) / StepMiB)) *
                    StepMiB;
            return std::clamp(stepped, MinimumMiB, MaximumMiB);
        }

        void SetLoggedBoolean(const char* name, bool& target, bool value)
        {
            const bool previous = target;
            target = value;
            if(previous != target)
                DiagnosticSessionLogger::Instance().MenuEvent(
                    "setting_changed", "Settings", {
                        {"setting", name},
                        {"previousValue", previous ? "true" : "false"},
                        {"newValue", target ? "true" : "false"}});
        }

        template<typename Number>
        void SetLoggedDiscrete(const char* name, Number& target, Number value)
        {
            const auto previous = target;
            target = value;
            if(previous != target)
                DiagnosticSessionLogger::Instance().MenuEvent(
                    "setting_changed", "Settings", {
                        {"setting", name},
                        {"previousValue", std::to_string(previous)},
                        {"newValue", std::to_string(target)}});
        }

        void SetLoggedSlider(const char* name, float& target, float value)
        {
            const float previous = target;
            target = value;
            DiagnosticSessionLogger::Instance().SliderChanged(
                name, previous, target);
        }
    }

    Settings& Settings::Instance()
    {
        static Settings settings;
        return settings;
    }

    void Settings::Load()
    {
        auto& configuration = GetConfiguration();
        const auto configPath = std::filesystem::path(
            Configuration::getConfigFilePath(configuration.info));
        std::error_code fileError;
        if(std::filesystem::is_regular_file(configPath, fileError) && !fileError)
        {
            std::ifstream stream(configPath, std::ios::binary);
            const std::string bytes{
                std::istreambuf_iterator<char>(stream), {}};
            rapidjson::Document validation;
            validation.Parse(bytes.data(), bytes.size());
            if(validation.HasParseError() || !validation.IsObject())
            {
                const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                const auto quarantine = configPath.string() +
                    ".corrupt-" + std::to_string(stamp);
                std::filesystem::rename(configPath, quarantine, fileError);
                if(fileError)
                    BigScreen::BigScreenLogger.error(
                        "Big Screen settings were invalid and could not be quarantined: {}",
                        fileError.message());
                else
                    BigScreen::BigScreenLogger.warn(
                        "Quarantined invalid Big Screen settings as '{}' before restoring defaults",
                        quarantine);
                ErrorManager::Instance().RecordError(
                    "Loading Big Screen settings",
                    fileError
                        ? "The settings JSON was invalid and quarantine failed: " + fileError.message()
                        : "The settings JSON was invalid and was preserved at " + quarantine);
            }
        }
        configuration.Load();
        auto& document = configuration.config;
        if(!document.IsObject())
            document.SetObject();

        ApplySettingsMigrations(document);

        modEnabled_ = ReadBool(document, "modEnabled", true);
        distractionFreeMenu_ = ReadBool(
            document,
            "distractionFreeMenu",
            true);
        // Migrate the former Boolean without changing what an existing user
        // sees. A missing four-state value maps true to Menu Environment and
        // false to No Environment; future map modes persist as their enum.
        const bool legacyShowMenuEnvironment = ReadBool(
            document, "showMenuEnvironment", true);
        menuEnvironmentMode_ = static_cast<MenuEnvironmentMode>(std::clamp(
            ReadInt(
                document,
                "menuEnvironmentMode",
                legacyShowMenuEnvironment
                    ? static_cast<int>(MenuEnvironmentMode::MenuEnvironment)
                    : static_cast<int>(MenuEnvironmentMode::NoEnvironment)),
            static_cast<int>(MenuEnvironmentMode::NoEnvironment),
            static_cast<int>(
                MenuEnvironmentMode::MapEnvironmentAndLightshow)));
        showMenuGameplayHud_ = ReadBool(
            document,
            "showMenuGameplayHud",
            false);
        // v0.7 originally stored lane-guide state under this development key.
        // It remains only as a one-time fallback for that independent option;
        // the four-state environment mode now owns scenery and floor visibility.
        const bool legacyOpenFloorPlacement = ReadBool(
            document,
            "menuPlacementGuideEnabled",
            false);
        showLaneGuidesEnabled_ = ReadBool(
            document,
            "showLaneGuidesEnabled",
            legacyOpenFloorPlacement);
        // v0.6 promotes the former per-selection default to one persistent,
        // game-wide Video switch. Read the old key as a migration fallback so
        // an upgrade preserves the user's existing choice.
        videoEnabled_ = ReadBool(
            document,
            "videoEnabled",
            ReadBool(document, "videoEnabledByDefault", true));
        menuPreviewEnabled_ = ReadBool(document, "showMenuPreview", true);
        // These two values were global in early builds. Use them only as the
        // migration fallback for every profile; Save writes the new per-layout
        // keys and removes the obsolete global keys after this load finishes.
        const bool legacyAdvancedControls = ReadBool(
            document, "advancedOptionsEnabled", false);
        const bool legacyTransparency = ReadBool(
            document, "transparencyEnabled", false);
        // Profile 1 migrates the original single-layout keys. Newly introduced
        // profiles 4 and 5 begin at documented defaults without changing the
        // three layouts saved by earlier versions.
        for(int index = 0; index < static_cast<int>(screenLayouts_.size()); ++index)
        {
            const auto prefix = "screenLayout" + std::to_string(index + 1);
            auto& layout = screenLayouts_[index];
            const bool legacy = index == 0;
            layout.advancedControls = ReadBool(
                document,
                (prefix + "AdvancedControls").c_str(),
                legacyAdvancedControls);
            layout.letterboxTransparency = ReadBool(
                document,
                (prefix + "LetterboxTransparency").c_str(),
                ReadBool(
                    document,
                    (prefix + "Transparency").c_str(),
                    legacyTransparency));
            layout.videoOpacity = std::clamp(ReadFloat(
                document,
                (prefix + "VideoOpacity").c_str(),
                1.0f), 0.0f, 1.0f);
            layout.distanceOffset = std::clamp(ReadFloat(
                document, (prefix + "Distance").c_str(),
                legacy ? ReadFloat(document, "screenDistanceOffset", 0.0f) : 0.0f), -180.0f, 180.0f);
            layout.horizontalOffset = std::clamp(ReadFloat(
                document, (prefix + "Horizontal").c_str(),
                legacy ? ReadFloat(document, "screenHorizontalOffset", 0.0f) : 0.0f), -180.0f, 180.0f);
            layout.verticalOffset = std::clamp(ReadFloat(
                document, (prefix + "Vertical").c_str(),
                legacy ? ReadFloat(document, "screenVerticalOffset", 0.0f) : 0.0f), -180.0f, 180.0f);
            layout.tiltOffset = std::clamp(ReadFloat(
                document, (prefix + "Tilt").c_str(),
                legacy ? ReadFloat(document, "screenTiltOffset", 0.0f) : 0.0f), -180.0f, 180.0f);
            const float configuredScale = ReadFloat(
                document, (prefix + "Scale").c_str(),
                legacy ? ReadFloat(document, "screenScale", 1.0f) : 1.0f);
            layout.curved = ReadBool(
                document, (prefix + "Curved").c_str(),
                legacy ? ReadBool(document, "curvedScreenEnabled", false) : false);
            // Read the geometry mode before normalizing scale. Both modes now
            // permit 8x, while this shared normalization still protects
            // against hand-edited values outside the supported range.
            layout.scale = CoreLogic::NormalizeScreenScale(
                configuredScale,
                layout.curved);
            layout.curvature = std::clamp(ReadFloat(
                document, (prefix + "Curvature").c_str(),
                legacy ? ReadFloat(document, "screenCurvature", 0.35f) : 0.35f),
                -MaximumScreenCurvature, MaximumScreenCurvature);
            layout.maintainAspectRatio = ReadBool(
                document, (prefix + "MaintainAspect").c_str(),
                legacy ? ReadBool(document, "maintainCurveAspectRatio", false) : false);
            layout.screenRoll = std::clamp(ReadFloat(
                document, (prefix + "ScreenRoll").c_str(), 0.0f), -180.0f, 180.0f);
            layout.videoRotation = std::clamp(ReadFloat(
                document, (prefix + "VideoRotation").c_str(), 0.0f), -180.0f, 180.0f);
            layout.videoZoom = std::clamp(ReadFloat(
                document, (prefix + "VideoZoom").c_str(), 1.0f), 0.5f, 3.0f);
            layout.videoOffsetX = std::clamp(ReadFloat(
                document, (prefix + "VideoOffsetX").c_str(), 0.0f), -1.0f, 1.0f);
            layout.videoOffsetY = std::clamp(ReadFloat(
                document, (prefix + "VideoOffsetY").c_str(), 0.0f), -1.0f, 1.0f);
            layout.videoTilt = std::clamp(ReadFloat(
                document, (prefix + "VideoTilt").c_str(), 0.0f), -75.0f, 75.0f);
            layout.stretchVideoToFit = ReadBool(
                document, (prefix + "StretchVideoToFit").c_str(), false);
            layout.undocked = ReadBool(
                document, (prefix + "Undocked").c_str(), false);
            layout.undockedConfigured = ReadBool(
                document, (prefix + "UndockedConfigured").c_str(), false);
            layout.undockedPositionX = std::clamp(ReadFloat(
                document, (prefix + "UndockedPositionX").c_str(), 0.0f), -100.0f, 100.0f);
            layout.undockedPositionY = std::clamp(ReadFloat(
                document, (prefix + "UndockedPositionY").c_str(), InitialUndockedY), -100.0f, 100.0f);
            layout.undockedPositionZ = std::clamp(ReadFloat(
                document, (prefix + "UndockedPositionZ").c_str(), InitialUndockedZ), -100.0f, 100.0f);
            layout.undockedRotationX = std::clamp(ReadFloat(
                document, (prefix + "UndockedRotationX").c_str(), 0.0f), -360.0f, 360.0f);
            layout.undockedRotationY = std::clamp(ReadFloat(
                document, (prefix + "UndockedRotationY").c_str(), 0.0f), -360.0f, 360.0f);
            layout.undockedRotationZ = std::clamp(ReadFloat(
                document, (prefix + "UndockedRotationZ").c_str(), 0.0f), -360.0f, 360.0f);
            layout.undockedWidth = std::clamp(ReadFloat(
                document, (prefix + "UndockedWidth").c_str(), InitialUndockedWidth), 0.5f, 50.0f);
            layout.undockedHeight = std::clamp(ReadFloat(
                document, (prefix + "UndockedHeight").c_str(), InitialUndockedHeight), 0.5f, 50.0f);

            // Early development builds initialized free positioning eight
            // metres away with a screen over five metres wide. That made the
            // controller overlay difficult to read and is exactly the value
            // already stored by testers who enabled the feature once. Migrate
            // only that exact legacy preset; genuinely placed screens retain
            // their saved transform and dimensions.
            const bool legacyUndockedPreset =
                layout.undockedConfigured &&
                NearlyEqual(layout.undockedPositionX, 0.0f) &&
                NearlyEqual(layout.undockedPositionY, 3.0f) &&
                NearlyEqual(layout.undockedPositionZ, 8.0f) &&
                NearlyEqual(layout.undockedRotationX, 0.0f) &&
                NearlyEqual(layout.undockedRotationY, 0.0f) &&
                NearlyEqual(layout.undockedRotationZ, 0.0f) &&
                NearlyEqual(layout.undockedWidth, 5.333333f) &&
                NearlyEqual(layout.undockedHeight, 3.0f);
            if(legacyUndockedPreset)
            {
                layout.undockedPositionY = InitialUndockedY;
                layout.undockedPositionZ = InitialUndockedZ;
                layout.undockedWidth = InitialUndockedWidth;
                layout.undockedHeight = InitialUndockedHeight;
                layout.undockedConfigured = false;
            }
        }
        activeScreenLayout_ = std::clamp(
            ReadInt(document, "activeScreenLayout", 0), 0, 4);
        respectMapperSettings_ = ReadBool(
            document, "respectMapperSettings", true);
        allowChromaOverride_ = ReadBool(document, "allowChromaOverride", true);
        mapLightShowEnabled_ = ReadBool(document, "mapLightShowEnabled", true);
        hideBackWallLights_ = ReadBool(document, "hideBackWallLights", true);
        hideRingLights_ = ReadBool(document, "hideRingLights", true);
        hideSideLaserLights_ = ReadBool(document, "hideSideLaserLights", true);
        environmentOverrideEnabled_ = ReadBool(document, "environmentOverrideEnabled", true);
        glassDesertOverrideEnabled_ = ReadBool(document, "glassDesertOverrideEnabled", false);
        // The original control expressed the positive state: On meant motion
        // was enabled. Migrate it by inversion so an existing user sees the
        // same gameplay behavior under the clearer disable-style toggle.
        disableEnvironmentMotion_ = ReadBool(
            document,
            "disableEnvironmentMotion",
            // If the old positive-state key exists, invert it. If neither key
            // exists (a first install), false inverts to the new ON default.
            !ReadBool(document, "environmentMotionEnabled", false));
        hideTrackRings_ = ReadBool(document, "hideTrackRings", true);
        hideSideBars_ = ReadBool(
            document,
            "hideSideBars",
            ReadBool(document, "hideLaserRigs", true));
        hideSpectrogramBars_ = ReadBool(document, "hideSpectrogramBars", true);
        playbackFpsLimit_ = NormalizePlaybackFps(
            ReadInt(document, "playbackFpsLimit", 30));
        useFfmpeg9_ = ReadBool(document, "useFfmpeg9", true);
        embeddedVideoShaderEnabled_ = ReadBool(
            document, "embeddedVideoShaderEnabled", true);
        nativeBloomLevel_ = std::clamp(
            ReadFloat(document, "nativeBloomLevel", 1.0f), 0.0f, 1.0f);
        cinemaBloomLevel_ = std::clamp(
            ReadFloat(document, "cinemaBloomLevel", 1.0f), 0.0f, 2.0f);
        hardwareDecodingEnabled_ = ReadBool(
            document, "hardwareDecodingEnabled", true);
        gpuVideoConversionEnabled_ = ReadBool(
            document, "gpuVideoConversionEnabled", true);
        consolidatedYuvUploadEnabled_ = ReadBool(
            document, "consolidatedYuvUploadEnabled", false);
        gpuReadAheadMemoryMiB_ = NormalizeGpuReadAheadMemoryMiB(
            ReadInt(document, "gpuReadAheadMemoryMiB", 64));
        automaticPerformanceEnabled_ = ReadBool(
            document, "automaticPerformanceEnabled", false);
        automaticPerformanceThreshold_ = std::clamp(
            ReadInt(document, "automaticPerformanceThreshold", 5), 1, 15);
        automaticPerformanceAttackSeconds_ = std::clamp(
            ReadFloat(
                document,
                "automaticPerformanceAttackSeconds",
                ReadFloat(document, "automaticPerformanceResponseSeconds", 5.0f)),
            0.5f,
            10.0f);
        automaticPerformanceReleaseSeconds_ = std::clamp(
            ReadFloat(document, "automaticPerformanceReleaseSeconds", 5.0f),
            0.5f,
            30.0f);
        automaticPerformanceFpsStep_ = std::clamp(
            ReadInt(document, "automaticPerformanceFpsStep", 5), 1, 5);
        automaticPerformanceOscillationPreventionEnabled_ = ReadBool(
            document,
            "automaticPerformanceOscillationPreventionEnabled",
            true);
        automaticPerformanceOscillationLimit_ = std::clamp(
            ReadInt(document, "automaticPerformanceOscillationLimit", 3),
            1,
            10);
        performanceDiagnosticsEnabled_ = ReadBool(
            document, "performanceDiagnosticsEnabled", false);
        performancePanelPositionX_ = std::clamp(ReadFloat(
            document, "performancePanelPositionX", 0.0f), -100.0f, 100.0f);
        performancePanelPositionY_ = std::clamp(ReadFloat(
            document, "performancePanelPositionY", 3.05f), -100.0f, 100.0f);
        performancePanelPositionZ_ = std::clamp(ReadFloat(
            document, "performancePanelPositionZ", 4.25f), -100.0f, 100.0f);
        performancePanelRotationX_ = std::clamp(ReadFloat(
            document, "performancePanelRotationX", 0.0f), -360.0f, 360.0f);
        performancePanelRotationY_ = std::clamp(ReadFloat(
            document, "performancePanelRotationY", 0.0f), -360.0f, 360.0f);
        performancePanelRotationZ_ = std::clamp(ReadFloat(
            document, "performancePanelRotationZ", 0.0f), -360.0f, 360.0f);
        powerBenchmarkEnabled_ = ReadBool(
            document, "powerBenchmarkEnabled", false);
        detailedDiagnosticLoggingEnabled_ = ReadBool(
            document, "detailedDiagnosticLoggingEnabled", true);
        nightlyDownloaderUpdates_ = ReadBool(document, "nightlyDownloaderUpdates", false);

        // Preview decoding is an avoidable performance cost when videos are
        // globally switched off. Persist the dependency so the disabled state
        // is also honored on the next launch, before any menu exists.
        if(!videoEnabled_)
            menuPreviewEnabled_ = false;

        // Persist migrations immediately. The debounce is intended for rapid
        // interactive changes, not startup normalization that may otherwise
        // be lost if Beat Saber exits before its first menu update.
        WriteNow();
    }

    void Settings::Reset()
    {
        // Reconstruct from the member initializers in Settings.hpp instead of
        // maintaining a second handwritten defaults list here. This resets
        // every current field, including hidden experimental settings, and
        // makes a newly added setting participate automatically as long as it
        // declares its intended default beside the member itself.
        *this = Settings{};
        if(!DiagnosticSessionLogger::Instance().MenuSessionActive())
            DiagnosticSessionLogger::Instance().BeginMenuSession({
                {"startedBy", "Reset to Defaults"}});
        DiagnosticSessionLogger::Instance().MenuEvent(
            "settings_reset", "Settings", {{"scope", "all"}});
        ErrorManager::Instance().ResetCircuitBreaker();
        WriteNow();
    }

    void Settings::ResetActiveScreenLayout()
    {
        // ScreenLayoutProfile owns the authoritative defaults beside each
        // field declaration. Reconstructing just this array entry prevents a
        // new per-layout option from being missed here while preserving all
        // other layouts and every game-wide setting.
        screenLayouts_[activeScreenLayout_] = ScreenLayoutProfile{};
        DiagnosticSessionLogger::Instance().MenuEvent(
            "settings_reset", "Settings", {
                {"scope", "screen_layout"},
                {"layout", std::to_string(activeScreenLayout_ + 1)}});
        Save();
    }

    void Settings::SetModEnabled(bool value)
    {
        SetLoggedBoolean("Big Screen Enabled", modEnabled_, value);
        if(value)
            ErrorManager::Instance().ResetCircuitBreaker();
        Save();
        if(!value)
            Flush();
    }

    void Settings::SetDistractionFreeMenu(bool value)
    {
        SetLoggedBoolean("Distraction Free Menu", distractionFreeMenu_, value);
        Save();
    }

    void Settings::SetShowMenuEnvironment(bool value)
    {
        SetMenuEnvironmentMode(value
            ? MenuEnvironmentMode::MenuEnvironment
            : MenuEnvironmentMode::NoEnvironment);
    }

    void Settings::SetMenuEnvironmentMode(MenuEnvironmentMode value)
    {
        const auto normalized = static_cast<MenuEnvironmentMode>(std::clamp(
            static_cast<int>(value),
            static_cast<int>(MenuEnvironmentMode::NoEnvironment),
            static_cast<int>(
                MenuEnvironmentMode::MapEnvironmentAndLightshow)));
        const auto previous = menuEnvironmentMode_;
        menuEnvironmentMode_ = normalized;
        if(previous != menuEnvironmentMode_)
            DiagnosticSessionLogger::Instance().MenuEvent(
                "setting_changed", "Settings", {
                    {"setting", "Menu Environment Mode"},
                    {"previousValue", std::to_string(
                        static_cast<int>(previous))},
                    {"newValue", std::to_string(
                        static_cast<int>(menuEnvironmentMode_))}});
        Save();
    }

    void Settings::SetShowMenuGameplayHud(bool value)
    {
        SetLoggedBoolean(
            "Show Gameplay HUD", showMenuGameplayHud_, value);
        Save();
    }

    void Settings::SetShowLaneGuidesEnabled(bool value)
    {
        SetLoggedBoolean("Show Lane Guides", showLaneGuidesEnabled_, value);
        Save();
    }

    void Settings::SetVideoEnabled(bool value)
    {
        SetLoggedBoolean("Video In Map", videoEnabled_, value);
        if(!value)
            menuPreviewEnabled_ = false;
        Save();
    }

    void Settings::SetMenuPreviewEnabled(bool value)
    {
        // Do not permit a stale UI callback or hand-authored config to enable
        // a decoder while the global video switch is disabled.
        SetLoggedBoolean(
            "Preview Video On Song Selection",
            menuPreviewEnabled_, videoEnabled_ && value);
        Save();
    }

    void Settings::SetAdvancedOptionsEnabled(bool value)
    {
        SetLoggedBoolean(
            "Advanced Screen Controls",
            screenLayouts_[activeScreenLayout_].advancedControls, value);
        Save();
    }

    void Settings::SetActiveScreenLayout(int value)
    {
        SetLoggedDiscrete(
            "Editing Screen Layout", activeScreenLayout_,
            std::clamp(value, 0, 4));
        Save();
    }

    void Settings::SetScreenDistanceOffset(float value)
    {
        SetLoggedSlider(
            "Screen Distance",
            screenLayouts_[activeScreenLayout_].distanceOffset,
            std::clamp(value, -180.0f, 180.0f));
        Save();
    }

    void Settings::SetScreenHorizontalOffset(float value)
    {
        SetLoggedSlider(
            "Screen X",
            screenLayouts_[activeScreenLayout_].horizontalOffset,
            std::clamp(value, -180.0f, 180.0f));
        Save();
    }

    void Settings::SetScreenVerticalOffset(float value)
    {
        // Large canvases need substantially more travel than the original
        // +/-40 control provided. Use the same symmetric range as X and depth
        // so every docked placement axis behaves predictably.
        SetLoggedSlider(
            "Screen Y",
            screenLayouts_[activeScreenLayout_].verticalOffset,
            std::clamp(value, -180.0f, 180.0f));
        Save();
    }

    void Settings::SetScreenTiltOffset(float value)
    {
        SetLoggedSlider(
            "Screen Tilt",
            screenLayouts_[activeScreenLayout_].tiltOffset,
            std::clamp(value, -180.0f, 180.0f));
        Save();
    }

    void Settings::SetScreenScale(float value)
    {
        auto& layout = screenLayouts_[activeScreenLayout_];
        SetLoggedSlider(
            "Screen Size Multiplier", layout.scale,
            CoreLogic::NormalizeScreenScale(value, layout.curved));
        Save();
    }

    void Settings::SetCurvedScreenEnabled(bool value)
    {
        auto& layout = screenLayouts_[activeScreenLayout_];
        SetLoggedBoolean("Curved Screen", layout.curved, value);
        // Normalize through the mode-aware helper even though flat and curved
        // currently share an 8x ceiling. Keeping one path prevents stale or
        // hand-edited values from bypassing the supported range.
        layout.scale = CoreLogic::NormalizeScreenScale(layout.scale, value);
        Save();
    }

    float Settings::MaximumScreenScale() const
    {
        return CoreLogic::ScreenScaleMaximum(CurvedScreenEnabled());
    }

    void Settings::SetScreenCurvature(float value)
    {
        // Preserve the original response through +/-1 while permitting a
        // strong wrap without the impractical geometry produced by the old
        // experimental +/-25 limit.
        SetLoggedSlider(
            "Screen Curve",
            screenLayouts_[activeScreenLayout_].curvature,
            std::clamp(value, -MaximumScreenCurvature, MaximumScreenCurvature));
        Save();
    }

    void Settings::SetMaintainCurveAspectRatio(bool value)
    {
        SetLoggedBoolean(
            "Maintain Aspect Ratio",
            screenLayouts_[activeScreenLayout_].maintainAspectRatio, value);
        Save();
    }

    void Settings::SetScreenRoll(float value)
    {
        SetLoggedSlider(
            "Screen Rotation", screenLayouts_[activeScreenLayout_].screenRoll,
            std::clamp(value, -180.0f, 180.0f));
        Save();
    }

    void Settings::SetVideoRotation(float value)
    {
        SetLoggedSlider(
            "Video Rotation", screenLayouts_[activeScreenLayout_].videoRotation,
            std::clamp(value, -180.0f, 180.0f));
        Save();
    }

    void Settings::SetVideoZoom(float value)
    {
        SetLoggedSlider(
            "Video Zoom", screenLayouts_[activeScreenLayout_].videoZoom,
            std::clamp(value, 0.5f, 3.0f));
        Save();
    }

    void Settings::SetVideoOffsetX(float value)
    {
        SetLoggedSlider(
            "Video X", screenLayouts_[activeScreenLayout_].videoOffsetX,
            std::clamp(value, -1.0f, 1.0f));
        Save();
    }

    void Settings::SetVideoOffsetY(float value)
    {
        SetLoggedSlider(
            "Video Y", screenLayouts_[activeScreenLayout_].videoOffsetY,
            std::clamp(value, -1.0f, 1.0f));
        Save();
    }

    void Settings::SetVideoTilt(float value)
    {
        SetLoggedSlider(
            "Video Tilt", screenLayouts_[activeScreenLayout_].videoTilt,
            std::clamp(value, -75.0f, 75.0f));
        Save();
    }

    void Settings::SetStretchVideoToFit(bool value)
    {
        SetLoggedBoolean(
            "Stretch Video To Fit",
            screenLayouts_[activeScreenLayout_].stretchVideoToFit, value);
        Save();
    }

    void Settings::SetUndockedScreenEnabled(bool value)
    {
        auto& layout = screenLayouts_[activeScreenLayout_];
        SetLoggedBoolean("Undock Screen", layout.undocked, value);
        if(value && !layout.undockedConfigured)
        {
            // Start close enough for the instructions and grab handles to be
            // readable, but small enough that the screen cannot cover both
            // side menus before the player has positioned it.
            layout.undockedPositionX = 0.0f;
            layout.undockedPositionY = InitialUndockedY;
            layout.undockedPositionZ = InitialUndockedZ;
            layout.undockedRotationX = 0.0f;
            layout.undockedRotationY = 0.0f;
            layout.undockedRotationZ = 0.0f;
            layout.undockedWidth = InitialUndockedWidth;
            layout.undockedHeight = InitialUndockedHeight;
        }
        Save();
    }

    void Settings::SaveUndockedScreen(
        float positionX, float positionY, float positionZ,
        float rotationX, float rotationY, float rotationZ,
        float width, float height)
    {
        auto& layout = screenLayouts_[activeScreenLayout_];
        layout.undockedPositionX = std::clamp(positionX, -100.0f, 100.0f);
        layout.undockedPositionY = std::clamp(positionY, -100.0f, 100.0f);
        layout.undockedPositionZ = std::clamp(positionZ, -100.0f, 100.0f);
        layout.undockedRotationX = std::clamp(rotationX, -360.0f, 360.0f);
        layout.undockedRotationY = std::clamp(rotationY, -360.0f, 360.0f);
        layout.undockedRotationZ = std::clamp(rotationZ, -360.0f, 360.0f);
        layout.undockedWidth = std::clamp(width, 0.5f, 50.0f);
        layout.undockedHeight = std::clamp(height, 0.5f, 50.0f);
        layout.undockedConfigured = true;
        DiagnosticSessionLogger::Instance().MenuEvent(
            "screen_saved", "Settings", {
                {"layout", std::to_string(activeScreenLayout_ + 1)},
                {"width", std::to_string(layout.undockedWidth)},
                {"height", std::to_string(layout.undockedHeight)}});
        Save();
    }

    void Settings::SetAllowChromaOverride(bool value)
    {
        SetLoggedBoolean("Allow Chroma Override", allowChromaOverride_, value);
        Save();
    }

    void Settings::SetLetterboxTransparencyEnabled(bool value)
    {
        SetLoggedBoolean(
            "Letter Box Transparency",
            screenLayouts_[activeScreenLayout_].letterboxTransparency, value);
        Save();
    }

    void Settings::SetVideoOpacity(float value)
    {
        SetLoggedSlider(
            "Video Transparency",
            screenLayouts_[activeScreenLayout_].videoOpacity,
            std::clamp(value, 0.0f, 1.0f));
        Save();
    }

    void Settings::SetMapLightShowEnabled(bool value)
    {
        SetLoggedBoolean("Map Light Show", mapLightShowEnabled_, value);
        Save();
    }

    void Settings::SetHideBackWallLights(bool value)
    {
        SetLoggedBoolean("Hide Back Wall Lights", hideBackWallLights_, value);
        Save();
    }

    void Settings::SetHideRingLights(bool value)
    {
        SetLoggedBoolean("Hide Ring Lights", hideRingLights_, value);
        Save();
    }

    void Settings::SetHideSideLaserLights(bool value)
    {
        SetLoggedBoolean("Hide Side Laser Lights", hideSideLaserLights_, value);
        Save();
    }

    void Settings::SetEnvironmentOverrideEnabled(bool value)
    {
        SetLoggedBoolean("Environment Override", environmentOverrideEnabled_, value);
        Save();
    }

    void Settings::SetGlassDesertOverrideEnabled(bool value)
    {
        SetLoggedBoolean("Force Glass Desert", glassDesertOverrideEnabled_, value);
        Save();
    }

    void Settings::SetDisableEnvironmentMotion(bool value)
    {
        SetLoggedBoolean("Disable Rotation And Motion", disableEnvironmentMotion_, value);
        Save();
    }

    void Settings::SetHideTrackRings(bool value)
    {
        SetLoggedBoolean("Hide Track Rings", hideTrackRings_, value);
        Save();
    }

    void Settings::SetHideSideBars(bool value)
    {
        SetLoggedBoolean("Hide Side Bars", hideSideBars_, value);
        Save();
    }

    void Settings::SetHideSpectrogramBars(bool value)
    {
        SetLoggedBoolean("Hide Spectrogram Bars", hideSpectrogramBars_, value);
        Save();
    }

    void Settings::SetPlaybackFpsLimit(int value)
    {
        SetLoggedDiscrete(
            "Playback FPS Limit", playbackFpsLimit_, NormalizePlaybackFps(value));
        Save();
    }

    void Settings::SetUseFfmpeg9(bool value)
    {
        SetLoggedBoolean("Use FFmpeg 9", useFfmpeg9_, value);
        Save();
    }

    void Settings::SetEmbeddedVideoShaderEnabled(bool value)
    {
        SetLoggedBoolean("Use Embedded Video Shader", embeddedVideoShaderEnabled_, value);
        Save();
    }

    void Settings::SetHardwareDecodingEnabled(bool value)
    {
        SetLoggedBoolean("Hardware Decoding", hardwareDecodingEnabled_, value);
        Save();
    }

    void Settings::SetGpuVideoConversionEnabled(bool value)
    {
        SetLoggedBoolean(
            "GPU Video Conversion", gpuVideoConversionEnabled_, value);
        Save();
    }

    void Settings::SetConsolidatedYuvUploadEnabled(bool value)
    {
        SetLoggedBoolean(
            "Consolidated YUV Upload", consolidatedYuvUploadEnabled_, value);
        Save();
    }

    void Settings::SetGpuReadAheadMemoryMiB(int value)
    {
        SetLoggedDiscrete(
            "GPU Read-Ahead Memory",
            gpuReadAheadMemoryMiB_,
            NormalizeGpuReadAheadMemoryMiB(value));
        Save();
    }

    void Settings::SetNativeBloomLevel(float value)
    {
        SetLoggedSlider(
            "Native Bloom Level", nativeBloomLevel_, std::clamp(
                std::round(value * 10.0f) / 10.0f, 0.0f, 1.0f));
        Save();
    }

    void Settings::SetCinemaBloomLevel(float value)
    {
        SetLoggedSlider(
            "Cinema Blur Level", cinemaBloomLevel_, std::clamp(
                std::round(value * 10.0f) / 10.0f, 0.0f, 2.0f));
        Save();
    }

    void Settings::SetAutomaticPerformanceEnabled(bool value)
    {
        SetLoggedBoolean("Automatic Performance", automaticPerformanceEnabled_, value);
        Save();
    }

    void Settings::SetAutomaticPerformanceThreshold(int value)
    {
        const auto normalized = std::clamp(value, 1, 15);
        SetLoggedDiscrete(
            "Frame Rate Loss Threshold", automaticPerformanceThreshold_, normalized);
        Save();
    }

    void Settings::SetAutomaticPerformanceAttackSeconds(float value)
    {
        // Quantize persisted values to the same tenth-second grid presented by
        // the slider. This prevents binary float noise from accumulating after
        // repeated arrow taps or JSON load/save cycles.
        SetLoggedSlider(
            "Scaling Attack Time", automaticPerformanceAttackSeconds_,
            std::clamp(std::round(value * 10.0f) / 10.0f, 0.5f, 10.0f));
        Save();
    }

    void Settings::SetRespectMapperSettings(bool value)
    {
        SetLoggedBoolean("Respect Mapper Settings", respectMapperSettings_, value);
        Save();
    }

    void Settings::SetAutomaticPerformanceReleaseSeconds(float value)
    {
        SetLoggedSlider(
            "Scaling Release Time", automaticPerformanceReleaseSeconds_,
            std::clamp(std::round(value * 10.0f) / 10.0f, 0.5f, 30.0f));
        Save();
    }

    void Settings::SetAutomaticPerformanceFpsStep(int value)
    {
        const auto normalized = std::clamp(value, 1, 5);
        SetLoggedDiscrete(
            "FPS Adjustment Step", automaticPerformanceFpsStep_, normalized);
        Save();
    }

    void Settings::SetAutomaticPerformanceOscillationPreventionEnabled(bool value)
    {
        SetLoggedBoolean(
            "Prevent FPS Oscillation",
            automaticPerformanceOscillationPreventionEnabled_, value);
        Save();
    }

    void Settings::SetAutomaticPerformanceOscillationLimit(int value)
    {
        const auto normalized = std::clamp(value, 1, 10);
        SetLoggedDiscrete(
            "FPS Oscillation Limit",
            automaticPerformanceOscillationLimit_, normalized);
        Save();
    }

    void Settings::SetPerformanceDiagnosticsEnabled(bool value)
    {
        SetLoggedBoolean("Show Performance", performanceDiagnosticsEnabled_, value);
        Save();
    }

    void Settings::SetPerformancePanelPlacement(
        float positionX,
        float positionY,
        float positionZ,
        float rotationX,
        float rotationY,
        float rotationZ)
    {
        // A corrupted or transient Unity transform must not make the panel
        // permanently unrecoverable on the next launch. Runtime capture also
        // checks finiteness before calling this boundary.
        performancePanelPositionX_ = std::clamp(positionX, -100.0f, 100.0f);
        performancePanelPositionY_ = std::clamp(positionY, -100.0f, 100.0f);
        performancePanelPositionZ_ = std::clamp(positionZ, -100.0f, 100.0f);
        performancePanelRotationX_ = std::clamp(rotationX, -360.0f, 360.0f);
        performancePanelRotationY_ = std::clamp(rotationY, -360.0f, 360.0f);
        performancePanelRotationZ_ = std::clamp(rotationZ, -360.0f, 360.0f);
        DiagnosticSessionLogger::Instance().MenuEvent(
            "panel_placement_saved", "Settings", {
                {"panel", "performance"},
                {"positionX", std::to_string(performancePanelPositionX_)},
                {"positionY", std::to_string(performancePanelPositionY_)},
                {"positionZ", std::to_string(performancePanelPositionZ_)}});
        Save();
    }

    void Settings::ResetPerformancePanelPlacement()
    {
        performancePanelPositionX_ = 0.0f;
        performancePanelPositionY_ = 3.05f;
        performancePanelPositionZ_ = 4.25f;
        performancePanelRotationX_ = 0.0f;
        performancePanelRotationY_ = 0.0f;
        performancePanelRotationZ_ = 0.0f;
        DiagnosticSessionLogger::Instance().MenuEvent(
            "settings_reset", "Settings", {{"scope", "performance_panel"}});
        Save();
    }

    void Settings::SetPowerBenchmarkEnabled(bool value)
    {
        SetLoggedBoolean("Record Power Benchmark", powerBenchmarkEnabled_, value);
        Save();
    }

    void Settings::SetDetailedDiagnosticLoggingEnabled(bool value)
    {
        detailedDiagnosticLoggingEnabled_ = value;
        Save();
    }

    void Settings::SetNightlyDownloaderUpdates(bool value)
    {
        SetLoggedBoolean("Use Nightly Downloader Updates", nightlyDownloaderUpdates_, value);
        Save();
    }

    void Settings::Save()
    {
        // An unlocked screen is a transactional preview. Keeping these writes
        // in memory is what makes process death, focus loss, and menu teardown
        // fall back to the last explicitly saved screen instead of persisting
        // a half-finished slider or controller movement.
        if(screenEditActive_)
        {
            screenEditDirty_ = true;
            return;
        }
        // Slider callbacks may arrive dozens of times per second. Apply values
        // to the live preview immediately but coalesce their complete JSON
        // rewrite into one durable save after the user stops adjusting them.
        savePending_ = true;
        saveDeadline_ = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(400);
    }

    void Settings::TickPersistence()
    {
        if(!screenEditActive_ && savePending_ &&
           std::chrono::steady_clock::now() >= saveDeadline_)
            WriteNow();
    }

    void Settings::Flush()
    {
        if(!screenEditActive_ && savePending_)
            WriteNow();
    }

    void Settings::BeginScreenEditTransaction()
    {
        if(screenEditActive_)
            return;

        // Commit any setting made before Position Screen was pressed. This
        // establishes an exact durable rollback point (notably the user's
        // decision to enable Undock Screen) before draft editing begins.
        Flush();
        screenEditLayouts_ = screenLayouts_;
        screenEditActiveLayout_ = activeScreenLayout_;
        screenEditAllowChromaOverride_ = allowChromaOverride_;
        screenEditRespectMapperSettings_ = respectMapperSettings_;
        screenEditDirty_ = false;
        screenEditActive_ = true;
    }

    void Settings::CommitScreenEditTransaction()
    {
        if(!screenEditActive_)
            return;
        const bool dirty = screenEditDirty_;
        screenEditActive_ = false;
        screenEditDirty_ = false;
        if(dirty)
            WriteNow();
    }

    void Settings::CancelScreenEditTransaction()
    {
        if(!screenEditActive_)
            return;
        screenLayouts_ = screenEditLayouts_;
        activeScreenLayout_ = screenEditActiveLayout_;
        allowChromaOverride_ = screenEditAllowChromaOverride_;
        respectMapperSettings_ = screenEditRespectMapperSettings_;
        screenEditActive_ = false;
        screenEditDirty_ = false;
    }

    void Settings::WriteNow()
    {
        savePending_ = false;
        auto& configuration = GetConfiguration();
        auto& document = configuration.config;
        if(!document.IsObject())
            document.SetObject();

        // Record the migration ledger alongside ordinary settings on every
        // write. This is what makes forced upgrade values one-time changes
        // rather than permanent overrides of later user choices.
        Replace(
            document,
            "settingsMigrationVersion",
            std::max(
                CurrentSettingsMigrationVersion,
                ReadInt(document, "settingsMigrationVersion", 0)));

        Replace(document, "modEnabled", modEnabled_);
        Replace(document, "distractionFreeMenu", distractionFreeMenu_);
        Replace(
            document,
            "menuEnvironmentMode",
            static_cast<int>(menuEnvironmentMode_));
        Replace(document, "showMenuGameplayHud", showMenuGameplayHud_);
        document.RemoveMember("showMenuEnvironment");
        // Development builds briefly exposed a second floor switch. The
        // environment switch now owns scenery, lighting, and floor together.
        document.RemoveMember("showMenuFloor");
        document.RemoveMember("menuPlacementGuideEnabled");
        Replace(document, "showLaneGuidesEnabled", showLaneGuidesEnabled_);
        // Remove superseded keys after migration so the configuration has one
        // unambiguous source of truth on all later launches.
        document.RemoveMember("videoEnabledByDefault");
        document.RemoveMember("menuScreenPreviewEnabled");
        Replace(document, "videoEnabled", videoEnabled_);
        Replace(document, "showMenuPreview", menuPreviewEnabled_);
        document.RemoveMember("advancedOptionsEnabled");
        document.RemoveMember("transparencyEnabled");
        for(const auto* oldKey : {
            "screenDistanceOffset", "screenHorizontalOffset", "screenVerticalOffset",
            "screenTiltOffset", "screenScale", "curvedScreenEnabled",
            "screenCurvature", "maintainCurveAspectRatio"})
            document.RemoveMember(oldKey);
        Replace(document, "activeScreenLayout", activeScreenLayout_);
        for(int index = 0; index < static_cast<int>(screenLayouts_.size()); ++index)
        {
            const auto prefix = "screenLayout" + std::to_string(index + 1);
            const auto& layout = screenLayouts_[index];
            Replace(document, (prefix + "AdvancedControls").c_str(), layout.advancedControls);
            // Remove the development-era combined transparency key after
            // migrating it to the explicitly letterbox-only setting.
            document.RemoveMember((prefix + "Transparency").c_str());
            Replace(
                document,
                (prefix + "LetterboxTransparency").c_str(),
                layout.letterboxTransparency);
            Replace(document, (prefix + "VideoOpacity").c_str(), layout.videoOpacity);
            Replace(document, (prefix + "Distance").c_str(), layout.distanceOffset);
            Replace(document, (prefix + "Horizontal").c_str(), layout.horizontalOffset);
            Replace(document, (prefix + "Vertical").c_str(), layout.verticalOffset);
            Replace(document, (prefix + "Tilt").c_str(), layout.tiltOffset);
            Replace(document, (prefix + "Scale").c_str(), layout.scale);
            Replace(document, (prefix + "Curved").c_str(), layout.curved);
            Replace(document, (prefix + "Curvature").c_str(), layout.curvature);
            Replace(document, (prefix + "MaintainAspect").c_str(), layout.maintainAspectRatio);
            Replace(document, (prefix + "ScreenRoll").c_str(), layout.screenRoll);
            Replace(document, (prefix + "VideoRotation").c_str(), layout.videoRotation);
            Replace(document, (prefix + "VideoZoom").c_str(), layout.videoZoom);
            Replace(document, (prefix + "VideoOffsetX").c_str(), layout.videoOffsetX);
            Replace(document, (prefix + "VideoOffsetY").c_str(), layout.videoOffsetY);
            Replace(document, (prefix + "VideoTilt").c_str(), layout.videoTilt);
            Replace(document, (prefix + "StretchVideoToFit").c_str(), layout.stretchVideoToFit);
            Replace(document, (prefix + "Undocked").c_str(), layout.undocked);
            Replace(document, (prefix + "UndockedConfigured").c_str(), layout.undockedConfigured);
            Replace(document, (prefix + "UndockedPositionX").c_str(), layout.undockedPositionX);
            Replace(document, (prefix + "UndockedPositionY").c_str(), layout.undockedPositionY);
            Replace(document, (prefix + "UndockedPositionZ").c_str(), layout.undockedPositionZ);
            Replace(document, (prefix + "UndockedRotationX").c_str(), layout.undockedRotationX);
            Replace(document, (prefix + "UndockedRotationY").c_str(), layout.undockedRotationY);
            Replace(document, (prefix + "UndockedRotationZ").c_str(), layout.undockedRotationZ);
            Replace(document, (prefix + "UndockedWidth").c_str(), layout.undockedWidth);
            Replace(document, (prefix + "UndockedHeight").c_str(), layout.undockedHeight);
        }
        Replace(document, "allowChromaOverride", allowChromaOverride_);
        Replace(document, "respectMapperSettings", respectMapperSettings_);
        Replace(document, "mapLightShowEnabled", mapLightShowEnabled_);
        Replace(document, "hideBackWallLights", hideBackWallLights_);
        Replace(document, "hideRingLights", hideRingLights_);
        Replace(document, "hideSideLaserLights", hideSideLaserLights_);
        Replace(document, "environmentOverrideEnabled", environmentOverrideEnabled_);
        Replace(document, "glassDesertOverrideEnabled", glassDesertOverrideEnabled_);
        document.RemoveMember("environmentMotionEnabled");
        Replace(document, "disableEnvironmentMotion", disableEnvironmentMotion_);
        Replace(document, "hideTrackRings", hideTrackRings_);
        document.RemoveMember("hideLaserRigs");
        Replace(document, "hideSideBars", hideSideBars_);
        Replace(document, "hideSpectrogramBars", hideSpectrogramBars_);
        Replace(document, "playbackFpsLimit", playbackFpsLimit_);
        // Playback now always presents the selected file at its native
        // resolution. Silently discard the retired playback-only cap when an
        // older configuration is loaded and written by this version.
        document.RemoveMember("resolutionHeight");
        Replace(document, "useFfmpeg9", useFfmpeg9_);
        Replace(
            document,
            "embeddedVideoShaderEnabled",
            embeddedVideoShaderEnabled_);
        Replace(document, "nativeBloomLevel", nativeBloomLevel_);
        Replace(document, "cinemaBloomLevel", cinemaBloomLevel_);
        Replace(document, "hardwareDecodingEnabled", hardwareDecodingEnabled_);
        Replace(
            document,
            "gpuVideoConversionEnabled",
            gpuVideoConversionEnabled_);
        Replace(
            document,
            "consolidatedYuvUploadEnabled",
            consolidatedYuvUploadEnabled_);
        Replace(
            document,
            "gpuReadAheadMemoryMiB",
            gpuReadAheadMemoryMiB_);
        Replace(document, "automaticPerformanceEnabled", automaticPerformanceEnabled_);
        Replace(document, "automaticPerformanceThreshold", automaticPerformanceThreshold_);
        document.RemoveMember("automaticPerformanceResponseSeconds");
        Replace(
            document,
            "automaticPerformanceAttackSeconds",
            automaticPerformanceAttackSeconds_);
        Replace(
            document,
            "automaticPerformanceReleaseSeconds",
            automaticPerformanceReleaseSeconds_);
        Replace(document, "automaticPerformanceFpsStep", automaticPerformanceFpsStep_);
        Replace(
            document,
            "automaticPerformanceOscillationPreventionEnabled",
            automaticPerformanceOscillationPreventionEnabled_);
        Replace(
            document,
            "automaticPerformanceOscillationLimit",
            automaticPerformanceOscillationLimit_);
        Replace(document, "performanceDiagnosticsEnabled", performanceDiagnosticsEnabled_);
        Replace(document, "performancePanelPositionX", performancePanelPositionX_);
        Replace(document, "performancePanelPositionY", performancePanelPositionY_);
        Replace(document, "performancePanelPositionZ", performancePanelPositionZ_);
        Replace(document, "performancePanelRotationX", performancePanelRotationX_);
        Replace(document, "performancePanelRotationY", performancePanelRotationY_);
        Replace(document, "performancePanelRotationZ", performancePanelRotationZ_);
        Replace(document, "powerBenchmarkEnabled", powerBenchmarkEnabled_);
        Replace(
            document,
            "detailedDiagnosticLoggingEnabled",
            detailedDiagnosticLoggingEnabled_);
        Replace(document, "nightlyDownloaderUpdates", nightlyDownloaderUpdates_);
        try
        {
            configuration.Write();
        }
        catch(const std::exception& exception)
        {
            BigScreen::BigScreenLogger.error("Could not save Big Screen settings: {}", exception.what());
            ErrorManager::Instance().ReportUserVisible(
                "Settings were not saved",
                "Big Screen could not write its settings file. Your changes will remain active until Beat Saber closes. Check the Big Screen log for details.");
        }
        catch(...)
        {
            BigScreen::BigScreenLogger.error("Could not save Big Screen settings: unknown write error");
            ErrorManager::Instance().ReportUserVisible(
                "Settings were not saved",
                "Big Screen could not write its settings file. Your changes will remain active until Beat Saber closes. Check the Big Screen log for details.");
        }
    }
}
