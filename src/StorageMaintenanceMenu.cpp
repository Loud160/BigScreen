// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/StorageMaintenanceMenu.hpp"
#include "BigScreen/CenterScreenModal.hpp"
#include "BigScreen/UiUtility.hpp"
#include "BigScreen/Utility.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/StorageManager.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
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
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"

namespace BigScreen {
    namespace {
        using UiUtility::EnsureLayout;

        // A center ViewController has substantially more horizontal room than
        // either angled side screen. Use most of that canvas so category,
        // size, and real-world filenames remain readable instead of applying
        // the 54-unit side-panel width to a center-screen maintenance table.
        constexpr float PanelWidth = 120.0f;

        void ConfigureLayout(
            UnityEngine::Component* component,
            float preferredWidth,
            float preferredHeight,
            float flexibleWidth = 0.0f,
            float flexibleHeight = 0.0f)
        {
            auto* layout = EnsureLayout(component);
            if(!layout) return;
            if(preferredWidth >= 0.0f)
                layout->set_preferredWidth(preferredWidth);
            if(preferredHeight >= 0.0f)
                layout->set_preferredHeight(preferredHeight);
            layout->set_flexibleWidth(flexibleWidth);
            layout->set_flexibleHeight(flexibleHeight);
        }

        std::size_t SnapshotFingerprint(const StorageSnapshot& snapshot)
        {
            // StorageSnapshot is an immutable copy and survives process/menu
            // recreation, so it has no reliable shared version counter. Hash
            // every rendered field instead; even a theoretical collision only
            // defers one harmless redraw until the next snapshot changes.
            std::size_t value = static_cast<std::size_t>(snapshot.state);
            const auto mix = [&value](std::size_t part)
            {
                value ^= part + 0x9e3779b9U + (value << 6) + (value >> 2);
            };
            mix(std::hash<std::uint64_t>{}(snapshot.removableBytes));
            mix(std::hash<std::uint64_t>{}(snapshot.downloadedBytes));
            mix(std::hash<std::uint64_t>{}(snapshot.importedBytes));
            mix(std::hash<std::uint64_t>{}(snapshot.freeBytes));
            mix(std::hash<std::string>{}(snapshot.message));
            for(const auto& item : snapshot.items)
            {
                mix(std::hash<std::string>{}(
                    item.path.lexically_normal().string()));
                mix(std::hash<std::string>{}(item.category));
                mix(std::hash<std::uint64_t>{}(item.bytes));
            }
            return value;
        }
    }

    StorageMaintenanceMenu& StorageMaintenanceMenu::Instance()
    {
        static StorageMaintenanceMenu menu;
        return menu;
    }

    void StorageMaintenanceMenu::ForgetUi()
    {
        *this = StorageMaintenanceMenu{};
    }

    void StorageMaintenanceMenu::CreateUi(
        HMUI::ViewController* controller,
        std::function<void()> onBack)
    {
        controller_ = controller;

        // The maintenance page occupies most of the center screen. A fixed,
        // wide and vertically stretched layout prevents center-screen
        // dimensions from scattering the header and footer outside the list
        // while preserving enough horizontal space for useful filenames.
        auto* root = BSML::Lite::CreateVerticalLayoutGroup(controller);
        root->set_spacing(0.55f);
        root->set_childControlWidth(true);
        root->set_childControlHeight(true);
        root->set_childForceExpandWidth(true);
        root->set_childForceExpandHeight(false);
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
            rect->set_sizeDelta({PanelWidth, -4.0f});
        }

        auto* close = BSML::Lite::CreateUIButton(
            root,
            "Close Storage Menu",
            {0.0f, 0.0f},
            {PanelWidth, 7.5f},
            [callback = std::move(onBack)]()
            {
                if(callback)
                    callback();
            });
        ConfigureLayout(close, PanelWidth, 7.5f, 1.0f);
        BSML::Lite::SetButtonTextSize(close, 3.0f);

