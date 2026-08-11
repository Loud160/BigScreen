#include "BigScreen/SettingsMenu.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include "bsml/shared/BSML/Components/Settings/DropdownListSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
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

        void SetToggleWithoutNotification(BSML::ToggleSetting* setting, bool value)
        {
            if(!setting)
                return;
            setting->currentValue = value;
            if(setting->toggle)
                setting->toggle->SetIsOnWithoutNotify(value);
        }

        void ApplyDisplaySettingsAndRefreshPreview()
        {
            // The playback session keeps an effective configuration for the
            // selected song. Rebuild it as well as the visible settings screen
            // so leaving this menu cannot resurrect stale size or placement.
            PlaybackSession::Instance().RefreshDisplaySettings();
            ScreenPreview::Instance().Refresh();
        }
    }

    SettingsMenu& SettingsMenu::Instance()
    {
        static SettingsMenu menu;
        return menu;
    }

    void SettingsMenu::Register()
    {
        // A flow coordinator gives Big Screen a dedicated left settings panel
        // while leaving the center view available for real world-scale screen
        // placement instead of a misleading thumbnail.
        BSML::Register::RegisterMainMenuFlowCoordinator(
            "Big Screen",
            "Configure video playback, screen appearance, and environments.",
            csTypeOf(MenuFlowCoordinator*));
    }

    void SettingsMenu::CreateUi(
        HMUI::ViewController* viewController,
        std::function<void()> onBack)
    {
        if(!viewController)
            return;
        if(settingsViewController_ == viewController)
        {
            RefreshControls();
            return;
        }

        // MenuCore restarts replace the controller and all of its children.
        // Clear every cached component before constructing the new scene's UI.
        settingsViewController_ = viewController;
        modEnabledToggle_ = nullptr;
        videoEnabledToggle_ = nullptr;
        previewToggle_ = nullptr;
        distanceSetting_ = nullptr;
        horizontalSetting_ = nullptr;
        verticalSetting_ = nullptr;
        tiltSetting_ = nullptr;
        sizeSetting_ = nullptr;
        curvedScreenToggle_ = nullptr;
        curvatureSlider_ = nullptr;
        transparencyToggle_ = nullptr;
        lightShowToggle_ = nullptr;
        environmentOverrideToggle_ = nullptr;
        environmentMotionToggle_ = nullptr;
        resolutionDropdown_ = nullptr;
        resetButton_ = nullptr;

        // Hide the FlowCoordinator's center title strip and recreate its useful
        // navigation inside this left panel. Anchor a dedicated header to the
        // panel's top edge instead of guessing absolute coordinates: side-screen
        // dimensions differ from the center screen, and the former coordinates
        // placed the Back button beyond the panel's clipping rectangle.
        auto* header = BSML::Lite::CreateHorizontalLayoutGroup(viewController);
        if(header)
        {
            header->set_spacing(1.5f);
            header->set_childControlWidth(true);
            header->set_childControlHeight(true);
            header->set_childForceExpandWidth(false);
            header->set_childForceExpandHeight(false);

            auto headerTransform = header->get_rectTransform();
            headerTransform->set_anchorMin({0.0f, 1.0f});
            headerTransform->set_anchorMax({1.0f, 1.0f});
            headerTransform->set_pivot({0.5f, 1.0f});
            headerTransform->set_anchoredPosition({0.0f, -1.5f});
            headerTransform->set_sizeDelta({-4.0f, 8.0f});
        }

        const BSML::Lite::TransformWrapper headerParent =
            header
                ? BSML::Lite::TransformWrapper(header)
                : BSML::Lite::TransformWrapper(viewController);

        auto* backButton = BSML::Lite::CreateUIButton(
            headerParent,
            "< Back",
            UnityEngine::Vector2{0.0f, 0.0f},
            UnityEngine::Vector2{18.0f, 8.0f},
            [callback = std::move(onBack)]()
            {
                if(callback)
                    callback();
            });
        if(backButton)
        {
            BSML::Lite::SetButtonTextSize(backButton, 3.2f);
            if(auto* layout = backButton->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredWidth(18.0f);
                layout->set_preferredHeight(8.0f);
            }
        }

        auto* title = BSML::Lite::CreateText(
            headerParent,
            "Big Screen",
            5.0f,
            UnityEngine::Vector2{0.0f, 0.0f},
            UnityEngine::Vector2{38.0f, 8.0f});
        if(title)
        {
            title->set_alignment(TMPro::TextAlignmentOptions::Center);
            if(auto* layout = title->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                // The title consumes whatever width remains after the fixed
                // Back control, keeping both controls inside the side panel.
                layout->set_preferredWidth(38.0f);
                layout->set_preferredHeight(8.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }

        auto& settings = Settings::Instance();
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(viewController);
        if(!container)
        {
            PaperLogger.error("Could not create the Big Screen settings container");
            return;
        }

        // Reserve the upper portion of the left panel for the anchored header,
        // keeping the first setting from drawing underneath its controls.
        if(auto* external = container->GetComponent<BSML::ExternalComponents*>())
        {
            if(auto* scroll = external->Get<UnityEngine::RectTransform*>())
            {
                scroll->set_anchoredPosition({2.0f, -2.0f});
                scroll->set_sizeDelta({0.0f, -32.0f});
            }
        }

        modEnabledToggle_ = BSML::Lite::CreateToggle(
            container,
            "Big Screen Enabled",
            settings.ModEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetModEnabled(enabled);

                // Hooks remain installed so the menu stays reachable, but
                // disabling immediately tears down every screen and decoder.
                SelectionVideoToggle::Instance().ModEnabledChanged(enabled);
                ScreenPreview::Instance().SetEnabled(enabled);
                RefreshControls();
                PaperLogger.info("Big Screen switched {}", enabled ? "on" : "off");
            });
        BSML::Lite::AddHoverHint(
            modEnabledToggle_,
            "Disables all Big Screen effects while keeping this settings menu available.");

        videoEnabledToggle_ = BSML::Lite::CreateToggle(
            container,
            "Video In Map",
            settings.VideoEnabled(),
            [this](bool enabled)
            {
                auto& current = Settings::Instance();
                current.SetVideoEnabled(enabled);

                // This is one persistent game-wide state shared with the song
                // selection control; changing songs never resets it.
                SelectionVideoToggle::Instance().ApplyGlobalVideoEnabled(enabled);
                RefreshControls();
            });
        BSML::Lite::AddHoverHint(
            videoEnabledToggle_,
            "Shows videos during map gameplay and is shared with song selection.");

        previewToggle_ = BSML::Lite::CreateToggle(
            container,
            "Preview Video",
            settings.MenuPreviewEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetMenuPreviewEnabled(enabled);
                SelectionVideoToggle::Instance().MenuPreviewPreferenceChanged();
            });
        BSML::Lite::AddHoverHint(
            previewToggle_,
            "Plays videos during song selection and is shared with song selection.");

        distanceSetting_ = BSML::Lite::CreateIncrementSetting(
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
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            distanceSetting_,
            "Adds meters to the map position. Negative is closer; positive is farther away.");

        horizontalSetting_ = BSML::Lite::CreateIncrementSetting(
            container,
            "Screen X Offset",
            0,
            1.0f,
            settings.ScreenHorizontalOffset(),
            -40.0f,
            40.0f,
            [](float value)
            {
                Settings::Instance().SetScreenHorizontalOffset(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            horizontalSetting_,
            "Moves the screen left with negative values and right with positive values.");

        verticalSetting_ = BSML::Lite::CreateIncrementSetting(
            container,
            "Screen Y Offset",
            0,
            1.0f,
            settings.ScreenVerticalOffset(),
            -40.0f,
            40.0f,
            [](float value)
            {
                Settings::Instance().SetScreenVerticalOffset(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            verticalSetting_,
            "Moves the screen down with negative values and up with positive values.");

        tiltSetting_ = BSML::Lite::CreateIncrementSetting(
            container,
            "Screen Tilt Offset",
            0,
            1.0f,
            settings.ScreenTiltOffset(),
            -30.0f,
            30.0f,
            [](float value)
            {
                Settings::Instance().SetScreenTiltOffset(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            tiltSetting_,
            "Adds degrees to the map's vertical tilt. Positive values lift the screen face upward.");

        sizeSetting_ = BSML::Lite::CreateIncrementSetting(
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
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            sizeSetting_,
            "Multiplies the map-authored screen size. 1.0 keeps the original size.");

        curvedScreenToggle_ = BSML::Lite::CreateToggle(
            container,
            "Curved Screen",
            settings.CurvedScreenEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetCurvedScreenEnabled(enabled);
                RefreshCurvatureControl();
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            curvedScreenToggle_,
            "Off uses a flat screen. On reveals the signed screen-curve adjustment below.");

        // Keeping this directly after Curved Screen makes the vertical layout
        // collapse cleanly when the signed curve control is irrelevant.
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
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            curvatureSlider_,
            "Positive values wrap the edges toward you; negative values bend them away.");

        transparencyToggle_ = BSML::Lite::CreateToggle(
            container,
            "Video Transparency",
            settings.TransparencyEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetTransparencyEnabled(enabled);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            transparencyToggle_,
            "Lets environment lights and objects remain partially visible through the video.");

        lightShowToggle_ = BSML::Lite::CreateToggle(
            container,
            "Map Light Show",
            settings.MapLightShowEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetMapLightShowEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            lightShowToggle_,
            "Keeps the selected map's lighting events active while its video plays.");

        environmentOverrideToggle_ = BSML::Lite::CreateToggle(
            container,
            "Use Big Mirror Override",
            settings.EnvironmentOverrideEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetEnvironmentOverrideEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            environmentOverrideToggle_,
            "When disabled, the map's intended background is used and may partially block the video.");

        environmentMotionToggle_ = BSML::Lite::CreateToggle(
            container,
            "Environment Rotation and Motion",
            settings.EnvironmentMotionEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetEnvironmentMotionEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            environmentMotionToggle_,
            "Turns rotating and moving background scenery on or off for video maps.");

        resolutionDropdown_ = BSML::Lite::CreateDropdown(
            container,
            "Video Resolution",
            ResolutionLabel(settings.ResolutionHeight()),
            ResolutionChoices,
            [](StringW value)
            {
                Settings::Instance().SetResolutionHeight(ResolutionValue(value));
            });
        BSML::Lite::AddHoverHint(
            resolutionDropdown_,
            "720p is recommended. 1080p may cause performance issues and decrease battery life.");

        resetButton_ = BSML::Lite::CreateUIButton(
            container,
            "Reset to Defaults",
            UnityEngine::Vector2{0.0f, 0.0f},
            UnityEngine::Vector2{42.0f, 8.0f},
            [this]()
            {
                ResetToDefaults();
            });
        BSML::Lite::AddHoverHint(
            resetButton_,
            "Restores every Big Screen option, including placement, to its original value.");

        RefreshControls();
    }

    void SettingsMenu::RefreshControls()
    {
        // Every reactivation and state-changing callback uses the same complete
        // synchronization path. Previously this refreshed only three toggles,
        // allowing numeric and appearance controls to retain stale UI values.
        RefreshValues();
        RefreshEnabledState();
        RefreshCurvatureControl();
    }

    void SettingsMenu::RefreshValues()
    {
        const auto& settings = Settings::Instance();
        SetToggleWithoutNotification(modEnabledToggle_, settings.ModEnabled());
        SetToggleWithoutNotification(videoEnabledToggle_, settings.VideoEnabled());
        SetToggleWithoutNotification(previewToggle_, settings.MenuPreviewEnabled());
        SetToggleWithoutNotification(curvedScreenToggle_, settings.CurvedScreenEnabled());
        SetToggleWithoutNotification(transparencyToggle_, settings.TransparencyEnabled());
        SetToggleWithoutNotification(lightShowToggle_, settings.MapLightShowEnabled());
        SetToggleWithoutNotification(
            environmentOverrideToggle_,
            settings.EnvironmentOverrideEnabled());
        SetToggleWithoutNotification(
            environmentMotionToggle_,
            settings.EnvironmentMotionEnabled());

        if(distanceSetting_)
            distanceSetting_->set_Value(settings.ScreenDistanceOffset());
        if(horizontalSetting_)
            horizontalSetting_->set_Value(settings.ScreenHorizontalOffset());
        if(verticalSetting_)
            verticalSetting_->set_Value(settings.ScreenVerticalOffset());
        if(tiltSetting_)
            tiltSetting_->set_Value(settings.ScreenTiltOffset());
        if(sizeSetting_)
            sizeSetting_->set_Value(settings.ScreenScale());
        if(curvatureSlider_)
            curvatureSlider_->set_Value(settings.ScreenCurvature());
        if(resolutionDropdown_)
        {
            const int index = settings.ResolutionHeight() == 480
                ? 0
                : settings.ResolutionHeight() == 1080 ? 2 : 1;
            resolutionDropdown_->index = index;
            if(resolutionDropdown_->dropdown)
                resolutionDropdown_->dropdown->SelectCellWithIdx(index);
            resolutionDropdown_->UpdateState();
        }
    }

    void SettingsMenu::RefreshEnabledState()
    {
        const auto& settings = Settings::Instance();
        const bool enabled = settings.ModEnabled();

        // The master switch and Reset remain usable. Every setting capable of
        // affecting Beat Saber is explicitly locked while the mod is off.
        if(videoEnabledToggle_)
            videoEnabledToggle_->set_interactable(enabled);
        if(previewToggle_)
        {
            SetToggleWithoutNotification(previewToggle_, settings.MenuPreviewEnabled());
            previewToggle_->set_interactable(enabled && settings.VideoEnabled());
        }
        if(distanceSetting_)
            distanceSetting_->set_interactable(enabled);
        if(horizontalSetting_)
            horizontalSetting_->set_interactable(enabled);
        if(verticalSetting_)
            verticalSetting_->set_interactable(enabled);
        if(tiltSetting_)
            tiltSetting_->set_interactable(enabled);
        if(sizeSetting_)
            sizeSetting_->set_interactable(enabled);
        if(curvedScreenToggle_)
            curvedScreenToggle_->set_interactable(enabled);
        if(curvatureSlider_)
            curvatureSlider_->set_interactable(enabled);
        if(transparencyToggle_)
            transparencyToggle_->set_interactable(enabled);
        if(lightShowToggle_)
            lightShowToggle_->set_interactable(enabled);
        if(environmentOverrideToggle_)
            environmentOverrideToggle_->set_interactable(enabled);
        if(environmentMotionToggle_)
            environmentMotionToggle_->set_interactable(enabled);
        if(resolutionDropdown_)
            resolutionDropdown_->set_interactable(enabled);
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

    void SettingsMenu::ResetToDefaults()
    {
        auto& settings = Settings::Instance();
        settings.Reset();

        // Rebuild the selected song config before recreating the world screen.
        // This is the missing live-effect step that left the displayed screen
        // at its previous size even though the control correctly showed 1.0.
        PlaybackSession::Instance().RefreshDisplaySettings();

        // Reset changes persistent values first, then mirrors those values into
        // the already-open controls without generating a chain of fake clicks.
        SelectionVideoToggle::Instance().ModEnabledChanged(true);
        SelectionVideoToggle::Instance().ApplyGlobalVideoEnabled(true);
        SelectionVideoToggle::Instance().MenuPreviewPreferenceChanged();
        ScreenPreview::Instance().SetEnabled(true);
        RefreshControls();
        PaperLogger.info("Reset all Big Screen settings to defaults");
    }
}
