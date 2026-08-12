#pragma once

namespace BSML {
    class IncrementSetting;
    class ToggleSetting;
}

namespace GlobalNamespace {
    class PauseMenuManager;
}

namespace BigScreen {
    /// Adds one compact Layout 1/2/3 selector to Beat Saber's pause menu only
    /// while a Big Screen-controlled gameplay video is active.
    class PauseMenuLayoutSelector final {
    public:
        static PauseMenuLayoutSelector& Instance();

        void CreateUi(GlobalNamespace::PauseMenuManager* pauseMenu);
        void MenuShown();
        void ForgetUi();

    private:
        PauseMenuLayoutSelector() = default;
        void LayoutChanged(float value);
        void VideoScreenChanged(bool enabled);

        BSML::IncrementSetting* selector_ = nullptr;
        BSML::ToggleSetting* videoScreenToggle_ = nullptr;
    };
}
