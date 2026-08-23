// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/MenuFlowCoordinator.hpp"

#include "BigScreen/MenuModal.hpp"
#include "BigScreen/DiagnosticSessionLogger.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/LocalVideoBrowserMenu.hpp"
#include "BigScreen/MenuEnvironmentVisibility.hpp"
#include "BigScreen/MenuPlacementGuide.hpp"
#include "BigScreen/PerformancePanel.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "BigScreen/ShowcaseMenu.hpp"
#include "BigScreen/StorageMaintenanceMenu.hpp"
#include "BigScreen/ThumbnailPickerMenu.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/MainMenuViewController.hpp"
#include "GlobalNamespace/OVRManager.hpp"
#include "GlobalNamespace/SoloFreePlayFlowCoordinator.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "bsml/shared/Helpers/creation.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/MenuButtons/MenuButton.hpp"
#include "bsml/shared/BSML/MenuButtons/MenuButtons.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "main.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

DEFINE_TYPE(BigScreen, MenuFlowCoordinator);

namespace BigScreen {
    namespace {
        bool foveationOverrideActive = false;
        int savedFoveationLevel = 0;
        bool savedDynamicFoveation = false;
        bool distractionFreeMenuActive = false;
        std::vector<UnityW<UnityEngine::GameObject>> hiddenMenuObjects;
        // Finding every Transform in a heavily modded menu can stall Unity's
        // main thread. These exact-name targets are stable for the lifetime of
        // the menu scene, so retain weak Unity handles and rescan only after a
        // scene rebuild invalidates one of them.
        std::vector<UnityW<UnityEngine::GameObject>> knownDistractionObjects;
        // Main-menu and Solo shortcuts share one retained flow. Every menu
        // page is backed by process-lifetime native singletons, so creating a
        // second coordinator would let two Unity hierarchies point at and
        // tear down the same controls.
        UnityW<MenuFlowCoordinator> retainedMenuFlow = nullptr;
        UnityW<MenuFlowCoordinator> activeMenuFlow = nullptr;
        bool activeLaunchFromSongSelection = false;
        std::string pendingVideoEditorLevelId;
        BSML::MenuButton* bigScreenMenuButton = nullptr;
        bool menuReentryBlocked = false;
        float menuReentryNotBefore = 0.0f;
        int stableEntryHierarchyFrames = 0;
        UnityW<MenuFlowCoordinator> pendingFailedMenuExit = nullptr;
        int pendingFailedMenuExitFrames = 0;
        std::vector<UnityW<BSML::ModalView>> frontmostMenuModals;

        void RaiseModalRoot(BSML::ModalView* modal)
        {
            if(!UnityW<BSML::ModalView>::isAlive(modal))
                return;
            auto modalTransform = modal->get_transform();
            if(UnityW<UnityEngine::Transform>::isAlive(modalTransform))
                modalTransform->SetAsLastSibling();
        }

        BSML::MenuButton* ResolveBigScreenMenuButton()
        {
            if(bigScreenMenuButton)
                return bigScreenMenuButton;
            auto* buttons = BSML::MenuButtons::get_instance();
            if(!buttons)
                return nullptr;
            for(auto* item : buttons->get_buttons())
            {
                auto candidate = il2cpp_utils::try_cast<BSML::MenuButton>(item);
                if(!candidate.has_value())
                    continue;
                auto* button = candidate.value();
                if(button && std::string(button->text) == "Big Screen")
                {
                    bigScreenMenuButton = button;
                    return button;
                }
            }
            return nullptr;
        }

        void SetBigScreenMenuButtonInteractable(bool interactable)
        {
            if(auto* button = ResolveBigScreenMenuButton())
                button->set_interactable(interactable);
        }

        void BeginMenuReentryGuard()
        {
            // Beat Saber completes a dismissal over several frames even when
            // HMUI's immediate flag is used. Starting this retained flow again
            // while MainMenuViewController is inactive asks Unity to start a
            // coroutine on an inactive object and leaves the complete menu
            // hierarchy unresponsive. Disable only Big Screen's entry until
            // a supported parent hierarchy has remained stable.
            menuReentryBlocked = true;
            stableEntryHierarchyFrames = 0;
            menuReentryNotBefore =
                UnityEngine::Time::get_realtimeSinceStartup() + 1.25f;
            SetBigScreenMenuButtonInteractable(false);
        }

        std::string NormalizeObjectName(StringW value)
        {
            std::string normalized;
            for(const unsigned char character : std::string(value))
                if(std::isalnum(character))
                    normalized.push_back(static_cast<char>(std::tolower(character)));
            return normalized;
        }

        bool IsDistractionObjectName(const std::string& name)
        {
            // ClockCanvas is the optional Quest ClockMod root. The remaining
            // names cover the stock menu-title assets used across Beat Saber
            // menu-environment revisions. Exact normalized matches avoid
            // hiding unrelated mod icons or UI labels containing "logo".
            return name == "clockcanvas" ||
                   name == "beatsaberlogo" ||
                   name == "neonbeatsaber" ||
                   name == "neonlogo" ||
                   name == "saberlogo" ||
                   name == "logobeat" ||
                   name == "logobat" ||
                   name == "logosaber" ||
                   name == "logo";
        }

