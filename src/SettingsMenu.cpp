// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/SettingsMenu.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "fmt/format.h"

#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/MenuEnvironmentVisibility.hpp"
#include "BigScreen/MenuPlacementGuide.hpp"
#include "BigScreen/NestedHoverHintOverride.hpp"
#include "BigScreen/PerformancePanel.hpp"
#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/ShowcaseLauncher.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/UiSettingsUtility.hpp"
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
#include "UnityEngine/Transform.hpp"
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
        using UiUtility::SetToggleWithoutNotification;

        std::array<std::string_view, 4> ResolutionChoices{
            "480p",
            "720p",
            "1080p",
            "1440p"
        };

        std::array<std::string_view, 3> PlaybackFpsChoices{
            "15 FPS",
            "30 FPS",
            "60 FPS"
        };

        std::array<std::string_view, 5> ScreenLayoutChoices{
            "Layout 1", "Layout 2", "Layout 3", "Layout 4", "Layout 5"
        };

        std::array<std::string_view, 5> SettingsTabNames{
            "General", "Screen", "Environment", "Update", "Misc"
        };

        // Reset controls intentionally share one compact visual contract. The
        // glyph may be enlarged for readability, but the fixed button footprint
        // must remain unchanged so neither settings row is pushed or clipped.
        constexpr float ResetGlyphTextSize = 6.0f;
        constexpr float ResetButtonSize = 7.0f;

        constexpr float VideoOffsetToZoomSlider(float offset)
        {
            return 1.75f + (offset * 1.25f);
        }

        constexpr float ZoomSliderToVideoOffset(float sliderValue)
        {
            return (sliderValue - 1.75f) / 1.25f;
        }

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
            if(text == "1440p")
                return 1440;
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

        void ApplyDisplaySettingsAndRefreshPreview()
        {
            if(ScreenPreview::Instance().IsUndockedEditing())
            {
                ScreenPreview::Instance().RefreshUndockedEditingFromSettings();
                return;
            }
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
        std::function<void()> onManageStorage,
        std::function<void()> onShowShowcase,
        std::function<void(bool)> onModEnabledChanged)
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
        modEnabledUiChanged_ = std::move(onModEnabledChanged);
        // The coordinator applies the initial right/center layout after all
        // view controllers have been created and provided. From this point on,
        // only an actual state transition needs the callback.
        displayedEnabledState_ = Settings::Instance().ModEnabled();
        displayedEnabledStateKnown_ = true;
        settingsTabs_ = nullptr;
        tabViewRoots_.fill(nullptr);
        generalContentRoot_ = nullptr;
        generalMasterRoot_ = nullptr;
        selectedTab_ = 0;
        modEnabledToggle_ = nullptr;
        distractionFreeMenuToggle_ = nullptr;
        showMenuEnvironmentToggle_ = nullptr;
        showLaneGuidesToggle_ = nullptr;
        advancedOptionsToggle_ = nullptr;
        videoEnabledToggle_ = nullptr;
        previewToggle_ = nullptr;
        screenLayoutResetButton_ = nullptr;
        screenLayoutDropdown_ = nullptr;
        allowChromaOverrideToggle_ = nullptr;
        distanceSetting_ = nullptr;
        horizontalSetting_ = nullptr;
        verticalSetting_ = nullptr;
        tiltSetting_ = nullptr;
        sizeSetting_ = nullptr;
        distanceHint_ = nullptr;
        horizontalHint_ = nullptr;
        verticalHint_ = nullptr;
        tiltHint_ = nullptr;
        sizeHint_ = nullptr;
        curvedScreenToggle_ = nullptr;
        maintainCurveAspectToggle_ = nullptr;
        curvatureSlider_ = nullptr;
        transparencyToggle_ = nullptr;
        videoOpacitySlider_ = nullptr;
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
        ffmpeg9Toggle_ = nullptr;
        hardwareDecodingToggle_ = nullptr;
        automaticPerformanceToggle_ = nullptr;
        automaticPerformanceWarningModal_ = nullptr;
        automaticPerformanceThresholdSlider_ = nullptr;
        automaticPerformanceResponseSlider_ = nullptr;
        performancePanelResetButton_ = nullptr;
        performanceDiagnosticsToggle_ = nullptr;
        powerBenchmarkToggle_ = nullptr;
        nightlyUpdatesToggle_ = nullptr;
        nightlyWarningModal_ = nullptr;
        localVideoInstructionsModal_ = nullptr;
        resetConfirmationModal_ = nullptr;
        advancedWarningModal_ = nullptr;
        advancedWarningText_ = nullptr;
        undockWarningModal_ = nullptr;
        unsavedScreenModal_ = nullptr;
        pendingScreenNavigation_ = {};
        updaterButton_ = nullptr;
        updaterHoverHint_ = nullptr;
        updaterStatus_ = nullptr;
        resetButton_ = nullptr;
        errorModal_ = nullptr;
        errorModalText_ = nullptr;
        showcaseButton_ = nullptr;
        showcaseStatus_ = nullptr;

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
            "Close",
            UnityEngine::Vector2{0.0f, 0.0f},
            UnityEngine::Vector2{18.0f, 8.0f},
            [this, callback = std::move(onBack)]()
            {
                RequestLeave(callback);
            });
        if(backButton)
        {
            BSML::Lite::SetButtonTextSize(backButton, 3.2f);
            if(auto* layout = backButton->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredWidth(18.0f);
                layout->set_preferredHeight(8.0f);
            }
            BSML::Lite::AddHoverHint(
                backButton,
                "Closes the Big Screen menu and returns to Beat Saber's main menu. If a screen has unsaved placement changes, Big Screen asks what to do first.");
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

        // A native segmented control keeps the five categories visible while
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
            ErrorManager::Instance().RecordError(
                "Creating Big Screen settings tabs",
                "Beat Saber did not create every tab scroll container");
            return;
        }
        generalContentRoot_ = generalContainer;

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

        auto* showcaseSectionTitle = BSML::Lite::CreateText(
            storageContainer, "Showcase", 4.2f);
        if(showcaseSectionTitle)
        {
            showcaseSectionTitle->set_fontStyle(TMPro::FontStyles::Bold);
            showcaseSectionTitle->set_alignment(
                TMPro::TextAlignmentOptions::Center);
            showcaseSectionTitle->set_color({0.35f, 0.85f, 1.0f, 1.0f});
            if(auto* layout = showcaseSectionTitle->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredHeight(5.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }
        showcaseButton_ = BSML::Lite::CreateUIButton(
            storageContainer,
            "Play Big Screen Showcase",
            {0.0f, 0.0f},
            {42.0f, 8.0f},
            [callback = std::move(onShowShowcase)]()
            {
                if(callback)
                    callback();
            });
        BSML::Lite::SetButtonTextSize(showcaseButton_, 2.8f);
        BSML::Lite::AddHoverHint(
            showcaseButton_,
            "Opens a readiness page where you can check requirements, download missing showcase assets, and start Lawless Expert+.");
        showcaseStatus_ = BSML::Lite::CreateText(
            storageContainer, "", 2.5f);
        if(showcaseStatus_)
        {
            showcaseStatus_->set_enableWordWrapping(true);
            showcaseStatus_->set_alignment(
                TMPro::TextAlignmentOptions::Center);
            if(auto* layout = showcaseStatus_->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
                layout->set_preferredHeight(5.0f);
        }
        auto* showcaseSectionSpacer = BSML::Lite::CreateText(
            storageContainer, "", 1.0f,
            {0.0f, 0.0f}, {48.0f, 2.0f});
        if(auto* spacerLayout = showcaseSectionSpacer->get_gameObject()
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
                PlaybackSession::Instance().RefreshPlaybackFpsLimitLive();
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
            "Sets the highest resolution used during playback. It never changes which download choices are offered or replaces the saved video. Lower-resolution videos are not enlarged. 720p is recommended; 1080p and 1440p may reduce performance and battery life. 1440p requires hardware decoding.");

        ffmpeg9Toggle_ = BSML::Lite::CreateToggle(
            performanceParent,
            "Use FFmpeg 9",
            settings.UseFfmpeg9(),
            [](bool enabled)
            {
                Settings::Instance().SetUseFfmpeg9(enabled);
                // A decoder cannot change ABI while its worker owns FFmpeg
                // structures. Reopen an active library preview at its retained
                // time; normal song-menu and gameplay sessions use the choice
                // the next time they start.
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            ffmpeg9Toggle_,
            "Experimental: selects the default FFmpeg 9.0.1 playback runtime. Turn this off to use FFmpeg 4.4.8 for compatibility or side-by-side testing. An active Video Library preview restarts at the same position; gameplay uses the selection on the next map. This does not change or redownload the video.");

        hardwareDecodingToggle_ = BSML::Lite::CreateToggle(
            performanceParent,
            "Hardware Video Decoding",
            settings.HardwareDecodingEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetHardwareDecodingEnabled(enabled);
                // Decoder ownership and pixel format are fixed when an FFmpeg
                // context opens. Reuse the proven preview recreation path so
                // the experiment changes immediately in the Video Library,
                // while gameplay adopts it only on the next map.
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            hardwareDecodingToggle_,
            "Uses the Quest's dedicated MediaCodec decoders by default to reduce CPU work and decode latency. H.264, VP8, and VP9 at 1080p or lower can fall back to software. H.265 and video above 1080p require hardware and stop video safely if it fails. Turn this off to force supported videos through software decoding. An active Video Library preview restarts immediately; gameplay uses the setting on the next map.");

        automaticPerformanceToggle_ = BSML::Lite::CreateToggle(
            performanceParent,
            "Automatic Performance",
            settings.AutomaticPerformanceEnabled(),
            [this](bool enabled)
            {
                if(suppressAutomaticPerformanceCallback_)
                    return;
                if(enabled)
                {
                    // Keep both the persisted setting and its dependent
                    // sliders disabled until the player accepts the warning.
                    // This mirrors the nightly/advanced confirmation contract:
                    // opening a modal must never silently opt into a feature.
                    suppressAutomaticPerformanceCallback_ = true;
                    SetToggleWithoutNotification(
                        automaticPerformanceToggle_, false);
                    suppressAutomaticPerformanceCallback_ = false;
                    if(automaticPerformanceWarningModal_)
                        automaticPerformanceWarningModal_->Show();
                    return;
                }
                Settings::Instance().SetAutomaticPerformanceEnabled(false);
                RefreshEnabledState();
            });
        BSML::Lite::AddHoverHint(
            automaticPerformanceToggle_,
            "Experimental: continuously adjusts video quality while a map is playing. Sustained frame loss lowers frame rate and then resolution one step at a time; sustained recovery restores those exact steps in reverse order. It never exceeds your saved quality choices.");
        automaticPerformanceThresholdSlider_ = BSML::Lite::CreateSliderSetting(
            performanceParent,
            "Frame Rate Loss Trigger",
            1.0f,
            static_cast<float>(settings.AutomaticPerformanceThreshold()),
            1.0f,
            15.0f,
            0.15f,
            true,
            {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetAutomaticPerformanceThreshold(
                    static_cast<int>(std::round(value)));
            });
        automaticPerformanceThresholdSlider_->isInt = true;
        automaticPerformanceThresholdSlider_->digits = 0;
        automaticPerformanceThresholdSlider_->slider->UpdateVisuals();
        BSML::Lite::AddHoverHint(
            automaticPerformanceThresholdSlider_,
            "Sets the video frame-rate loss that triggers Automatic Performance. At or above this percentage, quality moves down one step; below it, quality can move back up one step. The response-time setting controls how long either condition must continue.");
        automaticPerformanceResponseSlider_ = BSML::Lite::CreateSliderSetting(
            performanceParent,
            "Scaling Response Time",
            0.1f,
            settings.AutomaticPerformanceResponseSeconds(),
            0.5f,
            10.0f,
            0.15f,
            true,
            {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetAutomaticPerformanceResponseSeconds(value);
            });
        automaticPerformanceResponseSlider_->digits = 1;
        automaticPerformanceResponseSlider_->slider->UpdateVisuals();
        BSML::Lite::AddHoverHint(
            automaticPerformanceResponseSlider_,
            "Sets how long video performance must remain at or above the trigger before quality drops one step, or below it before quality rises one step. Automatic Performance keeps reevaluating this interval throughout the map.");
        // Keep the recovery action visibly attached to the diagnostics toggle.
        // The panel is freely movable in six degrees, so this one-click default
        // is the escape hatch if it is accidentally dragged out of reach.
        auto* diagnosticsRow = BSML::Lite::CreateHorizontalLayoutGroup(
            performanceParent);
        if(diagnosticsRow)
        {
            diagnosticsRow->set_spacing(2.0f);
            diagnosticsRow->set_childControlWidth(true);
            diagnosticsRow->set_childControlHeight(true);
            diagnosticsRow->set_childForceExpandWidth(false);
            diagnosticsRow->set_childForceExpandHeight(false);
            diagnosticsRow->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
            if(auto* layout = diagnosticsRow->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredHeight(8.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }
        const BSML::Lite::TransformWrapper diagnosticsParent = diagnosticsRow
            ? BSML::Lite::TransformWrapper(diagnosticsRow)
            : performanceParent;
        performancePanelResetButton_ = BSML::Lite::CreateUIButton(
            diagnosticsParent, "↻", {0.0f, 0.0f}, {8.0f, 8.0f},
            []()
            {
                PerformancePanel::Instance().ResetPlacement();
                PaperLogger.info("Reset performance panel placement to defaults");
            });
        BSML::Lite::SetButtonTextSize(
            performancePanelResetButton_, ResetGlyphTextSize);
        if(auto* layout = performancePanelResetButton_->get_gameObject()
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
        {
            layout->set_minWidth(ResetButtonSize);
            layout->set_preferredWidth(ResetButtonSize);
            layout->set_preferredHeight(ResetButtonSize);
            layout->set_flexibleWidth(0.0f);
        }
        constexpr const char* PerformanceResetHint =
            "Resets the performance panel to its default position and angle. The same placement is used in the Video Library and during video gameplay.";
        BSML::Lite::AddHoverHint(
            performancePanelResetButton_,
            PerformanceResetHint);

        performanceDiagnosticsToggle_ = BSML::Lite::CreateToggle(
            diagnosticsParent,
            "Show Performance Information",
            settings.PerformanceDiagnosticsEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetPerformanceDiagnosticsEnabled(enabled);
                PerformancePanel::Instance().SetEnabled(enabled);
            });
        if(auto* layout = performanceDiagnosticsToggle_->get_gameObject()
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
        {
            layout->set_minWidth(82.0f);
            layout->set_preferredWidth(82.0f);
            layout->set_flexibleWidth(1.0f);
        }
        // Attach the reset button directly beside the toggle control. This is
        // the same overlay pattern used by the screen-layout reset below: it
        // keeps the glyph next to the interactive control instead of placing it
        // beside the row label, and it consumes no additional row width.
        if(performancePanelResetButton_ && performanceDiagnosticsToggle_)
        {
            auto resetRect = performancePanelResetButton_->get_transform()
                .cast<UnityEngine::RectTransform>();
            resetRect->SetParent(
                performanceDiagnosticsToggle_->get_transform(), false);
            resetRect->set_anchorMin({0.0f, 0.5f});
            resetRect->set_anchorMax({0.0f, 0.5f});
            resetRect->set_pivot({1.0f, 0.5f});
            resetRect->set_anchoredPosition({-1.5f, 0.0f});
            resetRect->set_sizeDelta({ResetButtonSize, ResetButtonSize});
            resetRect->SetAsLastSibling();
        }
        constexpr const char* PerformanceToggleHint =
            "Shows a movable performance panel in the Video Library and during video maps. Hold the trigger anywhere on the panel to move and angle it. Its placement is saved when you turn it off or leave the menu, then reused during gameplay. Completed, failed, and exited video maps are appended to the performance log.";
        auto* performanceToggleHoverHint = BSML::Lite::AddHoverHint(
            performanceDiagnosticsToggle_,
            PerformanceToggleHint);
        if(performancePanelResetButton_ && performanceToggleHoverHint)
        {
            performancePanelResetButton_->get_gameObject()
                ->AddComponent<NestedHoverHintOverride*>()
                ->Configure(
                    performanceToggleHoverHint,
                    PerformanceToggleHint,
                    PerformanceResetHint);
        }

        powerBenchmarkToggle_ = BSML::Lite::CreateToggle(
            performanceParent,
            "Record Power Benchmark",
            settings.PowerBenchmarkEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetPowerBenchmarkEnabled(enabled);
            });
        BSML::Lite::AddHoverHint(
            powerBenchmarkToggle_,
            "Records one-second Quest battery/current readings, decoder CPU time, whole-game CPU time, and playback statistics for every played map. Results are saved as CSV files in Big Screen's Logs folder after the map ends. For a meaningful battery comparison, unplug external power and play the same map once with Video In Map on and once with it off.");

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
                ErrorManager::Instance().Guard(
                    "updating menu placement visuals after master toggle", []()
                    {
                        MenuPlacementGuide::Instance().Apply();
                        MenuEnvironmentVisibility::Instance().Apply();
                    });

                // Hooks remain installed so the menu stays reachable, but
                // disabling immediately tears down every screen and decoder.
                SelectionVideoToggle::Instance().ModEnabledChanged(enabled);
                ScreenPreview::Instance().SetEnabled(enabled);
                PerformancePanel::Instance().SetEnabled(
                    enabled && Settings::Instance().PerformanceDiagnosticsEnabled());
                RefreshControls();
                PaperLogger.info("Big Screen switched {}", enabled ? "on" : "off");
            });
        BSML::Lite::AddHoverHint(
            modEnabledToggle_,
            "Turns Big Screen playback, previews, downloads, and environment changes on or off. This menu remains available so the mod can be turned back on.");

        // ToggleSetting may insert an intermediate layout object. Remember the
        // direct child of the General content container that owns the master
        // switch so disabled mode can hide every sibling without relying on a
        // fragile handwritten list of current and future General controls.
        if(modEnabledToggle_ && generalContentRoot_)
        {
            auto containerTransform = generalContentRoot_->get_transform();
            auto masterTransform = modEnabledToggle_->get_transform();
            while(masterTransform && masterTransform->get_parent() &&
                  masterTransform->get_parent() != containerTransform)
                masterTransform = masterTransform->get_parent();
            if(masterTransform && masterTransform->get_parent() == containerTransform)
                generalMasterRoot_ = masterTransform->get_gameObject();
        }

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

        showMenuEnvironmentToggle_ = BSML::Lite::CreateToggle(
            generalContainer,
            "Show Menu Environment",
            settings.ShowMenuEnvironment(),
            [](bool enabled)
            {
                Settings::Instance().SetShowMenuEnvironment(enabled);
                ErrorManager::Instance().Guard(
                    "updating Show Menu Environment", []()
                    {
                        // The one switch owns scenery, lighting, and floor.
                        // The focused floor scan catches compatible geometry
                        // outside Beat Saber's resolved environment root.
                        MenuEnvironmentVisibility::Instance().Apply();
                        MenuPlacementGuide::Instance().Apply();
                        MenuEnvironmentVisibility::Instance().Apply();
                    });
            });
        BSML::Lite::AddHoverHint(
            showMenuEnvironmentToggle_,
            "Shows Beat Saber's normal menu scenery, lighting, and floor behind Big Screen. Turn this off for an unlit, unobstructed placement space that also keeps screens visible below floor height. Big Screen's menus, video screen, lane guides, and controller input remain active, and the environment is restored when you leave.");

        showLaneGuidesToggle_ = BSML::Lite::CreateToggle(
            generalContainer,
            "Show Lane Guides",
            settings.ShowLaneGuidesEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetShowLaneGuidesEnabled(enabled);
                ErrorManager::Instance().Guard(
                    "updating Show Lane Guides", [this]()
                    {
                        if(!MenuPlacementGuide::Instance().Apply())
                        {
                            // Guide creation is optional. If Unity rejects it,
                            // Apply saves Off and this mirrors that safe value
                            // without invoking the toggle callback a second time.
                            SetToggleWithoutNotification(
                                showLaneGuidesToggle_,
                                Settings::Instance().ShowLaneGuidesEnabled());
                        }
                    });
            });
        BSML::Lite::AddHoverHint(
            showLaneGuidesToggle_,
            "Shows thin, non-interactive lane rails, depth marks, and a player-origin marker while Big Screen's menu is open. This can be used whether the menu floor or full environment is shown or hidden, and never changes gameplay.");

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
            [this](bool enabled)
            {
                Settings::Instance().SetMenuPreviewEnabled(enabled);
                SelectionVideoToggle::Instance().MenuPreviewPreferenceChanged();
                RefreshControls();
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

        // Treat the per-layout reset and selector as one deliberate control
        // group. The compact square button uses Beat Saber's rounded button
        // background, producing the familiar circular-reset treatment while
        // leaving enough separation that it cannot be mistaken for an arrow
        // belonging to the dropdown itself.
        auto* screenLayoutRow = BSML::Lite::CreateHorizontalLayoutGroup(
            screenContainer);
        if(screenLayoutRow)
        {
            screenLayoutRow->set_spacing(2.0f);
            screenLayoutRow->set_childControlWidth(true);
            screenLayoutRow->set_childControlHeight(true);
            screenLayoutRow->set_childForceExpandWidth(false);
            screenLayoutRow->set_childForceExpandHeight(false);
            screenLayoutRow->set_childAlignment(
                UnityEngine::TextAnchor::MiddleCenter);
            if(auto* layout = screenLayoutRow->get_gameObject()
                   ->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_minWidth(90.0f);
                layout->set_preferredWidth(90.0f);
                layout->set_preferredHeight(8.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }
        const BSML::Lite::TransformWrapper screenLayoutParent = screenLayoutRow
            ? BSML::Lite::TransformWrapper(screenLayoutRow)
            : BSML::Lite::TransformWrapper(screenContainer);

        screenLayoutResetButton_ = BSML::Lite::CreateUIButton(
            screenLayoutParent, "↻", {0.0f, 0.0f}, {8.0f, 8.0f},
            [this]()
            {
                Settings::Instance().ResetActiveScreenLayout();

                // Beat Saber's animated switch graphic listens to the native
                // Toggle change event. SetToggleWithoutNotification updates
                // the stored/native bool but deliberately suppresses that
                // event, leaving a reset switch visibly On even though its
                // dependent controls already reflect Off. A per-layout reset
                // is a discrete user action, so drive every affected switch
                // through ToggleSetting's normal visual/callback pipeline.
                const auto applyLayoutToggle = [](
                    BSML::ToggleSetting* control, bool value)
                {
                    if(control)
                        control->set_Value(value);
                };
                const auto& defaults = Settings::Instance();
                // Apply ordinary presentation toggles before the two controls
                // whose Off callbacks perform a full RefreshControls pass.
                // Otherwise that early pass writes the remaining native bools
                // without notification and consumes their visual transition.
                applyLayoutToggle(
                    curvedScreenToggle_,
                    defaults.CurvedScreenEnabled());
                applyLayoutToggle(
                    maintainCurveAspectToggle_,
                    defaults.MaintainCurveAspectRatio());
                applyLayoutToggle(
                    transparencyToggle_,
                    defaults.LetterboxTransparencyEnabled());
                applyLayoutToggle(
                    stretchVideoToggle_,
                    defaults.StretchVideoToFit());

                // The settings model is already authoritative after Reset.
                // Suppress only Big Screen's expensive/re-entrant callbacks;
                // ToggleSetting still emits the native event that updates the
                // animated switch graphic.
                suppressAdvancedCallback_ = true;
                applyLayoutToggle(
                    advancedOptionsToggle_,
                    defaults.AdvancedOptionsEnabled());
                suppressAdvancedCallback_ = false;
                suppressUndockCallback_ = true;
                applyLayoutToggle(
                    undockScreenToggle_,
                    defaults.UndockedScreenEnabled());
                suppressUndockCallback_ = false;
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshControls();
                PaperLogger.info(
                    "Reset screen layout {} to defaults",
                    Settings::Instance().ActiveScreenLayout() + 1);
            });
        BSML::Lite::SetButtonTextSize(
            screenLayoutResetButton_, ResetGlyphTextSize);
        if(auto* layout = screenLayoutResetButton_
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
        {
            layout->set_minWidth(ResetButtonSize);
            layout->set_preferredWidth(ResetButtonSize);
            layout->set_preferredHeight(ResetButtonSize);
            layout->set_flexibleWidth(0.0f);
        }
        constexpr const char* ScreenLayoutResetHint =
            "Resets the currently selected screen layout to its default settings. Your other screen layouts are not changed.";
        BSML::Lite::AddHoverHint(
            screenLayoutResetButton_,
            ScreenLayoutResetHint);

        screenLayoutDropdown_ = BSML::Lite::CreateDropdown(
            screenLayoutParent,
            "Editing Screen Layout",
            "Layout " + std::to_string(settings.ActiveScreenLayout() + 1),
            ScreenLayoutChoices,
            [this](StringW value)
            {
                const std::string label(value);
                const int index = !label.empty() && label.back() >= '1' && label.back() <= '5'
                    ? label.back() - '1' : 0;
                if(ScreenPreview::Instance().IsUndockedEditing())
                    ScreenPreview::Instance().StageCurrentUndockedPlacement();
                Settings::Instance().SetActiveScreenLayout(index);
                ApplyDisplaySettingsAndRefreshPreview();
                SelectionVideoToggle::Instance().ScreenLayoutPreferenceChanged();
                RefreshControls();
            });
        if(auto* layout = screenLayoutDropdown_->get_gameObject()
               ->GetComponent<UnityEngine::UI::LayoutElement*>())
        {
            layout->set_minWidth(80.0f);
            layout->set_preferredWidth(80.0f);
            layout->set_flexibleWidth(1.0f);
        }
        // DropdownListSetting's transform is the selector box, while its row
        // label is owned by the surrounding settings prefab. Parent the reset
        // glyph to that selector, then anchor it just outside the selector's
        // left edge. Center anchoring put the glyph directly over the current
        // Layout value; making it an extra layout child put it before the row
        // label. This overlay placement changes neither row width nor clipping.
        if(screenLayoutResetButton_ && screenLayoutDropdown_)
        {
            auto resetRect = screenLayoutResetButton_->get_transform()
                .cast<UnityEngine::RectTransform>();
            resetRect->SetParent(screenLayoutDropdown_->get_transform(), false);
            resetRect->set_anchorMin({0.0f, 0.5f});
            resetRect->set_anchorMax({0.0f, 0.5f});
            resetRect->set_pivot({1.0f, 0.5f});
            resetRect->set_anchoredPosition({-1.5f, 0.0f});
            resetRect->set_sizeDelta({ResetButtonSize, ResetButtonSize});
            resetRect->SetAsLastSibling();
        }
        constexpr const char* ScreenLayoutDropdownHint =
            "Chooses which of your five saved layouts is active and which layout the controls below edit. It is used for previews and the next video map.";
        auto* screenLayoutDropdownHoverHint = BSML::Lite::AddHoverHint(
            screenLayoutDropdown_,
            ScreenLayoutDropdownHint);
        if(screenLayoutResetButton_ && screenLayoutDropdownHoverHint)
        {
            screenLayoutResetButton_->get_gameObject()
                ->AddComponent<NestedHoverHintOverride*>()
                ->Configure(
                    screenLayoutDropdownHoverHint,
                    ScreenLayoutDropdownHint,
                    ScreenLayoutResetHint);
        }

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

        distanceSetting_ = BSML::Lite::CreateSliderSetting(
            screenContainer,
            "Screen Distance Offset",
            2.0f,
            settings.ScreenDistanceOffset(),
            -180.0f,
            180.0f,
            0.15f,
            true,
            {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetScreenDistanceOffset(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        // BSML only infers integer formatting for a 1.0 increment. Distance
        // intentionally advances by 2 units, so declare its integer display.
        distanceSetting_->isInt = true;
        distanceSetting_->digits = 0;
        distanceHint_ = BSML::Lite::AddHoverHint(
            distanceSetting_,
            "Moves the screen closer with negative values and farther away with positive values. Free positioning replaces this control while Undock Screen is enabled.");

        horizontalSetting_ = BSML::Lite::CreateSliderSetting(
            screenContainer,
            "Screen X Offset",
            1.0f,
            settings.ScreenHorizontalOffset(),
            -180.0f,
            180.0f,
            0.15f,
            true,
            {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetScreenHorizontalOffset(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        horizontalHint_ = BSML::Lite::AddHoverHint(
            horizontalSetting_,
            "Moves the screen left with negative values and right with positive values. Free positioning replaces this control while Undock Screen is enabled.");

        verticalSetting_ = BSML::Lite::CreateSliderSetting(
            screenContainer,
            "Screen Y Offset",
            1.0f,
            settings.ScreenVerticalOffset(),
            -180.0f,
            180.0f,
            0.15f,
            true,
            {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetScreenVerticalOffset(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        verticalHint_ = BSML::Lite::AddHoverHint(
            verticalSetting_,
            "Moves the screen down with negative values and up with positive values. The expanded range lets even an 8x screen clear the menu floor. Free positioning replaces this control while Undock Screen is enabled.");

        tiltSetting_ = BSML::Lite::CreateSliderSetting(
            screenContainer,
            "Screen Tilt Offset",
            1.0f,
            settings.ScreenTiltOffset(),
            -180.0f,
            180.0f,
            0.15f,
            true,
            {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetScreenTiltOffset(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        tiltHint_ = BSML::Lite::AddHoverHint(
            tiltSetting_,
            "Adjusts the screen's vertical viewing angle in degrees. Free positioning replaces this control while Undock Screen is enabled.");

        sizeSetting_ = BSML::Lite::CreateSliderSetting(
            screenContainer,
            "Screen Size Multiplier",
            0.1f,
            settings.ScreenScale(),
            0.5f,
            settings.MaximumScreenScale(),
            0.15f,
            true,
            {0.0f, 0.0f},
            [this](float value)
            {
                Settings::Instance().SetScreenScale(value);
                // Reevaluate the maximum arrow after every step. Reaching the
                // cap hides it; one decrement makes it available again.
                RefreshCurvatureControl();
                ApplyDisplaySettingsAndRefreshPreview();
            });
        sizeSetting_->digits = 1;
        sizeSetting_->slider->UpdateVisuals();
        sizeHint_ = BSML::Lite::AddHoverHint(
            sizeSetting_,
            "Multiplies the screen's physical size. Flat and curved screens allow values from 0.5 to 8.0. The resize handle replaces this control for an undocked screen.");

        curvedScreenToggle_ = BSML::Lite::CreateToggle(
            screenContainer,
            "Curved Screen",
            settings.CurvedScreenEnabled(),
            [this](bool enabled)
            {
                Settings::Instance().SetCurvedScreenEnabled(enabled);
                // Both modes currently share the same 8x cap, but refreshing
                // keeps the slider range correct if their limits intentionally
                // diverge again in a future release.
                RefreshCurvatureControl();
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            curvedScreenToggle_,
            "Switches between a flat screen and a curved screen. Turning this on reveals the curve controls below; both modes support screen multipliers up to 8.0.");

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

        // VIDEO OPACITY MAINTENANCE NOTE:
        // This is a normal native BSML slider and is intentionally outside the
        // advanced group. Keep its value text and handle under TextSlider's
        // ownership; redraw it only after the Screen tab becomes visible.
        videoOpacitySlider_ = BSML::Lite::CreateSliderSetting(
            screenContainer,
            "Video Opacity",
            0.05f,
            settings.VideoOpacity(),
            0.0f,
            1.0f,
            0.15f,
            true,
            {0.0f, 0.0f},
            [](float value)
            {
                Settings::Instance().SetVideoOpacity(value);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        videoOpacitySlider_->digits = 2;
        BSML::Lite::AddHoverHint(
            videoOpacitySlider_,
            "Controls the opacity of the video picture for this screen layout. 1.00 is fully opaque; lower values let the environment show through. This applies to both docked and undocked screens.");

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
            {
                // Top-level BSML settings rows are 90 units wide. Explicitly
                // give the nested advanced section that same width; relying on
                // flexible width allowed the scroll layout to compress it to
                // the slider's 52-unit control, which displaced numeric labels
                // and clipped the fine-adjustment arrows.
                layout->set_minWidth(90.0f);
                layout->set_preferredWidth(90.0f);
                layout->set_flexibleWidth(1.0f);
            }
        }
        const BSML::Lite::TransformWrapper advancedParent = advancedGroup
            ? BSML::Lite::TransformWrapper(advancedGroup)
            : BSML::Lite::TransformWrapper(screenContainer);

        // SCREEN ROTATION MAINTENANCE NOTE:
        // This is a native BSML TextSlider. Its value text and thin vertical
        // drag handle are separate objects managed by TextSlider::UpdateVisuals.
        // Do not reparent either object and do not add a second handle graphic.
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

        // VIDEO ROTATION MAINTENANCE NOTE:
        // Rotation and Tilt retain their signed native ranges, so their value
        // text needs the separate handle-relative correction below. Video X/Y
        // do not use this path: they deliberately clone Video Zoom's working
        // positive native range and translate that value to a signed offset.
        videoRotationSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video Rotation", 1.0f, settings.VideoRotation(),
            -180.0f, 180.0f, 0.15f, true, {0.0f, 0.0f},
            [this](float value)
            {
                Settings::Instance().SetVideoRotation(value);
                ApplyDisplaySettingsAndRefreshPreview();
                AlignVideoValueLabels();
            });
        BSML::Lite::AddHoverHint(
            videoRotationSlider_,
            "Rotates the picture inside the screen without rotating or reshaping the screen itself. Empty areas use the Letterbox Transparency setting.");

        // VIDEO ZOOM MAINTENANCE NOTE -- VERIFIED-GOOD REFERENCE CONTROL:
        // Its native numeric value follows the drag handle correctly. Preserve
        // this control unchanged and use its final rendered text-to-handle
        // offset as the reference for the signed-range video controls.
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

        // X and Y are deliberately created with Video Zoom's exact native
        // configuration. The native control owns its text/handle positioning;
        // RefreshVideoOffsetValueTexts changes only the rendered number after
        // UpdateVisuals has placed it at the handle.
        videoHorizontalSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video X Position", 0.05f,
            VideoOffsetToZoomSlider(settings.VideoOffsetX()),
            0.5f, 3.0f, 0.15f, true, {0.0f, 0.0f},
            [this](float value)
            {
                Settings::Instance().SetVideoOffsetX(
                    ZoomSliderToVideoOffset(value));
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshVideoOffsetValueTexts();
            });
        BSML::Lite::AddHoverHint(
            videoHorizontalSlider_,
            "Moves the picture left or right inside the fixed screen frame. This is useful after zooming or rotating a video.");

        videoVerticalSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video Y Position", 0.05f,
            VideoOffsetToZoomSlider(settings.VideoOffsetY()),
            0.5f, 3.0f, 0.15f, true, {0.0f, 0.0f},
            [this](float value)
            {
                Settings::Instance().SetVideoOffsetY(
                    ZoomSliderToVideoOffset(value));
                ApplyDisplaySettingsAndRefreshPreview();
                RefreshVideoOffsetValueTexts();
            });
        BSML::Lite::AddHoverHint(
            videoVerticalSlider_,
            "Moves the picture down or up inside the fixed screen frame. This is useful after zooming or rotating a video.");
        RefreshVideoOffsetValueTexts();

        // VIDEO TILT MAINTENANCE NOTE:
        // Tilt uses the same signed-range, handle-relative correction as Video
        // Rotation. It does not alter or reorder the native handle graphic.
        videoTiltSlider_ = BSML::Lite::CreateSliderSetting(
            advancedParent, "Video Tilt", 1.0f, settings.VideoTilt(),
            -75.0f, 75.0f, 0.15f, true, {0.0f, 0.0f},
            [this](float value)
            {
                Settings::Instance().SetVideoTilt(value);
                ApplyDisplaySettingsAndRefreshPreview();
                AlignVideoValueLabels();
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
            "Letterbox Transparency",
            settings.LetterboxTransparencyEnabled(),
            [](bool enabled)
            {
                Settings::Instance().SetLetterboxTransparencyEnabled(enabled);
                ApplyDisplaySettingsAndRefreshPreview();
            });
        BSML::Lite::AddHoverHint(
            transparencyToggle_,
            "Makes unused letterbox areas transparent when the video does not fill the screen. It does not fade the picture; use Video Opacity for that. This applies to both docked and undocked screens.");

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

        automaticPerformanceWarningModal_ = BSML::Lite::CreateModal(
            viewController, {72.0f, 38.0f}, nullptr, false);
        auto* automaticPerformanceWarningText = BSML::Lite::CreateText(
            automaticPerformanceWarningModal_,
            "Enable Automatic Performance?\n\nAutomatic Performance is an experimental feature that is still under development. It can lower and restore video frame rate and resolution during a map when sustained frame loss is detected. Results may vary by video, map, and headset.",
            TMPro::FontStyles::Normal,
            3.0f,
            {0.0f, 6.0f},
            {64.0f, 23.0f});
        automaticPerformanceWarningText->set_enableWordWrapping(true);
        automaticPerformanceWarningText->set_enableAutoSizing(true);
        automaticPerformanceWarningText->set_fontSizeMin(2.5f);
        automaticPerformanceWarningText->set_fontSizeMax(3.0f);
        automaticPerformanceWarningText->set_overflowMode(
            TMPro::TextOverflowModes::Ellipsis);
        automaticPerformanceWarningText->set_alignment(
            TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            automaticPerformanceWarningModal_->get_transform(), "Cancel",
            {18.0f, -28.0f}, {25.0f, 8.0f},
            [this]()
            {
                if(automaticPerformanceWarningModal_)
                    automaticPerformanceWarningModal_->Hide();
                suppressAutomaticPerformanceCallback_ = true;
                SetToggleWithoutNotification(
                    automaticPerformanceToggle_, false);
                suppressAutomaticPerformanceCallback_ = false;
                RefreshEnabledState();
            });
        BSML::Lite::CreateUIButton(
            automaticPerformanceWarningModal_->get_transform(), "Enable",
            {48.0f, -28.0f}, {27.0f, 8.0f},
            [this]()
            {
                Settings::Instance().SetAutomaticPerformanceEnabled(true);
                suppressAutomaticPerformanceCallback_ = true;
                SetToggleWithoutNotification(
                    automaticPerformanceToggle_, true);
                suppressAutomaticPerformanceCallback_ = false;
                if(automaticPerformanceWarningModal_)
                    automaticPerformanceWarningModal_->Hide();
                RefreshEnabledState();
            });

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
            "Undock this screen?\n\nFree placement is an advanced feature. Keep the screen clear of Beat Saber's menus and use a comfortable size and distance. Big Screen asks before leaving with unsaved edits; opening the Quest system menu safely discards them.",
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

        unsavedScreenModal_ = BSML::Lite::CreateModal(
            viewController, {78.0f, 36.0f}, nullptr, false);
        auto* unsavedText = BSML::Lite::CreateText(
            unsavedScreenModal_,
            "Save screen changes?\n\nThe unlocked screen has changes that have not been saved. Save them before leaving, discard them, or keep editing.",
            TMPro::FontStyles::Normal,
            3.2f,
            {0.0f, 5.0f},
            {70.0f, 20.0f});
        unsavedText->set_enableWordWrapping(true);
        unsavedText->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            unsavedScreenModal_->get_transform(), "Discard",
            {14.0f, -25.0f}, {20.0f, 8.0f},
            [this]()
            {
                if(unsavedScreenModal_)
                    unsavedScreenModal_->Hide();
                ScreenPreview::Instance().CancelUndockedEditing();
                auto continuation = std::move(pendingScreenNavigation_);
                pendingScreenNavigation_ = {};
                if(continuation)
                    continuation();
            });
        BSML::Lite::CreateUIButton(
            unsavedScreenModal_->get_transform(), "Keep Editing",
            {39.0f, -25.0f}, {25.0f, 8.0f},
            [this]()
            {
                if(unsavedScreenModal_)
                    unsavedScreenModal_->Hide();
                pendingScreenNavigation_ = {};
                if(settingsTabs_)
                    settingsTabs_->SelectCellWithNumber(1);
            });
        BSML::Lite::CreateUIButton(
            unsavedScreenModal_->get_transform(), "Save",
            {65.0f, -25.0f}, {18.0f, 8.0f},
            [this]()
            {
                if(unsavedScreenModal_)
                    unsavedScreenModal_->Hide();
                ScreenPreview::Instance().SaveUndockedEditing();
                auto continuation = std::move(pendingScreenNavigation_);
                pendingScreenNavigation_ = {};
                if(continuation)
                    continuation();
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
            "For a custom or WIP map, copy a compatible MP4 or WebM video into that map's folder.\n\n"
            "For any song—including OST and DLC—copy the video into:\n"
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
            "Explains how to assign a compatible MP4 or WebM video from a map folder or Big Screen's Video Import folder.");

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

    void SettingsMenu::ForgetUi()
    {
        // SettingsMenu owns no worker or persistent resource. Replacing its
        // value state is the safest exhaustive reset: newly added cached UI
        // fields cannot accidentally be omitted from a hand-maintained list.
        *this = SettingsMenu{};
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
        RefreshDisabledModeView();
    }

    void SettingsMenu::RefreshDisabledModeView()
    {
        const bool enabled = Settings::Instance().ModEnabled();

        // Disabled mode is deliberately not a merely greyed-out full menu.
        // Collapse General to its master switch and remove navigation to every
        // feature that cannot operate, making the recovery action unambiguous.
        if(generalContentRoot_ && generalMasterRoot_)
        {
            auto content = generalContentRoot_->get_transform();
            const int childCount = content->get_childCount();
            for(int index = 0; index < childCount; ++index)
            {
                auto child = content->GetChild(index);
                if(child)
                    child->get_gameObject()->SetActive(
                        enabled || child->get_gameObject().ptr() == generalMasterRoot_);
            }
        }

        if(!enabled)
        {
            selectedTab_ = 0;
            for(int page = 0; page < static_cast<int>(tabViewRoots_.size()); ++page)
                if(tabViewRoots_[page])
                    tabViewRoots_[page]->SetActive(page == 0);
        }
        if(settingsTabs_)
            settingsTabs_->get_gameObject()->SetActive(enabled);

        if(!displayedEnabledStateKnown_ || displayedEnabledState_ != enabled)
        {
            displayedEnabledState_ = enabled;
            displayedEnabledStateKnown_ = true;
            if(modEnabledUiChanged_)
                modEnabledUiChanged_(enabled);
        }
    }

    void SettingsMenu::RefreshValues()
    {
        const auto& settings = Settings::Instance();
        SetToggleWithoutNotification(modEnabledToggle_, settings.ModEnabled());
        SetToggleWithoutNotification(
            distractionFreeMenuToggle_,
            settings.DistractionFreeMenu());
        SetToggleWithoutNotification(
            showMenuEnvironmentToggle_, settings.ShowMenuEnvironment());
        SetToggleWithoutNotification(
            showLaneGuidesToggle_, settings.ShowLaneGuidesEnabled());
        SetToggleWithoutNotification(
            advancedOptionsToggle_, settings.AdvancedOptionsEnabled());
        SetToggleWithoutNotification(videoEnabledToggle_, settings.VideoEnabled());
        SetToggleWithoutNotification(previewToggle_, settings.MenuPreviewEnabled());
        SetToggleWithoutNotification(
            allowChromaOverrideToggle_, settings.AllowChromaOverride());
        SetToggleWithoutNotification(
            automaticPerformanceToggle_, settings.AutomaticPerformanceEnabled());
        SetToggleWithoutNotification(ffmpeg9Toggle_, settings.UseFfmpeg9());
        SetToggleWithoutNotification(
            hardwareDecodingToggle_, settings.HardwareDecodingEnabled());
        SetToggleWithoutNotification(
            performanceDiagnosticsToggle_, settings.PerformanceDiagnosticsEnabled());
        SetToggleWithoutNotification(
            powerBenchmarkToggle_, settings.PowerBenchmarkEnabled());
        SetToggleWithoutNotification(curvedScreenToggle_, settings.CurvedScreenEnabled());
        SetToggleWithoutNotification(
            maintainCurveAspectToggle_,
            settings.MaintainCurveAspectRatio());
        SetToggleWithoutNotification(
            transparencyToggle_,
            settings.LetterboxTransparencyEnabled());
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
        if(videoOpacitySlider_)
            videoOpacitySlider_->set_Value(settings.VideoOpacity());
        if(screenRotationSlider_)
            screenRotationSlider_->set_Value(settings.ScreenRoll());
        if(videoRotationSlider_)
            videoRotationSlider_->set_Value(settings.VideoRotation());
        if(videoZoomSlider_)
            videoZoomSlider_->set_Value(settings.VideoZoom());
        if(videoHorizontalSlider_)
            videoHorizontalSlider_->set_Value(
                VideoOffsetToZoomSlider(settings.VideoOffsetX()));
        if(videoVerticalSlider_)
            videoVerticalSlider_->set_Value(
                VideoOffsetToZoomSlider(settings.VideoOffsetY()));
        if(videoTiltSlider_)
            videoTiltSlider_->set_Value(settings.VideoTilt());
        RefreshVideoOffsetValueTexts();
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
                : settings.ResolutionHeight() == 1080 ? 2
                : settings.ResolutionHeight() == 1440 ? 3 : 1;
            resolutionDropdown_->index = index;
            if(resolutionDropdown_->dropdown)
                resolutionDropdown_->dropdown->SelectCellWithIdx(index);
            resolutionDropdown_->UpdateState();
        }
        if(automaticPerformanceThresholdSlider_)
            automaticPerformanceThresholdSlider_->set_Value(
                static_cast<float>(settings.AutomaticPerformanceThreshold()));
        if(automaticPerformanceResponseSlider_)
            automaticPerformanceResponseSlider_->set_Value(
                settings.AutomaticPerformanceResponseSeconds());
    }

    void SettingsMenu::RefreshEnabledState()
    {
        const auto& settings = Settings::Instance();
        const bool enabled = settings.ModEnabled();
        const bool editingScreen =
            ScreenPreview::Instance().IsUndockedEditing();
        const bool dockedGeometryEnabled = enabled &&
            (editingScreen ||
            !(settings.AdvancedOptionsEnabled() &&
              settings.UndockedScreenEnabled()));
        const bool advancedEnabled =
            enabled && (settings.AdvancedOptionsEnabled() || editingScreen);
        const bool lightingChildrenEnabled =
            enabled && settings.MapLightShowEnabled();

        // The master switch and Reset remain usable. Every setting capable of
        // affecting Beat Saber is explicitly locked while the mod is off.
        if(distractionFreeMenuToggle_)
            distractionFreeMenuToggle_->set_interactable(enabled);
        if(showMenuEnvironmentToggle_)
            showMenuEnvironmentToggle_->set_interactable(enabled);
        if(showLaneGuidesToggle_)
            showLaneGuidesToggle_->set_interactable(enabled);
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
        {
            distanceSetting_->set_interactable(dockedGeometryEnabled);
            if(distanceSetting_->text)
                distanceSetting_->text->get_gameObject()->SetActive(
                    dockedGeometryEnabled);
            if(distanceSetting_->slider &&
               distanceSetting_->slider->get_handleRect())
                distanceSetting_->slider->get_handleRect()
                    ->get_gameObject()->SetActive(dockedGeometryEnabled);
        }
        if(horizontalSetting_)
        {
            horizontalSetting_->set_interactable(dockedGeometryEnabled);
            if(horizontalSetting_->text)
                horizontalSetting_->text->get_gameObject()->SetActive(
                    dockedGeometryEnabled);
            if(horizontalSetting_->slider &&
               horizontalSetting_->slider->get_handleRect())
                horizontalSetting_->slider->get_handleRect()
                    ->get_gameObject()->SetActive(dockedGeometryEnabled);
        }
        if(verticalSetting_)
        {
            verticalSetting_->set_interactable(dockedGeometryEnabled);
            if(verticalSetting_->text)
                verticalSetting_->text->get_gameObject()->SetActive(
                    dockedGeometryEnabled);
            if(verticalSetting_->slider &&
               verticalSetting_->slider->get_handleRect())
                verticalSetting_->slider->get_handleRect()
                    ->get_gameObject()->SetActive(dockedGeometryEnabled);
        }
        if(tiltSetting_)
        {
            tiltSetting_->set_interactable(dockedGeometryEnabled);
            if(tiltSetting_->text)
                tiltSetting_->text->get_gameObject()->SetActive(
                    dockedGeometryEnabled);
            if(tiltSetting_->slider &&
               tiltSetting_->slider->get_handleRect())
                tiltSetting_->slider->get_handleRect()
                    ->get_gameObject()->SetActive(dockedGeometryEnabled);
        }
        if(sizeSetting_)
        {
            sizeSetting_->set_interactable(dockedGeometryEnabled);
            if(sizeSetting_->text)
                sizeSetting_->text->get_gameObject()->SetActive(
                    dockedGeometryEnabled);
            if(sizeSetting_->slider && sizeSetting_->slider->get_handleRect())
                sizeSetting_->slider->get_handleRect()
                    ->get_gameObject()->SetActive(dockedGeometryEnabled);
        }

        // Hover hints remain useful on disabled canvas rows, but their normal
        // descriptions would imply that the controls should still respond.
        // Explain that the unlocked screen's direct manipulation replaces
        // these controls until the user saves or cancels positioning.
        const char* freePositionHint =
            "This control is unavailable while the screen is unlocked. Move or resize the screen directly, then save or cancel positioning to use this slider again.";
        if(distanceHint_)
            distanceHint_->set_text(dockedGeometryEnabled
                ? "Moves the screen closer with negative values and farther away with positive values. Free positioning replaces this control while Undock Screen is enabled."
                : freePositionHint);
        if(horizontalHint_)
            horizontalHint_->set_text(dockedGeometryEnabled
                ? "Moves the screen left with negative values and right with positive values. Free positioning replaces this control while Undock Screen is enabled."
                : freePositionHint);
        if(verticalHint_)
            verticalHint_->set_text(dockedGeometryEnabled
                ? "Moves the screen down with negative values and up with positive values. Free positioning replaces this control while Undock Screen is enabled."
                : freePositionHint);
        if(tiltHint_)
            tiltHint_->set_text(dockedGeometryEnabled
                ? "Adjusts the screen's vertical viewing angle in degrees. Free positioning replaces this control while Undock Screen is enabled."
                : freePositionHint);
        if(sizeHint_)
            sizeHint_->set_text(dockedGeometryEnabled
                ? "Multiplies the map-authored screen size. Flat and curved screens allow values from 0.5 to 8.0, and a playing preview remains visible while resizing. The resize handle replaces this control for an undocked screen."
                : freePositionHint);
        if(curvedScreenToggle_)
            curvedScreenToggle_->set_interactable(enabled);
        if(screenLayoutDropdown_)
            screenLayoutDropdown_->set_interactable(enabled);
        if(screenLayoutResetButton_)
            screenLayoutResetButton_->set_interactable(enabled);
        if(allowChromaOverrideToggle_)
            allowChromaOverrideToggle_->set_interactable(enabled);
        if(maintainCurveAspectToggle_)
            maintainCurveAspectToggle_->set_interactable(enabled);
        if(curvatureSlider_)
            curvatureSlider_->set_interactable(enabled);
        if(videoOpacitySlider_)
            videoOpacitySlider_->set_interactable(enabled);
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
                advancedEnabled &&
                (settings.UndockedScreenEnabled() || editingScreen));
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
        if(ffmpeg9Toggle_)
            ffmpeg9Toggle_->set_interactable(enabled);
        if(hardwareDecodingToggle_)
            hardwareDecodingToggle_->set_interactable(enabled);
        if(automaticPerformanceToggle_)
            automaticPerformanceToggle_->set_interactable(enabled);
        if(automaticPerformanceThresholdSlider_)
            automaticPerformanceThresholdSlider_->set_interactable(
                enabled && settings.AutomaticPerformanceEnabled());
        if(automaticPerformanceResponseSlider_)
            automaticPerformanceResponseSlider_->set_interactable(
                enabled && settings.AutomaticPerformanceEnabled());
        if(performancePanelResetButton_)
            performancePanelResetButton_->set_interactable(enabled);
        if(performanceDiagnosticsToggle_)
            performanceDiagnosticsToggle_->set_interactable(enabled);
        if(powerBenchmarkToggle_)
            powerBenchmarkToggle_->set_interactable(enabled);
        if(nightlyUpdatesToggle_)
            nightlyUpdatesToggle_->set_interactable(enabled);
        if(updaterButton_)
            updaterButton_->set_interactable(enabled && DownloadManager::Instance().IsReady());
        if(showcaseButton_)
            // The readiness page must remain reachable when the downloader is
            // unavailable or an asset transfer is active; that page is where
            // the user sees the actionable status and progress.
            showcaseButton_->set_interactable(enabled);
    }

    void SettingsMenu::RefreshCurvatureControl()
    {
        const auto& settings = Settings::Instance();
        const bool curved = settings.CurvedScreenEnabled();

        if(sizeSetting_)
        {
            if(sizeSetting_->slider)
                sizeSetting_->slider->set_maxValue(
                    settings.MaximumScreenScale());
            sizeSetting_->set_Value(settings.ScreenScale());

            // BSML normally leaves an unusable maximum arrow visible but
            // disabled. Hide that arrow at the active mode's cap and restore
            // it immediately after the value is reduced.
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
            Settings::Instance().AdvancedOptionsEnabled() ||
            ScreenPreview::Instance().IsUndockedEditing();
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

            // Entering Position Screen changes which rows are visible and
            // forces this scroll content through a new layout pass. TextSlider
            // calculates its value-label position relative to the handle, but
            // RefreshValues runs before this rebuild. The rebuild therefore
            // moves the slider/handle and leaves most value labels at their
            // previous coordinates. Redraw all Screen sliders only after the
            // final canvas layout, matching the Video Zoom row that happened
            // to receive a later redraw already.
            // Establish the reference row first, then redraw each target.
            if(videoZoomSlider_ && videoZoomSlider_->slider)
                videoZoomSlider_->slider->UpdateVisuals();
            for(auto* setting : {
                    distanceSetting_, horizontalSetting_, verticalSetting_,
                    tiltSetting_, sizeSetting_, curvatureSlider_,
                    screenRotationSlider_, videoRotationSlider_,
                    videoHorizontalSlider_, videoVerticalSlider_,
                    videoTiltSlider_})
            {
                if(setting && setting->slider)
                    setting->slider->UpdateVisuals();
            }

            // Apply the remaining signed-control alignment only after the final
            // layout. X/Y use the separate native Zoom-style redraw below.
            AlignVideoValueLabels();
            RefreshVideoOffsetValueTexts();
        }
        if(cancelPositioningButton_)
            cancelPositioningButton_->get_gameObject()->SetActive(
                ScreenPreview::Instance().IsUndockedEditing());
    }

    void SettingsMenu::AlignVideoValueLabels()
    {
        // SLIDER VALUE-LABEL REGRESSION GUARD:
        // Video Zoom is the verified-good reference row. The signed-range
        // video controls use its handle-relative value-text placement.
        //
        // Known failed approaches -- DO NOT REINTRODUCE:
        // 1. Reparenting value text to the handle. Unity renders the child text
        //    over the stock handle graphic, making the grab handle disappear.
        // 2. Adding a custom image under handleRect. This produced oversized
        //    white blocks rather than the stock thin draggable handle.
        // 3. Applying world position only once during UI construction. The
        //    later Screen-tab layout and TextSlider redraw moved values left.
        // 4. Calling UpdateVisuals alone. It reproduces the signed-slider bug.
        //
        // Correct contract: keep the native text and handle in their original
        // BSML hierarchy. Reapply Zoom's handle-relative text offset after the
        // final layout and on every value change. This makes the text follow
        // without changing the handle's shape, visibility, or drag behavior.
        if(!videoZoomSlider_ || !videoZoomSlider_->slider ||
           !videoZoomSlider_->text)
            return;

        auto* referenceHandle =
            videoZoomSlider_->slider->get_handleRect().ptr();
        if(!referenceHandle)
            return;

        // Measure the good row in handle-local coordinates after layout.
        const auto referenceOffset = referenceHandle->InverseTransformPoint(
            videoZoomSlider_->text->get_transform()->get_position());

        for(auto* setting : {
                videoRotationSlider_, videoTiltSlider_})
        {
            if(!setting || !setting->slider || !setting->text)
                continue;

            auto* handle = setting->slider->get_handleRect().ptr();
            if(!handle)
                continue;

            // Preserve both native hierarchies. Transform the reference offset
            // into this handle's world position, then move only the value text.
            // Each slider callback reapplies this after TextSlider updates.
            handle->get_gameObject()->SetActive(true);
            setting->text->get_gameObject()->SetActive(true);
            auto textTransform = setting->text->get_transform();
            textTransform->set_position(
                handle->TransformPoint(referenceOffset));
        }
    }

    void SettingsMenu::RefreshVideoOffsetValueTexts()
    {
        const auto& settings = Settings::Instance();
        const auto refresh = [](BSML::SliderSetting* setting, float offset)
        {
            if(!setting || !setting->slider || !setting->text)
                return;

            // This is the complete working Video Zoom visual path. Let HMUI
            // position both native objects first, then replace only the string.
            // Do not alter either transform or the handle graphic.
            setting->slider->UpdateVisuals();
            setting->text->set_text(fmt::format("{:.2f}", offset));
        };

        refresh(videoHorizontalSlider_, settings.VideoOffsetX());
        refresh(videoVerticalSlider_, settings.VideoOffsetY());
    }

    void SettingsMenu::ShowSettingsTab(int index)
    {
        index = std::clamp(index, 0, 4);
        if(!Settings::Instance().ModEnabled() && index != 0)
            return;
        if(selectedTab_ == 1 && index != 1)
        {
            if(ScreenPreview::Instance().IsUndockedEditing())
            {
                if(settingsTabs_)
                    settingsTabs_->SelectCellWithNumber(1);
                RequestLeave([this, index]() { ShowSettingsTab(index); });
                return;
            }
        }
        selectedTab_ = index;
        for(int page = 0; page < static_cast<int>(tabViewRoots_.size()); ++page)
            if(tabViewRoots_[page])
                tabViewRoots_[page]->SetActive(page == selectedTab_);

        // Slider values are initially loaded while the Screen tab is hidden.
        // HMUI therefore cannot calculate the final handle/text coordinates at
        // that time. Rebuild and redraw only after the tab is visible, matching
        // the native update that previously occurred on the first user drag.
        if(selectedTab_ == 1)
        {
            RefreshAdvancedControls();
            RefreshVideoOffsetValueTexts();
            if(videoOpacitySlider_ && videoOpacitySlider_->slider)
                videoOpacitySlider_->slider->UpdateVisuals();
        }
        else if(selectedTab_ == 4)
        {
            // Misc is also constructed while hidden. Force the two Automatic
            // Performance sliders through the same native post-visibility
            // layout pass so their saved/default values start at the handle
            // instead of remaining at the left edge until the first drag.
            if(automaticPerformanceThresholdSlider_ &&
               automaticPerformanceThresholdSlider_->slider)
                automaticPerformanceThresholdSlider_->slider->UpdateVisuals();
            if(automaticPerformanceResponseSlider_ &&
               automaticPerformanceResponseSlider_->slider)
                automaticPerformanceResponseSlider_->slider->UpdateVisuals();
        }
    }

    void SettingsMenu::RequestLeave(std::function<void()> continuation)
    {
        if(!ScreenPreview::Instance().IsUndockedEditing())
        {
            if(continuation)
                continuation();
            return;
        }

        pendingScreenNavigation_ = std::move(continuation);
        if(unsavedScreenModal_)
            unsavedScreenModal_->Show();
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
        // Disabling the live panel persists its current transform. Do that
        // before Reset so the subsequent reset remains the final authority for
        // the default panel placement instead of being overwritten afterward.
        PerformancePanel::Instance().SetEnabled(false);
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
            transparencyToggle_, settings.LetterboxTransparencyEnabled());
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
        ErrorManager::Instance().Guard(
            "resetting menu placement visuals", []()
            {
                MenuPlacementGuide::Instance().Apply();
                MenuEnvironmentVisibility::Instance().Apply();
            });
        RefreshControls();
        PaperLogger.info("Reset all Big Screen settings to defaults");
    }

    void SettingsMenu::RefreshDownloaderStatus()
    {
        if(auto recovery = VideoLibrary::Instance().TakeRecoveryNotice())
            ErrorManager::Instance().ReportUserVisible("Video library recovered", *recovery);
        if(auto update = DownloadManager::Instance().TakeUpdateNotice())
            ErrorManager::Instance().ReportUserVisible("Downloader rollback", *update);
        // TakePendingDialog clears the queue. Consume only after this menu has
        // a live modal, otherwise a recoverable creation/order change would
        // discard the one user-visible explanation before it can be shown.
        if(errorModal_ && errorModalText_)
        {
            if(auto message = ErrorManager::Instance().TakePendingDialog())
            {
                errorModalText_->set_text(
                    "<b>" + message->first + "</b>\n\n" + message->second);
                errorModal_->Show();
                RefreshControls();
            }
        }
        if(showcaseButton_ && showcaseStatus_)
        {
            const auto showcase = ShowcaseLauncher::Instance().Snapshot();
            showcaseStatus_->set_text(
                showcase.Active() ? showcase.message : "");
            BSML::Lite::SetButtonText(
                showcaseButton_,
                showcase.Active()
                    ? "Open Showcase Progress"
                    : "Play Big Screen Showcase");
            showcaseButton_->set_interactable(
                Settings::Instance().ModEnabled());
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
