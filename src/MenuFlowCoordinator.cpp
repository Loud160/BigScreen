#include "BigScreen/MenuFlowCoordinator.hpp"

#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "bsml/shared/Helpers/creation.hpp"

DEFINE_TYPE(BigScreen, MenuFlowCoordinator);

namespace BigScreen {
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

        SetTitle("Big Screen", HMUI::ViewController::AnimationType::None);
        set_showBackButton(true);

        if(firstActivation)
        {
            settingsViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();
            previewViewController =
                BSML::Helpers::CreateViewController<HMUI::ViewController*>();

            SettingsMenu::Instance().CreateUi(settingsViewController);
            ScreenPreview::Instance().Bind(this, previewViewController);

            const auto& settings = Settings::Instance();
            auto* initialPreview =
                settings.ModEnabled() && settings.MenuScreenPreviewEnabled()
                ? previewViewController
                : nullptr;
            ProvideInitialViewControllers(
                settingsViewController,
                nullptr,
                initialPreview,
                nullptr,
                nullptr);
            ScreenPreview::Instance().ActivateCurrentState();
            return;
        }

        // BSML retains flow coordinators between visits. Rebind the native
        // preview owner after a prior deactivation released its render target.
        ScreenPreview::Instance().Bind(this, previewViewController);
        SettingsMenu::Instance().RefreshControls();
        ScreenPreview::Instance().ActivateCurrentState();
    }

    void MenuFlowCoordinator::DidDeactivate(
        bool removedFromHierarchy,
        bool screenSystemDisabling)
    {
        // A hidden camera would continue rendering every menu frame. Releasing
        // it here keeps the optional preview at zero GPU cost outside this page.
        ScreenPreview::Instance().Suspend();

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