        void RestoreMenuFoveation()
        {
            if(!foveationOverrideActive)
                return;

            // Mark inactive first so an exception during restoration cannot
            // cause later lifecycle calls to apply a partially stale value.
            foveationOverrideActive = false;
            try
            {
                GlobalNamespace::OVRManager::set_foveatedRenderingLevel(
                    GlobalNamespace::OVRManager_FoveatedRenderingLevel{
                        savedFoveationLevel});
                GlobalNamespace::OVRManager::set_useDynamicFoveatedRendering(
                    savedDynamicFoveation);
                PaperLogger.info(
                    "Restored menu foveation level {} with dynamic FFR {}",
                    savedFoveationLevel,
                    savedDynamicFoveation ? "enabled" : "disabled");
            }
            catch(...)
            {
                PaperLogger.error(
                    "Could not restore Beat Saber's foveated-rendering state");
                ErrorManager::Instance().RecordError(
                    "Restoring menu foveated rendering",
                    "Beat Saber rejected the saved foveated-rendering state");
            }
        }

        void DisableFoveationForMenu()
        {
            if(foveationOverrideActive)
                return;

            try
            {
                const auto level =
                    GlobalNamespace::OVRManager::get_foveatedRenderingLevel();
                savedFoveationLevel = level.value__;
                savedDynamicFoveation =
                    GlobalNamespace::OVRManager::get_useDynamicFoveatedRendering();
                foveationOverrideActive = true;

                // Dynamic FFR must be disabled first or it may immediately
                // raise the level again after the explicit Off request.
                GlobalNamespace::OVRManager::set_useDynamicFoveatedRendering(false);
                GlobalNamespace::OVRManager::set_foveatedRenderingLevel(
                    GlobalNamespace::OVRManager_FoveatedRenderingLevel::Off);
                PaperLogger.info(
                    "Temporarily disabled foveated rendering for Big Screen's menu (saved level {}, dynamic {})",
                    savedFoveationLevel,
                    savedDynamicFoveation ? "enabled" : "disabled");
            }
            catch(...)
            {
                PaperLogger.error(
                    "Could not disable foveated rendering for Big Screen's menu");
                ErrorManager::Instance().RecordError(
                    "Disabling menu foveated rendering",
                    "Beat Saber rejected the temporary foveated-rendering override");
                RestoreMenuFoveation();
            }
        }

        MenuFlowCoordinator* ResolveMenuFlow()
        {
            if(!UnityW<MenuFlowCoordinator>::isAlive(retainedMenuFlow))
                retainedMenuFlow =
                    BSML::Helpers::CreateFlowCoordinator<MenuFlowCoordinator*>();
            return retainedMenuFlow.ptr();
        }

        bool IsInSoloHierarchy(
            HMUI::FlowCoordinator* child,
            GlobalNamespace::SoloFreePlayFlowCoordinator* solo)
        {
            for(int depth = 0; child && depth < 16; ++depth)
            {
                if(child == solo)
                    return true;
                child = child->__cordl_internal_get__parentFlowCoordinator().ptr();
            }
            return false;
        }

        void LogMenuLifecycleDuration(
            const char* phase,
            std::chrono::steady_clock::time_point started,
            bool firstActivation = false)
        {
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            PaperLogger.info(
                "Big Screen menu {} completed in {} ms{}",
                phase,
                elapsed,
                firstActivation ? " (first activation)" : "");
        }

        bool PresentSharedMenu(
            std::string_view editorLevelId,
            bool requireSoloSelection)
        {
            if(activeMenuFlow || menuReentryBlocked || pendingFailedMenuExit)
                return false;

            auto* mainFlow = BSML::Helpers::GetMainFlowCoordinator();
            auto* parent = mainFlow
                ? mainFlow->YoungestChildFlowCoordinatorOrSelf().ptr()
                : nullptr;
            auto* solo = mainFlow
                ? mainFlow->__cordl_internal_get__soloFreePlayFlowCoordinator().ptr()
                : nullptr;
            if(!mainFlow || !parent || parent->get_isInTransition() ||
               !parent->get_isActivated())
                return false;
            if(requireSoloSelection &&
               (!solo || !IsInSoloHierarchy(parent, solo)))
                return false;

            auto* coordinator = ResolveMenuFlow();
            if(!coordinator || coordinator == parent)
                return false;

            pendingVideoEditorLevelId = std::string(editorLevelId);
            activeLaunchFromSongSelection = requireSoloSelection;
            try
            {
                parent->PresentFlowCoordinator(
                    coordinator,
                    nullptr,
                    HMUI::ViewController::AnimationDirection::Horizontal,
                    false,
                    false);
                return true;
            }
            catch(...)
            {
                pendingVideoEditorLevelId.clear();
                activeLaunchFromSongSelection = false;
                throw;
            }
        }

        void FinishMenuLaunch(bool returnToSongSelection) noexcept
        {
            activeLaunchFromSongSelection = false;
            pendingVideoEditorLevelId.clear();
            if(!returnToSongSelection)
                return;
            try
            {
                SelectionVideoToggle::Instance()
                    .BigScreenMenuClosedToSongSelection();
                PaperLogger.info(
                    "Returned from Big Screen to the retained Solo song selection");
            }
            catch(const std::exception& exception)
            {
                PaperLogger.warn(
                    "Could not prepare the retained Solo video preview after closing Big Screen: {}",
                    exception.what());
            }
            catch(...)
            {
                PaperLogger.warn(
                    "Could not prepare the retained Solo video preview after closing Big Screen");
            }
        }
    }

    bool IsBigScreenMenuActive()
    {
        return activeMenuFlow;
    }

    bool OpenBigScreenMenu() noexcept
    {
        try
        {
            return PresentSharedMenu({}, false);
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not open Big Screen from the main menu: {}",
                exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not open Big Screen from the main menu");
        }
        return false;
    }

