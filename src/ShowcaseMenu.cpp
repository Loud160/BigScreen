// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/ShowcaseMenu.hpp"
#include "BigScreen/MenuModal.hpp"

#include <string>
#include <utility>

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/ShowcaseLauncher.hpp"
#include "BigScreen/UiUtility.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"

namespace BigScreen {
    namespace {
        using UiUtility::EnsureLayout;

        constexpr float PanelWidth = 104.0f;

        void ConfigureLayout(
            UnityEngine::Component* component,
            float width,
            float height,
            float flexibleWidth = 0.0f)
        {
            if(auto* layout = EnsureLayout(component))
            {
                layout->set_preferredWidth(width);
                layout->set_preferredHeight(height);
                layout->set_flexibleWidth(flexibleWidth);
                layout->set_flexibleHeight(0.0f);
            }
        }

        TMPro::TextMeshProUGUI* CreateRowLabel(
            UnityEngine::Component* parent,
            std::string text,
            float width,
            TMPro::TextAlignmentOptions alignment)
        {
            auto* label = BSML::Lite::CreateText(parent, text, 3.25f);
            if(!label)
                return nullptr;
            ConfigureLayout(label, width, 7.0f);
            label->set_enableWordWrapping(false);
            label->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
            label->set_alignment(alignment);
            return label;
        }

        void SetStatus(
            TMPro::TextMeshProUGUI* label,
            bool ready,
            std::string_view readyText,
            std::string_view missingText)
        {
            if(!label)
                return;
            label->set_text(std::string(ready ? readyText : missingText));
            label->set_color(
                ready
                    ? UnityEngine::Color{0.35f, 1.0f, 0.45f, 1.0f}
                    : UnityEngine::Color{1.0f, 0.45f, 0.35f, 1.0f});
        }
    }

    ShowcaseMenu& ShowcaseMenu::Instance()
    {
        static ShowcaseMenu menu;
        return menu;
    }

    void ShowcaseMenu::ForgetUi()
    {
        DismissTransientUi();
        *this = ShowcaseMenu{};
    }

    void ShowcaseMenu::DismissTransientUi() noexcept
    {
        try
        {
            if(!UnityW<BSML::ModalView>::isAlive(warningModal_.unsafePtr()))
                return;
            warningModal_->Hide();
            // ModalView's blocker can survive a retained FlowCoordinator
            // transition even after Hide queued its animation. Deactivating
            // the complete modal root immediately guarantees it cannot remain
            // above the next center page and consume every pointer event.
            if(auto object = warningModal_->get_gameObject())
                object->SetActive(false);
        }
        catch(...)
        {
            // Teardown must remain safe when Unity has already destroyed the
            // retained controller. ForgetUi will clear the stale pointer.
        }
    }

