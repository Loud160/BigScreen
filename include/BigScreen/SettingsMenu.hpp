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
            std::function<void()> onBack);
        void RefreshControls();
        void RefreshDownloaderStatus();

    private:
        SettingsMenu() = default;

        void RefreshValues();
        void RefreshEnabledState();
        void RefreshCurvatureControl();
        void RefreshUpdaterHint();
        void ShowSettingsTab(int index);
        void ResetToDefaults();

        HMUI::ViewController* settingsViewController_ = nullptr;
        HMUI::TextSegmentedControl* settingsTabs_ = nullptr;
        std::array<UnityEngine::GameObject*, 4> tabViewRoots_{};
        int selectedTab_ = 0;
        BSML::ToggleSetting* modEnabledToggle_ = nullptr;
        BSML::ToggleSetting* videoEnabledToggle_ = nullptr;
        BSML::ToggleSetting* previewToggle_ = nullptr;
        BSML::IncrementSetting* distanceSetting_ = nullptr;
        BSML::IncrementSetting* horizontalSetting_ = nullptr;
        BSML::IncrementSetting* verticalSetting_ = nullptr;
        BSML::IncrementSetting* tiltSetting_ = nullptr;
        BSML::IncrementSetting* sizeSetting_ = nullptr;
        BSML::ToggleSetting* curvedScreenToggle_ = nullptr;
        BSML::SliderSetting* curvatureSlider_ = nullptr;
        BSML::ToggleSetting* transparencyToggle_ = nullptr;
        BSML::ToggleSetting* lightShowToggle_ = nullptr;
        BSML::ToggleSetting* environmentOverrideToggle_ = nullptr;
        BSML::ToggleSetting* glassDesertOverrideToggle_ = nullptr;
        BSML::ToggleSetting* environmentMotionToggle_ = nullptr;
        BSML::ToggleSetting* hideTrackRingsToggle_ = nullptr;
        BSML::DropdownListSetting* playbackFpsDropdown_ = nullptr;
        BSML::DropdownListSetting* resolutionDropdown_ = nullptr;
        BSML::ToggleSetting* nightlyUpdatesToggle_ = nullptr;
        BSML::ModalView* nightlyWarningModal_ = nullptr;
        UnityEngine::UI::Button* updaterButton_ = nullptr;
        HMUI::HoverHint* updaterHoverHint_ = nullptr;
        TMPro::TextMeshProUGUI* updaterStatus_ = nullptr;
        UnityEngine::UI::Button* resetButton_ = nullptr;
        bool suppressNightlyCallback_ = false;
    };
}
