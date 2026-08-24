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
        // A retained FlowCoordinator receives DidActivate while HMUI is still
        // restoring its side screens. Replacing the right controller from that
        // callback can be overwritten by the enclosing presentation even though
        // the requested map was selected correctly. Preserve the level ID until
        // two normal menu frames have elapsed, then use VideoLibraryMenu's same
        // proven browser-to-editor navigation path as an ordinary row click.
        std::string pendingActivatedVideoEditorLevelId;
        int pendingVideoEditorNavigationFrames = 0;
        BSML::MenuButton* bigScreenMenuButton = nullptr;
        bool menuReentryBlocked = false;
        float menuReentryNotBefore = 0.0f;
        UnityW<MenuFlowCoordinator> pendingFailedMenuExit = nullptr;
        int pendingFailedMenuExitFrames = 0;
        std::vector<UnityW<BSML::ModalView>> frontmostMenuModals;
        int menuPrewarmStableFrames = 0;
        int menuPrewarmSpacingFrames = 0;

        // Let Beat Saber, MenuCore, and other mods finish their own first-menu
        // work before Big Screen starts allocating retained BSML objects. At
        // 90 Hz this is roughly one second; it is a stability window rather
        // than a wall-clock delay, so transitions reset it automatically.
        constexpr int MenuPrewarmStableFrameRequirement = 90;
        // Each logical page is built on its own update. A few empty frames
        // between stages give Unity time to process layout/mesh work instead
        // of converting several smaller operations back into one long spike.
        constexpr int MenuPrewarmStageSpacingFrames = 3;

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
            // hierarchy unresponsive. The guard is released by DidDeactivate,
            // which is HMUI's authoritative notification that this child has
            // actually left the hierarchy. The timestamp is only a fail-safe
            // for an interrupted teardown; it is not a user-visible cooldown.
            menuReentryBlocked = true;
            menuReentryNotBefore =
                UnityEngine::Time::get_realtimeSinceStartup() + 2.0f;
        }

        void CompleteMenuReentryGuard() noexcept
        {
            menuReentryBlocked = false;
            menuReentryNotBefore = 0.0f;
            // Prewarming and transition safety must never leave the public
            // MenuCore entry disabled. PresentSharedMenu validates the actual
            // parent transition again at the moment the user clicks it.
            SetBigScreenMenuButtonInteractable(true);
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
            {
                retainedMenuFlow =
                    BSML::Helpers::CreateFlowCoordinator<MenuFlowCoordinator*>();
                if(auto* coordinator = retainedMenuFlow.ptr())
                {
                    coordinator->menuUiConstructed = false;
                    coordinator->menuPrewarmStage = 0;
                }
            }
            return retainedMenuFlow.ptr();
        }

        bool StableMainMenuForPrewarm()
        {
            auto* mainFlow = BSML::Helpers::GetMainFlowCoordinator();
            auto* mainMenu = mainFlow
                ? mainFlow->__cordl_internal_get__mainMenuViewController().ptr()
                : nullptr;
            return mainFlow && mainMenu && !activeMenuFlow &&
                mainFlow->get_isActivated() &&
                !mainFlow->get_isInTransition() &&
                mainMenu->get_isActivated() &&
                !mainMenu->get_isInTransition() &&
                mainMenu->get_gameObject() &&
                mainMenu->get_gameObject()->get_activeInHierarchy();
        }

        void PrewarmDistractionFreeCache()
        {
            const bool cachedSceneInvalid = std::any_of(
                knownDistractionObjects.begin(),
                knownDistractionObjects.end(),
                [](UnityW<UnityEngine::GameObject> object)
                {
                    return !UnityW<UnityEngine::GameObject>::isAlive(
                        object.unsafePtr());
                });
            if(!knownDistractionObjects.empty() && !cachedSceneInvalid)
                return;

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
                "Prewarmed {} distraction-free menu object(s) for this scene",
                knownDistractionObjects.size());
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
            if(activeMenuFlow || pendingFailedMenuExit)
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
            if(menuReentryBlocked)
            {
                PaperLogger.info(
                    "Deferred Big Screen menu entry while the prior hierarchy finishes dismissing");
                return false;
            }

            auto* coordinator = ResolveMenuFlow();
            if(!coordinator || coordinator == parent)
                return false;
            pendingVideoEditorLevelId = std::string(editorLevelId);
            pendingActivatedVideoEditorLevelId.clear();
            pendingVideoEditorNavigationFrames = 0;
            activeLaunchFromSongSelection = requireSoloSelection;
            if(requireSoloSelection)
            {
                PaperLogger.info(
                    "Opening Big Screen from Solo for level '{}'",
                    pendingVideoEditorLevelId);
            }
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
            pendingActivatedVideoEditorLevelId.clear();
            pendingVideoEditorNavigationFrames = 0;
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

            if(!menuReentryBlocked)
                return;

            // Normal dismissal clears the guard in DidDeactivate. This branch
            // exists only for an interrupted lifecycle where that callback was
            // skipped after the retained coordinator was already destroyed.
            // Do not walk Unity parent pointers here: a destroyed FlowCoordinator
            // wrapper caused the former guard to throw every frame forever.
            if(UnityW<MenuFlowCoordinator>::isAlive(activeMenuFlow) ||
               UnityEngine::Time::get_realtimeSinceStartup() <
                   menuReentryNotBefore)
                return;

            CompleteMenuReentryGuard();
            PaperLogger.warn(
                "Big Screen menu entry recovered after an interrupted HMUI dismissal callback");
        }
        catch(const std::exception& exception)
        {
            PaperLogger.warn(
                "Could not service Big Screen's menu transition guard: {}",
                exception.what());
        }
        catch(...) {}
    }

    void TickPendingMenuNavigation() noexcept
    {
        if(pendingActivatedVideoEditorLevelId.empty())
            return;

        try
        {
            auto* coordinator = activeMenuFlow.ptr();
            if(!coordinator)
            {
                pendingActivatedVideoEditorLevelId.clear();
                pendingVideoEditorNavigationFrames = 0;
                return;
            }

            // DidActivate is invoked inside PresentFlowCoordinator's hierarchy
            // mutation. Waiting for two subsequent updates guarantees that the
            // enclosing transition has had a chance to publish its retained
            // side controllers before Big Screen replaces the browser with the
            // requested editor. This is scheduling only; descriptor I/O and
            // video preparation remain on their existing paths.
            if(++pendingVideoEditorNavigationFrames < 2 ||
               !coordinator->get_isActivated() ||
               coordinator->get_isInTransition())
                return;

            const std::string levelId =
                std::exchange(pendingActivatedVideoEditorLevelId, {});
            pendingVideoEditorNavigationFrames = 0;
            // navigateToEditor=true deliberately reuses ShowEditor's
            // established callback, including its page_changed diagnostic and
            // right-screen animation. Do not recreate that transition here.
            const bool opened =
                VideoLibraryMenu::Instance().OpenEditorForLevelId(
                    levelId,
                    true);
            if(!opened)
            {
                PaperLogger.warn(
                    "Big Screen opened from Solo, but level '{}' was not available to the video editor",
                    levelId);
            }
            else
            {
                PaperLogger.info(
                    "Completed Big Screen's deferred Configure Video navigation for level '{}'",
                    levelId);
            }
        }
        catch(const std::exception& exception)
        {
            const std::string levelId =
                std::exchange(pendingActivatedVideoEditorLevelId, {});
            pendingVideoEditorNavigationFrames = 0;
            PaperLogger.error(
                "Could not complete Big Screen's deferred Solo video shortcut for '{}': {}",
                levelId,
                exception.what());
            ErrorManager::Instance().ReportInternal(
                "opening the selected map's video editor",
                exception.what());
        }
        catch(...)
        {
            const std::string levelId =
                std::exchange(pendingActivatedVideoEditorLevelId, {});
            pendingVideoEditorNavigationFrames = 0;
            PaperLogger.error(
                "Could not complete Big Screen's deferred Solo video shortcut for '{}'",
                levelId);
            ErrorManager::Instance().ReportInternal(
                "opening the selected map's video editor",
                "Unknown native exception");
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

    void TickMenuPrewarm()
    {
        const auto& settings = Settings::Instance();
        if(!settings.ModEnabled() ||
           ErrorManager::Instance().MenuRecoveryActive())
        {
            // The master switch and circuit breaker are authoritative. Do not
            // create another Unity object after either disables Big Screen.
            // A partial retained hierarchy is harmless and can resume only if
            // the user deliberately enables the mod again.
            menuPrewarmStableFrames = 0;
            menuPrewarmSpacingFrames = 0;
            SetBigScreenMenuButtonInteractable(true);
            return;
        }

        if(UnityW<MenuFlowCoordinator>::isAlive(retainedMenuFlow) &&
           retainedMenuFlow->menuUiConstructed)
        {
            // Catalog descriptors are an optional cache warm-up, not part of
            // menu readiness. Continue a few entries at a time only while the
            // stock main menu is stable. If the user enters first, the library
            // uses the same incremental path while remaining interactive.
            if(StableMainMenuForPrewarm() &&
               menuPrewarmSpacingFrames++ >= MenuPrewarmStageSpacingFrames)
            {
                menuPrewarmSpacingFrames = 0;
                VideoLibraryMenu::Instance().PrewarmCatalogStep(4);
            }
            return;
        }

        if(!StableMainMenuForPrewarm())
        {
            menuPrewarmStableFrames = 0;
            menuPrewarmSpacingFrames = 0;
            return;
        }

        // This preparation is never an access gate. The MenuCore button remains
        // usable, and DidActivate can finish any remaining UI stages using the
        // same single authoritative builder if the player enters immediately.
        SetBigScreenMenuButtonInteractable(true);
        if(menuPrewarmStableFrames < MenuPrewarmStableFrameRequirement)
        {
            ++menuPrewarmStableFrames;
            return;
        }
        if(menuPrewarmSpacingFrames++ < MenuPrewarmStageSpacingFrames)
            return;
        menuPrewarmSpacingFrames = 0;

        auto* coordinator = ResolveMenuFlow();
        if(!coordinator)
            throw std::runtime_error(
                "Big Screen could not allocate its retained menu coordinator");
        if(!coordinator->PrewarmNextMenuStage())
            return;

        SetBigScreenMenuButtonInteractable(true);
        PaperLogger.info(
            "Big Screen staged menu prewarming completed; first entry can reuse the retained UI");
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
        PrewarmDistractionFreeCache();

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

    bool MenuFlowCoordinator::PrewarmNextMenuStage()
    {
        if(menuUiConstructed)
            return true;

        const auto stageStarted = std::chrono::steady_clock::now();
        const char* stageName = "complete";
        bool stageComplete = true;
        switch(menuPrewarmStage)
        {
            case 0:
            {
                stageName = "retained controllers";
                // Native menu singletons survive MenuCore soft restarts even
                // when every IL2CPP view they cached was destroyed. This is
                // the single authoritative scene boundary: clear the old
                // pointers before any replacement controller is constructed.
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
                if(!centerViewController || !settingsViewController ||
                   !libraryBrowserViewController || !libraryEditorViewController ||
                   !storageViewController || !showcaseViewController ||
                   !localVideoBrowserViewController ||
                   !thumbnailPickerViewController)
                {
                    throw std::runtime_error(
                        "one or more retained Big Screen view controllers could not be created");
                }
                break;
            }
            case 1:
                stageName = "settings page";
                SettingsMenu::Instance().CreateUi(
                    settingsViewController,
                    [this]() { BackButtonWasPressed(centerViewController); },
                    [this]()
                    {
                        DiagnosticSessionLogger::Instance().MenuEvent(
                            "page_opened", "SettingsMenu",
                            {{"page", "storage_maintenance"}});
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
                break;
            case 2:
                stageName = "video-library pages";
                VideoLibraryMenu::Instance().CreateUi(
                    libraryBrowserViewController,
                    libraryEditorViewController,
                    [this](bool showEditor)
                    {
                        DiagnosticSessionLogger::Instance().MenuEvent(
                            "page_changed", "VideoLibraryMenu",
                            {{"page", showEditor
                                ? "video_editor" : "song_list"}});
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
                        VideoLibraryMenu::Instance().StopActivePreview();
                        ThumbnailPickerMenu::Instance().Show(level);
                        restoreCenterOnActivation = true;
                        ReplaceTopViewController(
                            thumbnailPickerViewController,
                            nullptr,
                            HMUI::ViewController::AnimationType::In,
                            HMUI::ViewController::AnimationDirection::Horizontal);
                    },
                    false);
                break;
            case 3:
                stageName = "storage page";
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
                break;
            case 4:
                stageName = "showcase page";
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
                break;
            case 5:
                stageName = "local-video browser";
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
                break;
            case 6:
                stageName = "thumbnail picker";
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
                break;
            case 7:
                stageName = "menu-environment cache";
                MenuEnvironmentVisibility::Instance().PrewarmCache();
                break;
            case 8:
                stageName = "menu-floor cache";
                MenuPlacementGuide::Instance().PrewarmCache();
                break;
            case 9:
                stageName = "distraction-object cache";
                PrewarmDistractionFreeCache();
                break;
            default:
                stageName = "completion marker";
                restoreCenterOnActivation = false;
                menuUiConstructed = true;
                break;
        }

        if(!stageComplete)
            return false;

        ++menuPrewarmStage;
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stageStarted).count();
        PaperLogger.info(
            "Big Screen menu prewarm stage '{}' completed in {} ms",
            stageName,
            elapsed);
        return menuUiConstructed;
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
            // Enabled sessions normally reach this callback with every page
            // already constructed by TickMenuPrewarm. The loop is retained
            // only for the explicit recovery path where the master switch was
            // off and automatic preparation was intentionally suppressed. It
            // prevents two independent builders from drifting out of sync.
            while(!menuUiConstructed)
                PrewarmNextMenuStage();

            // Construction does not claim song-preview ownership. Activation
            // is the first point at which the Video Library may rebuild its
            // retained catalog or interact with the selected-song preview.
            VideoLibraryMenu::Instance().Refresh();
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
        const bool modEnabled = Settings::Instance().ModEnabled();
        ApplyModEnabledUi(modEnabled);
        if(!requestedEditorLevelId.empty() && modEnabled)
        {
            // The browser is now HMUI's one controller for this activation.
            // Schedule the normal browser-to-editor callback after the parent
            // presentation finishes; a direct SetRightScreenViewController in
            // DidActivate was accepted internally but then visually overwritten.
            pendingActivatedVideoEditorLevelId = requestedEditorLevelId;
            pendingVideoEditorNavigationFrames = 0;
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
        pendingActivatedVideoEditorLevelId.clear();
        pendingVideoEditorNavigationFrames = 0;
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
        if(activeMenuFlow.ptr() == this)
            activeMenuFlow = nullptr;
        // DidDeactivate is HMUI's completion boundary for the dismissal that
        // began in BackButtonWasPressed (or an error/showcase exit). Re-enable
        // both entry paths here instead of polling Unity parent objects after
        // they may already have been destroyed.
        CompleteMenuReentryGuard();
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
            CompleteMenuReentryGuard();
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
            CompleteMenuReentryGuard();
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
