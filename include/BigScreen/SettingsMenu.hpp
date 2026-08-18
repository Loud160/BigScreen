// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>

namespace BSML {
    class DropdownListSetting;
    class IncrementSetting;
    class SliderSetting;
    class ToggleSetting;
    class ModalView;
}

namespace HMUI {
    class HoverHint;
    class TextSegmentedControl;
    class ViewController;
}

namespace UnityEngine { class GameObject; }

namespace UnityEngine::UI {
    class Button;
}
namespace TMPro { class TextMeshProUGUI; }

namespace BigScreen {
    /// Registers and constructs Big Screen's dedicated main-menu page.
    class SettingsMenu final {
    public:
        static SettingsMenu& Instance();

        void Register();
        void CreateUi(
            HMUI::ViewController* viewController,
            HMUI::ViewController* errorHostViewController,
            std::function<void()> onBack,
            std::function<void()> onManageStorage,
            std::function<void()> onShowShowcase,
            std::function<void(bool)> onModEnabledChanged);
        /// Clears scene-owned IL2CPP references before a replacement menu
        /// hierarchy is constructed. Native singletons outlive MenuCore's
        /// view objects, so stale non-null pointers must never be reused.
        void ForgetUi();
        void RefreshControls();
        void RefreshDownloaderStatus();
        /// Runs the requested navigation immediately unless an unlocked screen
        /// has unsaved edits, in which case the user chooses save/discard/cancel.
        void RequestLeave(std::function<void()> continuation);

    private:
        SettingsMenu() = default;

        void RefreshValues();
        void RefreshEnabledState();
        void RefreshCurvatureControl();
        void RefreshAdvancedControls();
        /// Restores the FPS dropdown to the persisted value without changing
        /// playback. Used when the player cancels the 60 FPS warning.
        void RefreshPlaybackFpsControl();
        /// Aligns the affected video value labels with their slider handles
        /// after Beat Saber has completed the Screen tab's layout pass.
        void AlignVideoValueLabels();
        /// Lets the native Zoom-style X/Y sliders position their own text,
        /// then replaces only the displayed number with the signed offset.
        void RefreshVideoOffsetValueTexts();
        void RefreshUpdaterHint();
        void RefreshDisabledModeView();
        void ShowSettingsTab(int index);
        void ResetToDefaults();

