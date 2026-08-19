// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/MenuFlowCoordinator.hpp"

#include "BigScreen/CenterScreenModal.hpp"
#include "BigScreen/DiagnosticSessionLogger.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/LocalVideoBrowserMenu.hpp"
#include "BigScreen/MenuEnvironmentVisibility.hpp"
#include "BigScreen/MenuPlacementGuide.hpp"
#include "BigScreen/PerformancePanel.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "BigScreen/ShowcaseMenu.hpp"
#include "BigScreen/StorageMaintenanceMenu.hpp"
#include "BigScreen/ThumbnailPickerMenu.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/MainMenuViewController.hpp"
#include "GlobalNamespace/OVRManager.hpp"
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

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

DEFINE_TYPE(BigScreen, MenuFlowCoordinator);

namespace BigScreen {
    namespace {
        bool foveationOverrideActive = false;
        int savedFoveationLevel = 0;
        bool savedDynamicFoveation = false;
        bool distractionFreeMenuActive = false;
        std::vector<UnityW<UnityEngine::GameObject>> hiddenMenuObjects;
        UnityW<MenuFlowCoordinator> activeMenuFlow = nullptr;
        BSML::MenuButton* bigScreenMenuButton = nullptr;
        bool menuReentryBlocked = false;
        float menuReentryNotBefore = 0.0f;
        int stableMainMenuFrames = 0;
        UnityW<MenuFlowCoordinator> pendingFailedMenuExit = nullptr;
        int pendingFailedMenuExitFrames = 0;

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
            // the parent and its main view have both remained stable.
            menuReentryBlocked = true;
            stableMainMenuFrames = 0;
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
    }

    bool IsBigScreenMenuActive()
    {
        return activeMenuFlow;
    }

    HMUI::ViewController* ActiveCenterModalHost() noexcept
    {
        try
        {
            auto* coordinator = activeMenuFlow.ptr();
            if(!coordinator)
                return nullptr;
            auto* controllers =
                coordinator->__cordl_internal_get__mainScreenViewControllers();
            if(!controllers || controllers->get_Count() <= 0)
                return coordinator->centerViewController;
            return controllers->get_Item(controllers->get_Count() - 1);
        }
        catch(...)
        {
            return nullptr;
        }
    }

    void ShowModalOnCenterScreen(BSML::ModalView* modal) noexcept
    {
        if(!modal)
            return;
        try
        {
            // Modal objects are retained with their menu controllers. Resolve
            // the top center page every time so opening Storage, Showcase, or
            // the file browser cannot leave a side-triggered dialog parented
            // beneath an inactive neutral center controller.
            if(auto* host = ActiveCenterModalHost())
            {
                auto modalTransform = modal->get_transform();
                auto hostTransform = host->get_transform();
                if(modalTransform && hostTransform &&
                   modalTransform->get_parent().ptr() != hostTransform.ptr())
                {
                    modalTransform->SetParent(hostTransform.ptr(), false);
                }
            }
            modal->Show();
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not present a Big Screen dialog on the center screen: {}",
                exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not present a Big Screen dialog on the center screen");
        }
    }

    bool IsBigScreenMenuTransitionPending() noexcept
    {
        return menuReentryBlocked || pendingFailedMenuExit;
    }

    void TickMenuReentryGuard() noexcept
    {
        if(!menuReentryBlocked)
            return;
        try
        {
            // Reassert this in case BSML rebuilt its button view during the
            // parent transition after the guard first disabled the model.
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

            auto* parent = BSML::Helpers::GetMainFlowCoordinator();
            auto* mainMenu = parent
                ? parent->__cordl_internal_get__mainMenuViewController().ptr()
                : nullptr;
            const bool stable = parent && mainMenu &&
                parent->get_isActivated() && !parent->get_isInTransition() &&
                mainMenu->get_isActivated() &&
                !mainMenu->get_isInTransition() &&
                mainMenu->get_gameObject() &&
                mainMenu->get_gameObject()->get_activeInHierarchy();
            stableMainMenuFrames = stable ? stableMainMenuFrames + 1 : 0;
            if(stableMainMenuFrames < 12)
                return;

            menuReentryBlocked = false;
            stableMainMenuFrames = 0;
            SetBigScreenMenuButtonInteractable(true);
            PaperLogger.info(
                "Big Screen menu entry re-enabled after the main menu stabilized");
        }
        catch(const std::exception& exception)
        {
            // The guard is safety UI, not gameplay functionality. Keep the
            // entry disabled and retry on a later stable menu frame.
            stableMainMenuFrames = 0;
            PaperLogger.warn(
                "Could not verify Big Screen menu re-entry yet: {}",
                exception.what());
        }
        catch(...)
        {
            stableMainMenuFrames = 0;
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
        for(auto* transform : UnityEngine::Object::FindObjectsOfType<
                UnityEngine::Transform*>(true))
        {
            if(!transform)
                continue;
            auto object = transform->get_gameObject();
            if(!object || !object->get_activeInHierarchy())
                continue;

            const auto normalized = NormalizeObjectName(object->get_name());
            if(!IsDistractionObjectName(normalized))
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
                centerViewController,
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
                centerViewController,
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

            // Main, left, right, bottom, and top are supplied in that order.
            // Keeping main empty leaves the user's forward view clear while
            // the complete settings list remains available on the left.
            ProvideInitialViewControllers(
                centerViewController,
                settingsViewController,
                Settings::Instance().ModEnabled()
                    ? libraryBrowserViewController
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
            return;
        }

        // BSML retains flow coordinators between visits, while the world-space
        // preview is deliberately recreated for each visit.
        SettingsMenu::Instance().RefreshControls();
        VideoLibraryMenu::Instance().Refresh();
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
        ScreenPreview::Instance().ActivateCurrentState();
        PerformancePanel::Instance().ActivateMenu();
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
        if(!enabled)
            VideoLibraryMenu::Instance().Deactivate();
        else
            VideoLibraryMenu::Instance().Refresh();
    }

    void MenuFlowCoordinator::DidDeactivate(
        bool removedFromHierarchy,
        bool screenSystemDisabling)
    {
        DiagnosticSessionLogger::Instance().MenuEvent(
            "menu_deactivation_started", "MenuFlowCoordinator");
        try
        {
        BeginMenuReentryGuard();
        if(activeMenuFlow.ptr() == this)
            activeMenuFlow = nullptr;
        PerformancePanel::Instance().SuspendMenu();
        // The world screen only belongs to this page. Releasing it here keeps
        // the placement preview at zero GPU cost everywhere else in the menu.
        ScreenPreview::Instance().Suspend();
        VideoLibraryMenu::Instance().Deactivate();
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
            DiagnosticSessionLogger::Instance().EndMenuSession(
                "menu_teardown_error");
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
            DiagnosticSessionLogger::Instance().EndMenuSession(
                "menu_teardown_error");
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
