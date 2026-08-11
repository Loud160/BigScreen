#include "BigScreen/SettingsMenu.hpp"

#include <array>
#include <string>
#include <string_view>

#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "HMUI/ViewController.hpp"
#include "UnityEngine/GameObject.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
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
        // A flow coordinator rather than the one-panel callback registration
        // gives Big Screen a proper optional right-side preview screen.
        BSML::Register::RegisterMainMenuFlowCoordinator(
            "Big Screen",
            "Configure video playback, screen appearance, and environments.",
            csTypeOf(MenuFlowCoordinator*));
    }

    void SettingsMenu::CreateUi(HMUI::ViewController* viewController)
    {
        if(!viewController)
            return;
        if(settingsViewController_ == viewController)
        {
            RefreshControls();
            return;
        }

        // MenuCore restarts replace the controller and all of its children.
        // Clear native UI references before constructing the new scene's page.
        settingsViewController_ = viewController;
        previewToggle_ = nullptr;
        screenPreviewToggle_ = nullptr;
        curvatureSlider_ = nullptr;

        auto& settings = Settings::Instance();
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(viewController);
        if(!container)
        {
            PaperLogger.error("Could not create the Big Screen settings container");
            return;
        }

        auto* modEnabled = BSML::Lite::CreateToggle(
            container,
            "Big Screen Enabled",
            settings.ModEnabled(),
            [this](bool enabled)
            {
                auto& current = Settings::Instance();
                current.SetModEnabled(enabled);

                // The hooks remain installed so this menu can still be
                // reached, but disabling immediately removes every Big Screen
                // object, decoder, selection state, and optional preview.
                SelectionVideoToggle::Instance().ModEnabledChanged(enabled);
                ScreenPreview::Instance().SetEnabled(
                    enabled && current.MenuScreenPreviewEnabled());
                RefreshControls();
                PaperLogger.info("Big Screen switched {}", enabled ? "on" : "off");
            });
        BSML::Lite::AddHoverHint(
            modEnabled,
            "Disables all Big Screen effects while keeping this settings menu available.");

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

        screenPreviewToggle_ = BSML::Lite::CreateToggle(
            container,
            "Settings Screen Preview",
            settings.MenuScreenPreviewEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetMenuScreenPreviewEnabled(enabled);
                ScreenPreview::Instance().SetEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            screenPreviewToggle_,
            "Shows a full-size screen in the menu at the selected map's real placement, or Big Screen's default placement when no video map is selected.");

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
                ScreenPreview::Instance().Refresh();
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
                ScreenPreview::Instance().Refresh();
            });
        BSML::Lite::AddHoverHint(
            size,
            "Multiplies the map-authored screen size. 1.0 keeps the original size.");

        auto* curvedScreen = BSML::Lite::CreateToggle(
            container,
            "Curved Screen",
            settings.CurvedScreenEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetCurvedScreenEnabled(enabled);
                RefreshCurvatureControl();
                ScreenPreview::Instance().Refresh();
            });
        BSML::Lite::AddHoverHint(
            curvedScreen,
            "Off uses a flat screen. On reveals the signed screen-curve adjustment below.");

        // This control is created immediately after its parent toggle so the
        // vertical settings layout always places it directly underneath. It is
        // only active in curved mode, avoiding an irrelevant adjustment while
        // the screen is explicitly flat.
        curvatureSlider_ = BSML::Lite::CreateSliderSetting(
            container,
            "Screen Curve",
            0.05f,
            settings.ScreenCurvature(),
            -1.0f,
            1.0f,
            [](float value)
            {
                Settings::Instance().SetScreenCurvature(value);
                ScreenPreview::Instance().Refresh();
            });
        BSML::Lite::AddHoverHint(
            curvatureSlider_,
            "Positive values wrap the edges toward you; negative values bend them away.");

        auto* transparency = BSML::Lite::CreateToggle(
            container,
            "Video Transparency",
            settings.TransparencyEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetTransparencyEnabled(enabled);
                ScreenPreview::Instance().Refresh();
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

        RefreshControls();
    }

    void SettingsMenu::RefreshControls()
    {
        RefreshPreviewControl();
        RefreshCurvatureControl();
    }

    void SettingsMenu::RefreshPreviewControl()
    {
        const auto& settings = Settings::Instance();
        if(previewToggle_)
        {
            previewToggle_->currentValue = settings.MenuPreviewEnabled();
            if(previewToggle_->toggle)
                previewToggle_->toggle->SetIsOnWithoutNotify(settings.MenuPreviewEnabled());
            previewToggle_->set_interactable(
                settings.ModEnabled() && settings.VideoEnabledByDefault());
        }
        if(screenPreviewToggle_)
            screenPreviewToggle_->set_interactable(settings.ModEnabled());
    }

    void SettingsMenu::RefreshCurvatureControl()
    {
        if(!curvatureSlider_)
            return;

        // SetActive participates in the vertical layout pass, so hiding the
        // slider also closes its row instead of leaving an empty menu gap.
        curvatureSlider_->get_gameObject()->SetActive(
            Settings::Instance().CurvedScreenEnabled());
    }
}
