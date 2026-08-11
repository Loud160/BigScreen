#pragma once

#include <functional>

namespace BSML {
    class DropdownListSetting;
    class IncrementSetting;
    class SliderSetting;
    class ToggleSetting;
}

namespace HMUI {
    class ViewController;
}

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
        void ResetToDefaults();

        HMUI::ViewController* settingsViewController_ = nullptr;
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
        BSML::ToggleSetting* environmentMotionToggle_ = nullptr;
        BSML::DropdownListSetting* playbackFpsDropdown_ = nullptr;
        BSML::DropdownListSetting* resolutionDropdown_ = nullptr;
        BSML::ToggleSetting* nightlyUpdatesToggle_ = nullptr;
        UnityEngine::UI::Button* updaterButton_ = nullptr;
        TMPro::TextMeshProUGUI* updaterStatus_ = nullptr;
        UnityEngine::UI::Button* resetButton_ = nullptr;
    };
}
