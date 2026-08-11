#pragma once

namespace BSML {
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

    private:
        SettingsMenu() = default;

        void DidActivate(
            HMUI::ViewController* viewController,
            bool firstActivation,
            bool addedToHierarchy,
            bool screenSystemEnabling);
        void RefreshPreviewControl();

        BSML::ToggleSetting* previewToggle_ = nullptr;
    };
}