        auto* title = BSML::Lite::CreateText(
            root, "Video Storage Maintenance", 4.0f);
        ConfigureLayout(title, PanelWidth, 6.0f, 1.0f);
        title->set_enableWordWrapping(false);
        title->set_alignment(TMPro::TextAlignmentOptions::Center);

        summary_ = BSML::Lite::CreateText(root, "", 4.3f);
        // The summary contains four deliberate lines. Give the final
        // Selected/size line enough height at the larger font so TextMeshPro
        // never replaces its tail with an ellipsis.
        ConfigureLayout(summary_, PanelWidth, 25.0f, 1.0f);
        summary_->set_enableWordWrapping(true);
        summary_->set_enableAutoSizing(true);
        summary_->set_fontSizeMin(3.5f);
        summary_->set_fontSizeMax(4.3f);
        summary_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        summary_->set_alignment(TMPro::TextAlignmentOptions::Center);

        // Scroll only the candidate rows. The action buttons remain siblings
        // below this flexible viewport, so a long scan can never push them into
        // the middle of the file list or make them impossible to select.
        fileListContent_ = BSML::Lite::CreateScrollableSettingsContainer(root);
        if(auto* external = fileListContent_
               ->GetComponent<BSML::ExternalComponents*>())
        {
            if(auto* listLayout = external->Get<UnityEngine::UI::LayoutElement*>())
            {
                listLayout->set_minHeight(22.0f);
                listLayout->set_preferredHeight(36.0f);
                listLayout->set_flexibleHeight(1.0f);
                listLayout->set_preferredWidth(PanelWidth);
                listLayout->set_flexibleWidth(1.0f);
            }
        }
        if(auto* rows = fileListContent_
               ->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>())
        {
            rows->set_spacing(0.35f);
            rows->set_childControlWidth(true);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandWidth(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
        }

        auto* footer = BSML::Lite::CreateHorizontalLayoutGroup(root);
        footer->set_spacing(1.0f);
        footer->set_childControlWidth(true);
        footer->set_childControlHeight(true);
        footer->set_childForceExpandWidth(true);
        footer->set_childForceExpandHeight(false);
        ConfigureLayout(footer, PanelWidth, 8.0f, 1.0f);

        scanButton_ = BSML::Lite::CreateUIButton(
            footer, "Scan Storage", {0.0f, 0.0f}, {25.0f, 8.0f},
            [this]() { BeginScan(); });
        ConfigureLayout(scanButton_, 25.0f, 8.0f, 1.0f);
        BSML::Lite::SetButtonTextSize(scanButton_, 2.7f);
        BSML::Lite::AddHoverHint(
            scanButton_,
            "Checks Big Screen's folders for incomplete downloads, unused thumbnails, and unassigned downloaded videos. A scan does not remove anything.");

        cleanButton_ = BSML::Lite::CreateUIButton(
            footer,
            "Clean Selected Files",
            {0.0f, 0.0f},
            {28.0f, 8.0f},
            [this]()
            {
                const auto snapshot = StorageManager::Instance().Snapshot();
                std::size_t count = 0;
                std::uint64_t bytes = 0;
                for(const auto& item : snapshot.items)
                {
                    if(selectedPaths_.contains(
                           item.path.lexically_normal().string()))
                    {
                        ++count;
                        bytes += item.bytes;
                    }
                }
                if(count == 0 || !confirmationModal_)
                    return;
                if(confirmationText_)
                {
                    confirmationText_->set_text(
                        "<b>Remove " + std::to_string(count) +
                        " selected Big Screen file(s)?</b>\n\n" +
                        Utility::FormatStorageSize(bytes, 1, 2) +
                        " will be removed. Unchecked files and user-owned map-folder or Video Import MP4s will remain untouched.");
                }
                ShowModalOnCenterScreen(confirmationModal_);
            });
        ConfigureLayout(cleanButton_, 28.0f, 8.0f, 1.0f);
        BSML::Lite::SetButtonTextSize(cleanButton_, 2.55f);
        BSML::Lite::AddHoverHint(
            cleanButton_,
            "Reviews and removes only checked scan results. Uncheck anything you want Big Screen to leave in place.");

        confirmationModal_ = BSML::Lite::CreateModal(
            controller, {64.0f, 34.0f}, nullptr, true);
        confirmationText_ = BSML::Lite::CreateText(
            confirmationModal_,
            "",
            TMPro::FontStyles::Normal,
            3.0f,
            {0.0f, 4.0f},
            {58.0f, 20.0f});
        confirmationText_->set_enableWordWrapping(true);
        confirmationText_->set_enableAutoSizing(true);
        confirmationText_->set_fontSizeMin(2.4f);
        confirmationText_->set_fontSizeMax(3.0f);
        confirmationText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        confirmationText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            confirmationModal_->get_transform(),
            "Cancel",
            {18.0f, -27.0f},
            {20.0f, 7.0f},
            [this]()
            {
                if(confirmationModal_)
                    confirmationModal_->Hide();
            });
        auto* remove = BSML::Lite::CreateUIButton(
            confirmationModal_->get_transform(),
            "Remove",
            {46.0f, -27.0f},
            {20.0f, 7.0f},
            [this]()
            {
                if(confirmationModal_)
                    confirmationModal_->Hide();
                BeginCleanup();
            });
        if(auto* removeText = remove->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            removeText->set_color({1.0f, 0.35f, 0.35f, 1.0f});

        lastFingerprint_ = std::numeric_limits<std::size_t>::max();
        Refresh();
    }