    void ShowcaseMenu::CreateUi(
        HMUI::ViewController* controller,
        std::function<void()> onClose)
    {
        if(!controller)
        {
            ErrorManager::Instance().RecordError(
                "Creating the showcase menu",
                "Beat Saber did not provide a center view controller");
            return;
        }
        controller_ = controller;
        onClose_ = std::move(onClose);

        auto* root = BSML::Lite::CreateVerticalLayoutGroup(controller);
        if(!root)
        {
            ErrorManager::Instance().RecordError(
                "Creating the showcase menu",
                "BSML could not create the showcase root layout");
            return;
        }
        root->set_spacing(0.8f);
        root->set_childControlWidth(true);
        root->set_childControlHeight(true);
        root->set_childForceExpandWidth(true);
        root->set_childForceExpandHeight(false);
        root->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
        if(auto* fitter = root->get_gameObject()
               ->GetComponent<UnityEngine::UI::ContentSizeFitter*>())
        {
            fitter->set_horizontalFit(
                UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
            fitter->set_verticalFit(
                UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
        }
        if(auto rect = root->get_rectTransform())
        {
            rect->set_anchorMin({0.5f, 0.0f});
            rect->set_anchorMax({0.5f, 1.0f});
            rect->set_pivot({0.5f, 0.5f});
            rect->set_anchoredPosition({0.0f, 0.0f});
            rect->set_sizeDelta({PanelWidth, -5.0f});
        }

        auto* close = BSML::Lite::CreateUIButton(
            root,
            "Close Showcase Menu",
            {0.0f, 0.0f},
            {PanelWidth, 7.0f},
            [this]()
            {
                DismissTransientUi();
                if(onClose_)
                    onClose_();
            });
        ConfigureLayout(close, PanelWidth, 7.0f, 1.0f);
        if(close)
            BSML::Lite::SetButtonTextSize(close, 3.0f);

        auto* title = BSML::Lite::CreateText(
            root, "Big Screen Showcase", 5.0f);
        ConfigureLayout(title, PanelWidth, 7.0f, 1.0f);
        if(title)
        {
            title->set_fontStyle(TMPro::FontStyles::Bold);
            title->set_alignment(TMPro::TextAlignmentOptions::Center);
            title->set_color({0.35f, 0.85f, 1.0f, 1.0f});
        }

        auto* instructions = BSML::Lite::CreateText(
            root,
            "Check every requirement below. Missing map and video files are downloaded only when you press their individual buttons.",
            3.2f);
        ConfigureLayout(instructions, PanelWidth - 6.0f, 12.0f, 1.0f);
        if(instructions)
        {
            instructions->set_enableWordWrapping(true);
            instructions->set_alignment(TMPro::TextAlignmentOptions::Center);
        }

        const auto makeRow = [root](
            std::string name,
            TMPro::TextMeshProUGUI*& status,
            UnityEngine::UI::Button*& action,
            std::string actionText,
            std::function<void()> callback)
        {
            auto* row = BSML::Lite::CreateHorizontalLayoutGroup(root);
            if(!row)
                return;
            row->set_spacing(1.2f);
            row->set_childControlWidth(true);
            row->set_childControlHeight(true);
            row->set_childForceExpandWidth(false);
            row->set_childForceExpandHeight(false);
            row->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
            ConfigureLayout(row, PanelWidth, 8.0f, 1.0f);
            CreateRowLabel(
                row, std::move(name), 31.0f,
                TMPro::TextAlignmentOptions::MidlineLeft);
            status = CreateRowLabel(
                row, "Checking...", 36.0f,
                TMPro::TextAlignmentOptions::Center);
            action = BSML::Lite::CreateUIButton(
                row,
                actionText,
                {0.0f, 0.0f},
                {28.0f, 7.0f},
                std::move(callback));
            ConfigureLayout(action, 28.0f, 7.0f);
            if(action)
                BSML::Lite::SetButtonTextSize(action, 2.65f);
        };

        makeRow(
            "Chroma",
            chromaStatus_,
            recheckModsButton_,
            "Recheck Mods",
            [this]() { Refresh(); });
        makeRow(
            "Noodle Extensions",
            noodleStatus_,
            recheckNoodleButton_,
            "Recheck Mods",
            [this]() { Refresh(); });
        // Both dependency buttons perform the same immediate capability check.
        // Keep the second row action visible so either missing requirement has
        // an adjacent, discoverable recovery action.

        makeRow(
            "Showcase Map",
            mapStatus_,
            mapButton_,
            "Download Map",
            [this]()
            {
                const auto readiness = ShowcaseLauncher::Instance().Readiness();
                if(readiness.mapFilesPresent && !readiness.mapReady)
                    RecheckMap();
                else
                    DownloadMap();
            });
        makeRow(
            "Showcase Video",
            videoStatus_,
            videoButton_,
            "Download Video",
            [this]() { DownloadVideo(); });

        auto* runtimeRow = BSML::Lite::CreateHorizontalLayoutGroup(root);
        if(runtimeRow)
        {
            runtimeRow->set_spacing(1.2f);
            runtimeRow->set_childControlWidth(true);
            runtimeRow->set_childControlHeight(true);
            runtimeRow->set_childForceExpandWidth(false);
            runtimeRow->set_childForceExpandHeight(false);
            runtimeRow->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
            ConfigureLayout(runtimeRow, PanelWidth, 8.0f, 1.0f);
            CreateRowLabel(
                runtimeRow, "Downloader Runtime", 31.0f,
                TMPro::TextAlignmentOptions::MidlineLeft);
            downloaderStatus_ = CreateRowLabel(
                runtimeRow, "Checking...", 65.0f,
                TMPro::TextAlignmentOptions::Center);
        }

        activityStatus_ = BSML::Lite::CreateText(root, "", 3.15f);
        ConfigureLayout(activityStatus_, PanelWidth - 8.0f, 10.0f, 1.0f);
        if(activityStatus_)
        {
            activityStatus_->set_enableWordWrapping(true);
            activityStatus_->set_alignment(TMPro::TextAlignmentOptions::Center);
        }

        playButton_ = BSML::Lite::CreateUIButton(
            root,
            "Play Showcase",
            {0.0f, 0.0f},
            {42.0f, 9.0f},
            [this]() { ConfirmPlay(); });
        ConfigureLayout(playButton_, 42.0f, 9.0f);
        if(playButton_)
        {
            BSML::Lite::SetButtonTextSize(playButton_, 3.2f);
            BSML::Lite::AddHoverHint(
                playButton_,
                "Starts the managed Up & Down Lawless Expert+ showcase after all listed requirements are ready.");
        }

        warningModal_ = BSML::Lite::CreateModal(
            controller, {76.0f, 42.0f}, nullptr, true);
        auto* warningText = warningModal_ ? BSML::Lite::CreateText(
            warningModal_,
            "<b>Play the Big Screen showcase?</b>\n\nThis demonstration uses intense full-field screen movement and may cause motion sickness.",
            TMPro::FontStyles::Normal,
            3.2f,
            {0.0f, 5.0f},
            {68.0f, 23.0f}) : nullptr;
        if(warningText)
        {
            warningText->set_enableWordWrapping(true);
            warningText->set_alignment(TMPro::TextAlignmentOptions::Center);
        }
        if(warningModal_)
        {
            BSML::Lite::CreateUIButton(
                warningModal_->get_transform(),
                "Cancel",
                {20.0f, -31.0f},
                {23.0f, 8.0f},
                [this]()
                {
                    if(warningModal_)
                        warningModal_->Hide();
                });
            BSML::Lite::CreateUIButton(
                warningModal_->get_transform(),
                "Play",
                {54.0f, -31.0f},
                {29.0f, 8.0f},
                [this]() { Play(); });
        }

        Refresh();
    }

    void ShowcaseMenu::Show()
    {
        VideoLibraryMenu::Instance().StopActivePreview();
        DismissTransientUi();
        tickCounter_ = 0;
        Refresh();
    }

    void ShowcaseMenu::Tick()
    {
        if(!controller_ || !controller_->get_isActivated())
            return;
        if(++tickCounter_ < 10)
            return;
        tickCounter_ = 0;
        Refresh();
    }

    void ShowcaseMenu::DownloadMap()
    {
        std::string error;
        if(!ShowcaseLauncher::Instance().DownloadMap(error))
            ReportActionFailure("Showcase map download could not start", error);
        Refresh();
    }

    void ShowcaseMenu::RecheckMap()
    {
        std::string error;
        if(!ShowcaseLauncher::Instance().RecheckMap(error))
            ReportActionFailure("Showcase map could not be rechecked", error);
        Refresh();
    }

    void ShowcaseMenu::DownloadVideo()
    {
        std::string error;
        if(!ShowcaseLauncher::Instance().DownloadVideo(error))
            ReportActionFailure("Showcase video download could not start", error);
        Refresh();
    }

    void ShowcaseMenu::ConfirmPlay()
    {
        if(!ShowcaseLauncher::Instance().Readiness().ReadyToPlay())
        {
            ReportActionFailure(
                "Showcase is not ready",
                "Prepare every missing requirement shown on this page before playing.");
            return;
        }
        if(warningModal_)
        {
            if(auto object = warningModal_->get_gameObject())
                object->SetActive(true);
            ShowModalInFront(warningModal_);
        }
    }

    void ShowcaseMenu::Play()
    {
        DismissTransientUi();
        std::string error;
        if(!ShowcaseLauncher::Instance().Play(error))
            ReportActionFailure("Could not start showcase", error);
        Refresh();
    }

    void ShowcaseMenu::ReportActionFailure(
        const char* title,
        const std::string& detail)
    {
        ErrorManager::Instance().ReportUserVisible(
            title,
            detail.empty()
                ? "Big Screen could not complete the requested showcase action."
                : detail);
    }

    void ShowcaseMenu::Refresh()
    {
        const auto readiness = ShowcaseLauncher::Instance().Readiness();
        const auto launch = ShowcaseLauncher::Instance().Snapshot();

        SetStatus(chromaStatus_, readiness.chromaActive, "Active", "Missing");
        SetStatus(noodleStatus_, readiness.noodleActive, "Active", "Missing");
        SetStatus(mapStatus_, readiness.mapReady, "Ready", "Missing");
        SetStatus(videoStatus_, readiness.videoReady, "Ready", "Missing");
        SetStatus(
            downloaderStatus_,
            readiness.downloaderReady,
            "Available",
            "Unavailable");

        const bool busy = launch.Active();
        if(recheckModsButton_)
            recheckModsButton_->set_interactable(!busy);
        if(recheckNoodleButton_)
            recheckNoodleButton_->set_interactable(!busy);
        if(mapButton_)
        {
            BSML::Lite::SetButtonText(
                mapButton_,
                readiness.mapFilesPresent && !readiness.mapReady
                    ? "Recheck Map"
                    : "Download Map");
            mapButton_->set_interactable(
                !busy && !readiness.mapReady &&
                (readiness.mapFilesPresent || readiness.downloaderReady));
        }
        if(videoButton_)
            videoButton_->set_interactable(
                !busy && readiness.mapReady && !readiness.videoReady &&
                readiness.downloaderReady);
        if(playButton_)
            playButton_->set_interactable(
                !busy && readiness.ReadyToPlay());

        if(activityStatus_)
        {
            if(busy)
                activityStatus_->set_text(launch.message);
            else if(!readiness.chromaActive || !readiness.noodleActive)
                activityStatus_->set_text(
                    "Install or enable the missing gameplay mods, restart Beat Saber if necessary, then press Recheck Mods.");
            else if(!readiness.downloaderReady &&
                    (!readiness.mapReady || !readiness.videoReady))
                activityStatus_->set_text(readiness.downloaderMessage);
            else if(readiness.ReadyToPlay())
                activityStatus_->set_text(
                    "Every requirement is ready. Play will close this page and open the showcase directly.");
            else
                activityStatus_->set_text(
                    "Use the button beside each missing asset to prepare it.");
        }
    }
}
