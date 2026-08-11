#include "BigScreen/VideoLibraryMenu.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BeatmapLevelPack.hpp"
#include "GlobalNamespace/BeatmapLevelsModel.hpp"
#include "GlobalNamespace/BeatmapLevelsRepository.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerSensitivityFlag.hpp"
#include "HMUI/InputFieldView.hpp"
#include "HMUI/TableView.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Lists.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/CustomListTableData.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string StorageLabel(std::uint64_t used, std::uint64_t free)
        {
            std::ostringstream text;
            text << std::fixed << std::setprecision(1)
                 << "Videos " << used / 1073741824.0 << " GB  |  Free "
                 << free / 1073741824.0 << " GB";
            return text.str();
        }

        bool ExplicitAllowed()
        {
            auto* container = BSML::Helpers::GetDiContainer();
            auto* model = container ? container->Resolve<GlobalNamespace::PlayerDataModel*>() : nullptr;
            auto* data = model ? model->get_playerData() : nullptr;
            return data && data->get_desiredSensitivityFlag().value__ >=
                GlobalNamespace::PlayerSensitivityFlag::Explicit.value__;
        }
    }

    VideoLibraryMenu& VideoLibraryMenu::Instance()
    {
        static VideoLibraryMenu menu;
        return menu;
    }

    void VideoLibraryMenu::CreateUi(HMUI::ViewController* controller)
    {
        if(!controller) return;
        controller_ = controller;
        auto* title = BSML::Lite::CreateText(controller, "Video Library", 4.2f, {0, 34}, {55, 7});
        if(title) title->set_alignment(TMPro::TextAlignmentOptions::Center);
        storageText_ = BSML::Lite::CreateText(controller, "", 2.6f, {0, 29}, {55, 6});
        if(storageText_) storageText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        searchInput_ = BSML::Lite::CreateStringSetting(
            controller, "Search", "", {0, 23}, [this](StringW value) {
                search_ = std::string(value);
                RebuildVisibleRows();
            });
        list_ = BSML::Lite::CreateScrollableList(controller, {0, 7}, {55, 25}, [this](int row) {
            SelectRow(row);
        });
        detailText_ = BSML::Lite::CreateText(controller, "Select a song", 2.6f, {0, -9}, {55, 8});
        if(detailText_) detailText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        urlInput_ = BSML::Lite::CreateStringSetting(
            controller, "YouTube URL", "", {0, -17}, [this](StringW value) {
                url_ = std::string(value);
            });
        offsetSetting_ = BSML::Lite::CreateIncrementSetting(
            controller, "Start Offset", 2, 0.25f, 0.0f, -60.0f, 60.0f, {0, -24},
            [this](float value) {
                offset_ = value;
                if(selected_) VideoLibrary::Instance().UpdateTiming(
                    std::string(selected_->levelID), VideoOrigin::User, offset_, rate_);
            });
        rateSetting_ = BSML::Lite::CreateIncrementSetting(
            controller, "Playback Speed", 2, 0.05f, 1.0f, 0.05f, 8.0f, {0, -30},
            [this](float value) {
                rate_ = value;
                if(selected_) VideoLibrary::Instance().UpdateTiming(
                    std::string(selected_->levelID), VideoOrigin::User, offset_, rate_);
            });
        downloadButton_ = BSML::Lite::CreateUIButton(
            controller, "Download / Replace", {-13, -37}, {27, 7}, [this]() { StartOrCancelDownload(); });
        BSML::Lite::CreateUIButton(
            controller, "Fit to Song", {13, -37}, {22, 7}, [this]() { FitToSong(); });
        removeButton_ = BSML::Lite::CreateUIButton(
            controller,
            "Remove Override",
            UnityEngine::Vector2{0, -44},
            UnityEngine::Vector2{28, 7},
            [this]() { RemoveOverride(); });
        RebuildCatalog();
        Refresh();
    }

    void VideoLibraryMenu::RebuildCatalog()
    {
        catalog_.clear();
        std::unordered_set<std::string> ids;
        auto* container = BSML::Helpers::GetDiContainer();
        auto* model = container ? container->Resolve<GlobalNamespace::BeatmapLevelsModel*>() : nullptr;
        auto* repository = model ? model->__cordl_internal_get__allLoadedBeatmapLevelsRepository() : nullptr;
        if(repository)
        {
            for(auto* pack : repository->__cordl_internal_get__beatmapLevelPacks())
            {
                if(!pack) continue;
                for(auto* level : pack->__cordl_internal_get_beatmapLevels())
                {
                    if(level && level->levelID && ids.emplace(std::string(level->levelID)).second)
                        catalog_.push_back(level);
                }
            }
        }
        std::sort(catalog_.begin(), catalog_.end(), [](auto* left, auto* right) {
            return Lower(std::string(left->songName)) < Lower(std::string(right->songName));
        });
        RebuildVisibleRows();
    }

    void VideoLibraryMenu::RebuildVisibleRows()
    {
        visible_.clear();
        if(!list_) return;
        list_->data->Clear();
        const auto query = Lower(search_);
        for(auto* level : catalog_)
        {
            const auto name = std::string(level->songName);
            const auto author = level->songAuthorName ? std::string(level->songAuthorName) : std::string{};
            if(!query.empty() && Lower(name + " " + author).find(query) == std::string::npos)
                continue;
            visible_.push_back(level);
            const auto descriptor = VideoLibrary::Instance().Describe(level);
            std::string marker = descriptor.hasUserOverride ? "User video" :
                descriptor.CanPlay() ? "Video ready" : descriptor.CanDownload() ? "Download available" : "No video";
            list_->data->Add(BSML::CustomCellInfo::construct(name, marker));
        }
        if(list_->tableView) list_->tableView->ReloadData();
    }

    void VideoLibraryMenu::SelectRow(int row)
    {
        if(row < 0 || row >= static_cast<int>(visible_.size())) return;
        selected_ = visible_[row];
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        url_ = descriptor.downloadUrl.value_or("");
        offset_ = descriptor.playableConfig ? descriptor.playableConfig->offsetSeconds : 0.0;
        rate_ = descriptor.playableConfig ? descriptor.playableConfig->playbackRate : 1.0;
        if(urlInput_) urlInput_->SetText(url_);
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        RefreshDetails();
    }

    void VideoLibraryMenu::StartOrCancelDownload()
    {
        auto& downloader = DownloadManager::Instance();
        if(downloader.Snapshot().Active()) { downloader.Cancel(); return; }
        if(!selected_ || url_.empty()) return;
        DownloadRequest request{
            std::string(selected_->levelID),
            std::string(selected_->songName),
            selected_->songAuthorName ? std::string(selected_->songAuthorName) : std::string{},
            url_, VideoOrigin::User, ExplicitAllowed(), offset_, rate_};
        std::string error;
        if(!downloader.Start(std::move(request), error) && detailText_)
            detailText_->set_text(error);
        RefreshDetails();
    }

    void VideoLibraryMenu::RemoveOverride()
    {
        if(!selected_) return;
        VideoLibrary::Instance().RemoveUserOverride(std::string(selected_->levelID), true);
        RebuildVisibleRows();
        RefreshDetails();
    }

    void VideoLibraryMenu::FitToSong()
    {
        if(!selected_ || selected_->songDuration <= 0.0f) return;
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        if(!descriptor.playableConfig || descriptor.playableConfig->declaredDurationSeconds <= 0.0) return;
        rate_ = std::clamp(
            descriptor.playableConfig->declaredDurationSeconds / selected_->songDuration,
            0.05, 8.0);
        VideoLibrary::Instance().UpdateTiming(
            std::string(selected_->levelID), VideoOrigin::User, offset_, rate_);
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        RefreshDetails();
    }

    void VideoLibraryMenu::RefreshDetails()
    {
        if(storageText_) storageText_->set_text(StorageLabel(
            VideoLibrary::Instance().LibraryBytes(), VideoLibrary::Instance().FreeBytes()));
        if(!selected_ || !detailText_) return;
        const auto download = DownloadManager::Instance().Snapshot();
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        const bool thisDownload = download.levelId == std::string(selected_->levelID);
        detailText_->set_text(thisDownload && download.state != DownloadState::Idle
            ? download.message
            : descriptor.hasUserOverride ? "User override active" :
              descriptor.CanPlay() ? "Mapper video ready" : "Paste a YouTube URL to add a video");
        if(downloadButton_) BSML::Lite::SetButtonText(
            downloadButton_, download.Active() ? "Pause Download" :
            descriptor.hasUserOverride ? "Replace Video" : "Download Video");
        if(removeButton_) removeButton_->set_interactable(descriptor.hasUserOverride);
    }

    void VideoLibraryMenu::Refresh()
    {
        if(!controller_) return;
        // Beat Saber can finish loading custom/WIP repositories after this
        // retained view was first constructed. Rebuild only when the panel is
        // activated so late-loaded songs appear without a per-frame scan.
        RebuildCatalog();
        RefreshDetails();
        if(!Settings::Instance().ModEnabled())
            DownloadManager::Instance().Cancel();
    }

    void VideoLibraryMenu::Deactivate()
    {
        DownloadManager::Instance().Cancel();
    }
}
