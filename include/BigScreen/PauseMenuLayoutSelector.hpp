// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

namespace BSML {
    class IncrementSetting;
    class ToggleSetting;
}

namespace GlobalNamespace {
    class PauseMenuManager;
}

namespace BigScreen {
    /// Adds one compact Layout 1-5 selector to Beat Saber's pause menu only
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
