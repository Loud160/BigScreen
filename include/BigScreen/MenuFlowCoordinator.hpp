#pragma once

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "custom-types/shared/macros.hpp"

/// Places Big Screen's settings on Beat Saber's left panel and keeps an empty
/// center view so the optional full-size world preview remains unobstructed.
DECLARE_CLASS_CODEGEN(BigScreen, MenuFlowCoordinator, HMUI::FlowCoordinator,
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, centerViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, settingsViewController);

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
