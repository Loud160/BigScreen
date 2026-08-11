#pragma once

namespace BSML {
    class SliderSetting;
    class ToggleSetting;
}

namespace HMUI {
    class ViewController;
}

namespace BigScreen {
    /// Registers and constructs Big Screen's dedicated main-menu page.
    class SettingsMenu final {
    public:
        static SettingsMenu& Instance();

        void Register();
        void CreateUi(HMUI::ViewController* viewController);
        void RefreshControls();

    private:
        SettingsMenu() = default;

        void RefreshPreviewControl();
        void RefreshCurvatureControl();

        HMUI::ViewController* settingsViewController_ = nullptr;
        BSML::ToggleSetting* previewToggle_ = nullptr;
        BSML::SliderSetting* curvatureSlider_ = nullptr;
    };
}
