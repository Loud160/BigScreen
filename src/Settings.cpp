#include "BigScreen/Settings.hpp"

#include <algorithm>

#include "beatsaber-hook/shared/config/config-utils.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
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

        videoEnabledByDefault_ = ReadBool(document, "videoEnabledByDefault", true);
        menuPreviewEnabled_ = ReadBool(document, "showMenuPreview", true);
        screenDistanceOffset_ = std::clamp(
            ReadFloat(document, "screenDistanceOffset", 0.0f),
            -40.0f,
            40.0f);
        screenScale_ = std::clamp(
            ReadFloat(document, "screenScale", 1.0f),
            0.5f,
            2.0f);
        transparencyEnabled_ = ReadBool(document, "transparencyEnabled", false);
        mapLightShowEnabled_ = ReadBool(document, "mapLightShowEnabled", true);
        environmentOverrideEnabled_ = ReadBool(document, "environmentOverrideEnabled", true);
        environmentMotionEnabled_ = ReadBool(document, "environmentMotionEnabled", true);
        resolutionHeight_ = NormalizeResolution(
            ReadInt(document, "resolutionHeight", 720));

        // Preview decoding is an avoidable performance cost when videos are
        // globally defaulted off. Persist the dependency so the disabled state
        // is also honored on the next launch, before any menu exists.
        if(!videoEnabledByDefault_)
            menuPreviewEnabled_ = false;

        Save();
    }

    void Settings::Reset()
    {
        videoEnabledByDefault_ = true;
        menuPreviewEnabled_ = true;
        screenDistanceOffset_ = 0.0f;
        screenScale_ = 1.0f;
        transparencyEnabled_ = false;
        mapLightShowEnabled_ = true;
        environmentOverrideEnabled_ = true;
        environmentMotionEnabled_ = true;
        resolutionHeight_ = 720;
        Save();
    }

    void Settings::SetVideoEnabledByDefault(bool value)
    {
        videoEnabledByDefault_ = value;
        if(!value)
            menuPreviewEnabled_ = false;
        Save();
    }

    void Settings::SetMenuPreviewEnabled(bool value)
    {
        // Do not permit a stale UI callback or hand-authored config to enable
        // a decoder while the master video default is disabled.
        menuPreviewEnabled_ = videoEnabledByDefault_ && value;
        Save();
    }

    void Settings::SetScreenDistanceOffset(float value)
    {
        screenDistanceOffset_ = std::clamp(value, -40.0f, 40.0f);
        Save();
    }

    void Settings::SetScreenScale(float value)
    {
        screenScale_ = std::clamp(value, 0.5f, 2.0f);
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

    void Settings::SetResolutionHeight(int value)
    {
        resolutionHeight_ = NormalizeResolution(value);
        Save();
    }

    void Settings::Save()
    {
        auto& configuration = GetConfiguration();
        auto& document = configuration.config;
        if(!document.IsObject())
            document.SetObject();

        Replace(document, "videoEnabledByDefault", videoEnabledByDefault_);
        Replace(document, "showMenuPreview", menuPreviewEnabled_);
        Replace(document, "screenDistanceOffset", screenDistanceOffset_);
        Replace(document, "screenScale", screenScale_);
        Replace(document, "transparencyEnabled", transparencyEnabled_);
        Replace(document, "mapLightShowEnabled", mapLightShowEnabled_);
        Replace(document, "environmentOverrideEnabled", environmentOverrideEnabled_);
        Replace(document, "environmentMotionEnabled", environmentMotionEnabled_);
        Replace(document, "resolutionHeight", resolutionHeight_);
        configuration.Write();
    }
}