    bool OpenBigScreenVideoEditor(std::string_view levelId) noexcept
    {
        if(levelId.empty())
            return false;
        try
        {
            return PresentSharedMenu(levelId, true);
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not open Big Screen's Solo video shortcut: {}",
                exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not open Big Screen's Solo video shortcut");
        }
        return false;
    }

    bool BigScreenMenuOpenedFromSongSelection() noexcept
    {
        return activeLaunchFromSongSelection && activeMenuFlow;
    }

    void ShowModalInFront(BSML::ModalView* modal) noexcept
    {
        if(!UnityW<BSML::ModalView>::isAlive(modal))
            return;
        try
        {
            // A modal is created on the same controller as the action that
            // opens it. Never reparent it to another screen: that can strand
            // an invisible input blocker behind a side panel. Last-sibling
            // ordering guarantees the retained modal is rendered and receives
            // pointer input above the rest of its owning controller.
            // Keep a small ordered modal stack. Nested warnings are rare, but
            // retaining every visible modal means dismissing the newest one
            // cannot expose an older popup underneath an unrelated refreshed
            // control. Showing an existing modal again moves it to the top.
            frontmostMenuModals.erase(
                std::remove_if(
                    frontmostMenuModals.begin(),
                    frontmostMenuModals.end(),
                    [modal](UnityW<BSML::ModalView> candidate)
                    {
                        return candidate.ptr() == modal;
                    }),
                frontmostMenuModals.end());
            frontmostMenuModals.emplace_back(modal);
            RaiseModalRoot(modal);
            modal->Show();
            RaiseModalRoot(modal);
        }

        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not present a Big Screen dialog in front of its menu: {}",
                exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not present a Big Screen dialog in front of its menu");
        }
    }

    void TickFrontmostMenuModal() noexcept
    {
        try
        {
            frontmostMenuModals.erase(
                std::remove_if(
                    frontmostMenuModals.begin(),
                    frontmostMenuModals.end(),
                    [](UnityW<BSML::ModalView> candidate)
                    {
                        auto* modal = candidate.ptr();
                        if(!UnityW<BSML::ModalView>::isAlive(modal))
                            return true;
                        auto gameObject = modal->get_gameObject();
                        return !UnityW<UnityEngine::GameObject>::isAlive(
                                   gameObject) ||
                               !gameObject->get_activeInHierarchy();
                    }),
                frontmostMenuModals.end());

            // Raise in opening order so every retained popup remains above its
            // panel and the newest visible popup remains above older popups.
            // This runs after normal menu work, so no late status refresh can
            // cover the dialog or intercept its OK/Cancel buttons.
            for(auto& candidate : frontmostMenuModals)
                RaiseModalRoot(candidate.ptr());
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not retain a Big Screen dialog in front of its menu: {}",
                exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not retain a Big Screen dialog in front of its menu");
        }
    }

    void DismissTrackedMenuModals() noexcept
    {
        for(auto& candidate : frontmostMenuModals)
        {
            try
            {
                auto* modal = candidate.ptr();
                if(UnityW<BSML::ModalView>::isAlive(modal))
                    modal->Hide();
            }
            catch(...)
            {
                // Continue through the complete stack. A destroyed retained
                // controller must not prevent other input blockers from being
                // retired during menu shutdown.
            }
        }
        frontmostMenuModals.clear();
    }

    bool IsBigScreenMenuTransitionPending() noexcept
    {
        return menuReentryBlocked || pendingFailedMenuExit;
    }

    void TickMenuReentryGuard() noexcept
    {
        if(!menuReentryBlocked && !pendingFailedMenuExit)
            return;
        try
        {
            // Reassert this in case BSML rebuilt its button view during the
            // parent transition after the guard first disabled the model.
            if(menuReentryBlocked)
                SetBigScreenMenuButtonInteractable(false);

            // ErrorManager can request recovery from inside DidActivate. HMUI
            // is still mutating its controller hierarchy during that callback,
            // so dismissing the flow synchronously there can strand the player
            // in an environment with no usable menu. Wait for the next update,
            // then dismiss through Beat Saber's parent flow. The retained
            // UnityW keeps the coordinator valid until this work is complete.
            if(pendingFailedMenuExit)
            {
                if(++pendingFailedMenuExitFrames < 2)
                    return;

                auto* coordinator = pendingFailedMenuExit.ptr();
                auto* parent = coordinator
                    ? coordinator->__cordl_internal_get__parentFlowCoordinator().ptr()
                    : nullptr;
                if(!parent)
                    parent = BSML::Helpers::GetMainFlowCoordinator();
                if(!coordinator || !parent)
                    throw std::runtime_error(
                        "Big Screen's failed menu hierarchy was unavailable");

                parent->DismissFlowCoordinator(
                    coordinator,
                    HMUI::ViewController::AnimationDirection::Horizontal,
                    nullptr,
                    true);
                pendingFailedMenuExit = nullptr;
                pendingFailedMenuExitFrames = 0;
                PaperLogger.warn(
                    "Dismissed Big Screen's menu on a safe frame after an internal UI failure");
                return;
            }

            if(activeMenuFlow ||
               UnityEngine::Time::get_realtimeSinceStartup() <
                   menuReentryNotBefore)
                return;

            auto* mainFlow = BSML::Helpers::GetMainFlowCoordinator();
            auto* mainMenu = mainFlow
                ? mainFlow->__cordl_internal_get__mainMenuViewController().ptr()
                : nullptr;
            const bool stableMainMenu = mainFlow && mainMenu &&
                mainFlow->get_isActivated() &&
                !mainFlow->get_isInTransition() &&
                mainMenu->get_isActivated() &&
                !mainMenu->get_isInTransition() &&
                mainMenu->get_gameObject() &&
                mainMenu->get_gameObject()->get_activeInHierarchy();

            // A normal close can be followed immediately by entering Solo.
            // The old guard recognized only MainMenuViewController, so once
            // Solo deactivated that view the guard could never clear and the
            // Configure Video shortcut remained blocked indefinitely. Solo's
            // retained hierarchy is an equally safe parent after the same
            // cooldown and multi-frame stability requirement.
            auto* solo = mainFlow
                ? mainFlow->__cordl_internal_get__soloFreePlayFlowCoordinator().ptr()
                : nullptr;
            auto* youngest = mainFlow
                ? mainFlow->YoungestChildFlowCoordinatorOrSelf().ptr()
                : nullptr;
            const bool stableSolo = mainFlow && solo && youngest &&
                mainFlow->get_isActivated() &&
                !mainFlow->get_isInTransition() &&
                solo->get_isActivated() && !solo->get_isInTransition() &&
                youngest->get_isActivated() &&
                !youngest->get_isInTransition() &&
                IsInSoloHierarchy(youngest, solo);

            const bool stableEntryHierarchy = stableMainMenu || stableSolo;
            stableEntryHierarchyFrames = stableEntryHierarchy
                ? stableEntryHierarchyFrames + 1
                : 0;
            if(stableEntryHierarchyFrames < 12)
                return;

            menuReentryBlocked = false;
            stableEntryHierarchyFrames = 0;
            SetBigScreenMenuButtonInteractable(true);
            PaperLogger.info(
                "Big Screen menu entry re-enabled after the {} hierarchy stabilized",
                stableSolo ? "Solo" : "main-menu");
        }
        catch(const std::exception& exception)
        {
            // The guard is safety UI, not gameplay functionality. Keep the
            // entry disabled and retry on a later stable menu frame.
            stableEntryHierarchyFrames = 0;
            PaperLogger.warn(
                "Could not verify Big Screen menu re-entry yet: {}",
                exception.what());
        }
        catch(...)
        {
            stableEntryHierarchyFrames = 0;
        }
    }

    bool ExitBigScreenMenuAfterError() noexcept
    {
        auto* coordinator = activeMenuFlow.ptr();
        if(!coordinator)
            return false;
        try
        {
            // Do not use BackButtonWasPressed here. Its unsaved-edit dialog is
            // itself part of Big Screen's UI and may be the component that
            // failed. Discard transient interaction immediately, but defer the
            // actual HMUI dismissal until the next main-thread update. Calling
            // DismissFlowCoordinator reentrantly from DidActivate can report
            // success while leaving the player in an empty environment.
            ScreenPreview::Instance().CancelUndockedEditing();
            VideoLibraryMenu::Instance().StopActivePreview();
            ThumbnailPickerMenu::Instance().Hide();
            LocalVideoBrowserMenu::Instance().CancelScan();
            ShowcaseMenu::Instance().DismissTransientUi();
            if(!activeLaunchFromSongSelection)
                BeginMenuReentryGuard();
            pendingFailedMenuExit = coordinator;
            pendingFailedMenuExitFrames = 0;
            PaperLogger.warn(
                "Queued Big Screen's failed menu for safe-frame dismissal");
            return true;
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not dismiss Big Screen's failed menu: {}",
                exception.what());
            ErrorManager::Instance().RecordError(
                "Dismissing Big Screen after a menu failure",
                exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not dismiss Big Screen's failed menu: unknown exception");
            ErrorManager::Instance().RecordError(
                "Dismissing Big Screen after a menu failure",
                "Unknown native exception");
        }
        return false;
    }

    bool ExitBigScreenMenuForShowcase() noexcept
    {
        auto* coordinator = activeMenuFlow.ptr();
        if(!coordinator)
            return false;
        try
        {
            ScreenPreview::Instance().CancelUndockedEditing();
            VideoLibraryMenu::Instance().StopActivePreview();
            // The Showcase page is a replacement on Big Screen's retained
            // center stack. Restore the neutral controller before dismissing
            // this flow, while HMUI still has a valid top controller. Gameplay
            // clears that stack; trying to restore it during the next
            // DidActivate caused System.ArgumentOutOfRangeException and left
            // the player looking at an environment with no usable menus.
            coordinator->PrepareForDismissal();
            auto parent =
                coordinator->__cordl_internal_get__parentFlowCoordinator();
            if(!parent)
                throw std::runtime_error(
                    "Big Screen's parent menu flow was unavailable");
            BeginMenuReentryGuard();
            parent->DismissFlowCoordinator(
                coordinator,
                HMUI::ViewController::AnimationDirection::Horizontal,
                nullptr,
                true);
            PaperLogger.info(
                "Dismissed Big Screen before opening the managed showcase");
            return true;
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not dismiss Big Screen for the showcase: {}",
                exception.what());
            ErrorManager::Instance().RecordError(
                "Dismissing Big Screen for the showcase",
                exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not dismiss Big Screen for the showcase: unknown exception");
            ErrorManager::Instance().RecordError(
                "Dismissing Big Screen for the showcase",
                "Unknown native exception");
        }
        return false;
    }

    void MenuFlowCoordinator::PrepareForDismissal()
    {
        ShowcaseMenu::Instance().DismissTransientUi();
        ThumbnailPickerMenu::Instance().Hide();
        LocalVideoBrowserMenu::Instance().CancelScan();
        if(!restoreCenterOnActivation)
            return;

        auto* mainControllers =
            __cordl_internal_get__mainScreenViewControllers();
        if(!centerViewController || !mainControllers ||
           mainControllers->get_Count() <= 0)
        {
            // A system interruption or another flow may already have cleared
            // the stack. There is nothing safe to replace in that state; clear
            // only Big Screen's stale navigation marker and let the enclosing
            // dismissal return ownership to Beat Saber.
            restoreCenterOnActivation = false;
            PaperLogger.warn(
                "Skipped center-page restoration because HMUI's main stack was already empty");
            return;
        }

        // This mirrors the working Close Showcase Menu path. AnimationType::None
        // makes the stack replacement synchronous before the parent flow is
        // dismissed; it does not attempt to reconstruct HMUI after gameplay.
        ReplaceTopViewController(
            centerViewController,
            nullptr,
            HMUI::ViewController::AnimationType::None,
            HMUI::ViewController::AnimationDirection::Horizontal);
        restoreCenterOnActivation = false;
    }

    void RestoreDistractionFreeMenu()
    {
        if(!distractionFreeMenuActive)
            return;

        // Clear the active marker first for the same fail-safe reason as the
        // foveation restoration path. Only objects that were visibly active
        // and explicitly hidden by Big Screen are ever recorded here.
        distractionFreeMenuActive = false;
        int restored = 0;
        for(auto object : hiddenMenuObjects)
        {
            try
            {
                if(object)
                {
                    object->SetActive(true);
                    ++restored;
                }
            }
            catch(...)
            {
                PaperLogger.error(
                    "Could not restore one distraction-free menu object");
                ErrorManager::Instance().RecordError(
                    "Restoring distraction-free menu objects",
                    "Beat Saber rejected one saved menu object");
            }
        }
        hiddenMenuObjects.clear();
        PaperLogger.info(
            "Restored {} distraction-free menu objects",
            restored);
    }

    void ApplyDistractionFreeMenu()
    {
        const auto& settings = Settings::Instance();
        if(!settings.ModEnabled() || !settings.DistractionFreeMenu())
        {
            RestoreDistractionFreeMenu();
            return;
        }
        if(distractionFreeMenuActive)
            return;

        distractionFreeMenuActive = true;
        hiddenMenuObjects.clear();
        const bool cachedSceneInvalid = std::any_of(
            knownDistractionObjects.begin(),
            knownDistractionObjects.end(),
            [](UnityW<UnityEngine::GameObject> object)
            {
                return !UnityW<UnityEngine::GameObject>::isAlive(
                    object.unsafePtr());
            });
        if(knownDistractionObjects.empty() || cachedSceneInvalid)
        {
            knownDistractionObjects.clear();
            for(auto* transform : UnityEngine::Object::FindObjectsOfType<
                    UnityEngine::Transform*>(true))
            {
                if(!transform)
                    continue;
                auto object = transform->get_gameObject();
                if(!object ||
                   !IsDistractionObjectName(
                       NormalizeObjectName(object->get_name())))
                    continue;
                knownDistractionObjects.emplace_back(object);
            }
            PaperLogger.info(
                "Cached {} distraction-free menu object(s) for this scene",
                knownDistractionObjects.size());
        }

        for(auto object : knownDistractionObjects)
        {
            if(!UnityW<UnityEngine::GameObject>::isAlive(object.unsafePtr()) ||
               !object->get_activeInHierarchy())
                continue;
            hiddenMenuObjects.emplace_back(object);
            object->SetActive(false);
            PaperLogger.info(
                "Distraction Free Menu hid '{}'",
                std::string(object->get_name()));
        }
        PaperLogger.info(
            "Distraction Free Menu active with {} hidden objects",
            hiddenMenuObjects.size());
    }

    void MenuFlowCoordinator::DidActivate(
        bool firstActivation,
        bool addedToHierarchy,
        bool screenSystemEnabling)
    {
        const auto activationStarted = std::chrono::steady_clock::now();
        const std::string requestedEditorLevelId =
            std::exchange(pendingVideoEditorLevelId, {});
        try
        {
        activeMenuFlow = this;
        if(Settings::Instance().DetailedDiagnosticLoggingEnabled() &&
           !DiagnosticSessionLogger::Instance().MenuSessionActive())
        {
            DiagnosticSessionLogger::Instance().BeginMenuSession({
                {"firstActivation", firstActivation ? "true" : "false"},
                {"modEnabled", Settings::Instance().ModEnabled()
                    ? "true" : "false"},
                {"activeLayout", std::to_string(
                    Settings::Instance().ActiveScreenLayout() + 1)},
                {"menuEnvironment", Settings::Instance().ShowMenuEnvironment()
                    ? "visible" : "hidden"}});
        }
        // Do not call HMUI::FlowCoordinator::DidActivate from a custom-types
        // override. The generated CORDL wrapper performs virtual dispatch, so
        // calling it through the base type returns to this override and grows
        // the native stack until Beat Saber crashes. HMUI lifecycle overrides
        // are expected to provide their own activation work, as BSML's own
        // flow coordinators do.
        (void)addedToHierarchy;
        (void)screenSystemEnabling;

        // This override is scoped to Big Screen's own flow coordinator. Beat
        // Saber's menu/gameplay values are saved before any change and restored
        // as soon as this page leaves the screen hierarchy.
        DisableFoveationForMenu();
        ApplyDistractionFreeMenu();
        MenuPlacementGuide::Instance().Apply();
        // Apply the environment last so its Off state remains authoritative
        // even if the positive floor toggle restored a renderer moments ago.
        MenuEnvironmentVisibility::Instance().Apply();

        // The standard title and Back strip spans the center screen and partly
        // covers the world-space placement preview. SettingsMenu recreates both
        // controls inside the left panel, so leave the center header empty.
        SetTitle("", HMUI::ViewController::AnimationType::None);
        set_showBackButton(false);

        if(firstActivation)
        {
            // Native menu singletons survive MenuCore soft restarts even when
            // every IL2CPP view they cached was destroyed. Clear that old
            // scene before attaching the replacement controllers.
            SettingsMenu::Instance().ForgetUi();
            VideoLibraryMenu::Instance().ForgetUi();
            StorageMaintenanceMenu::Instance().ForgetUi();
            ShowcaseMenu::Instance().ForgetUi();
            LocalVideoBrowserMenu::Instance().ForgetUi();
            ThumbnailPickerMenu::Instance().ForgetUi();
            centerViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();
            settingsViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();
            libraryBrowserViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();
            libraryEditorViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();
            storageViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();
            showcaseViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();
            localVideoBrowserViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();
            thumbnailPickerViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();

            SettingsMenu::Instance().CreateUi(
                settingsViewController,
                [this]()
                {
                    BackButtonWasPressed(centerViewController);
                },
                [this]()
                {
                    DiagnosticSessionLogger::Instance().MenuEvent(
                        "page_opened", "SettingsMenu",
                        {{"page", "storage_maintenance"}});
                    // Storage maintenance belongs on the center screen and is
                    // intentionally non-playback UI. Stop both song audio and
                    // video before presenting it, but leave downloads and the
                    // right-side library state intact.
                    VideoLibraryMenu::Instance().StopActivePreview();
                    StorageMaintenanceMenu::Instance().Show();
                    restoreCenterOnActivation = true;
                    ReplaceTopViewController(
                        storageViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::In,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                },
                [this]()
                {
                    DiagnosticSessionLogger::Instance().MenuEvent(
                        "page_opened", "SettingsMenu",
                        {{"page", "showcase"}});
                    // The showcase readiness page owns all of its download
                    // actions. Opening it is read-only apart from stopping an
                    // active preview so its center-screen status remains easy
                    // to understand.
                    VideoLibraryMenu::Instance().StopActivePreview();
                    ShowcaseMenu::Instance().Show();
                    restoreCenterOnActivation = true;
                    ReplaceTopViewController(
                        showcaseViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::In,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                },
                [this](bool enabled) { ApplyModEnabledUi(enabled); });
            VideoLibraryMenu::Instance().CreateUi(
                libraryBrowserViewController,
                libraryEditorViewController,
                [this](bool showEditor)
                {
                    DiagnosticSessionLogger::Instance().MenuEvent(
                        "page_changed", "VideoLibraryMenu",
                        {{"page", showEditor ? "video_editor" : "song_list"}});
                    SetRightScreenViewController(
                        showEditor
                            ? libraryEditorViewController
                            : libraryBrowserViewController,
                        HMUI::ViewController::AnimationType::In);
                },
                [this](GlobalNamespace::BeatmapLevel* level)
                {
                    DiagnosticSessionLogger::Instance().MenuEvent(
                        "page_opened", "VideoLibraryMenu",
                        {{"page", "local_video_browser"}});
                    VideoLibraryMenu::Instance().StopActivePreview();
                    LocalVideoBrowserMenu::Instance().Show(level);
                    restoreCenterOnActivation = true;
                    ReplaceTopViewController(
                        localVideoBrowserViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::In,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                },
                [this](GlobalNamespace::BeatmapLevel* level)
                {
                    DiagnosticSessionLogger::Instance().MenuEvent(
                        "page_opened", "VideoLibraryMenu",
                        {{"page", "thumbnail_picker"}});
                    // The picker opens its own read-only decoder on the same
                    // file, so the library preview must release its decoder
                    // and audio first, exactly like the file-browser path.
                    VideoLibraryMenu::Instance().StopActivePreview();
                    ThumbnailPickerMenu::Instance().Show(level);
                    restoreCenterOnActivation = true;
                    ReplaceTopViewController(
                        thumbnailPickerViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::In,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                });
            StorageMaintenanceMenu::Instance().CreateUi(
                storageViewController,
                [this]()
                {
                    restoreCenterOnActivation = false;
                    ReplaceTopViewController(
                        centerViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::Out,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                });
            ShowcaseMenu::Instance().CreateUi(
                showcaseViewController,
                [this]()
                {
                    restoreCenterOnActivation = false;
                    ReplaceTopViewController(
                        centerViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::Out,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                });
            LocalVideoBrowserMenu::Instance().CreateUi(
                localVideoBrowserViewController,
                [this]()
                {
                    restoreCenterOnActivation = false;
                    ReplaceTopViewController(
                        centerViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::Out,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                },
                [this](const std::string& fileName)
                {
                    restoreCenterOnActivation = false;
                    ReplaceTopViewController(
                        centerViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::Out,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                    VideoLibraryMenu::Instance().LocalVideoAssignmentChanged(
                        fileName);
                });
            ThumbnailPickerMenu::Instance().CreateUi(
                thumbnailPickerViewController,
                [this]()
                {
                    restoreCenterOnActivation = false;
                    ReplaceTopViewController(
                        centerViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::Out,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                },
                [this](const std::string& thumbnailPath)
                {
                    restoreCenterOnActivation = false;
                    ReplaceTopViewController(
                        centerViewController,
                        nullptr,
                        HMUI::ViewController::AnimationType::Out,
                        HMUI::ViewController::AnimationDirection::Horizontal);
                    VideoLibraryMenu::Instance().LocalThumbnailChanged(
                        thumbnailPath);
                });

            const bool openedRequestedEditor =
                !requestedEditorLevelId.empty() &&
                Settings::Instance().ModEnabled() &&
                VideoLibraryMenu::Instance().OpenEditorForLevelId(
                    requestedEditorLevelId,
                    false);

            // Main, left, right, bottom, and top are supplied in that order.
            // Keeping main empty leaves the user's forward view clear while
            // the complete settings list remains available on the left.
            ProvideInitialViewControllers(
                centerViewController,
                settingsViewController,
                Settings::Instance().ModEnabled()
                    ? (openedRequestedEditor
                        ? libraryEditorViewController
                        : libraryBrowserViewController)
                    : nullptr,
                nullptr,
                nullptr);
            restoreCenterOnActivation = false;
            // ProvideInitialViewControllers is the only safe way to choose
            // panels during first activation. Calling ReplaceTopViewController
            // here previously queried a top controller before HMUI had
            // established one and terminated the game with an uncaught IL2CPP
            // exception. ApplyModEnabledUi is reserved for later live changes.
            ScreenPreview::Instance().ActivateCurrentState();
            PerformancePanel::Instance().ActivateMenu();
            LogMenuLifecycleDuration(
                "activation", activationStarted, firstActivation);
            return;
        }

        // BSML retains flow coordinators between visits, while the world-space
        // preview is deliberately recreated for each visit.
        SettingsMenu::Instance().RefreshControls();
        // HMUI retains the main-view stack, including a picker/browser that
        // was active when the complete flow was dismissed. Restore only when
        // our explicit navigation state says a transient page was retained.
        // In particular, never pass the center controller to
        // SetTopScreenViewController: despite its similar name, that method
        // owns the separate physical panel above the player and does not
        // replace the active controller in the center stack. Doing so gave one
        // controller two incompatible screen roles and eventually prevented
        // Beat Saber's MainMenuViewController from returning after dismissal.
        // Also do not query get_topViewController during activation. Beat Saber
        // can temporarily clear that property after gameplay, and the generated
        // getter throws instead of returning null in that state.
        if(restoreCenterOnActivation)
        {
            ThumbnailPickerMenu::Instance().Hide();
            LocalVideoBrowserMenu::Instance().CancelScan();
            auto* mainControllers =
                __cordl_internal_get__mainScreenViewControllers();
            if(centerViewController && mainControllers &&
               mainControllers->get_Count() > 0)
            {
                ReplaceTopViewController(
                    centerViewController,
                    nullptr,
                    HMUI::ViewController::AnimationType::None,
                    HMUI::ViewController::AnimationDirection::Horizontal);
            }
            else
            {
                // Never ask ReplaceTopViewController to index an empty list.
                // Side panels can still be activated normally, and the neutral
                // center view is intentionally blank in Big Screen's layout.
                PaperLogger.warn(
                    "Cleared stale Big Screen center navigation after HMUI emptied its stack");
            }
            restoreCenterOnActivation = false;
        }
        ApplyModEnabledUi(Settings::Instance().ModEnabled());
        if(!requestedEditorLevelId.empty() &&
           Settings::Instance().ModEnabled() &&
           !VideoLibraryMenu::Instance().OpenEditorForLevelId(
               requestedEditorLevelId))
        {
            PaperLogger.warn(
                "Big Screen opened from Solo, but the requested video editor could not be selected");
        }
        ScreenPreview::Instance().ActivateCurrentState();
        PerformancePanel::Instance().ActivateMenu();
        LogMenuLifecycleDuration(
            "activation", activationStarted, firstActivation);
        }
        catch(const std::exception& exception)
        {
            ErrorManager::Instance().ReportInternal(
                "opening the Big Screen menu", exception.what());
        }
        catch(...)
        {
            ErrorManager::Instance().ReportInternal(
                "opening the Big Screen menu", "Unknown native exception");
        }
    }

    void MenuFlowCoordinator::ApplyModEnabledUi(bool enabled)
    {
        if(!centerViewController)
            return;

        // Tear down the editor before changing HMUI ownership. If replacing
        // the right controller throws, the selected map's notice/decoder state
        // has still crossed its mandatory close boundary.
        if(!enabled)
            VideoLibraryMenu::Instance().Deactivate();

        // The Video Library is functional Big Screen UI rather than navigation
        // needed to recover from a disabled mod. Remove its right panel until
        // the master switch is turned back on. Do not touch the center stack
        // here: its current page is managed only by the explicit storage and
        // local-file-browser navigation callbacks above. Disabling is the one
        // exception: no mod-owned decoder or scanner may remain behind an
        // inaccessible subpage.
        if(!enabled && restoreCenterOnActivation)
        {
            ThumbnailPickerMenu::Instance().Hide();
            LocalVideoBrowserMenu::Instance().CancelScan();
            ShowcaseMenu::Instance().DismissTransientUi();
            auto* mainControllers =
                __cordl_internal_get__mainScreenViewControllers();
            if(mainControllers && mainControllers->get_Count() > 0)
            {
                ReplaceTopViewController(
                    centerViewController,
                    nullptr,
                    HMUI::ViewController::AnimationType::None,
                    HMUI::ViewController::AnimationDirection::Horizontal);
            }
            else
            {
                PaperLogger.warn(
                    "Skipped disabled-menu center restoration because HMUI's main stack was empty");
            }
            restoreCenterOnActivation = false;
        }
        SetRightScreenViewController(
            enabled ? libraryBrowserViewController : nullptr,
            HMUI::ViewController::AnimationType::None);
        if(enabled)
            VideoLibraryMenu::Instance().Refresh();
    }

    void MenuFlowCoordinator::DidDeactivate(
        bool removedFromHierarchy,
        bool screenSystemDisabling)
    {
        const auto deactivationStarted = std::chrono::steady_clock::now();
        const bool returnToSongSelection =
            activeMenuFlow.ptr() == this && activeLaunchFromSongSelection;
        DiagnosticSessionLogger::Instance().MenuEvent(
            "menu_deactivation_started", "MenuFlowCoordinator");
        DismissTrackedMenuModals();

        // Clear the selected map and its operation notice before any unrelated
        // Unity teardown. PerformancePanel or ScreenPreview can encounter a
        // destroyed scene object; that must never prevent the video editor's
        // native strings/tokens and map-owned TMP surface from being retired.
        try
        {
            VideoLibraryMenu::Instance().Deactivate();
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Video Library deactivation failed while closing Big Screen: {}",
                exception.what());
            ErrorManager::Instance().RecordError(
                "Closing the Video Library", exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Video Library deactivation failed while closing Big Screen");
            ErrorManager::Instance().RecordError(
                "Closing the Video Library", "Unknown native exception");
        }

        try
        {
        if(!returnToSongSelection)
            BeginMenuReentryGuard();
        if(activeMenuFlow.ptr() == this)
            activeMenuFlow = nullptr;
        PerformancePanel::Instance().SuspendMenu();
        // The world screen only belongs to this page. Releasing it here keeps
        // the placement preview at zero GPU cost everywhere else in the menu.
        ScreenPreview::Instance().Suspend();
        // Closes the picker's private decoder if the whole menu is dismissed
        // while a frame was still being chosen; nothing was saved yet.
        ThumbnailPickerMenu::Instance().Hide();
        LocalVideoBrowserMenu::Instance().CancelScan();
        ShowcaseMenu::Instance().DismissTransientUi();
        MenuEnvironmentVisibility::Instance().Restore();
        MenuPlacementGuide::Instance().Suspend();
        RestoreDistractionFreeMenu();
        RestoreMenuFoveation();

        // The generated base wrapper has the same virtual-dispatch behavior as
        // DidActivate, so this override must not call it directly either.
        (void)removedFromHierarchy;
        (void)screenSystemDisabling;
        DiagnosticSessionLogger::Instance().EndMenuSession("menu_closed");
        FinishMenuLaunch(returnToSongSelection);
        LogMenuLifecycleDuration("deactivation", deactivationStarted);
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Big Screen menu deactivation failed: {}",
                exception.what());
            ErrorManager::Instance().RecordError(
                "Closing the Big Screen menu", exception.what());
            // These restorations are individually fail-safe and must still be
            // attempted if another teardown action threw first.
            MenuEnvironmentVisibility::Instance().Restore();
            MenuPlacementGuide::Instance().Suspend();
            RestoreDistractionFreeMenu();
            RestoreMenuFoveation();
            activeMenuFlow = nullptr;
            FinishMenuLaunch(returnToSongSelection);
            DiagnosticSessionLogger::Instance().EndMenuSession(
                "menu_teardown_error");
            LogMenuLifecycleDuration("failed deactivation", deactivationStarted);
        }
        catch(...)
        {
            PaperLogger.error("Big Screen menu deactivation failed");
            ErrorManager::Instance().RecordError(
                "Closing the Big Screen menu", "Unknown native exception");
            MenuEnvironmentVisibility::Instance().Restore();
            MenuPlacementGuide::Instance().Suspend();
            RestoreDistractionFreeMenu();
            RestoreMenuFoveation();
            activeMenuFlow = nullptr;
            FinishMenuLaunch(returnToSongSelection);
            DiagnosticSessionLogger::Instance().EndMenuSession(
                "menu_teardown_error");
            LogMenuLifecycleDuration("failed deactivation", deactivationStarted);
        }
    }

    void MenuFlowCoordinator::BackButtonWasPressed(
        HMUI::ViewController* topViewController)
    {
        try
        {
        (void)topViewController;
        // The custom left-panel Back button routes through the same prompt,
        // but Beat Saber's controller/back action can call this override
        // directly. Cover both paths so no normal menu exit silently drops an
        // unlocked screen edit.
        if(ScreenPreview::Instance().IsUndockedEditing())
        {
            SettingsMenu::Instance().RequestLeave([this]()
            {
                BackButtonWasPressed(centerViewController);
            });
            return;
        }
        auto parent = __cordl_internal_get__parentFlowCoordinator();
        if(parent)
        {
            // Restore a transient storage/showcase/browser page while HMUI's
            // center stack is still valid. Waiting until the next activation
            // is unsafe because Beat Saber can clear that list after dismissal.
            PrepareForDismissal();
            if(!activeLaunchFromSongSelection)
                BeginMenuReentryGuard();
            parent->DismissFlowCoordinator(
                this,
                HMUI::ViewController::AnimationDirection::Horizontal,
                nullptr,
                true);
        }
        }
        catch(const std::exception& exception)
        {
            ErrorManager::Instance().ReportInternal(
                "closing the Big Screen menu", exception.what());
        }
        catch(...)
        {
            ErrorManager::Instance().ReportInternal(
                "closing the Big Screen menu", "Unknown native exception");
        }
    }
}
