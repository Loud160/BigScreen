#include "BigScreen/SettingsMenu.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "HMUI/HoverHint.hpp"
#include "HMUI/TextSegmentedControl.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
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

        std::array<std::string_view, 3> ScreenLayoutChoices{
            "Layout 1", "Layout 2", "Layout 3"
        };

        std::array<std::string_view, 3> PerformanceThresholdChoices{
            "5% missed frames", "10% missed frames", "20% missed frames"
        };

        std::array<std::string_view, 5> SettingsTabNames{
            "General", "Screen", "Environment", "Update", "Storage"
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
        std::function<void()> onBack,
        std::function<void()> onManageStorage)
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
        distractionFreeMenuToggle_ = nullptr;
        videoEnabledToggle_ = nullptr;
        previewToggle_ = nullptr;
        screenLayoutDropdown_ = nullptr;
        distanceSetting_ = nullptr;
        horizontalSetting_ = nullptr;
        verticalSetting_ = nullptr;
        tiltSetting_ = nullptr;
        sizeSetting_ = nullptr;
        curvedScreenToggle_ = nullptr;
        maintainCurveAspectToggle_ = nullptr;
        curvatureSlider_ = nullptr;
        transparencyToggle_ = nullptr;
        lightShowToggle_ = nullptr;
        hideBackWallLightsToggle_ = nullptr;
        hideRingLightsToggle_ = nullptr;
        hideSideLaserLightsToggle_ = nullptr;
        hideBackWallLightsHint_ = nullptr;
        hideRingLightsHint_ = nullptr;
        hideSideLaserLightsHint_ = nullptr;
        environmentOverrideToggle_ = nullptr;
        disableEnvironmentMotionToggle_ = nullptr;
        hideTrackRingsToggle_ = nullptr;
        hideSideBarsToggle_ = nullptr;
        hideSpectrogramBarsToggle_ = nullptr;
        playbackFpsDropdown_ = nullptr;
        resolutionDropdown_ = nullptr;
        automaticPerformanceToggle_ = nullptr;
        automaticPerformanceThresholdDropdown_ = nullptr;
        performanceDiagnosticsToggle_ = nullptr;
        nightlyUpdatesToggle_ = nullptr;
        nightlyWarningModal_ = nullptr;
        localVideoInstructionsModal_ = nullptr;
        resetConfirmationModal_ = nullptr;
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
        auto* environmentContainer = createTabPage(2);
        auto* updateContainer = createTabPage(3);
        auto* storageContainer = createTabPage(4);
        if(!generalContainer || !screenContainer ||
           !environmentContainer || !updateContainer || !storageContainer)
        {
            PaperLogger.error("Could not create all Big Screen settings tabs");
            return;
        }

        auto* storageExplanation = BSML::Lite::CreateText(
            storageContainer,
            "Storage Maintenance scans for unassigned Big Screen downloads, unused thumbnails, and abandoned temporary files. You will see the exact list and total size before anything is removed. Map-folder videos and files in Video Import are never deleted.",
            3.0f);
        storageExplanation->set_enableWordWrapping(true);
        storageExplanation->set_alignment(TMPro::TextAlignmentOptions::Center);
        auto* manageStorageButton = BSML::Lite::CreateUIButton(
            storageContainer, "Manage Storage", {0.0f, 0.0f}, {38.0f, 8.0f},
            [callback = std::move(onManageStorage)]() { if(callback) callback(); });
        BSML::Lite::AddHoverHint(
            manageStorageButton,
            "Opens a review page that scans for safe-to-remove Big Screen files. Nothing is deleted without confirmation.");

        // Performance limits are global playback preferences rather than
        // environment behavior. Create them first so they remain at the top of
        // General regardless of settings added to that page later.
        playbackFpsDropdown_ = BSML::Lite::CreateDropdown(
            generalContainer,
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
            generalContainer,
            "Video Resolution",
            ResolutionLabel(settings.ResolutionHeight()),
            ResolutionChoices,
            [](StringW value)
            {
                Settings::Instance().SetResolutionHeight(ResolutionValue(value));
                // Resolution is fixed when FrameDecoder opens and cannot be
                // changed on an existing RGBA texture. Recreate any active
                // library preview immediately so the dropdown's new tier is
                // visible without making the player leave/reselect the song.
                // VideoLibraryMenu retains and reapplies the preview time.
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            resolutionDropdown_,
            "Maximum playback resolution. Sources below the selected tier are not upscaled. 720p is recommended; 1080p may reduce performance and battery life.");

        automaticPerformanceToggle_ = BSML::Lite::CreateToggle(
            generalContainer,
            "Automatic Performance",
            settings.AutomaticPerformanceEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetAutomaticPerformanceEnabled(enabled);
                RefreshEnabledState();
            });
        BSML::Lite::AddHoverHint(
            automaticPerformanceToggle_,
            "Lets Big Screen lower its frame-rate limit and then output resolution if decoding repeatedly falls behind. Your saved quality settings are not changed.");
        automaticPerformanceThresholdDropdown_ = BSML::Lite::CreateDropdown(
            generalContainer,
            "Automatic Performance Trigger",
            std::to_string(settings.AutomaticPerformanceThreshold()) + "% missed frames",
            PerformanceThresholdChoices,
            [](StringW value)
            {
                const std::string text(value);
                Settings::Instance().SetAutomaticPerformanceThreshold(
                    text.starts_with("5%") ? 5 : text.starts_with("20%") ? 20 : 10);
            });
        BSML::Lite::AddHoverHint(
            automaticPerformanceThresholdDropdown_,
            "Percentage of missed video presentation frames in a five-second window that triggers the next performance reduction.");
        performanceDiagnosticsToggle_ = BSML::Lite::CreateToggle(
            generalContainer,
            "Show Performance Information",
            settings.PerformanceDiagnosticsEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetPerformanceDiagnosticsEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            performanceDiagnosticsToggle_,
            "Shows source and output resolution, frame rate, missed frames, and decoder delay during video maps and on the results screen.");

        modEnabledToggle_ = BSML::Lite::CreateToggle(
            generalContainer,
            "Big Screen Enabled",
            settings.ModEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetModEnabled(enabled);

                // This menu-only visual override participates in the master
                // switch just like playback and screen creation do. Turning
                // Big Screen off must immediately restore the stock/mod UI.
                ApplyDistractionFreeMenu();

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

        distractionFreeMenuToggle_ = BSML::Lite::CreateToggle(
            generalContainer,
            "Distraction Free Menu",
            settings.DistractionFreeMenu(),
            [](bool enabled)
            {
                Settings::Instance().SetDistractionFreeMenu(enabled);
                // The player can judge the change without closing and
                // reopening Big Screen. Apply restores when disabled.
                ApplyDistractionFreeMenu();
            });
        BSML::Lite::AddHoverHint(
            distractionFreeMenuToggle_,
            "Hides the neon Beat Saber sign and any detected clock/battery display while this menu is open. Everything is restored when you leave.");

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

        screenLayoutDropdown_ = BSML::Lite::CreateDropdown(
            screenContainer,
            "Editing Screen Layout",
            "Layout " + std::to_string(settings.ActiveScreenLayout() + 1),
            ScreenLayoutChoices,
            [this](StringW value)
            {
                const std::string label(value);
                const int index = !label.empty() && label.back() >= '1' && label.back() <= '3'
                    ? label.back() - '1' : 0;
                Settings::Instance().SetActiveScreenLayout(index);
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshControls();
            });
        BSML::Lite::AddHoverHint(
            screenLayoutDropdown_,
            "Chooses which of the three saved screen placements the controls below edit and gameplay uses.");

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
            2.5f,
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

        maintainCurveAspectToggle_ = BSML::Lite::CreateToggle(
            screenContainer,
            "Maintain Aspect Ratio",
            settings.MaintainCurveAspectRatio(),
            [](bool enabled)
            {
                Settings::Instance().SetMaintainCurveAspectRatio(enabled);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            maintainCurveAspectToggle_,
            "Keeps the video's original width-to-height ratio as the screen curves. Turn this off to let stronger curves widen and stretch the screen like the original behavior.");

        // Both curve-specific rows stay directly below Curved Screen so the
        // vertical layout collapses cleanly when a flat screen is selected.
        // showButtons enables BSML's native clickable arrows; the slider keeps
        // the full range for coarse movement while each arrow changes 0.05.
        curvatureSlider_ = BSML::Lite::CreateSliderSetting(
            screenContainer,
            "Screen Curve",
            0.05f,
            settings.ScreenCurvature(),
            -7.0f,
            7.0f,
            0.15f,
            true,
            {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetScreenCurvature(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            curvatureSlider_,
            "Positive values wrap the edges toward you; negative values bend them away. The available range is -7 through +7.");

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
            environmentContainer,
            "Map Light Show",
            settings.MapLightShowEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetMapLightShowEnabled(enabled);
                SettingsMenu::Instance().RefreshControls();
            });
        BSML::Lite::AddHoverHint(
            lightShowToggle_,
            "Keeps the selected map's lighting events active while its video plays.");

        hideBackWallLightsToggle_ = BSML::Lite::CreateToggle(
            environmentContainer,
            "Hide Back Wall Lights",
            settings.HideBackWallLights(),
            [](bool enabled)
            {
                Settings::Instance().SetHideBackWallLights(enabled);
            });
        hideBackWallLightsHint_ = BSML::Lite::AddHoverHint(
            hideBackWallLightsToggle_,
            "Hides the back-wall and center-lane light groups that most often shine across the video. Other map lighting remains active. Takes effect when the next map starts.");

        hideRingLightsToggle_ = BSML::Lite::CreateToggle(
            environmentContainer,
            "Hide Ring Lights",
            settings.HideRingLights(),
            [](bool enabled)
            {
                Settings::Instance().SetHideRingLights(enabled);
            });
        hideRingLightsHint_ = BSML::Lite::AddHoverHint(
            hideRingLightsToggle_,
            "Hides the map's ring-light group while leaving the remaining light show active. Takes effect when the next map starts.");

        hideSideLaserLightsToggle_ = BSML::Lite::CreateToggle(
            environmentContainer,
            "Hide Side Laser Lights",
            settings.HideSideLaserLights(),
            [](bool enabled)
            {
                Settings::Instance().SetHideSideLaserLights(enabled);
            });
        hideSideLaserLightsHint_ = BSML::Lite::AddHoverHint(
            hideSideLaserLightsToggle_,
            "Hides the left and right laser-light groups when their beams cross the video. Takes effect when the next map starts.");

        environmentOverrideToggle_ = BSML::Lite::CreateToggle(
            environmentContainer,
            "Use Big Mirror Override",
            settings.EnvironmentOverrideEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetEnvironmentOverrideEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            environmentOverrideToggle_,
            "When disabled, the map's intended background is used and may partially block the video.");

        // The experimental Glass Desert override is intentionally absent from
        // the public menu. Its persisted setting and gameplay implementation
        // remain available so a future UI can restore the option without
        // rebuilding the environment-selection behavior.

        disableEnvironmentMotionToggle_ = BSML::Lite::CreateToggle(
            environmentContainer,
            "Disable Rotation and Motion",
            settings.DisableEnvironmentMotion(),
            [](bool enabled)
            {
                Settings::Instance().SetDisableEnvironmentMotion(enabled);
            });
        BSML::Lite::AddHoverHint(
            disableEnvironmentMotionToggle_,
            "When enabled, stops rotating and moving background scenery in video maps. Takes effect when the next map starts.");

        hideTrackRingsToggle_ = BSML::Lite::CreateToggle(
            environmentContainer,
            "Hide Track Rings",
            settings.HideTrackRings(),
            [](bool enabled)
            {
                Settings::Instance().SetHideTrackRings(enabled);
            });
        BSML::Lite::AddHoverHint(
            hideTrackRingsToggle_,
            "Hides the overhead ring and arch geometry that can cross in front of the video screen. Takes effect when the next map starts.");

        hideSideBarsToggle_ = BSML::Lite::CreateToggle(
            environmentContainer,
            "Hide Side Bars",
            settings.HideSideBars(),
            [](bool enabled)
            {
                Settings::Instance().SetHideSideBars(enabled);
            });
        BSML::Lite::AddHoverHint(
            hideSideBarsToggle_,
            "Hides Big Mirror's paired near-building structures when they obstruct the sides of the video. Takes effect when the next map starts.");

        hideSpectrogramBarsToggle_ = BSML::Lite::CreateToggle(
            environmentContainer,
            "Hide Spectrogram Bars",
            settings.HideSpectrogramBars(),
            [](bool enabled)
            {
                Settings::Instance().SetHideSpectrogramBars(enabled);
            });
        BSML::Lite::AddHoverHint(
            hideSpectrogramBarsToggle_,
            "Hides the audio-reactive spectrogram bars along the sides of the lanes. Takes effect when the next map starts.");

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

        localVideoInstructionsModal_ = BSML::Lite::CreateModal(
            viewController,
            {70.0f, 46.0f},
            nullptr,
            true);
        auto* localVideoInstructions = BSML::Lite::CreateText(
            localVideoInstructionsModal_,
            "<size=3.8><b>Add Your Own Video</b></size>\n\n"
            "For a custom or WIP map, copy an H.264/AVC MP4 (up to 1080p) into that map's folder.\n\n"
            "For any song—including OST and DLC—copy the MP4 into:\n"
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Video Import\n\n"
            "Open Video Library, choose the song, and press SET beside the file. You can then adjust timing normally. Big Screen only registers these user-owned files; Remove Video never deletes them.",
            TMPro::FontStyles::Normal,
            3.1f,
            {0.0f, 3.0f},
            {64.0f, 34.0f});
        // BSML text defaults to a zero-sized rectangle and permits glyphs to
        // overflow it. Give the instructions a real content box inside the
        // modal, wrap long lines, and allow a small automatic font reduction
        // if localization or font metrics require it.
        localVideoInstructions->set_enableWordWrapping(true);
        localVideoInstructions->set_enableAutoSizing(true);
        localVideoInstructions->set_fontSizeMin(2.7f);
        localVideoInstructions->set_fontSizeMax(3.1f);
        localVideoInstructions->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        localVideoInstructions->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            localVideoInstructionsModal_->get_transform(),
            "Close",
            {35.0f, -39.0f},
            {22.0f, 7.0f},
            [this]()
            {
                if(localVideoInstructionsModal_)
                    localVideoInstructionsModal_->Hide();
            });

        // Keep the informational and destructive actions on opposite sides of
        // one full-width footer. Stacking these similarly sized buttons made an
        // imprecise controller click much too likely to reset the mod.
        auto* generalActions = BSML::Lite::CreateHorizontalLayoutGroup(
            generalContainer);
        generalActions->set_spacing(4.0f);
        generalActions->set_childControlWidth(true);
        generalActions->set_childControlHeight(true);
        generalActions->set_childForceExpandWidth(true);
        generalActions->set_childForceExpandHeight(false);
        if(auto* actionsLayout = generalActions
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
        {
            actionsLayout->set_preferredHeight(8.0f);
            actionsLayout->set_flexibleWidth(1.0f);
        }

        auto* addLocalVideoButton = BSML::Lite::CreateUIButton(
            generalActions,
            "Add My Own Video",
            UnityEngine::Vector2{0.0f, 0.0f},
            UnityEngine::Vector2{24.0f, 8.0f},
            [this]()
            {
                if(localVideoInstructionsModal_)
                    localVideoInstructionsModal_->Show();
            });
        BSML::Lite::AddHoverHint(
            addLocalVideoButton,
            "Explains how to assign an H.264 MP4 from a map folder or Big Screen's Video Import folder.");

        resetButton_ = BSML::Lite::CreateUIButton(
            generalActions,
            "Reset to Defaults",
            UnityEngine::Vector2{0.0f, 0.0f},
            UnityEngine::Vector2{24.0f, 8.0f},
            [this]()
            {
                if(resetConfirmationModal_)
                    resetConfirmationModal_->Show();
            });
        BSML::Lite::AddHoverHint(
            resetButton_,
            "Restores every Big Screen option, including placement, to its original value.");

        resetConfirmationModal_ = BSML::Lite::CreateModal(
            viewController,
            {64.0f, 32.0f},
            nullptr,
            true);
        auto* resetConfirmationText = BSML::Lite::CreateText(
            resetConfirmationModal_,
            "<b>Reset all Big Screen settings?</b>\n\n"
            "Every option, including screen placement and environment controls, will return to its default value.",
            TMPro::FontStyles::Normal,
            3.2f,
            {0.0f, 4.0f},
            {56.0f, 16.0f});
        resetConfirmationText->set_enableWordWrapping(true);
        resetConfirmationText->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        resetConfirmationText->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            resetConfirmationModal_->get_transform(),
            "Cancel",
            {18.0f, -25.0f},
            {20.0f, 7.0f},
            [this]()
            {
                if(resetConfirmationModal_)
                    resetConfirmationModal_->Hide();
            });
        auto* confirmResetButton = BSML::Lite::CreateUIButton(
            resetConfirmationModal_->get_transform(),
            "Reset",
            {46.0f, -25.0f},
            {20.0f, 7.0f},
            [this]()
            {
                if(resetConfirmationModal_)
                    resetConfirmationModal_->Hide();
                ResetToDefaults();
            });
        if(auto* confirmResetText = confirmResetButton->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            confirmResetText->set_color({1.0f, 0.35f, 0.35f, 1.0f});

        // One shared error surface prevents repeated failures from building a
        // stack of modal views. ErrorManager keeps only the newest pending
        // message and never permits this UI to appear over active gameplay.
        errorModal_ = BSML::Lite::CreateModal(
            viewController, {72.0f, 42.0f}, nullptr, true);
        errorModalText_ = BSML::Lite::CreateText(
            errorModal_, "", TMPro::FontStyles::Normal, 3.0f,
            {0.0f, 3.0f}, {66.0f, 30.0f});
        errorModalText_->set_enableWordWrapping(true);
        errorModalText_->set_enableAutoSizing(true);
        errorModalText_->set_fontSizeMin(2.2f);
        errorModalText_->set_fontSizeMax(3.0f);
        errorModalText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        errorModalText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            errorModal_->get_transform(), "OK", {36.0f, -35.0f}, {20.0f, 7.0f},
            [this]() { if(errorModal_) errorModal_->Hide(); });

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
        SetToggleWithoutNotification(
            distractionFreeMenuToggle_,
            settings.DistractionFreeMenu());
        SetToggleWithoutNotification(videoEnabledToggle_, settings.VideoEnabled());
        SetToggleWithoutNotification(previewToggle_, settings.MenuPreviewEnabled());
        SetToggleWithoutNotification(
            automaticPerformanceToggle_, settings.AutomaticPerformanceEnabled());
        SetToggleWithoutNotification(
            performanceDiagnosticsToggle_, settings.PerformanceDiagnosticsEnabled());
        SetToggleWithoutNotification(curvedScreenToggle_, settings.CurvedScreenEnabled());
        SetToggleWithoutNotification(
            maintainCurveAspectToggle_,
            settings.MaintainCurveAspectRatio());
        SetToggleWithoutNotification(transparencyToggle_, settings.TransparencyEnabled());
        SetToggleWithoutNotification(lightShowToggle_, settings.MapLightShowEnabled());
        SetToggleWithoutNotification(
            hideBackWallLightsToggle_,
            settings.HideBackWallLights());
        SetToggleWithoutNotification(
            hideRingLightsToggle_,
            settings.HideRingLights());
        SetToggleWithoutNotification(
            hideSideLaserLightsToggle_,
            settings.HideSideLaserLights());
        SetToggleWithoutNotification(
            environmentOverrideToggle_,
            settings.EnvironmentOverrideEnabled());
        SetToggleWithoutNotification(
            disableEnvironmentMotionToggle_,
            settings.DisableEnvironmentMotion());
        SetToggleWithoutNotification(
            hideTrackRingsToggle_,
            settings.HideTrackRings());
        SetToggleWithoutNotification(
            hideSideBarsToggle_,
            settings.HideSideBars());
        SetToggleWithoutNotification(
            hideSpectrogramBarsToggle_,
            settings.HideSpectrogramBars());
        SetToggleWithoutNotification(
            nightlyUpdatesToggle_,
            settings.NightlyDownloaderUpdates());

        if(screenLayoutDropdown_)
        {
            const int index = settings.ActiveScreenLayout();
            screenLayoutDropdown_->index = index;
            if(screenLayoutDropdown_->dropdown)
                screenLayoutDropdown_->dropdown->SelectCellWithIdx(index);
            screenLayoutDropdown_->UpdateState();
        }

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
        if(automaticPerformanceThresholdDropdown_)
        {
            const int index = settings.AutomaticPerformanceThreshold() == 5
                ? 0 : settings.AutomaticPerformanceThreshold() == 20 ? 2 : 1;
            automaticPerformanceThresholdDropdown_->index = index;
            if(automaticPerformanceThresholdDropdown_->dropdown)
                automaticPerformanceThresholdDropdown_->dropdown->SelectCellWithIdx(index);
            automaticPerformanceThresholdDropdown_->UpdateState();
        }
    }

    void SettingsMenu::RefreshEnabledState()
    {
        const auto& settings = Settings::Instance();
        const bool enabled = settings.ModEnabled();
        const bool lightingChildrenEnabled =
            enabled && settings.MapLightShowEnabled();

        // The master switch and Reset remain usable. Every setting capable of
        // affecting Beat Saber is explicitly locked while the mod is off.
        if(distractionFreeMenuToggle_)
            distractionFreeMenuToggle_->set_interactable(enabled);
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
        if(screenLayoutDropdown_)
            screenLayoutDropdown_->set_interactable(enabled);
        if(maintainCurveAspectToggle_)
            maintainCurveAspectToggle_->set_interactable(enabled);
        if(curvatureSlider_)
            curvatureSlider_->set_interactable(enabled);
        if(transparencyToggle_)
            transparencyToggle_->set_interactable(enabled);
        if(lightShowToggle_)
            lightShowToggle_->set_interactable(enabled);

        // The master switch changes only whether these controls can be edited;
        // it deliberately does not rewrite their persisted toggle values. When
        // the light show is re-enabled, RefreshValues restores every user's
        // previous combination before making the controls interactive again.
        if(hideBackWallLightsToggle_)
            hideBackWallLightsToggle_->set_interactable(lightingChildrenEnabled);
        if(hideRingLightsToggle_)
            hideRingLightsToggle_->set_interactable(lightingChildrenEnabled);
        if(hideSideLaserLightsToggle_)
            hideSideLaserLightsToggle_->set_interactable(lightingChildrenEnabled);

        // A disabled Selectable still receives pointer-enter events, so its
        // separately attached HoverHint must follow the same master state.
        // The Map Light Show hint is intentionally unaffected because that
        // master control remains available to turn lighting back on.
        if(hideBackWallLightsHint_)
            hideBackWallLightsHint_->set_enabled(lightingChildrenEnabled);
        if(hideRingLightsHint_)
            hideRingLightsHint_->set_enabled(lightingChildrenEnabled);
        if(hideSideLaserLightsHint_)
            hideSideLaserLightsHint_->set_enabled(lightingChildrenEnabled);
        if(environmentOverrideToggle_)
            environmentOverrideToggle_->set_interactable(enabled);
        if(disableEnvironmentMotionToggle_)
            disableEnvironmentMotionToggle_->set_interactable(enabled);
        if(hideTrackRingsToggle_)
            hideTrackRingsToggle_->set_interactable(enabled);
        if(hideSideBarsToggle_)
            hideSideBarsToggle_->set_interactable(enabled);
        if(hideSpectrogramBarsToggle_)
            hideSpectrogramBarsToggle_->set_interactable(lightingChildrenEnabled);
        if(playbackFpsDropdown_)
            playbackFpsDropdown_->set_interactable(enabled);
        if(resolutionDropdown_)
            resolutionDropdown_->set_interactable(enabled);
        if(automaticPerformanceToggle_)
            automaticPerformanceToggle_->set_interactable(enabled);
        if(automaticPerformanceThresholdDropdown_)
            automaticPerformanceThresholdDropdown_->set_interactable(
                enabled && settings.AutomaticPerformanceEnabled());
        if(performanceDiagnosticsToggle_)
            performanceDiagnosticsToggle_->set_interactable(enabled);
        if(nightlyUpdatesToggle_)
            nightlyUpdatesToggle_->set_interactable(enabled);
        if(updaterButton_)
            updaterButton_->set_interactable(enabled && DownloadManager::Instance().IsReady());
    }

    void SettingsMenu::RefreshCurvatureControl()
    {
        const bool curved = Settings::Instance().CurvedScreenEnabled();

        // SetActive participates in the vertical layout pass, so hiding both
        // curve-only rows also closes their space instead of leaving gaps.
        if(maintainCurveAspectToggle_)
            maintainCurveAspectToggle_->get_gameObject()->SetActive(curved);
        if(curvatureSlider_)
            curvatureSlider_->get_gameObject()->SetActive(curved);
    }

    void SettingsMenu::ShowSettingsTab(int index)
    {
        index = std::clamp(index, 0, 4);
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

        // Environment lives on an inactive tab while the reset button is
        // pressed from General. Updating only Toggle/currentValue bypasses
        // BSML's normal setting pipeline and can leave the hidden switches,
        // their dependent interactable states, and their graphics showing the
        // old values even though bigscreen.json was correctly reset. Apply
        // these defaults through ToggleSetting so each control and callback is
        // synchronized exactly as if the player had selected the value.
        const auto applyEnvironmentToggle = [](BSML::ToggleSetting* control,
                                               bool value)
        {
            if(control)
                control->set_Value(value);
        };
        applyEnvironmentToggle(
            transparencyToggle_, settings.TransparencyEnabled());
        applyEnvironmentToggle(
            hideBackWallLightsToggle_, settings.HideBackWallLights());
        applyEnvironmentToggle(
            hideRingLightsToggle_, settings.HideRingLights());
        applyEnvironmentToggle(
            hideSideLaserLightsToggle_, settings.HideSideLaserLights());
        applyEnvironmentToggle(
            environmentOverrideToggle_, settings.EnvironmentOverrideEnabled());
        applyEnvironmentToggle(
            disableEnvironmentMotionToggle_, settings.DisableEnvironmentMotion());
        applyEnvironmentToggle(
            hideTrackRingsToggle_, settings.HideTrackRings());
        applyEnvironmentToggle(
            hideSideBarsToggle_, settings.HideSideBars());
        applyEnvironmentToggle(
            hideSpectrogramBarsToggle_, settings.HideSpectrogramBars());
        // Apply the master last because its callback refreshes the dependent
        // child states. At this point those children already hold their new
        // values, so that refresh cannot overwrite their transition.
        applyEnvironmentToggle(
            lightShowToggle_, settings.MapLightShowEnabled());

        // Rebuild the selected song config before recreating the world screen.
        // This is the missing live-effect step that left the displayed screen
        // at its previous size even though the control correctly showed 1.0.
        PlaybackSession::Instance().RefreshDisplaySettings();

        // Reset changes persistent values first, then mirrors those values into
        // the already-open controls without generating a chain of fake clicks.
        SelectionVideoToggle::Instance().ModEnabledChanged(settings.ModEnabled());
        SelectionVideoToggle::Instance().ApplyGlobalVideoEnabled(settings.VideoEnabled());
        SelectionVideoToggle::Instance().MenuPreviewPreferenceChanged();
        ScreenPreview::Instance().SetEnabled(settings.ModEnabled());
        ApplyDistractionFreeMenu();
        RefreshControls();
        PaperLogger.info("Reset all Big Screen settings to defaults");
    }

    void SettingsMenu::RefreshDownloaderStatus()
    {
        if(auto recovery = VideoLibrary::Instance().TakeRecoveryNotice())
            ErrorManager::Instance().ReportUserVisible("Video library recovered", *recovery);
        if(auto update = DownloadManager::Instance().TakeUpdateNotice())
            ErrorManager::Instance().ReportUserVisible("Downloader rollback", *update);
        if(auto message = ErrorManager::Instance().TakePendingDialog();
           message && errorModal_ && errorModalText_)
        {
            errorModalText_->set_text(
                "<b>" + message->first + "</b>\n\n" + message->second);
            errorModal_->Show();
            RefreshControls();
        }
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
