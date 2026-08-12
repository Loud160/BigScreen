#include "BigScreen/SettingsMenu.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "HMUI/HoverHint.hpp"
#include "HMUI/TextSegmentedControl.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
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
#include "bsml/shared/BSML/Components/ModalView.hpp"
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

        std::array<std::string_view, 3> PlaybackFpsChoices{
            "15 FPS",
            "30 FPS",
            "60 FPS"
        };

        std::array<std::string_view, 4> SettingsTabNames{
            "General", "Screen", "Playback", "Update"
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

        std::string PlaybackFpsLabel(int fps)
        {
            return std::to_string(fps) + " FPS";
        }

        int PlaybackFpsValue(StringW label)
        {
            const std::string text(label);
            if(text == "15 FPS")
                return 15;
            if(text == "60 FPS")
                return 60;
            return 30;
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
            if(PlaybackSession::Instance().IsLibraryPreviewActive())
            {
                VideoLibraryMenu::Instance().RefreshDisplaySettings();
                return;
            }
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
        settingsTabs_ = nullptr;
        tabViewRoots_.fill(nullptr);
        selectedTab_ = 0;
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
        playbackFpsDropdown_ = nullptr;
        resolutionDropdown_ = nullptr;
        nightlyUpdatesToggle_ = nullptr;
        nightlyWarningModal_ = nullptr;
        updaterButton_ = nullptr;
        updaterHoverHint_ = nullptr;
        updaterStatus_ = nullptr;
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

        // A native segmented control keeps the four categories visible while
        // their independent scroll views occupy the same content region.
        // Every page is scrollable from the start so future settings can be
        // added without another structural menu migration.
        settingsTabs_ = BSML::Lite::CreateTextSegmentedControl(
            viewController,
            {0.0f, 0.0f},
            {54.0f, 7.0f},
            SettingsTabNames,
            [this](int index) { ShowSettingsTab(index); });
        if(settingsTabs_)
        {
            auto tabsRect = settingsTabs_->get_transform()
                .cast<UnityEngine::RectTransform>();
            tabsRect->set_anchorMin({0.0f, 1.0f});
            tabsRect->set_anchorMax({1.0f, 1.0f});
            tabsRect->set_pivot({0.5f, 1.0f});
            tabsRect->set_anchoredPosition({0.0f, -10.0f});
            tabsRect->set_sizeDelta({-4.0f, 7.0f});
        }

        const auto createTabPage = [&](int index) -> UnityEngine::GameObject*
        {
            auto* content = BSML::Lite::CreateScrollableSettingsContainer(
                viewController);
            if(!content)
                return nullptr;
            if(auto* external = content->GetComponent<BSML::ExternalComponents*>())
            {
                if(auto* scroll = external->Get<UnityEngine::RectTransform*>())
                {
                    // With full-height anchors, these values produce a
                    // 19-unit top inset and a 3-unit bottom inset. The tab bar
                    // ends about 17 units below the panel top, leaving only a
                    // narrow visual gap while giving every page essentially
                    // the complete remaining vertical workspace.
                    scroll->set_anchoredPosition({2.0f, -8.0f});
                    scroll->set_sizeDelta({0.0f, -22.0f});
                    tabViewRoots_[index] = scroll->get_gameObject();
                }
            }
            if(!tabViewRoots_[index])
                tabViewRoots_[index] = content;
            return content;
        };

        auto* generalContainer = createTabPage(0);
        auto* screenContainer = createTabPage(1);
        auto* playbackContainer = createTabPage(2);
        auto* updateContainer = createTabPage(3);
        if(!generalContainer || !screenContainer ||
           !playbackContainer || !updateContainer)
        {
            PaperLogger.error("Could not create all Big Screen settings tabs");
            return;
        }

        modEnabledToggle_ = BSML::Lite::CreateToggle(
            generalContainer,
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
            generalContainer,
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
            generalContainer,
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
            screenContainer,
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
            screenContainer,
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
            screenContainer,
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
            screenContainer,
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
            screenContainer,
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
            screenContainer,
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
            screenContainer,
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
            screenContainer,
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
            playbackContainer,
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
            playbackContainer,
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
            playbackContainer,
            "Environment Rotation and Motion",
            settings.EnvironmentMotionEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetEnvironmentMotionEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            environmentMotionToggle_,
            "Turns rotating and moving background scenery on or off for video maps.");

        playbackFpsDropdown_ = BSML::Lite::CreateDropdown(
            playbackContainer,
            "Video Frame Rate Limit",
            PlaybackFpsLabel(settings.PlaybackFpsLimit()),
            PlaybackFpsChoices,
            [](StringW value)
            {
                Settings::Instance().SetPlaybackFpsLimit(PlaybackFpsValue(value));
            });
        BSML::Lite::AddHoverHint(
            playbackFpsDropdown_,
            "Maximum displayed video frame rate. Lower values reduce software conversion and texture-upload load; videos below the limit retain their native frame rate.");

        resolutionDropdown_ = BSML::Lite::CreateDropdown(
            playbackContainer,
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

        nightlyWarningModal_ = BSML::Lite::CreateModal(
            viewController,
            {64.0f, 31.0f},
            nullptr,
            false);
        auto* nightlyWarningText = BSML::Lite::CreateText(
            nightlyWarningModal_,
            "Use nightly yt-dlp builds?\n\nNightly builds contain the newest changes, but they are more likely to include bugs than stable releases.",
            TMPro::FontStyles::Normal,
            {0.0f, 6.0f});
        nightlyWarningText->set_fontSize(2.9f);
        nightlyWarningText->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            nightlyWarningModal_->get_transform(),
            "Stay on Stable",
            {16.0f, -22.5f},
            {24.0f, 8.0f},
            [this]()
            {
                if(nightlyWarningModal_)
                    nightlyWarningModal_->Hide();
                suppressNightlyCallback_ = true;
                SetToggleWithoutNotification(nightlyUpdatesToggle_, false);
                suppressNightlyCallback_ = false;
            });
        BSML::Lite::CreateUIButton(
            nightlyWarningModal_->get_transform(),
            "Use Nightly",
            {43.0f, -22.5f},
            {24.0f, 8.0f},
            [this]()
            {
                Settings::Instance().SetNightlyDownloaderUpdates(true);
                suppressNightlyCallback_ = true;
                SetToggleWithoutNotification(nightlyUpdatesToggle_, true);
                suppressNightlyCallback_ = false;
                if(nightlyWarningModal_)
                    nightlyWarningModal_->Hide();
                RefreshUpdaterHint();
            });

        nightlyUpdatesToggle_ = BSML::Lite::CreateToggle(
            updateContainer,
            "Use Nightly yt-dlp Updates",
            settings.NightlyDownloaderUpdates(),
            [this](bool enabled)
            {
                if(suppressNightlyCallback_)
                    return;
                if(!enabled)
                {
                    Settings::Instance().SetNightlyDownloaderUpdates(false);
                    RefreshUpdaterHint();
                    return;
                }

                // Do not persist the riskier channel until the player accepts
                // the modal warning. Revert the visual switch while the
                // decision is pending so Cancel has no hidden state change.
                suppressNightlyCallback_ = true;
                SetToggleWithoutNotification(nightlyUpdatesToggle_, false);
                suppressNightlyCallback_ = false;
                if(nightlyWarningModal_)
                    nightlyWarningModal_->Show();
            });
        BSML::Lite::AddHoverHint(
            nightlyUpdatesToggle_,
            "Warning: nightly builds may contain new bugs. Stable releases are recommended unless video downloads have stopped working.");
        updaterButton_ = BSML::Lite::CreateUIButton(
            updateContainer,
            "Check yt-dlp Update",
            UnityEngine::Vector2{0, 0},
            UnityEngine::Vector2{42, 8},
            [this]() {
                auto& downloader = DownloadManager::Instance();
                const bool install = downloader.Snapshot().state == DownloadState::UpdateAvailable;
                std::string error;
                if(!downloader.StartUpdaterCheck(
                       Settings::Instance().NightlyDownloaderUpdates(), install, error) && updaterStatus_)
                    updaterStatus_->set_text(error);
                RefreshDownloaderStatus();
            });
        updaterHoverHint_ = BSML::Lite::AddHoverHint(updaterButton_, "");
        updaterStatus_ = BSML::Lite::CreateText(updateContainer, "", 2.5f);
        if(updaterStatus_) updaterStatus_->set_alignment(TMPro::TextAlignmentOptions::Center);

        resetButton_ = BSML::Lite::CreateUIButton(
            generalContainer,
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

        ShowSettingsTab(0);
        if(settingsTabs_)
            settingsTabs_->SelectCellWithNumber(0);
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
        RefreshUpdaterHint();
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
        SetToggleWithoutNotification(
            nightlyUpdatesToggle_,
            settings.NightlyDownloaderUpdates());

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
        if(playbackFpsDropdown_)
        {
            const int index = settings.PlaybackFpsLimit() == 15
                ? 0
                : settings.PlaybackFpsLimit() == 60 ? 2 : 1;
            playbackFpsDropdown_->index = index;
            if(playbackFpsDropdown_->dropdown)
                playbackFpsDropdown_->dropdown->SelectCellWithIdx(index);
            playbackFpsDropdown_->UpdateState();
        }
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
        if(playbackFpsDropdown_)
            playbackFpsDropdown_->set_interactable(enabled);
        if(resolutionDropdown_)
            resolutionDropdown_->set_interactable(enabled);
        if(nightlyUpdatesToggle_)
            nightlyUpdatesToggle_->set_interactable(enabled);
        if(updaterButton_)
            updaterButton_->set_interactable(enabled && DownloadManager::Instance().IsReady());
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

    void SettingsMenu::ShowSettingsTab(int index)
    {
        index = std::clamp(index, 0, 3);
        selectedTab_ = index;
        for(int page = 0; page < static_cast<int>(tabViewRoots_.size()); ++page)
            if(tabViewRoots_[page])
                tabViewRoots_[page]->SetActive(page == selectedTab_);
    }

    void SettingsMenu::RefreshUpdaterHint()
    {
        if(!updaterHoverHint_)
            return;
        updaterHoverHint_->set_text(
            Settings::Instance().NightlyDownloaderUpdates()
                ? "Checks the yt-dlp nightly update channel. Nightly builds may contain bugs; use this channel only when stable downloads are failing."
                : "Checks for updates from the current stable yt-dlp release channel. Stable releases are recommended for normal use.");
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

    void SettingsMenu::RefreshDownloaderStatus()
    {
        if(!updaterButton_ || !updaterStatus_) return;
        const auto snapshot = DownloadManager::Instance().Snapshot();
        if(snapshot.levelId != "__updater__") return;
        updaterStatus_->set_text(snapshot.message);
        BSML::Lite::SetButtonText(
            updaterButton_,
            snapshot.state == DownloadState::UpdateAvailable
                ? "Install yt-dlp Update"
                : snapshot.Active() ? "Checking..." : "Check yt-dlp Update");
        updaterButton_->set_interactable(
            Settings::Instance().ModEnabled() && !snapshot.Active());
    }
}
