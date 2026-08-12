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
            std::function<void()> onBack,
            std::function<void()> onManageStorage);
        void RefreshControls();
        void RefreshDownloaderStatus();

    private:
        SettingsMenu() = default;

        void RefreshValues();
        void RefreshEnabledState();
        void RefreshCurvatureControl();
        void RefreshAdvancedControls();
        void RefreshUpdaterHint();
        void ShowSettingsTab(int index);
        void ResetToDefaults();

        HMUI::ViewController* settingsViewController_ = nullptr;
        HMUI::TextSegmentedControl* settingsTabs_ = nullptr;
        std::array<UnityEngine::GameObject*, 5> tabViewRoots_{};
        int selectedTab_ = 0;
        BSML::ToggleSetting* modEnabledToggle_ = nullptr;
        BSML::ToggleSetting* distractionFreeMenuToggle_ = nullptr;
        BSML::ToggleSetting* advancedOptionsToggle_ = nullptr;
        BSML::ToggleSetting* videoEnabledToggle_ = nullptr;
        BSML::ToggleSetting* previewToggle_ = nullptr;
        BSML::DropdownListSetting* screenLayoutDropdown_ = nullptr;
        BSML::ToggleSetting* allowChromaOverrideToggle_ = nullptr;
        BSML::IncrementSetting* distanceSetting_ = nullptr;
        BSML::IncrementSetting* horizontalSetting_ = nullptr;
        BSML::IncrementSetting* verticalSetting_ = nullptr;
        BSML::IncrementSetting* tiltSetting_ = nullptr;
        BSML::IncrementSetting* sizeSetting_ = nullptr;
        BSML::ToggleSetting* curvedScreenToggle_ = nullptr;
        BSML::ToggleSetting* maintainCurveAspectToggle_ = nullptr;
        BSML::SliderSetting* curvatureSlider_ = nullptr;
        BSML::ToggleSetting* transparencyToggle_ = nullptr;
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
        BSML::DropdownListSetting* resolutionDropdown_ = nullptr;
        BSML::ToggleSetting* automaticPerformanceToggle_ = nullptr;
        BSML::DropdownListSetting* automaticPerformanceThresholdDropdown_ = nullptr;
        BSML::ToggleSetting* performanceDiagnosticsToggle_ = nullptr;
        BSML::ToggleSetting* nightlyUpdatesToggle_ = nullptr;
        BSML::ModalView* nightlyWarningModal_ = nullptr;
        BSML::ModalView* localVideoInstructionsModal_ = nullptr;
        BSML::ModalView* resetConfirmationModal_ = nullptr;
        BSML::ModalView* advancedWarningModal_ = nullptr;
        TMPro::TextMeshProUGUI* advancedWarningText_ = nullptr;
        BSML::ModalView* undockWarningModal_ = nullptr;
        BSML::ModalView* errorModal_ = nullptr;
        TMPro::TextMeshProUGUI* errorModalText_ = nullptr;
        UnityEngine::UI::Button* updaterButton_ = nullptr;
        HMUI::HoverHint* updaterHoverHint_ = nullptr;
        TMPro::TextMeshProUGUI* updaterStatus_ = nullptr;
        UnityEngine::UI::Button* resetButton_ = nullptr;
        bool suppressNightlyCallback_ = false;
        bool suppressAdvancedCallback_ = false;
        bool suppressUndockCallback_ = false;
    };
}