    void StorageMaintenanceMenu::BeginScan()
    {
        selectAllOnNextResult_ = true;
        selectedPaths_.clear();
        lastFingerprint_ = std::numeric_limits<std::size_t>::max();
        ErrorManager::Instance().Guard("starting a storage scan", []()
        {
            std::string error;
            if(!StorageManager::Instance().StartScan(error))
                ErrorManager::Instance().ReportUserVisible(
                    "Storage scan could not start",
                    error.empty()
                        ? "Big Screen could not start the storage scan. See the error log for details."
                        : error);
        });
        Refresh();
    }

    void StorageMaintenanceMenu::BeginCleanup()
    {
        std::vector<std::filesystem::path> selected;
        selected.reserve(selectedPaths_.size());
        for(const auto& path : selectedPaths_)
            selected.emplace_back(path);
        lastFingerprint_ = std::numeric_limits<std::size_t>::max();
        ErrorManager::Instance().Guard(
            "starting storage cleanup",
            [selected = std::move(selected)]()
            {
                std::string error;
                if(!StorageManager::Instance().StartCleanup(selected, error))
                {
                    ErrorManager::Instance().ReportUserVisible(
                        "Storage cleanup could not start",
                        error.empty()
                            ? "Big Screen could not start storage cleanup. See the error log for details."
                            : error);
                }
            });
        Refresh();
    }

    void StorageMaintenanceMenu::Show()
    {
        BeginScan();
    }

    void StorageMaintenanceMenu::Tick()
    {
        if(++tickCounter_ < 20) return;
        tickCounter_ = 0;
        Refresh();
    }

