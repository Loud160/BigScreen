// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

namespace BSML { class ModalView; }
namespace HMUI { class ViewController; }

namespace BigScreen {
    /// Returns the center controller currently visible in Big Screen's main
    /// stack. The neutral preview page and every center subpage use the same
    /// stack, so callers must resolve this at presentation time rather than
    /// retaining only the controller that was active during UI construction.
    HMUI::ViewController* ActiveCenterModalHost() noexcept;

    /// Moves a retained BSML modal onto Big Screen's currently visible center
    /// controller before showing it. This prevents dialogs triggered from the
    /// left Settings or right Video Library panels from appearing behind those
    /// curved side screens, including while a center subpage is open.
    void ShowModalOnCenterScreen(BSML::ModalView* modal) noexcept;
}
