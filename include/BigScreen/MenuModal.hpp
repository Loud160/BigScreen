// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

namespace BSML { class ModalView; }

namespace BigScreen {
    /// Shows a retained BSML modal on the controller that created it and moves
    /// its root to the end of that controller's sibling order. Keeping dialogs
    /// with their owning left, right, or center panel makes their location
    /// predictable; the final sibling ordering keeps both the dialog and its
    /// input blocker in front of that panel.
    void ShowModalInFront(BSML::ModalView* modal) noexcept;
}