    void StorageMaintenanceMenu::RebuildFileRows(
        const StorageSnapshot& snapshot)
    {
        if(!fileListContent_)
            return;

        for(auto* row : fileRows_)
        {
            if(!row) continue;
            row->SetActive(false);
            UnityEngine::Object::Destroy(row);
        }
        fileRows_.clear();

        std::unordered_set<std::string> currentPaths;
        currentPaths.reserve(snapshot.items.size());
        for(const auto& item : snapshot.items)
            currentPaths.emplace(item.path.lexically_normal().string());

        if(selectAllOnNextResult_ && !snapshot.items.empty())
        {
            selectedPaths_ = currentPaths;
            selectAllOnNextResult_ = false;
        }
        else
        {
            std::erase_if(selectedPaths_, [&currentPaths](const std::string& path)
            {
                return !currentPaths.contains(path);
            });
        }

        if(snapshot.items.empty())
        {
            auto* empty = BSML::Lite::CreateText(
                fileListContent_,
                snapshot.state == StorageState::Scanning
                    ? "Scanning storage..."
                    : "No files are currently listed for removal.",
                2.7f);
            ConfigureLayout(empty, PanelWidth - 4.0f, 10.0f, 1.0f);
            empty->set_enableWordWrapping(true);
            empty->set_alignment(TMPro::TextAlignmentOptions::Center);
            fileRows_.push_back(empty->get_gameObject());
            return;
        }

        for(const auto& item : snapshot.items)
        {
            const auto key = item.path.lexically_normal().string();
            const std::string label = item.category + "  |  " +
                Utility::FormatStorageSize(item.bytes, 1, 2) +
                    "  |  " + item.path.filename().string();
            auto* checkbox = BSML::Lite::CreateToggle(
                fileListContent_,
                label,
                selectedPaths_.contains(key),
                [this, key](bool checked)
                {
                    if(checked)
                        selectedPaths_.insert(key);
                    else
                        selectedPaths_.erase(key);
                    RefreshSelectionState(
                        StorageManager::Instance().Snapshot());
                });
            ConfigureLayout(checkbox, PanelWidth - 3.5f, 7.5f, 1.0f);
            fileRows_.push_back(checkbox->get_gameObject());

            // ToggleSetting normally puts the switch at the right edge. This
            // review list intentionally behaves like a cleanup checklist, so
            // move the switch before its filename and keep every row aligned.
            if(checkbox->toggle)
                checkbox->toggle->get_transform()->SetAsFirstSibling();
            if(auto* text = checkbox->get_gameObject()
                   ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            {
                text->set_richText(false);
                text->set_fontSize(1.15f);
                text->set_enableWordWrapping(false);
                text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
                text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            }
            BSML::Lite::AddHoverHint(
                checkbox,
                "Checked files will be removed after confirmation. Turn this off to keep the file.");
        }
    }

    void StorageMaintenanceMenu::RefreshSelectionState(
        const StorageSnapshot& snapshot)
    {
        std::size_t selectedCount = 0;
        std::uint64_t selectedBytes = 0;
        for(const auto& item : snapshot.items)
        {
            if(selectedPaths_.contains(item.path.lexically_normal().string()))
            {
                ++selectedCount;
                selectedBytes += item.bytes;
            }
        }

        if(summary_)
        {
            summary_->set_text(
                snapshot.message +
                "\nDownloads: " + Utility::FormatStorageSize(snapshot.downloadedBytes, 1, 2) +
                "   |   Imports: " + Utility::FormatStorageSize(snapshot.importedBytes, 1, 2) +
                "\nFree: " + Utility::FormatStorageSize(snapshot.freeBytes, 1, 2) +
                "   |   Removable: " + Utility::FormatStorageSize(snapshot.removableBytes, 1, 2) +
                "\nSelected: " + std::to_string(selectedCount) +
                " file(s)   |   " + Utility::FormatStorageSize(selectedBytes, 1, 2));
        }

        const bool busy = snapshot.state == StorageState::Scanning ||
                          snapshot.state == StorageState::Cleaning;
        if(scanButton_)
            scanButton_->set_interactable(!busy);
        if(cleanButton_)
            cleanButton_->set_interactable(
                snapshot.state == StorageState::Ready && selectedCount > 0);
    }

    void StorageMaintenanceMenu::Refresh()
    {
        if(!summary_ || !fileListContent_)
            return;
        const auto snapshot = StorageManager::Instance().Snapshot();
        const auto fingerprint = SnapshotFingerprint(snapshot);
        if(fingerprint == lastFingerprint_)
            return;
        lastFingerprint_ = fingerprint;
        RebuildFileRows(snapshot);
        RefreshSelectionState(snapshot);
    }
}
