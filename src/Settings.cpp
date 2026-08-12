#include "BigScreen/Settings.hpp"

#include <algorithm>

#include "beatsaber-hook/shared/config/config-utils.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr float MaximumScreenCurvature = 25.0f;

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
        // v0.6 promotes the former per-selection default to one persistent,
        // game-wide Video switch. Read the old key as a migration fallback so
        // an upgrade preserves the user's existing choice.
        videoEnabled_ = ReadBool(
            document,
            "videoEnabled",
            ReadBool(document, "videoEnabledByDefault", true));
        menuPreviewEnabled_ = ReadBool(document, "showMenuPreview", true);
        screenDistanceOffset_ = std::clamp(
            ReadFloat(document, "screenDistanceOffset", 0.0f),
            -40.0f,
            40.0f);
        screenHorizontalOffset_ = std::clamp(
            ReadFloat(document, "screenHorizontalOffset", 0.0f),
            -40.0f,
            40.0f);
        screenVerticalOffset_ = std::clamp(
            ReadFloat(document, "screenVerticalOffset", 0.0f),
            -40.0f,
            40.0f);
        screenTiltOffset_ = std::clamp(
            ReadFloat(document, "screenTiltOffset", 0.0f),
            -30.0f,
            30.0f);
        screenScale_ = std::clamp(
            ReadFloat(document, "screenScale", 1.0f),
            0.5f,
            2.0f);
        curvedScreenEnabled_ = ReadBool(document, "curvedScreenEnabled", false);
        screenCurvature_ = std::clamp(
            ReadFloat(document, "screenCurvature", 0.35f),
            -MaximumScreenCurvature,
            MaximumScreenCurvature);
        transparencyEnabled_ = ReadBool(document, "transparencyEnabled", false);
        mapLightShowEnabled_ = ReadBool(document, "mapLightShowEnabled", true);
        environmentOverrideEnabled_ = ReadBool(document, "environmentOverrideEnabled", true);
        environmentMotionEnabled_ = ReadBool(document, "environmentMotionEnabled", true);
        hideTrackRings_ = ReadBool(document, "hideTrackRings", true);
        playbackFpsLimit_ = NormalizePlaybackFps(
            ReadInt(document, "playbackFpsLimit", 30));
        resolutionHeight_ = NormalizeResolution(
            ReadInt(document, "resolutionHeight", 720));
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
        modEnabled_ = true;
        videoEnabled_ = true;
        menuPreviewEnabled_ = true;
        screenDistanceOffset_ = 0.0f;
        screenHorizontalOffset_ = 0.0f;
        screenVerticalOffset_ = 0.0f;
        screenTiltOffset_ = 0.0f;
        screenScale_ = 1.0f;
        curvedScreenEnabled_ = false;
        screenCurvature_ = 0.35f;
        transparencyEnabled_ = false;
        mapLightShowEnabled_ = true;
        environmentOverrideEnabled_ = true;
        environmentMotionEnabled_ = true;
        hideTrackRings_ = true;
        playbackFpsLimit_ = 30;
        resolutionHeight_ = 720;
        nightlyDownloaderUpdates_ = false;
        Save();
    }

    void Settings::SetModEnabled(bool value)
    {
        modEnabled_ = value;
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

    void Settings::SetScreenDistanceOffset(float value)
    {
        screenDistanceOffset_ = std::clamp(value, -40.0f, 40.0f);
        Save();
    }

    void Settings::SetScreenHorizontalOffset(float value)
    {
        screenHorizontalOffset_ = std::clamp(value, -40.0f, 40.0f);
        Save();
    }

    void Settings::SetScreenVerticalOffset(float value)
    {
        screenVerticalOffset_ = std::clamp(value, -40.0f, 40.0f);
        Save();
    }

    void Settings::SetScreenTiltOffset(float value)
    {
        screenTiltOffset_ = std::clamp(value, -30.0f, 30.0f);
        Save();
    }

    void Settings::SetScreenScale(float value)
    {
        screenScale_ = std::clamp(value, 0.5f, 2.0f);
        Save();
    }

    void Settings::SetCurvedScreenEnabled(bool value)
    {
        curvedScreenEnabled_ = value;
        Save();
    }

    void Settings::SetScreenCurvature(float value)
    {
        // Preserve the original response through +/-1 while permitting a
        // dramatically stronger wrap when the player deliberately moves into
        // the expanded portion of the slider.
        screenCurvature_ = std::clamp(
            value,
            -MaximumScreenCurvature,
            MaximumScreenCurvature);
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

    void Settings::SetEnvironmentOverrideEnabled(bool value)
    {
        environmentOverrideEnabled_ = value;
        Save();
    }

    void Settings::SetEnvironmentMotionEnabled(bool value)
    {
        environmentMotionEnabled_ = value;
        Save();
    }

    void Settings::SetHideTrackRings(bool value)
    {
        hideTrackRings_ = value;
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
        // Remove superseded keys after migration so the configuration has one
        // unambiguous source of truth on all later launches.
        document.RemoveMember("videoEnabledByDefault");
        document.RemoveMember("menuScreenPreviewEnabled");
        Replace(document, "videoEnabled", videoEnabled_);
        Replace(document, "showMenuPreview", menuPreviewEnabled_);
        Replace(document, "screenDistanceOffset", screenDistanceOffset_);
        Replace(document, "screenHorizontalOffset", screenHorizontalOffset_);
        Replace(document, "screenVerticalOffset", screenVerticalOffset_);
        Replace(document, "screenTiltOffset", screenTiltOffset_);
        Replace(document, "screenScale", screenScale_);
        Replace(document, "curvedScreenEnabled", curvedScreenEnabled_);
        Replace(document, "screenCurvature", screenCurvature_);
        Replace(document, "transparencyEnabled", transparencyEnabled_);
        Replace(document, "mapLightShowEnabled", mapLightShowEnabled_);
        Replace(document, "environmentOverrideEnabled", environmentOverrideEnabled_);
        Replace(document, "environmentMotionEnabled", environmentMotionEnabled_);
        Replace(document, "hideTrackRings", hideTrackRings_);
        Replace(document, "playbackFpsLimit", playbackFpsLimit_);
        Replace(document, "resolutionHeight", resolutionHeight_);
        Replace(document, "nightlyDownloaderUpdates", nightlyDownloaderUpdates_);
        configuration.Write();
    }
}
