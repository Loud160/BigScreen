#include "BigScreen/SettingsMenu.hpp"

#include <array>
#include <string>
#include <string_view>

#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "HMUI/ViewController.hpp"
#include "UnityEngine/GameObject.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        std::array<std::string_view, 3> ResolutionChoices{
            "480p",
            "720p",
            "1080p"
        };

        std::string ResolutionLabel(int height)
        {
            return std::to_string(height) + "p";
        }

        int ResolutionValue(StringW label)
        {
            const std::string text(label);
            if(text == "480p")
                return 480;
            if(text == "1080p")
                return 1080;
            return 720;
        }
    }

    SettingsMenu& SettingsMenu::Instance()
    {
        static SettingsMenu menu;
        return menu;
    }

    void SettingsMenu::Register()
    {
        // This registration produces the familiar left-side main-menu button
        // used by major Quest mods, then lets BSML own navigation/back behavior.
        BSML::Register::RegisterMainMenu(
            "Big Screen Settings",
            "Big Screen",
            "Configure video playback, screen appearance, and environments.",
            [this](HMUI::ViewController* controller, bool first, bool added, bool enabling)
            {
                DidActivate(controller, first, added, enabling);
            });
    }

    void SettingsMenu::DidActivate(
        HMUI::ViewController* viewController,
        bool firstActivation,
        bool addedToHierarchy,
        bool screenSystemEnabling)
    {
        (void)addedToHierarchy;
        (void)screenSystemEnabling;
        if(!viewController || !firstActivation)
        {
            RefreshPreviewControl();
            return;
        }

        auto& settings = Settings::Instance();
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(viewController);
        if(!container)
        {
            PaperLogger.error("Could not create the Big Screen settings container");
            return;
        }

        auto* defaultVideo = BSML::Lite::CreateToggle(
            container,
            "Videos On by Default",
            settings.VideoEnabledByDefault(),
            [this](bool enabled)
            {
                auto& current = Settings::Instance();
                current.SetVideoEnabledByDefault(enabled);

                // Apply the new default to the song already selected as well as
                // future selections. Turning it off also tears down a running
                // preview immediately and forces the preview preference off.
                SelectionVideoToggle::Instance().ApplyDefaultVideoEnabled(enabled);
                RefreshPreviewControl();
            });
        BSML::Lite::AddHoverHint(
            defaultVideo,
            "Sets the initial Video switch state whenever a video map is selected.");

        previewToggle_ = BSML::Lite::CreateToggle(
            container,
            "Song Video Previews",
            settings.MenuPreviewEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetMenuPreviewEnabled(enabled);
                SelectionVideoToggle::Instance().MenuPreviewPreferenceChanged();
            });
        BSML::Lite::AddHoverHint(
            previewToggle_,
            "Controls song-selection previews separately from video during gameplay.");

        auto* distance = BSML::Lite::CreateIncrementSetting(
            container,
            "Screen Distance Offset",
            0,
            2.0f,
            settings.ScreenDistanceOffset(),
            -40.0f,
            40.0f,
            [](float value)
            {
                Settings::Instance().SetScreenDistanceOffset(value);
            });
        BSML::Lite::AddHoverHint(
            distance,
            "Adds meters to the map position. Negative is closer; positive is farther away.");

        auto* size = BSML::Lite::CreateIncrementSetting(
            container,
            "Screen Size Multiplier",
            1,
            0.1f,
            settings.ScreenScale(),
            0.5f,
            2.0f,
            [](float value)
            {
                Settings::Instance().SetScreenScale(value);
            });
        BSML::Lite::AddHoverHint(
            size,
            "Multiplies the map-authored screen size. 1.0 keeps the original size.");

        auto* transparency = BSML::Lite::CreateToggle(
            container,
            "Video Transparency",
            settings.TransparencyEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetTransparencyEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            transparency,
            "Lets environment lights and objects remain partially visible through the video.");

        auto* lightShow = BSML::Lite::CreateToggle(
            container,
            "Map Light Show",
            settings.MapLightShowEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetMapLightShowEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            lightShow,
            "Keeps the selected map's lighting events active while its video plays.");

        auto* environmentOverride = BSML::Lite::CreateToggle(
            container,
            "Use Big Mirror Override",
            settings.EnvironmentOverrideEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetEnvironmentOverrideEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            environmentOverride,
            "When disabled, the map's intended background is used and may partially block the video.");

        auto* environmentMotion = BSML::Lite::CreateToggle(
            container,
            "Environment Rotation and Motion",
            settings.EnvironmentMotionEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetEnvironmentMotionEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            environmentMotion,
            "Turns rotating and moving background scenery on or off for video maps.");

        auto* resolution = BSML::Lite::CreateDropdown(
            container,
            "Video Resolution",
            ResolutionLabel(settings.ResolutionHeight()),
            ResolutionChoices,
            [](StringW value)
            {
                Settings::Instance().SetResolutionHeight(ResolutionValue(value));
            });
        BSML::Lite::AddHoverHint(
            resolution,
            "720p is recommended. 1080p may cause performance issues and decrease battery life.");

        RefreshPreviewControl();
    }

    void SettingsMenu::RefreshPreviewControl()
    {
        if(!previewToggle_)
            return;

        const auto& settings = Settings::Instance();
        previewToggle_->currentValue = settings.MenuPreviewEnabled();
        if(previewToggle_->toggle)
            previewToggle_->toggle->SetIsOnWithoutNotify(settings.MenuPreviewEnabled());
        previewToggle_->set_interactable(settings.VideoEnabledByDefault());
    }
}
