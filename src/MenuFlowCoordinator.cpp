#include "BigScreen/MenuFlowCoordinator.hpp"

#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "BigScreen/StorageMaintenanceMenu.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "GlobalNamespace/OVRManager.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Transform.hpp"
#include "bsml/shared/Helpers/creation.hpp"
#include "main.hpp"

#include <cctype>
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
                    GlobalNamespace::__OVRManager__FoveatedRenderingLevel{
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
                    GlobalNamespace::__OVRManager__FoveatedRenderingLevel::Off);
                PaperLogger.info(
                    "Temporarily disabled foveated rendering for Big Screen's menu (saved level {}, dynamic {})",
                    savedFoveationLevel,
                    savedDynamicFoveation ? "enabled" : "disabled");
            }
            catch(...)
            {
                PaperLogger.error(
                    "Could not disable foveated rendering for Big Screen's menu");
                RestoreMenuFoveation();
            }
        }
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

        // The standard title and Back strip spans the center screen and partly
        // covers the world-space placement preview. SettingsMenu recreates both
        // controls inside the left panel, so leave the center header empty.
        SetTitle("", HMUI::ViewController::AnimationType::None);
        set_showBackButton(false);

        if(firstActivation)
        {
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

            SettingsMenu::Instance().CreateUi(
                settingsViewController,
                [this]()
                {
                    BackButtonWasPressed(centerViewController);
                },
                [this]()
                {
                    StorageMaintenanceMenu::Instance().Show();
                    SetRightScreenViewController(
                        storageViewController,
                        HMUI::ViewController::AnimationType::In);
                });
            VideoLibraryMenu::Instance().CreateUi(
                libraryBrowserViewController,
                libraryEditorViewController,
                [this](bool showEditor)
                {
                    SetRightScreenViewController(
                        showEditor
                            ? libraryEditorViewController
                            : libraryBrowserViewController,
                        HMUI::ViewController::AnimationType::In);
                });
            StorageMaintenanceMenu::Instance().CreateUi(
                storageViewController,
                [this]()
                {
                    SetRightScreenViewController(
                        libraryBrowserViewController,
                        HMUI::ViewController::AnimationType::Out);
                });

            // Main, left, right, bottom, and top are supplied in that order.
            // Keeping main empty leaves the user's forward view clear while
            // the complete settings list remains available on the left.
            ProvideInitialViewControllers(
                centerViewController,
                settingsViewController,
                libraryBrowserViewController,
                nullptr,
                nullptr);
            ScreenPreview::Instance().ActivateCurrentState();
            return;
        }

        // BSML retains flow coordinators between visits, while the world-space
        // preview is deliberately recreated for each visit.
        SettingsMenu::Instance().RefreshControls();
        VideoLibraryMenu::Instance().Refresh();
        SetRightScreenViewController(
            libraryBrowserViewController,
            HMUI::ViewController::AnimationType::None);
        ScreenPreview::Instance().ActivateCurrentState();
    }

    void MenuFlowCoordinator::DidDeactivate(
        bool removedFromHierarchy,
        bool screenSystemDisabling)
    {
        // The world screen only belongs to this page. Releasing it here keeps
        // the placement preview at zero GPU cost everywhere else in the menu.
        ScreenPreview::Instance().Suspend();
        VideoLibraryMenu::Instance().Deactivate();
        RestoreDistractionFreeMenu();
        RestoreMenuFoveation();

        // The generated base wrapper has the same virtual-dispatch behavior as
        // DidActivate, so this override must not call it directly either.
        (void)removedFromHierarchy;
        (void)screenSystemDisabling;
    }

    void MenuFlowCoordinator::BackButtonWasPressed(
        HMUI::ViewController* topViewController)
    {
        (void)topViewController;
        auto parent = __cordl_internal_get__parentFlowCoordinator();
        if(parent)
        {
            parent->DismissFlowCoordinator(
                this,
                HMUI::ViewController::AnimationDirection::Horizontal,
                nullptr,
                false);
        }
    }
}
