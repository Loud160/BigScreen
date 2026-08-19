// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/LocalVideoBrowserMenu.hpp"
#include "BigScreen/CenterScreenModal.hpp"
#include "BigScreen/UiUtility.hpp"
#include "BigScreen/Utility.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "BigScreen/ErrorManager.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/Backgroundable.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        using UiUtility::EnsureLayout;

        constexpr float PanelWidth = 124.0f;
        constexpr float BrowserListWidth = PanelWidth - 4.0f;

        void ConfigureLayout(
            UnityEngine::Component* component,
            float preferredWidth,
            float preferredHeight,
            float flexibleWidth = 0.0f,
            float flexibleHeight = 0.0f)
        {
            auto* layout = EnsureLayout(component);
            if(!layout) return;
            if(preferredWidth >= 0.0f) layout->set_preferredWidth(preferredWidth);
            if(preferredHeight >= 0.0f) layout->set_preferredHeight(preferredHeight);
            layout->set_flexibleWidth(flexibleWidth);
            layout->set_flexibleHeight(flexibleHeight);
        }

        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }
    }

    LocalVideoBrowserMenu& LocalVideoBrowserMenu::Instance()
    {
        static LocalVideoBrowserMenu menu;
        return menu;
    }

    LocalVideoBrowserMenu::~LocalVideoBrowserMenu()
    {
        CancelScan();
    }

    void LocalVideoBrowserMenu::CancelScan()
    {
        stopScan_ = true;
        if(worker_.joinable())
            worker_.join();
        std::scoped_lock lock(mutex_);
        pendingDirectory_.reset();
        if(snapshot_.state == ScanState::Scanning)
        {
            snapshot_.state = ScanState::Idle;
            snapshot_.message.clear();
            ++snapshot_.version;
        }
    }

    void LocalVideoBrowserMenu::ForgetUi()
    {
        // Stop between directory entries/probes so a folder with many videos
        // cannot hold menu teardown until every file has been inspected.
        CancelScan();
        controller_ = nullptr;
        selectedLevel_ = nullptr;
        onCancel_ = {};
        onAssigned_ = {};
        title_ = nullptr;
        statusText_ = nullptr;
        helpText_ = nullptr;
        breadcrumbContent_ = nullptr;
        listContent_ = nullptr;
        upButton_ = nullptr;
        setButton_ = nullptr;
        helpModal_ = nullptr;
        breadcrumbObjects_.clear();
        rowObjects_.clear();
        selectedPath_.clear();
        rootPath_.clear();
        pendingDirectory_.reset();
        {
            std::lock_guard lock(mutex_);
            snapshot_ = {};
        }
        nextRequest_ = 0;
        renderedVersion_ = 0;
        tickCounter_ = 0;
        stopScan_ = false;
    }

    void LocalVideoBrowserMenu::CreateUi(
        HMUI::ViewController* controller,
        std::function<void()> onCancel,
        std::function<void(const std::string&)> onAssigned)
    {
        controller_ = controller;
        onCancel_ = std::move(onCancel);
        onAssigned_ = std::move(onAssigned);
        rootPath_ = VideoLibrary::Instance().SharedStoragePath();

        // A blank-sprite ImageView is not reliably rendered by Beat Saber's
        // center view controller. Use the same stock Backgroundable panel that
        // visibly backs the thumbnail picker and editor cards. The plate is
        // slightly larger than the browser and non-interactive, so filenames
        // remain readable without stealing pointer events from the controls.
        auto* backgroundObject =
            UnityEngine::GameObject::New_ctor("BigScreenLocalFileBrowserPlate");
        backgroundObject->get_transform()->SetParent(
            controller->get_transform(), false);
        if(auto* backgroundable =
               backgroundObject->AddComponent<BSML::Backgroundable*>())
        {
            backgroundable->ApplyBackground("round-rect-panel");
            backgroundable->ApplyColor({0.0f, 0.0f, 0.0f, 1.0f});
            backgroundable->ApplyAlpha(0.78f);
            if(auto* image = backgroundable->background)
            {
                image->set_gradient(false);
                image->set_raycastTarget(false);
            }
        }
        if(auto backgroundRect = backgroundObject->get_transform()
               .try_cast<UnityEngine::RectTransform>().value_or(nullptr))
        {
            backgroundRect->set_anchorMin({0.5f, 0.5f});
            backgroundRect->set_anchorMax({0.5f, 0.5f});
            backgroundRect->set_pivot({0.5f, 0.5f});
            backgroundRect->set_anchoredPosition({0.0f, 0.0f});
            backgroundRect->set_sizeDelta({PanelWidth + 4.0f, 76.0f});
        }
        backgroundObject->get_transform()->SetAsFirstSibling();

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

        title_ = BSML::Lite::CreateText(
            root, "Select a Local Video", TMPro::FontStyles::Bold, 4.2f);
        ConfigureLayout(title_, PanelWidth, 6.0f, 1.0f);
        title_->set_alignment(TMPro::TextAlignmentOptions::Center);
        title_->set_enableWordWrapping(false);

        auto* pathRow = BSML::Lite::CreateHorizontalLayoutGroup(root);
        pathRow->set_spacing(0.55f);
        pathRow->set_childControlWidth(true);
        pathRow->set_childControlHeight(true);
        pathRow->set_childForceExpandWidth(false);
        pathRow->set_childForceExpandHeight(false);
        ConfigureLayout(pathRow, PanelWidth, 8.0f, 1.0f);
        upButton_ = BSML::Lite::CreateUIButton(
            pathRow, "Back One Folder", {0.0f, 0.0f}, {22.0f, 7.5f},
            [this]()
            {
                const auto snapshot = Snapshot();
                if(snapshot.state == ScanState::Scanning ||
                   snapshot.directory == rootPath_)
                    return;
                StartScan(snapshot.directory.parent_path());
            });
        ConfigureLayout(upButton_, 22.0f, 7.5f, 0.0f);
        BSML::Lite::SetButtonTextSize(upButton_, 2.35f);
        if(auto* text = upButton_->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            text->set_color({0.25f, 0.72f, 1.0f, 1.0f});

        // Breadcrumb buttons replace the passive path label. Each component
        // is independently clickable, so jumping from a map folder back to
        // SongCore, Mods, or Quest storage takes one action.
        auto* breadcrumbs = BSML::Lite::CreateHorizontalLayoutGroup(pathRow);
        breadcrumbs->set_spacing(0.15f);
        breadcrumbs->set_childControlWidth(true);
        breadcrumbs->set_childControlHeight(true);
        breadcrumbs->set_childForceExpandWidth(false);
        breadcrumbs->set_childForceExpandHeight(false);
        ConfigureLayout(breadcrumbs, 0.0f, 7.5f, 1.0f);
        breadcrumbContent_ = breadcrumbs->get_gameObject();

        listContent_ = BSML::Lite::CreateScrollableSettingsContainer(root);
        if(auto* external = listContent_
               ->GetComponent<BSML::ExternalComponents*>())
        {
            if(auto* layout = external->Get<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_minHeight(30.0f);
                layout->set_preferredHeight(50.0f);
                layout->set_flexibleHeight(1.0f);
                layout->set_preferredWidth(PanelWidth);
                layout->set_flexibleWidth(1.0f);
            }
        }
        if(auto* rows = listContent_
               ->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>())
        {
            rows->set_spacing(0.35f);
            rows->set_childControlWidth(true);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandWidth(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
        }

        statusText_ = BSML::Lite::CreateText(root, "", 2.55f);
        ConfigureLayout(statusText_, PanelWidth, 6.0f, 1.0f);
        statusText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        statusText_->set_enableWordWrapping(false);
        statusText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);

        auto* footer = BSML::Lite::CreateHorizontalLayoutGroup(root);
        footer->set_spacing(1.0f);
        footer->set_childControlWidth(true);
        footer->set_childControlHeight(true);
        footer->set_childForceExpandWidth(true);
        footer->set_childForceExpandHeight(false);
        ConfigureLayout(footer, PanelWidth, 8.0f, 1.0f);
        auto* cancelButton = BSML::Lite::CreateUIButton(
            footer, "Cancel", {0.0f, 0.0f}, {28.0f, 8.0f},
            [this]() { Cancel(); });
        ConfigureLayout(cancelButton, 28.0f, 8.0f, 1.0f);
        setButton_ = BSML::Lite::CreateUIButton(
            footer, "Set Video", {0.0f, 0.0f}, {28.0f, 8.0f},
            [this]() { SetSelectedVideo(); });
        ConfigureLayout(setButton_, 28.0f, 8.0f, 1.0f);
        setButton_->set_interactable(false);

        helpModal_ = BSML::Lite::CreateModal(
            controller, {76.0f, 40.0f}, nullptr, true);
        helpText_ = BSML::Lite::CreateText(
            helpModal_, "", TMPro::FontStyles::Normal, 3.0f,
            {0.0f, 4.0f}, {68.0f, 25.0f});
        helpText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        helpText_->set_enableWordWrapping(true);
        helpText_->set_enableAutoSizing(true);
        helpText_->set_fontSizeMin(2.4f);
        helpText_->set_fontSizeMax(3.0f);
        auto* closeHelp = BSML::Lite::CreateUIButton(
            helpModal_->get_transform(), "Close", {38.0f, -31.0f},
            {22.0f, 7.0f}, [this]()
            {
                if(helpModal_) helpModal_->Hide();
            });
        BSML::Lite::SetButtonTextSize(closeHelp, 2.8f);
    }

    void LocalVideoBrowserMenu::Show(GlobalNamespace::BeatmapLevel* level)
    {
        selectedLevel_ = level;
        selectedPath_.clear();
        if(title_)
        {
            const std::string song = level && level->songName
                ? std::string(level->songName) : "Selected Song";
            title_->set_text("Select a Local Video for " + song);
        }

        std::filesystem::path start = VideoLibrary::Instance().ImportPath();
        if(level && level->levelID)
        {
            if(auto* custom = SongCore::API::Loading::GetLevelByLevelID(
                   std::string(level->levelID)))
                start = std::filesystem::path(custom->get_customLevelPath());
        }
        StartScan(start);
    }

    void LocalVideoBrowserMenu::StartScan(
        const std::filesystem::path& requestedDirectory)
    {
        {
            std::scoped_lock lock(mutex_);
            if(snapshot_.state == ScanState::Scanning)
            {
                // Preserve the latest navigation request while the previous
                // folder is being probed. Blocking here would freeze Unity's
                // UI thread on folders containing many videos.
                pendingDirectory_ = requestedDirectory;
                return;
            }
            // A direct request made after the worker completed is newer than
            // any queued navigation from that completed scan.
            pendingDirectory_.reset();
        }
        if(worker_.joinable()) worker_.join();

        std::error_code directoryError;
        const auto directory = std::filesystem::absolute(
            requestedDirectory, directoryError).lexically_normal();
        if(directoryError || !Utility::IsPathInside(directory, rootPath_) ||
           !std::filesystem::is_directory(directory, directoryError) ||
           directoryError)
        {
            std::scoped_lock lock(mutex_);
            snapshot_.state = ScanState::Failed;
            snapshot_.directory = directory;
            snapshot_.message = "This folder is unavailable or outside Quest shared storage.";
            ++snapshot_.version;
            return;
        }

        selectedPath_.clear();
        std::uint64_t request = 0;
        {
            std::scoped_lock lock(mutex_);
            request = ++nextRequest_;
            snapshot_.state = ScanState::Scanning;
            snapshot_.directory = directory;
            snapshot_.directories.clear();
            snapshot_.videos.clear();
            snapshot_.message = "Scanning this folder...";
            ++snapshot_.version;
        }
        stopScan_ = false;
        try
        {
            worker_ = std::thread(
                [this, directory, request]()
                {
                    ScanWorker(directory, request);
                });
        }
        catch(const std::exception& exception)
        {
            std::scoped_lock lock(mutex_);
            snapshot_.state = ScanState::Failed;
            snapshot_.message =
                "Could not start the folder scan. See the Big Screen log.";
            ++snapshot_.version;
            ErrorManager::Instance().RecordError(
                "Starting the local video folder scanner", exception.what());
        }
    }

    void LocalVideoBrowserMenu::ScanWorker(
        std::filesystem::path directory,
        std::uint64_t request)
    {
        ScanSnapshot result;
        result.state = ScanState::Ready;
        result.directory = directory;
        try
        {
            std::error_code iteratorError;
            for(std::filesystem::directory_iterator iterator(directory, iteratorError), end;
                !iteratorError && iterator != end; iterator.increment(iteratorError))
            {
                if(stopScan_)
                    return;
                std::error_code typeError;
                if(iterator->is_directory(typeError) && !typeError)
                {
                    result.directories.push_back(iterator->path());
                    continue;
                }
                typeError.clear();
                if(!iterator->is_regular_file(typeError) || typeError)
                    continue;
                const auto extension =
                    Lower(iterator->path().extension().string());
                if(extension == ".mp4" || extension == ".webm")
                {
                    if(stopScan_)
                        return;
                    result.videos.push_back(
                        VideoLibrary::Instance().InspectLocalVideo(
                            iterator->path()));
                    if(stopScan_)
                        return;
                }
            }
            if(iteratorError)
            {
                result.state = ScanState::Failed;
                result.message = "Quest did not allow Big Screen to read this folder.";
            }
            else
            {
                const auto byName = [](const auto& left, const auto& right)
                {
                    return Lower(left.filename().string()) <
                           Lower(right.filename().string());
                };
                std::sort(
                    result.directories.begin(), result.directories.end(), byName);
                std::sort(
                    result.videos.begin(), result.videos.end(),
                    [](const LocalVideoFile& left, const LocalVideoFile& right)
                    {
                        return Lower(left.fileName) < Lower(right.fileName);
                    });
                result.message = result.videos.empty()
                    ? "No MP4 or WebM files are present in this folder."
                    : std::to_string(result.videos.size()) +
                        " video file(s) found. Select a green file, then choose Set Video.";
            }
        }
        catch(const std::exception& exception)
        {
            result.state = ScanState::Failed;
            result.message = "Could not scan this folder. See the Big Screen log.";
            ErrorManager::Instance().ReportInternal(
                "scanning the local video folder", exception.what());
        }
        catch(...)
        {
            result.state = ScanState::Failed;
            result.message = "Could not scan this folder. See the Big Screen log.";
            ErrorManager::Instance().ReportInternal(
                "scanning the local video folder", "Unknown native exception");
        }

        if(stopScan_)
            return;
        std::scoped_lock lock(mutex_);
        if(request != nextRequest_)
            return;
        result.version = snapshot_.version + 1;
        snapshot_ = std::move(result);
    }

    LocalVideoBrowserMenu::ScanSnapshot
    LocalVideoBrowserMenu::Snapshot() const
    {
        std::scoped_lock lock(mutex_);
        return snapshot_;
    }

    void LocalVideoBrowserMenu::Tick()
    {
        if(++tickCounter_ < 10) return;
        tickCounter_ = 0;
        ScanSnapshot snapshot;
        std::optional<std::filesystem::path> pending;
        {
            std::scoped_lock lock(mutex_);
            if(snapshot_.state != ScanState::Scanning && pendingDirectory_)
            {
                pending = std::move(pendingDirectory_);
                pendingDirectory_.reset();
            }
            if(snapshot_.version == renderedVersion_)
            {
                if(!pending) return;
            }
            renderedVersion_ = snapshot_.version;
            snapshot = snapshot_;
        }
        if(pending)
        {
            StartScan(*pending);
            return;
        }
        Refresh(snapshot);
    }

    void LocalVideoBrowserMenu::Refresh(const ScanSnapshot& snapshot)
    {
        RebuildBreadcrumbs(snapshot.directory);
        if(statusText_) statusText_->set_text(snapshot.message);
        if(upButton_)
            upButton_->set_interactable(
                snapshot.state != ScanState::Scanning &&
                snapshot.directory != rootPath_);
        if(setButton_)
            setButton_->set_interactable(
                snapshot.state == ScanState::Ready &&
                !selectedPath_.empty());
        RebuildRows(snapshot);
    }

    void LocalVideoBrowserMenu::RebuildBreadcrumbs(
        const std::filesystem::path& directory)
    {
        if(!breadcrumbContent_ || directory.empty()) return;
        for(auto* object : breadcrumbObjects_)
        {
            if(!object) continue;
            object->SetActive(false);
            UnityEngine::Object::Destroy(object);
        }
        breadcrumbObjects_.clear();

        std::vector<std::pair<std::string, std::filesystem::path>> parts;
        auto current = rootPath_.lexically_normal();
        parts.emplace_back("Quest Storage", current);
        const auto relative = directory.lexically_normal().lexically_relative(current);
        if(!relative.empty() && relative != ".")
        {
            for(const auto& component : relative)
            {
                if(component == ".") continue;
                current /= component;
                parts.emplace_back(component.string(), current);
            }
        }

        // Reserve room for separators and divide the remaining width among
        // all path tiers. These are clickable text labels rather than button
        // plates: the path keeps the same compact footprint as the old passive
        // label while every ancestor remains directly navigable.
        const float available = PanelWidth - 27.0f;
        const float separatorWidth = 1.9f;
        const float buttonWidth = std::clamp(
            (available - separatorWidth * (parts.size() - 1)) /
                static_cast<float>(parts.size()),
            8.0f,
            22.0f);
        for(std::size_t index = 0; index < parts.size(); ++index)
        {
            if(index > 0)
            {
                auto* separator = BSML::Lite::CreateText(
                    breadcrumbContent_->get_transform(), ">", 2.2f);
                ConfigureLayout(separator, separatorWidth, 7.0f, 0.0f);
                separator->set_alignment(TMPro::TextAlignmentOptions::Center);
                breadcrumbObjects_.push_back(separator->get_gameObject());
            }

            const auto target = parts[index].second;
            auto* button = BSML::Lite::CreateClickableText(
                breadcrumbContent_->get_transform(),
                parts[index].first,
                TMPro::FontStyles::Normal,
                2.15f,
                {0.0f, 0.0f},
                {buttonWidth, 7.0f},
                [this, target]() { StartScan(target); });
            ConfigureLayout(button, buttonWidth, 7.0f, 0.0f);
            const auto restingColor = index + 1 == parts.size()
                ? UnityEngine::Color{0.30f, 0.95f, 1.0f, 1.0f}
                : UnityEngine::Color{0.25f, 0.72f, 1.0f, 1.0f};
            button->set_defaultColor(restingColor);
            button->set_highlightColor(
                UnityEngine::Color{0.70f, 0.96f, 1.0f, 1.0f});
            button->set_color(restingColor);
            button->set_alignment(TMPro::TextAlignmentOptions::Center);
            button->set_enableWordWrapping(false);
            button->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
            BSML::Lite::AddHoverHint(button, target.string());
            breadcrumbObjects_.push_back(button->get_gameObject());
        }
    }

    void LocalVideoBrowserMenu::RebuildRows(const ScanSnapshot& snapshot)
    {
        if(!listContent_) return;
        for(auto* row : rowObjects_)
        {
            if(!row) continue;
            row->SetActive(false);
            UnityEngine::Object::Destroy(row);
        }
        rowObjects_.clear();

        if(snapshot.state == ScanState::Scanning ||
           (snapshot.directories.empty() && snapshot.videos.empty()))
        {
            auto* text = BSML::Lite::CreateText(
                listContent_, snapshot.message, 3.0f);
            ConfigureLayout(text, BrowserListWidth, 12.0f, 1.0f);
            text->set_alignment(TMPro::TextAlignmentOptions::Center);
            text->set_enableWordWrapping(true);
            rowObjects_.push_back(text->get_gameObject());
            return;
        }

        for(const auto& directory : snapshot.directories)
        {
            auto* button = BSML::Lite::CreateUIButton(
                listContent_, "[Folder]  " + directory.filename().string(),
                {0.0f, 0.0f}, {BrowserListWidth, 7.0f},
                [this, directory]() { StartScan(directory); });
            ConfigureLayout(button, BrowserListWidth, 7.0f, 0.0f);
            BSML::Lite::SetButtonTextSize(button, 2.5f);
            if(auto* text = button->get_gameObject()
                   ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            {
                text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
                text->set_color({0.35f, 0.85f, 1.0f, 1.0f});
                text->set_enableWordWrapping(false);
                text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
            }
            rowObjects_.push_back(button->get_gameObject());
        }

        for(const auto& file : snapshot.videos)
        {
            auto* row = BSML::Lite::CreateHorizontalLayoutGroup(listContent_);
            row->set_spacing(0.6f);
            row->set_childControlWidth(true);
            row->set_childControlHeight(true);
            row->set_childForceExpandWidth(false);
            row->set_childForceExpandHeight(false);
            ConfigureLayout(row, BrowserListWidth, 7.5f, 0.0f);
            rowObjects_.push_back(row->get_gameObject());

            const bool selected = file.compatible &&
                file.path.lexically_normal() == selectedPath_.lexically_normal();
            const float fileWidth = file.compatible
                ? BrowserListWidth
                : BrowserListWidth - 14.6f;
            const std::string visibleFileName =
                (selected ? "Selected:  " : "") + file.fileName;
            const std::string coloredFileName = file.compatible
                ? "<color=#33FF5C>" + visibleFileName + "</color>"
                : "<color=#FF4040>" + visibleFileName + "</color>";
            auto* fileButton = BSML::Lite::CreateUIButton(
                row,
                coloredFileName,
                {0.0f, 0.0f},
                {fileWidth, 7.0f},
                [this, file]()
                {
                    if(file.compatible)
                        SelectVideo(file.path);
                    else
                        ShowHelp(file);
                });
            ConfigureLayout(fileButton, fileWidth, 7.0f, 0.0f);
            BSML::Lite::SetButtonTextSize(fileButton, 2.45f);
            if(auto* text = fileButton->get_gameObject()
                   ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            {
                // Button ColorTint overwrites TextMeshPro's plain color on
                // Quest. Rich-text markup keeps valid names green and invalid
                // names red through normal, hover, and selected states.
                text->set_richText(true);
                text->set_text(coloredFileName);
                text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
                text->set_enableWordWrapping(false);
                text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
                text->set_color(UnityEngine::Color::get_white());
            }

            if(!file.compatible)
            {
                auto* help = BSML::Lite::CreateUIButton(
                    row, "<color=#FF4040>HELP</color>",
                    {0.0f, 0.0f}, {14.0f, 7.0f},
                    [this, file]() { ShowHelp(file); });
                ConfigureLayout(help, 14.0f, 7.0f, 0.0f);
                BSML::Lite::SetButtonTextSize(help, 2.6f);
                if(auto* helpText = help->get_gameObject()
                       ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
                {
                    helpText->set_richText(true);
                    helpText->set_text("<color=#FF4040>HELP</color>");
                    helpText->set_color(UnityEngine::Color::get_white());
                }
            }
        }
    }

    void LocalVideoBrowserMenu::SelectVideo(
        const std::filesystem::path& path)
    {
        selectedPath_ = path.lexically_normal();
        const auto snapshot = Snapshot();
        // Re-render immediately so selection feedback and Set Video's enabled
        // state do not wait for the periodic worker snapshot poll.
        if(setButton_) setButton_->set_interactable(true);
        RebuildRows(snapshot);
        if(statusText_)
            statusText_->set_text(
                "Selected " + path.filename().string() +
                ". Choose Set Video to assign it to this song.");
    }

    void LocalVideoBrowserMenu::SetSelectedVideo()
    {
        if(!selectedLevel_ || selectedPath_.empty())
            return;
        std::string error;
        try
        {
            if(VideoLibrary::Instance().SetVideoFileOverride(
                   selectedLevel_, selectedPath_, error))
            {
                const auto fileName = selectedPath_.filename().string();
                if(onAssigned_) onAssigned_(fileName);
                return;
            }
        }
        catch(const std::exception& exception)
        {
            error = "The video assignment could not be saved. Your previous "
                "assignment is still active.";
            ErrorManager::Instance().ReportInternal(
                "saving a local video assignment", exception.what());
        }
        catch(...)
        {
            error = "The video assignment could not be saved. Your previous "
                "assignment is still active.";
            ErrorManager::Instance().ReportInternal(
                "saving a local video assignment", "Unknown native exception");
        }

        if(error.empty())
            error = "The selected video could not be assigned.";
        ErrorManager::Instance().ReportUserVisible(
            "Local video could not be assigned", error);
        if(statusText_)
            statusText_->set_text(error);
    }

    void LocalVideoBrowserMenu::ShowHelp(const LocalVideoFile& file)
    {
        if(helpText_)
        {
            helpText_->set_text(
                "<b>" + file.fileName + " cannot be used</b>\n\n" +
                (file.problem.empty()
                    ? "Big Screen could not identify a compatible video stream in this file."
                    : file.problem));
        }
        if(helpModal_) ShowModalOnCenterScreen(helpModal_);
    }

    void LocalVideoBrowserMenu::Cancel()
    {
        CancelScan();
        selectedPath_.clear();
        if(onCancel_) onCancel_();
    }
}
