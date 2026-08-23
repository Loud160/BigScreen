// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "custom-types/shared/macros.hpp"

#include <string_view>

namespace BigScreen {
    /// Applies or restores optional clutter suppression while Big Screen's
    /// flow coordinator is visible. There is intentionally no ClockMod link;
    /// the stock configuration and optional UI mods use the same safe path.
    void ApplyDistractionFreeMenu();
    void RestoreDistractionFreeMenu();
    /// True only while Big Screen's own flow is the active menu hierarchy.
    bool IsBigScreenMenuActive();
    /// Keeps Big Screen's main-menu entry disabled until Beat Saber's parent
    /// menu is active and stable after this flow has been dismissed.
    void TickMenuReentryGuard() noexcept;
    /// True while Beat Saber is still settling its parent HMUI hierarchy after
    /// Big Screen closes. New child flows, including Solo, must not be
    /// presented until this shared transition gate has cleared.
    bool IsBigScreenMenuTransitionPending() noexcept;
    /// Cancels mod-owned interaction and dismisses Big Screen without routing
    /// through controls that may be part of the failed UI operation.
    bool ExitBigScreenMenuAfterError() noexcept;
    /// Safely dismisses Big Screen before the managed showcase opens Beat
    /// Saber's Solo flow. Beat Saber owns all navigation again after gameplay;
    /// Big Screen never tries to reconstruct or reopen its retained hierarchy.
    bool ExitBigScreenMenuForShowcase() noexcept;
    /// Presents the shared Big Screen flow from Beat Saber's normal main-menu
    /// entry. Keeping one retained coordinator for every entry path prevents
    /// two copies of the singleton-backed menu UI from owning the same state.
    bool OpenBigScreenMenu() noexcept;
    /// Presents Big Screen above the active Solo song-selection hierarchy and
    /// opens the selected map directly in the video editor. Dismissing Big
    /// Screen reveals the unchanged Solo selection underneath it.
    bool OpenBigScreenVideoEditor(std::string_view levelId) noexcept;
    /// Allows actions that require Beat Saber's main menu (currently the
    /// managed showcase launcher) to explain why they cannot start from the
    /// nested Solo shortcut without losing the player's current selection.
    bool BigScreenMenuOpenedFromSongSelection() noexcept;
}

/// Places Big Screen's settings and navigation on Beat Saber's left panel and
/// keeps an empty center view so the full-size world preview is unobstructed.
#if defined(__clang__)
#pragma clang diagnostic push
// CustomTypes' declaration macros create a local metadata counter that some
// expansions do not consume. Suppress only that generated-macro diagnostic;
// ordinary unused variables in Big Screen remain covered by -Wall/-Wextra.
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
DECLARE_CLASS_CODEGEN(BigScreen, MenuFlowCoordinator, HMUI::FlowCoordinator) {
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, centerViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, settingsViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, libraryBrowserViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, libraryEditorViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, storageViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, showcaseViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, localVideoBrowserViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, thumbnailPickerViewController);
    // HMUI retains whichever center subpage was on top when the whole flow was
    // dismissed. Track that state explicitly rather than querying a transient
    // top controller during activation, where Beat Saber may throw.
    DECLARE_INSTANCE_FIELD(bool, restoreCenterOnActivation);

    /// Returns the retained center stack to Big Screen's neutral controller
    /// while the flow is still active. Beat Saber may clear HMUI's center stack
    /// after the complete flow is dismissed, so every controlled exit performs
    /// this normalization before yielding the hierarchy to the parent menu.
    DECLARE_INSTANCE_METHOD(void, PrepareForDismissal);
    DECLARE_INSTANCE_METHOD(void, ApplyModEnabledUi, bool enabled);

    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        DidActivate,
        &HMUI::FlowCoordinator::DidActivate,
        bool firstActivation,
        bool addedToHierarchy,
        bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        DidDeactivate,
        &HMUI::FlowCoordinator::DidDeactivate,
        bool removedFromHierarchy,
        bool screenSystemDisabling);
    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        BackButtonWasPressed,
        &HMUI::FlowCoordinator::BackButtonWasPressed,
        HMUI::ViewController* topViewController);
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