        HMUI::ViewController* settingsViewController_ = nullptr;
        HMUI::TextSegmentedControl* settingsTabs_ = nullptr;
        std::array<UnityEngine::GameObject*, 5> tabViewRoots_{};
        UnityEngine::GameObject* generalContentRoot_ = nullptr;
        UnityEngine::GameObject* generalMasterRoot_ = nullptr;
        int selectedTab_ = 0;
        BSML::ToggleSetting* modEnabledToggle_ = nullptr;
        BSML::ToggleSetting* distractionFreeMenuToggle_ = nullptr;
        BSML::ToggleSetting* showMenuEnvironmentToggle_ = nullptr;
        BSML::ToggleSetting* showLaneGuidesToggle_ = nullptr;
        BSML::ToggleSetting* advancedOptionsToggle_ = nullptr;
        BSML::ToggleSetting* videoEnabledToggle_ = nullptr;
        BSML::ToggleSetting* previewToggle_ = nullptr;
        UnityEngine::UI::Button* screenLayoutResetButton_ = nullptr;
        BSML::DropdownListSetting* screenLayoutDropdown_ = nullptr;
        BSML::ToggleSetting* respectMapperSettingsToggle_ = nullptr;
        BSML::ToggleSetting* allowChromaOverrideToggle_ = nullptr;
        BSML::SliderSetting* distanceSetting_ = nullptr;
        BSML::SliderSetting* horizontalSetting_ = nullptr;
        BSML::SliderSetting* verticalSetting_ = nullptr;
        BSML::SliderSetting* tiltSetting_ = nullptr;
        BSML::SliderSetting* sizeSetting_ = nullptr;
        HMUI::HoverHint* distanceHint_ = nullptr;
        HMUI::HoverHint* horizontalHint_ = nullptr;
        HMUI::HoverHint* verticalHint_ = nullptr;
        HMUI::HoverHint* tiltHint_ = nullptr;
        HMUI::HoverHint* sizeHint_ = nullptr;
        BSML::ToggleSetting* curvedScreenToggle_ = nullptr;
        BSML::ToggleSetting* maintainCurveAspectToggle_ = nullptr;
        BSML::SliderSetting* curvatureSlider_ = nullptr;
        BSML::ToggleSetting* transparencyToggle_ = nullptr;
        BSML::SliderSetting* videoOpacitySlider_ = nullptr;
        UnityEngine::GameObject* screenCanvasHeader_ = nullptr;
        UnityEngine::GameObject* advancedScreenControlsRoot_ = nullptr;
        BSML::SliderSetting* screenRotationSlider_ = nullptr;
        BSML::SliderSetting* videoRotationSlider_ = nullptr;
        BSML::SliderSetting* videoZoomSlider_ = nullptr;
        BSML::SliderSetting* videoHorizontalSlider_ = nullptr;
        BSML::SliderSetting* videoVerticalSlider_ = nullptr;
        BSML::SliderSetting* videoTiltSlider_ = nullptr;
        BSML::ToggleSetting* stretchVideoToggle_ = nullptr;
        BSML::ToggleSetting* undockScreenToggle_ = nullptr;
        UnityEngine::UI::Button* positionScreenButton_ = nullptr;
        UnityEngine::UI::Button* cancelPositioningButton_ = nullptr;
        BSML::ToggleSetting* lightShowToggle_ = nullptr;
        BSML::ToggleSetting* hideBackWallLightsToggle_ = nullptr;
        BSML::ToggleSetting* hideRingLightsToggle_ = nullptr;
        BSML::ToggleSetting* hideSideLaserLightsToggle_ = nullptr;
        HMUI::HoverHint* hideBackWallLightsHint_ = nullptr;
        HMUI::HoverHint* hideRingLightsHint_ = nullptr;
        HMUI::HoverHint* hideSideLaserLightsHint_ = nullptr;
        BSML::ToggleSetting* environmentOverrideToggle_ = nullptr;
        BSML::ToggleSetting* disableEnvironmentMotionToggle_ = nullptr;
        BSML::ToggleSetting* hideTrackRingsToggle_ = nullptr;
        BSML::ToggleSetting* hideSideBarsToggle_ = nullptr;
        BSML::ToggleSetting* hideSpectrogramBarsToggle_ = nullptr;
        HMUI::HoverHint* hideSpectrogramBarsHint_ = nullptr;
        BSML::DropdownListSetting* playbackFpsDropdown_ = nullptr;
        BSML::ModalView* highFrameRateWarningModal_ = nullptr;
        BSML::ToggleSetting* ffmpeg9Toggle_ = nullptr;
        BSML::ToggleSetting* embeddedVideoShaderToggle_ = nullptr;
        BSML::SliderSetting* nativeBloomLevelSlider_ = nullptr;
        BSML::SliderSetting* cinemaBloomLevelSlider_ = nullptr;
        BSML::ToggleSetting* hardwareDecodingToggle_ = nullptr;
        BSML::ToggleSetting* automaticPerformanceToggle_ = nullptr;
        BSML::ModalView* automaticPerformanceWarningModal_ = nullptr;
        BSML::SliderSetting* automaticPerformanceThresholdSlider_ = nullptr;
        BSML::SliderSetting* automaticPerformanceAttackSlider_ = nullptr;
        BSML::SliderSetting* automaticPerformanceReleaseSlider_ = nullptr;
        BSML::SliderSetting* automaticPerformanceFpsStepSlider_ = nullptr;
        BSML::ToggleSetting* automaticPerformanceOscillationToggle_ = nullptr;
        BSML::SliderSetting* automaticPerformanceOscillationLimitSlider_ = nullptr;
        UnityEngine::UI::Button* performancePanelResetButton_ = nullptr;
        BSML::ToggleSetting* performanceDiagnosticsToggle_ = nullptr;
        BSML::ToggleSetting* powerBenchmarkToggle_ = nullptr;
        BSML::ToggleSetting* nightlyUpdatesToggle_ = nullptr;
        BSML::ModalView* nightlyWarningModal_ = nullptr;
        BSML::ModalView* localVideoInstructionsModal_ = nullptr;
        BSML::ModalView* resetConfirmationModal_ = nullptr;
        BSML::ModalView* advancedWarningModal_ = nullptr;
        TMPro::TextMeshProUGUI* advancedWarningText_ = nullptr;
        BSML::ModalView* undockWarningModal_ = nullptr;
        BSML::ModalView* unsavedScreenModal_ = nullptr;
        BSML::ModalView* errorModal_ = nullptr;
        TMPro::TextMeshProUGUI* errorModalText_ = nullptr;
        UnityEngine::UI::Button* showcaseButton_ = nullptr;
        TMPro::TextMeshProUGUI* showcaseStatus_ = nullptr;
        UnityEngine::UI::Button* updaterButton_ = nullptr;
        HMUI::HoverHint* updaterHoverHint_ = nullptr;
        TMPro::TextMeshProUGUI* updaterStatus_ = nullptr;
        UnityEngine::UI::Button* modUpdaterButton_ = nullptr;
        TMPro::TextMeshProUGUI* modUpdaterStatus_ = nullptr;
        TMPro::TextMeshProUGUI* modVersionText_ = nullptr;
        TMPro::TextMeshProUGUI* ytDlpVersionText_ = nullptr;
        UnityEngine::UI::Button* resetButton_ = nullptr;
        bool suppressNightlyCallback_ = false;
        bool suppressAutomaticPerformanceCallback_ = false;
        bool suppressAdvancedCallback_ = false;
        bool suppressUndockCallback_ = false;
        std::function<void()> pendingScreenNavigation_;
        std::function<void(bool)> modEnabledUiChanged_;
        bool displayedEnabledState_ = true;
        bool displayedEnabledStateKnown_ = false;
    };
}
