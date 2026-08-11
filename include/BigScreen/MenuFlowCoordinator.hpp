#pragma once

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "custom-types/shared/macros.hpp"

/// Owns Big Screen's center settings panel and optional right-side preview
/// panel. A dedicated flow coordinator is required because the convenient
/// BSML callback registration only provides one center view controller.
DECLARE_CLASS_CODEGEN(BigScreen, MenuFlowCoordinator, HMUI::FlowCoordinator,
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, settingsViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, previewViewController);

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
