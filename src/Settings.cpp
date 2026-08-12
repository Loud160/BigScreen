#include "BigScreen/Settings.hpp"

#include <algorithm>

#include "beatsaber-hook/shared/config/config-utils.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr float MaximumScreenCurvature = 7.0f;

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

        int NormalizeResolution(int value)
        {
            // The menu exposes exactly these three predictable performance
            // tiers. A hand-edited invalid value falls back to the balanced
            // Quest default instead of silently selecting an arbitrary size.
            return value == 480 || value == 720 || value == 1080 ? value : 720;
        }

        int NormalizePlaybackFps(int value)
        {
            // These are presentation ceilings, not forced rates. A 24 FPS
            // source remains 24 FPS even when the selected ceiling is 30 or 60.
            return value == 15 || value == 30 || value == 60 ? value : 30;
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
        configuration.Load();
        auto& document = configuration.config;
        if(!document.IsObject())
            document.SetObject();

        modEnabled_ = ReadBool(document, "modEnabled", true);
        distractionFreeMenu_ = ReadBool(
            document,
            "distractionFreeMenu",
            true);
        // v0.6 promotes the former per-selection default to one persistent,
        // game-wide Video switch. Read the old key as a migration fallback so
        // an upgrade preserves the user's existing choice.
        videoEnabled_ = ReadBool(
            document,
            "videoEnabled",
            ReadBool(document, "videoEnabledByDefault", true));
        menuPreviewEnabled_ = ReadBool(document, "showMenuPreview", true);
        // Profile 1 migrates the original single-layout keys. Profiles 2 and
        // 3 begin at documented defaults until the user changes them.
        for(int index = 0; index < static_cast<int>(screenLayouts_.size()); ++index)
        {
            const auto prefix = "screenLayout" + std::to_string(index + 1);
            auto& layout = screenLayouts_[index];
            const bool legacy = index == 0;
            layout.distanceOffset = std::clamp(ReadFloat(
                document, (prefix + "Distance").c_str(),
                legacy ? ReadFloat(document, "screenDistanceOffset", 0.0f) : 0.0f), -40.0f, 40.0f);
            layout.horizontalOffset = std::clamp(ReadFloat(
                document, (prefix + "Horizontal").c_str(),
                legacy ? ReadFloat(document, "screenHorizontalOffset", 0.0f) : 0.0f), -40.0f, 40.0f);
            layout.verticalOffset = std::clamp(ReadFloat(
                document, (prefix + "Vertical").c_str(),
                legacy ? ReadFloat(document, "screenVerticalOffset", 0.0f) : 0.0f), -40.0f, 40.0f);
            layout.tiltOffset = std::clamp(ReadFloat(
                document, (prefix + "Tilt").c_str(),
                legacy ? ReadFloat(document, "screenTiltOffset", 0.0f) : 0.0f), -30.0f, 30.0f);
            layout.scale = std::clamp(ReadFloat(
                document, (prefix + "Scale").c_str(),
                legacy ? ReadFloat(document, "screenScale", 1.0f) : 1.0f), 0.5f, 2.5f);
            layout.curved = ReadBool(
                document, (prefix + "Curved").c_str(),
                legacy ? ReadBool(document, "curvedScreenEnabled", false) : false);
            layout.curvature = std::clamp(ReadFloat(
                document, (prefix + "Curvature").c_str(),
                legacy ? ReadFloat(document, "screenCurvature", 0.35f) : 0.35f),
                -MaximumScreenCurvature, MaximumScreenCurvature);
            layout.maintainAspectRatio = ReadBool(
                document, (prefix + "MaintainAspect").c_str(),
                legacy ? ReadBool(document, "maintainCurveAspectRatio", false) : false);
        }
        activeScreenLayout_ = std::clamp(
            ReadInt(document, "activeScreenLayout", 0), 0, 2);
        transparencyEnabled_ = ReadBool(document, "transparencyEnabled", false);
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
            !ReadBool(document, "environmentMotionEnabled", true));
        hideTrackRings_ = ReadBool(document, "hideTrackRings", true);
        hideSideBars_ = ReadBool(
            document,
            "hideSideBars",
            ReadBool(document, "hideLaserRigs", true));
        hideSpectrogramBars_ = ReadBool(document, "hideSpectrogramBars", true);
        playbackFpsLimit_ = NormalizePlaybackFps(
            ReadInt(document, "playbackFpsLimit", 30));
        resolutionHeight_ = NormalizeResolution(
            ReadInt(document, "resolutionHeight", 720));
        automaticPerformanceEnabled_ = ReadBool(
            document, "automaticPerformanceEnabled", false);
        const auto threshold = ReadInt(document, "automaticPerformanceThreshold", 10);
        automaticPerformanceThreshold_ =
            threshold == 5 || threshold == 10 || threshold == 20 ? threshold : 10;
        performanceDiagnosticsEnabled_ = ReadBool(
            document, "performanceDiagnosticsEnabled", false);
        nightlyDownloaderUpdates_ = ReadBool(document, "nightlyDownloaderUpdates", false);

        // Preview decoding is an avoidable performance cost when videos are
        // globally switched off. Persist the dependency so the disabled state
        // is also honored on the next launch, before any menu exists.
        if(!videoEnabled_)
            menuPreviewEnabled_ = false;

        Save();
    }

    void Settings::Reset()
    {
        // Reconstruct from the member initializers in Settings.hpp instead of
        // maintaining a second handwritten defaults list here. This resets
        // every current field, including hidden experimental settings, and
        // makes a newly added setting participate automatically as long as it
        // declares its intended default beside the member itself.
        *this = Settings{};
        Save();
    }

    void Settings::SetModEnabled(bool value)
    {
        modEnabled_ = value;
        Save();
    }

    void Settings::SetDistractionFreeMenu(bool value)
    {
        distractionFreeMenu_ = value;
        Save();
    }

    void Settings::SetVideoEnabled(bool value)
    {
        videoEnabled_ = value;
        if(!value)
            menuPreviewEnabled_ = false;
        Save();
    }

    void Settings::SetMenuPreviewEnabled(bool value)
    {
        // Do not permit a stale UI callback or hand-authored config to enable
        // a decoder while the global video switch is disabled.
        menuPreviewEnabled_ = videoEnabled_ && value;
        Save();
    }

    void Settings::SetActiveScreenLayout(int value)
    {
        activeScreenLayout_ = std::clamp(value, 0, 2);
        Save();
    }

    void Settings::SetScreenDistanceOffset(float value)
    {
        screenLayouts_[activeScreenLayout_].distanceOffset = std::clamp(value, -40.0f, 40.0f);
        Save();
    }

    void Settings::SetScreenHorizontalOffset(float value)
    {
        screenLayouts_[activeScreenLayout_].horizontalOffset = std::clamp(value, -40.0f, 40.0f);
        Save();
    }

    void Settings::SetScreenVerticalOffset(float value)
    {
        screenLayouts_[activeScreenLayout_].verticalOffset = std::clamp(value, -40.0f, 40.0f);
        Save();
    }

    void Settings::SetScreenTiltOffset(float value)
    {
        screenLayouts_[activeScreenLayout_].tiltOffset = std::clamp(value, -30.0f, 30.0f);
        Save();
    }

    void Settings::SetScreenScale(float value)
    {
        screenLayouts_[activeScreenLayout_].scale = std::clamp(value, 0.5f, 2.5f);
        Save();
    }

    void Settings::SetCurvedScreenEnabled(bool value)
    {
        screenLayouts_[activeScreenLayout_].curved = value;
        Save();
    }

    void Settings::SetScreenCurvature(float value)
    {
        // Preserve the original response through +/-1 while permitting a
        // strong wrap without the impractical geometry produced by the old
        // experimental +/-25 limit.
        screenLayouts_[activeScreenLayout_].curvature = std::clamp(
            value,
            -MaximumScreenCurvature,
            MaximumScreenCurvature);
        Save();
    }

    void Settings::SetMaintainCurveAspectRatio(bool value)
    {
        screenLayouts_[activeScreenLayout_].maintainAspectRatio = value;
        Save();
    }

    void Settings::SetTransparencyEnabled(bool value)
    {
        transparencyEnabled_ = value;
        Save();
    }

    void Settings::SetMapLightShowEnabled(bool value)
    {
        mapLightShowEnabled_ = value;
        Save();
    }

    void Settings::SetHideBackWallLights(bool value)
    {
        hideBackWallLights_ = value;
        Save();
    }

    void Settings::SetHideRingLights(bool value)
    {
        hideRingLights_ = value;
        Save();
    }

    void Settings::SetHideSideLaserLights(bool value)
    {
        hideSideLaserLights_ = value;
        Save();
    }

    void Settings::SetEnvironmentOverrideEnabled(bool value)
    {
        environmentOverrideEnabled_ = value;
        Save();
    }

    void Settings::SetGlassDesertOverrideEnabled(bool value)
    {
        glassDesertOverrideEnabled_ = value;
        Save();
    }

    void Settings::SetDisableEnvironmentMotion(bool value)
    {
        disableEnvironmentMotion_ = value;
        Save();
    }

    void Settings::SetHideTrackRings(bool value)
    {
        hideTrackRings_ = value;
        Save();
    }

    void Settings::SetHideSideBars(bool value)
    {
        hideSideBars_ = value;
        Save();
    }

    void Settings::SetHideSpectrogramBars(bool value)
    {
        hideSpectrogramBars_ = value;
        Save();
    }

    void Settings::SetPlaybackFpsLimit(int value)
    {
        playbackFpsLimit_ = NormalizePlaybackFps(value);
        Save();
    }

    void Settings::SetResolutionHeight(int value)
    {
        resolutionHeight_ = NormalizeResolution(value);
        Save();
    }

    void Settings::SetAutomaticPerformanceEnabled(bool value)
    {
        automaticPerformanceEnabled_ = value;
        Save();
    }

    void Settings::SetAutomaticPerformanceThreshold(int value)
    {
        automaticPerformanceThreshold_ =
            value == 5 || value == 10 || value == 20 ? value : 10;
        Save();
    }

    void Settings::SetPerformanceDiagnosticsEnabled(bool value)
    {
        performanceDiagnosticsEnabled_ = value;
        Save();
    }

    void Settings::SetNightlyDownloaderUpdates(bool value)
    {
        nightlyDownloaderUpdates_ = value;
        Save();
    }

    void Settings::Save()
    {
        auto& configuration = GetConfiguration();
        auto& document = configuration.config;
        if(!document.IsObject())
            document.SetObject();

        Replace(document, "modEnabled", modEnabled_);
        Replace(document, "distractionFreeMenu", distractionFreeMenu_);
        // Remove superseded keys after migration so the configuration has one
        // unambiguous source of truth on all later launches.
        document.RemoveMember("videoEnabledByDefault");
        document.RemoveMember("menuScreenPreviewEnabled");
        Replace(document, "videoEnabled", videoEnabled_);
        Replace(document, "showMenuPreview", menuPreviewEnabled_);
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
            Replace(document, (prefix + "Distance").c_str(), layout.distanceOffset);
            Replace(document, (prefix + "Horizontal").c_str(), layout.horizontalOffset);
            Replace(document, (prefix + "Vertical").c_str(), layout.verticalOffset);
            Replace(document, (prefix + "Tilt").c_str(), layout.tiltOffset);
            Replace(document, (prefix + "Scale").c_str(), layout.scale);
            Replace(document, (prefix + "Curved").c_str(), layout.curved);
            Replace(document, (prefix + "Curvature").c_str(), layout.curvature);
            Replace(document, (prefix + "MaintainAspect").c_str(), layout.maintainAspectRatio);
        }
        Replace(document, "transparencyEnabled", transparencyEnabled_);
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
        Replace(document, "resolutionHeight", resolutionHeight_);
        Replace(document, "automaticPerformanceEnabled", automaticPerformanceEnabled_);
        Replace(document, "automaticPerformanceThreshold", automaticPerformanceThreshold_);
        Replace(document, "performanceDiagnosticsEnabled", performanceDiagnosticsEnabled_);
        Replace(document, "nightlyDownloaderUpdates", nightlyDownloaderUpdates_);
        configuration.Write();
    }
}
