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

    /// Reasserts the most recently shown, still-visible Big Screen modal at
    /// the front of its owning controller. Retained menu panels can append
    /// children during later refreshes, so one-time sibling ordering at Show()
    /// is not sufficient to guarantee that the dialog and its input blocker
    /// remain reachable.
    void TickFrontmostMenuModal() noexcept;

    /// Hides every retained popup before Big Screen releases its flow
    /// coordinator. This prevents a modal blocker from being reactivated with
    /// a stale controller the next time the menu opens.
    void DismissTrackedMenuModals() noexcept;
}
