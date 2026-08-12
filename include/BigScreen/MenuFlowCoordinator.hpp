#pragma once

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "custom-types/shared/macros.hpp"

namespace BigScreen {
    /// Applies or restores optional clutter suppression while Big Screen's
    /// flow coordinator is visible. There is intentionally no ClockMod link;
    /// the stock configuration and optional UI mods use the same safe path.
    void ApplyDistractionFreeMenu();
    void RestoreDistractionFreeMenu();
}

/// Places Big Screen's settings and navigation on Beat Saber's left panel and
/// keeps an empty center view so the full-size world preview is unobstructed.
DECLARE_CLASS_CODEGEN(BigScreen, MenuFlowCoordinator, HMUI::FlowCoordinator,
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, centerViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, settingsViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, libraryBrowserViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, libraryEditorViewController);

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
)
