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
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/LayoutRebuilder.hpp"
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

        std::array<std::string_view, 5> ScreenLayoutChoices{
            "Layout 1", "Layout 2", "Layout 3", "Layout 4", "Layout 5"
        };

        std::array<std::string_view, 3> PerformanceThresholdChoices{
            "5% missed frames", "10% missed frames", "20% missed frames"
        };

        std::array<std::string_view, 5> SettingsTabNames{
            "General", "Screen", "Environment", "Update", "Misc"
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
        advancedOptionsToggle_ = nullptr;
        videoEnabledToggle_ = nullptr;
        previewToggle_ = nullptr;
        screenLayoutDropdown_ = nullptr;
        allowChromaOverrideToggle_ = nullptr;
        distanceSetting_ = nullptr;
        horizontalSetting_ = nullptr;
        verticalSetting_ = nullptr;
        tiltSetting_ = nullptr;
        sizeSetting_ = nullptr;
        curvedScreenToggle_ = nullptr;
        maintainCurveAspectToggle_ = nullptr;
        curvatureSlider_ = nullptr;
        transparencyToggle_ = nullptr;
        screenCanvasHeader_ = nullptr;
        advancedScreenControlsRoot_ = nullptr;
        screenRotationSlider_ = nullptr;
        videoRotationSlider_ = nullptr;
        videoZoomSlider_ = nullptr;
        videoHorizontalSlider_ = nullptr;
        videoVerticalSlider_ = nullptr;
        videoTiltSlider_ = nullptr;
        stretchVideoToggle_ = nullptr;
        undockScreenToggle_ = nullptr;
        positionScreenButton_ = nullptr;
        cancelPositioningButton_ = nullptr;
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
        hideSpectrogramBarsHint_ = nullptr;
        playbackFpsDropdown_ = nullptr;
        resolutionDropdown_ = nullptr;
        automaticPerformanceToggle_ = nullptr;
        automaticPerformanceThresholdDropdown_ = nullptr;
        performanceDiagnosticsToggle_ = nullptr;
        nightlyUpdatesToggle_ = nullptr;
        nightlyWarningModal_ = nullptr;
        localVideoInstructionsModal_ = nullptr;
        resetConfirmationModal_ = nullptr;
        advancedWarningModal_ = nullptr;
        advancedWarningText_ = nullptr;
        undockWarningModal_ = nullptr;
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

        // Scroll containers align their first child directly with the upper
        // mask. A small real spacer keeps the first glyph row below that mask,
        // while an explicit text rectangle prevents the paragraph from
        // collapsing or clipping as the tab is activated.
        auto* storageTopSpacer = BSML::Lite::CreateText(
            storageContainer, "", 1.0f,
            {0.0f, 0.0f}, {48.0f, 1.5f});
        if(auto* spacerLayout = storageTopSpacer->get_gameObject()
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
            spacerLayout->set_preferredHeight(1.5f);
        auto* storageSectionTitle = BSML::Lite::CreateText(
            storageContainer, "Storage", 4.2f);
        if(storageSectionTitle)
        {
            storageSectionTitle->set_fontStyle(TMPro::FontStyles::Bold);
            storageSectionTitle->set_alignment(
                TMPro::TextAlignmentOptions::Center);
            storageSectionTitle->set_color({0.35f, 0.85f, 1.0f, 1.0f});
            if(auto* layout = storageSectionTitle->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredHeight(4.8f);
                layout->set_flexibleWidth(1.0f);
            }
        }
        auto* storageRow = BSML::Lite::CreateHorizontalLayoutGroup(
            storageContainer);
        if(storageRow)
        {
            storageRow->set_spacing(1.5f);
            storageRow->set_childControlWidth(true);
            storageRow->set_childControlHeight(false);
            storageRow->set_childForceExpandWidth(true);
            storageRow->set_childForceExpandHeight(false);
            storageRow->set_childAlignment(
                UnityEngine::TextAnchor::MiddleCenter);
            if(auto* layout = storageRow->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredHeight(28.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }
        const BSML::Lite::TransformWrapper storageRowParent = storageRow
            ? BSML::Lite::TransformWrapper(storageRow)
            : BSML::Lite::TransformWrapper(storageContainer);
        auto* storageExplanation = BSML::Lite::CreateText(
            storageRowParent,
            "Storage Maintenance scans for unassigned Big Screen downloads, unused thumbnails, and abandoned temporary files. You will see the exact list and total size before anything is removed. Map-folder videos and files in Video Import are never deleted.",
            2.7f,
            {0.0f, 0.0f},
            {36.0f, 26.0f});
        storageExplanation->set_enableWordWrapping(true);
        storageExplanation->set_enableAutoSizing(true);
        storageExplanation->set_fontSizeMin(2.3f);
        storageExplanation->set_fontSizeMax(2.7f);
        storageExplanation->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        storageExplanation->set_alignment(
            TMPro::TextAlignmentOptions::MidlineLeft);
        if(auto* layout = storageExplanation->get_gameObject()
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
        {
            // A 3:1 flexible-width ratio makes the explanatory copy occupy
            // three quarters of the row while preserving word wrapping.
            layout->set_preferredWidth(0.0f);
            layout->set_preferredHeight(26.0f);
            layout->set_flexibleWidth(3.0f);
        }
        auto* manageStorageButton = BSML::Lite::CreateUIButton(
            storageRowParent, "Manage Storage", {0.0f, 0.0f}, {18.0f, 8.0f},
            [callback = std::move(onManageStorage)]() { if(callback) callback(); });
        BSML::Lite::SetButtonTextSize(manageStorageButton, 2.5f);
        if(auto* layout = manageStorageButton
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
        {
            // An explicit width keeps the selectable/hover rectangle around
            // the complete label instead of stretching only a narrow flex
            // sliver beneath its center.
            layout->set_minWidth(18.0f);
            layout->set_preferredWidth(18.0f);
            layout->set_preferredHeight(8.0f);
            layout->set_flexibleWidth(0.0f);
        }
        BSML::Lite::AddHoverHint(
            manageStorageButton,
            "Opens a review page that scans for safe-to-remove Big Screen files. Nothing is deleted without confirmation.");

        // Preserve an unmistakable visual break between the compact storage
        // row and the next section without relying on layout-group spacing,
        // which is shared by every row in the scroll container.
        auto* storageSectionSpacer = BSML::Lite::CreateText(
            storageContainer, "", 1.0f,
            {0.0f, 0.0f}, {48.0f, 2.0f});
        if(auto* spacerLayout = storageSectionSpacer->get_gameObject()
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
            spacerLayout->set_preferredHeight(2.0f);

        auto* performanceSectionTitle = BSML::Lite::CreateText(
            storageContainer, "Performance", 4.2f);
        if(performanceSectionTitle)
        {
            performanceSectionTitle->set_fontStyle(TMPro::FontStyles::Bold);
            performanceSectionTitle->set_alignment(
                TMPro::TextAlignmentOptions::Center);
            performanceSectionTitle->set_color({0.35f, 0.85f, 1.0f, 1.0f});
            if(auto* layout = performanceSectionTitle->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredHeight(6.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }

        auto* performanceGroup = BSML::Lite::CreateVerticalLayoutGroup(
            storageContainer);
        if(performanceGroup)
        {
            performanceGroup->set_spacing(0.35f);
            performanceGroup->set_childControlWidth(true);
            performanceGroup->set_childControlHeight(true);
            performanceGroup->set_childForceExpandWidth(true);
            performanceGroup->set_childForceExpandHeight(false);
            if(auto* fitter = performanceGroup->get_gameObject()
                   ->GetComponent<UnityEngine::UI::ContentSizeFitter*>())
            {
                fitter->set_horizontalFit(
                    UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
                fitter->set_verticalFit(
                    UnityEngine::UI::ContentSizeFitter::FitMode::PreferredSize);
            }
            if(auto* layout = performanceGroup->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
                layout->set_flexibleWidth(1.0f);
        }
        const BSML::Lite::TransformWrapper performanceParent = performanceGroup
            ? BSML::Lite::TransformWrapper(performanceGroup)
            : BSML::Lite::TransformWrapper(storageContainer);

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
            "Limits how many video frames Big Screen displays each second. Lower limits can improve performance. Videos already below the limit keep their original frame rate.");

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
            "Sets the highest resolution used during playback. It does not download another copy or change the saved MP4. Lower-resolution videos are not enlarged. 720p is recommended; 1080p may reduce performance and battery life.");

        automaticPerformanceToggle_ = BSML::Lite::CreateToggle(
            performanceParent,
            "Automatic Performance",
            settings.AutomaticPerformanceEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetAutomaticPerformanceEnabled(enabled);
                RefreshEnabledState();
            });
        BSML::Lite::AddHoverHint(
            automaticPerformanceToggle_,
            "Temporarily lowers video frame rate and then resolution when playback cannot keep up. Your saved quality choices are restored for the next map.");
        automaticPerformanceThresholdDropdown_ = BSML::Lite::CreateDropdown(
            performanceParent,
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
            "Chooses how many video frames may be missed during five seconds before Automatic Performance lowers quality. A lower percentage reacts sooner.");
        performanceDiagnosticsToggle_ = BSML::Lite::CreateToggle(
            performanceParent,
            "Show Performance Information",
            settings.PerformanceDiagnosticsEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetPerformanceDiagnosticsEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            performanceDiagnosticsToggle_,
            "Shows video resolution, frame rate, missed frames, and decode time while playing. A summary remains visible on the failure or results screen.");

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
            "Turns Big Screen playback, previews, downloads, and environment changes on or off. This menu remains available so the mod can be turned back on.");

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
            "While the Big Screen menu is open, hides the neon Beat Saber sign and any supported clock or battery display it detects. Everything is restored when you leave.");

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
            "Master switch for song videos. Turning it off disables both in-map playback and song-selection previews. This setting is also available on the song-selection screen.");

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
            "Plays assigned videos while browsing songs. This requires Video In Map to be enabled and is also available on the song-selection screen.");

        // Keep a small layout-owned inset above the conditional title. The
        // scroll mask otherwise clips the top of the first line of text.
        auto* screenTopSpacer = BSML::Lite::CreateText(
            screenContainer, "", 1.0f,
            {0.0f, 0.0f}, {48.0f, 1.5f});
        if(auto* spacerLayout = screenTopSpacer->get_gameObject()
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
            spacerLayout->set_preferredHeight(1.5f);

        screenLayoutDropdown_ = BSML::Lite::CreateDropdown(
            screenContainer,
            "Editing Screen Layout",
            "Layout " + std::to_string(settings.ActiveScreenLayout() + 1),
            ScreenLayoutChoices,
            [this](StringW value)
            {
                const std::string label(value);
                const int index = !label.empty() && label.back() >= '1' && label.back() <= '5'
                    ? label.back() - '1' : 0;
                ScreenPreview::Instance().CancelUndockedEditing();
                Settings::Instance().SetActiveScreenLayout(index);
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshControls();
            });
        BSML::Lite::AddHoverHint(
            screenLayoutDropdown_,
            "Chooses which of your five saved layouts is active and which layout the controls below edit. It is used for previews and the next video map.");

        advancedOptionsToggle_ = BSML::Lite::CreateToggle(
            screenContainer,
            "Advanced Screen Controls",
            settings.AdvancedOptionsEnabled(),
            [this](bool enabled)
            {
                if(suppressAdvancedCallback_)
                    return;
                if(enabled)
                {
                    suppressAdvancedCallback_ = true;
                    SetToggleWithoutNotification(advancedOptionsToggle_, false);
                    suppressAdvancedCallback_ = false;
                    if(advancedWarningText_)
                    {
                        advancedWarningText_->set_text(
                            "Enable Screen " +
                            std::to_string(
                                Settings::Instance().ActiveScreenLayout() + 1) +
                            " Advanced Controls?\n\nThis enables detailed video framing and free screen placement for the selected layout. Extreme settings may reduce performance or interact poorly with mapper-authored effects. Other layouts are not changed.");
                    }
                    if(advancedWarningModal_)
                        advancedWarningModal_->Show();
                    return;
                }
                ScreenPreview::Instance().CancelUndockedEditing();
                Settings::Instance().SetAdvancedOptionsEnabled(false);
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshControls();
            });
        BSML::Lite::AddHoverHint(
            advancedOptionsToggle_,
            "Enables detailed video framing and free placement for the selected screen layout. Other layouts keep their own basic or advanced setting.");

        // The selector and its per-layout advanced switch deliberately come
        // first. Besides making their relationship obvious, those rows keep
        // this conditional heading below the scroll view's upper clipping
        // boundary instead of cutting off the top of its letters.
        auto* screenCanvasTitle = BSML::Lite::CreateText(
            screenContainer, "Screen Canvas", 4.2f);
        if(screenCanvasTitle)
        {
            screenCanvasTitle->set_fontStyle(TMPro::FontStyles::Bold);
            screenCanvasTitle->set_alignment(
                TMPro::TextAlignmentOptions::Center);
            screenCanvasTitle->set_color({0.35f, 0.85f, 1.0f, 1.0f});
            screenCanvasHeader_ = screenCanvasTitle->get_gameObject();
            if(auto* layout = screenCanvasHeader_
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredHeight(6.0f);
                layout->set_flexibleWidth(1.0f);
            }
            // Creation order keeps the selector and its toggle adjacent in
            // code. Visually, move the heading directly below the protective
            // spacer and above both rows.
            screenCanvasHeader_->get_transform()->SetSiblingIndex(1);
        }

        allowChromaOverrideToggle_ = BSML::Lite::CreateToggle(
            screenContainer,
            "Allow Chroma Override",
            settings.AllowChromaOverride(),
            [](bool enabled)
            {
                Settings::Instance().SetAllowChromaOverride(enabled);
                // A selected map may already be previewing. Rebuild from its
                // immutable mapper configuration so the change is visible
                // immediately without accumulating either layout's offsets.
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            allowChromaOverrideToggle_,
            "Lets a map's Cinema or Chroma data control screen placement and the environment. While active for a map, your Big Screen layout and environment overrides are not applied. Turn it off to use your own settings.");

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
            "Moves the screen closer with negative values and farther away with positive values. Free positioning replaces this control while Undock Screen is enabled.");

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
            "Moves the screen left with negative values and right with positive values. Free positioning replaces this control while Undock Screen is enabled.");

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
            "Moves the screen down with negative values and up with positive values. Free positioning replaces this control while Undock Screen is enabled.");

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
            "Adjusts the screen's vertical viewing angle in degrees. Free positioning replaces this control while Undock Screen is enabled.");

        sizeSetting_ = BSML::Lite::CreateIncrementSetting(
            screenContainer,
            "Screen Size Multiplier",
            1,
            0.1f,
            settings.ScreenScale(),
            0.5f,
            settings.MaximumScreenScale(),
            [this](float value)
            {
                Settings::Instance().SetScreenScale(value);
                // Reevaluate the maximum arrow after every step. Reaching the
                // cap hides it; one decrement makes it available again.
                RefreshCurvatureControl();
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            sizeSetting_,
            "Multiplies the map-authored screen size. Flat screens allow up to 4.0; curved screens allow up to 2.5. The resize handle replaces this control for an undocked screen.");

        curvedScreenToggle_ = BSML::Lite::CreateToggle(
            screenContainer,
            "Curved Screen",
            settings.CurvedScreenEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetCurvedScreenEnabled(enabled);
                // Enabling curvature can clamp scale from 4.0 to 2.5. Refresh
                // the value/range before rebuilding the preview so the text,
                // arrow state, saved setting, and visible surface change as
                // one user action.
                RefreshCurvatureControl();
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            curvedScreenToggle_,
            "Switches between a flat screen and a curved screen. Turning this on reveals the curve controls below and limits screen size to 2.5.");

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

        auto* advancedGroup = BSML::Lite::CreateVerticalLayoutGroup(screenContainer);
        advancedScreenControlsRoot_ = advancedGroup
            ? advancedGroup->get_gameObject() : nullptr;
        if(advancedGroup)
        {
            advancedGroup->set_spacing(0.35f);
            advancedGroup->set_childControlWidth(true);
            advancedGroup->set_childControlHeight(true);
            advancedGroup->set_childForceExpandWidth(true);
            advancedGroup->set_childForceExpandHeight(false);

            // BSML's VerticalTag fits width by default but does not fit its
            // height. That is appropriate for an anchored page, but this group
            // is one child inside a scroll view. Without vertical fitting its
            // RectTransform remains effectively zero-height: all advanced rows
            // render at the same bottom boundary and the scroll view never
            // includes them in its content range.
            if(auto* fitter = advancedGroup->get_gameObject()
                   ->GetComponent<UnityEngine::UI::ContentSizeFitter*>())
            {
                fitter->set_horizontalFit(
                    UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
                fitter->set_verticalFit(
                    UnityEngine::UI::ContentSizeFitter::FitMode::PreferredSize);
            }
            if(auto* layout = advancedGroup->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
                layout->set_flexibleWidth(1.0f);
        }
        const BSML::Lite::TransformWrapper advancedParent = advancedGroup
            ? BSML::Lite::TransformWrapper(advancedGroup)
            : BSML::Lite::TransformWrapper(screenContainer);

        screenRotationSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Screen Rotation", 1.0f, settings.ScreenRoll(),
            -180.0f, 180.0f, 0.15f, true, {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetScreenRoll(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            screenRotationSlider_,
            "Rotates a docked screen frame clockwise or counterclockwise while keeping its saved width and height. Use Position Screen to rotate an undocked screen freely.");

        videoRotationSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video Rotation", 1.0f, settings.VideoRotation(),
            -180.0f, 180.0f, 0.15f, true, {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetVideoRotation(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            videoRotationSlider_,
            "Rotates the picture inside the screen without rotating or reshaping the screen itself. Empty areas use the Video Transparency setting.");

        videoZoomSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video Zoom", 0.05f, settings.VideoZoom(),
            0.5f, 3.0f, 0.15f, true, {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetVideoZoom(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            videoZoomSlider_,
            "Changes the size of the picture inside the screen. Values above 1 crop the edges; values below 1 reveal more letterbox background.");

        videoHorizontalSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video X Position", 0.01f, settings.VideoOffsetX(),
            -1.0f, 1.0f, 0.15f, true, {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetVideoOffsetX(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            videoHorizontalSlider_,
            "Moves the picture left or right inside the fixed screen frame. This is useful after zooming or rotating a video.");

        videoVerticalSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video Y Position", 0.01f, settings.VideoOffsetY(),
            -1.0f, 1.0f, 0.15f, true, {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetVideoOffsetY(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            videoVerticalSlider_,
            "Moves the picture down or up inside the fixed screen frame. This is useful after zooming or rotating a video.");

        videoTiltSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video Tilt", 1.0f, settings.VideoTilt(),
            -75.0f, 75.0f, 0.15f, true, {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetVideoTilt(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            videoTiltSlider_,
            "Tilts the picture in perspective so its top or bottom appears closer, without changing the screen frame's angle.");

        stretchVideoToggle_ = BSML::Lite::CreateToggle(
            advancedParent, "Stretch Video to Fit", settings.StretchVideoToFit(),
            [](bool enabled)
            {
                Settings::Instance().SetStretchVideoToFit(enabled);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            stretchVideoToggle_,
            "Forces the picture to fill the screen even when their aspect ratios differ. Turn this off to preserve the video shape and use letterboxing or zoom instead.");

        auto* undockRow = BSML::Lite::CreateHorizontalLayoutGroup(advancedParent);
        if(undockRow)
        {
            undockRow->set_spacing(1.5f);
            undockRow->set_childControlWidth(true);
            undockRow->set_childControlHeight(true);
            undockRow->set_childForceExpandWidth(true);
            undockRow->set_childForceExpandHeight(false);
            if(auto* layout = undockRow
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredHeight(8.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }
        const BSML::Lite::TransformWrapper undockParent = undockRow
            ? BSML::Lite::TransformWrapper(undockRow)
            : advancedParent;
        undockScreenToggle_ = BSML::Lite::CreateToggle(
            undockParent, "Undock Screen", settings.UndockedScreenEnabled(),
            [this](bool enabled)
            {
                if(suppressUndockCallback_)
                    return;
                if(enabled)
                {
                    suppressUndockCallback_ = true;
                    SetToggleWithoutNotification(undockScreenToggle_, false);
                    suppressUndockCallback_ = false;
                    if(undockWarningModal_)
                        undockWarningModal_->Show();
                    return;
                }
                ScreenPreview::Instance().CancelUndockedEditing();
                Settings::Instance().SetUndockedScreenEnabled(false);
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshControls();
            });
        BSML::Lite::AddHoverHint(
            undockScreenToggle_,
            "Uses a freely positioned screen instead of placing it from the map's back-wall location. Each screen layout saves its own free position and shape.");

        positionScreenButton_ = BSML::Lite::CreateUIButton(
            undockParent, "Position Screen", {0.0f, 0.0f}, {20.0f, 8.0f},
            []() { ScreenPreview::Instance().BeginUndockedEditing(); });
        BSML::Lite::AddHoverHint(
            positionScreenButton_,
            "Opens the controller-based screen editor. Save the screen to keep its new position, angle, width, and height.");
        cancelPositioningButton_ = BSML::Lite::CreateUIButton(
            advancedParent, "Cancel Positioning", {0.0f, 0.0f}, {38.0f, 8.0f},
            []() { ScreenPreview::Instance().CancelUndockedEditing(); });
        BSML::Lite::AddHoverHint(
            cancelPositioningButton_,
            "Leaves positioning without saving and restores the last saved screen placement.");

        auto* videoControlsTitle = BSML::Lite::CreateText(
            advancedParent, "Video Controls", 4.2f);
        if(videoControlsTitle)
        {
            videoControlsTitle->set_fontStyle(TMPro::FontStyles::Bold);
            videoControlsTitle->set_alignment(
                TMPro::TextAlignmentOptions::Center);
            videoControlsTitle->set_color({0.35f, 0.85f, 1.0f, 1.0f});
            if(auto* layout = videoControlsTitle->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredHeight(6.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }

        transparencyToggle_ = BSML::Lite::CreateToggle(
            advancedParent,
            "Video Transparency",
            settings.TransparencyEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetTransparencyEnabled(enabled);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            transparencyToggle_,
            "Makes the video partly transparent so lights and scenery behind it can remain visible. Turn this off for an opaque picture that blocks the environment behind the screen.");

        // The controls above are created in callback-friendly groups, then
        // placed into their user-facing sections. Keep frame rotation and free
        // positioning under Screen Canvas; begin Video Controls with its title
        // and transparency setting, followed by every picture-only transform.
        if(undockRow)
            undockRow->get_transform()->SetSiblingIndex(1);
        if(cancelPositioningButton_)
            cancelPositioningButton_->get_transform()->SetSiblingIndex(2);
        if(videoControlsTitle)
            videoControlsTitle->get_transform()->SetSiblingIndex(3);
        if(transparencyToggle_)
            transparencyToggle_->get_transform()->SetSiblingIndex(4);

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
            "Allows the map's light show during video maps. Turning this off disables all map lighting and temporarily locks the individual lighting controls below.");

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
            "Uses Big Mirror as the background for video maps. Turn this off to keep each map's intended environment, which may contain objects that block part of the screen. Takes effect when the next map starts.");

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
        hideSpectrogramBarsHint_ = BSML::Lite::AddHoverHint(
            hideSpectrogramBarsToggle_,
            "Hides the audio-reactive spectrogram bars along the sides of the lanes. Takes effect when the next map starts.");

        advancedWarningModal_ = BSML::Lite::CreateModal(
            viewController, {72.0f, 38.0f}, nullptr, false);
        advancedWarningText_ = BSML::Lite::CreateText(
            advancedWarningModal_,
            "Enable Screen 1 Advanced Controls?\n\nThis enables detailed video framing and free screen placement for the selected layout. Extreme settings may reduce performance or interact poorly with mapper-authored effects. Other layouts are not changed.",
            TMPro::FontStyles::Normal,
            3.0f,
            {0.0f, 6.0f},
            {64.0f, 23.0f});
        advancedWarningText_->set_enableWordWrapping(true);
        advancedWarningText_->set_enableAutoSizing(true);
        advancedWarningText_->set_fontSizeMin(2.5f);
        advancedWarningText_->set_fontSizeMax(3.0f);
        advancedWarningText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        advancedWarningText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            advancedWarningModal_->get_transform(), "Cancel",
            {18.0f, -28.0f}, {25.0f, 8.0f},
            [this]()
            {
                if(advancedWarningModal_)
                    advancedWarningModal_->Hide();
                suppressAdvancedCallback_ = true;
                SetToggleWithoutNotification(advancedOptionsToggle_, false);
                suppressAdvancedCallback_ = false;
            });
        BSML::Lite::CreateUIButton(
            advancedWarningModal_->get_transform(), "I Understand",
            {48.0f, -28.0f}, {27.0f, 8.0f},
            [this]()
            {
                Settings::Instance().SetAdvancedOptionsEnabled(true);
                suppressAdvancedCallback_ = true;
                SetToggleWithoutNotification(advancedOptionsToggle_, true);
                suppressAdvancedCallback_ = false;
                if(advancedWarningModal_)
                    advancedWarningModal_->Hide();
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshControls();
            });

        undockWarningModal_ = BSML::Lite::CreateModal(
            viewController, {72.0f, 38.0f}, nullptr, false);
        auto* undockWarningText = BSML::Lite::CreateText(
            undockWarningModal_,
            "Undock this screen?\n\nFree placement is an advanced feature. Keep the screen clear of Beat Saber's menus and use a comfortable size and distance. Leaving Big Screen or opening the Quest system menu cancels unsaved positioning.",
            TMPro::FontStyles::Normal,
            3.0f,
            {0.0f, 6.0f},
            {64.0f, 23.0f});
        undockWarningText->set_enableWordWrapping(true);
        undockWarningText->set_enableAutoSizing(true);
        undockWarningText->set_fontSizeMin(2.5f);
        undockWarningText->set_fontSizeMax(3.0f);
        undockWarningText->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        undockWarningText->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            undockWarningModal_->get_transform(), "Cancel",
            {18.0f, -28.0f}, {25.0f, 8.0f},
            [this]()
            {
                if(undockWarningModal_)
                    undockWarningModal_->Hide();
                suppressUndockCallback_ = true;
                SetToggleWithoutNotification(undockScreenToggle_, false);
                suppressUndockCallback_ = false;
            });
        BSML::Lite::CreateUIButton(
            undockWarningModal_->get_transform(), "I Understand",
            {48.0f, -28.0f}, {27.0f, 8.0f},
            [this]()
            {
                Settings::Instance().SetUndockedScreenEnabled(true);
                suppressUndockCallback_ = true;
                SetToggleWithoutNotification(undockScreenToggle_, true);
                suppressUndockCallback_ = false;
                if(undockWarningModal_)
                    undockWarningModal_->Hide();
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshControls();
            });

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
            "Uses yt-dlp's frequently updated nightly channel. Nightly versions may contain new bugs; use this only when YouTube downloads fail with the stable version.");
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
            "Restores every Big Screen setting and all five screen layouts to their original values. Downloaded videos and timing assignments are not removed.");

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
        RefreshAdvancedControls();
        RefreshUpdaterHint();
    }

    void SettingsMenu::RefreshValues()
    {
        const auto& settings = Settings::Instance();
        SetToggleWithoutNotification(modEnabledToggle_, settings.ModEnabled());
        SetToggleWithoutNotification(
            distractionFreeMenuToggle_,
            settings.DistractionFreeMenu());
        SetToggleWithoutNotification(
            advancedOptionsToggle_, settings.AdvancedOptionsEnabled());
        SetToggleWithoutNotification(videoEnabledToggle_, settings.VideoEnabled());
        SetToggleWithoutNotification(previewToggle_, settings.MenuPreviewEnabled());
        SetToggleWithoutNotification(
            allowChromaOverrideToggle_, settings.AllowChromaOverride());
        SetToggleWithoutNotification(
            automaticPerformanceToggle_, settings.AutomaticPerformanceEnabled());
        SetToggleWithoutNotification(
            performanceDiagnosticsToggle_, settings.PerformanceDiagnosticsEnabled());
        SetToggleWithoutNotification(curvedScreenToggle_, settings.CurvedScreenEnabled());
        SetToggleWithoutNotification(
            maintainCurveAspectToggle_,
            settings.MaintainCurveAspectRatio());
        SetToggleWithoutNotification(transparencyToggle_, settings.TransparencyEnabled());
        SetToggleWithoutNotification(stretchVideoToggle_, settings.StretchVideoToFit());
        SetToggleWithoutNotification(undockScreenToggle_, settings.UndockedScreenEnabled());
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
        if(screenRotationSlider_)
            screenRotationSlider_->set_Value(settings.ScreenRoll());
        if(videoRotationSlider_)
            videoRotationSlider_->set_Value(settings.VideoRotation());
        if(videoZoomSlider_)
            videoZoomSlider_->set_Value(settings.VideoZoom());
        if(videoHorizontalSlider_)
            videoHorizontalSlider_->set_Value(settings.VideoOffsetX());
        if(videoVerticalSlider_)
            videoVerticalSlider_->set_Value(settings.VideoOffsetY());
        if(videoTiltSlider_)
            videoTiltSlider_->set_Value(settings.VideoTilt());
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
        const bool dockedGeometryEnabled = enabled &&
            !(settings.AdvancedOptionsEnabled() &&
              settings.UndockedScreenEnabled());
        const bool advancedEnabled =
            enabled && settings.AdvancedOptionsEnabled();
        const bool lightingChildrenEnabled =
            enabled && settings.MapLightShowEnabled();

        // The master switch and Reset remain usable. Every setting capable of
        // affecting Beat Saber is explicitly locked while the mod is off.
        if(distractionFreeMenuToggle_)
            distractionFreeMenuToggle_->set_interactable(enabled);
        if(advancedOptionsToggle_)
            advancedOptionsToggle_->set_interactable(enabled);
        if(videoEnabledToggle_)
            videoEnabledToggle_->set_interactable(enabled);
        if(previewToggle_)
        {
            SetToggleWithoutNotification(previewToggle_, settings.MenuPreviewEnabled());
            previewToggle_->set_interactable(enabled && settings.VideoEnabled());
        }
        if(distanceSetting_)
            distanceSetting_->set_interactable(dockedGeometryEnabled);
        if(horizontalSetting_)
            horizontalSetting_->set_interactable(dockedGeometryEnabled);
        if(verticalSetting_)
            verticalSetting_->set_interactable(dockedGeometryEnabled);
        if(tiltSetting_)
            tiltSetting_->set_interactable(dockedGeometryEnabled);
        if(sizeSetting_)
            sizeSetting_->set_interactable(dockedGeometryEnabled);
        if(curvedScreenToggle_)
            curvedScreenToggle_->set_interactable(enabled);
        if(screenLayoutDropdown_)
            screenLayoutDropdown_->set_interactable(enabled);
        if(allowChromaOverrideToggle_)
            allowChromaOverrideToggle_->set_interactable(enabled);
        if(maintainCurveAspectToggle_)
            maintainCurveAspectToggle_->set_interactable(enabled);
        if(curvatureSlider_)
            curvatureSlider_->set_interactable(enabled);
        if(transparencyToggle_)
            transparencyToggle_->set_interactable(advancedEnabled);
        if(screenRotationSlider_)
            screenRotationSlider_->set_interactable(
                advancedEnabled && dockedGeometryEnabled);
        if(videoRotationSlider_)
            videoRotationSlider_->set_interactable(advancedEnabled);
        if(videoZoomSlider_)
            videoZoomSlider_->set_interactable(advancedEnabled);
        if(videoHorizontalSlider_)
            videoHorizontalSlider_->set_interactable(advancedEnabled);
        if(videoVerticalSlider_)
            videoVerticalSlider_->set_interactable(advancedEnabled);
        if(videoTiltSlider_)
            videoTiltSlider_->set_interactable(advancedEnabled);
        if(stretchVideoToggle_)
            stretchVideoToggle_->set_interactable(advancedEnabled);
        if(undockScreenToggle_)
            undockScreenToggle_->set_interactable(advancedEnabled);
        if(positionScreenButton_)
            positionScreenButton_->set_interactable(
                advancedEnabled && settings.UndockedScreenEnabled());
        if(cancelPositioningButton_)
            cancelPositioningButton_->set_interactable(advancedEnabled);
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
        if(hideSpectrogramBarsHint_)
            hideSpectrogramBarsHint_->set_enabled(lightingChildrenEnabled);
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
        const auto& settings = Settings::Instance();
        const bool curved = settings.CurvedScreenEnabled();

        if(sizeSetting_)
        {
            sizeSetting_->maxValue = settings.MaximumScreenScale();
            sizeSetting_->set_Value(settings.ScreenScale());

            // BSML normally leaves an unusable maximum arrow visible but
            // disabled. Hide that arrow at the active mode's cap, then restore
            // it as soon as flat mode raises the cap from 2.5x to 4.0x.
            if(sizeSetting_->incButton)
            {
                const bool canIncrease =
                    settings.ScreenScale() + 0.0001f <
                    settings.MaximumScreenScale();
                sizeSetting_->incButton->get_gameObject()->SetActive(canIncrease);
            }
        }

        // SetActive participates in the vertical layout pass, so hiding both
        // curve-only rows also closes their space instead of leaving gaps.
        if(maintainCurveAspectToggle_)
            maintainCurveAspectToggle_->get_gameObject()->SetActive(curved);
        if(curvatureSlider_)
            curvatureSlider_->get_gameObject()->SetActive(curved);
    }

    void SettingsMenu::RefreshAdvancedControls()
    {
        const bool advancedEnabled =
            Settings::Instance().AdvancedOptionsEnabled();
        if(screenCanvasHeader_)
            screenCanvasHeader_->SetActive(advancedEnabled);
        if(advancedScreenControlsRoot_)
        {
            advancedScreenControlsRoot_->SetActive(advancedEnabled);

            // Visibility changes alter the scroll content's preferred height.
            // Rebuild from the nested group outward so the scrollbar receives
            // the new range during the same click instead of retaining the
            // shorter basic-options range until the menu is reopened.
            auto advancedRect = advancedScreenControlsRoot_->get_transform()
                .cast<UnityEngine::RectTransform>();
            UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(
                advancedRect);
            auto parentRect = advancedScreenControlsRoot_->get_transform()
                ->get_parent().cast<UnityEngine::RectTransform>();
            if(parentRect)
                UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(
                    parentRect);
            UnityEngine::Canvas::ForceUpdateCanvases();
        }
        if(cancelPositioningButton_)
            cancelPositioningButton_->get_gameObject()->SetActive(
                ScreenPreview::Instance().IsUndockedEditing());
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
            DownloadManager::Instance().Snapshot().state == DownloadState::UpdateAvailable
                ? "Downloads the available yt-dlp update, verifies it against the official SHA-256 checksum, and activates it after Beat Saber restarts."
                : Settings::Instance().NightlyDownloaderUpdates()
                    ? "Checks yt-dlp's nightly update channel. Nightly versions may contain bugs; use this only when stable downloads are failing."
                    : "Checks the official stable yt-dlp release channel. Stable releases are recommended for normal use.");
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
