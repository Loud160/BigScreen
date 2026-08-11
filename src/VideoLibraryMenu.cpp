#include "BigScreen/VideoLibraryMenu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/ScreenPreview.hpp"
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
#include "UnityEngine/Time.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Lists.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/CustomListTableData.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "main.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

namespace BigScreen {
    namespace {
        constexpr std::array<std::string_view, 6> FilterNames{
            "Show All Maps", "Custom Maps", "WIP Maps",
            "OST Maps", "DLC Maps", "Maps With Video"
        };

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
            auto* model = container
                ? container->Resolve<GlobalNamespace::PlayerDataModel*>()
                : nullptr;
            auto* data = model ? model->get_playerData() : nullptr;
            return data && data->get_desiredSensitivityFlag().value__ >=
                GlobalNamespace::PlayerSensitivityFlag::Explicit.value__;
        }

        bool IsPathUnder(
            const std::filesystem::path& child,
            const std::filesystem::path& parent)
        {
            const auto normalizedChild = child.lexically_normal();
            const auto normalizedParent = parent.lexically_normal();
            auto childPart = normalizedChild.begin();
            for(auto parentPart = normalizedParent.begin();
                parentPart != normalizedParent.end(); ++parentPart, ++childPart)
            {
                if(childPart == normalizedChild.end() || *childPart != *parentPart)
                    return false;
            }
            return true;
        }

        bool IsWip(SongCore::SongLoader::CustomBeatmapLevel* level)
        {
            if(!level) return false;
            const auto path = std::filesystem::path(level->get_customLevelPath());
            for(const auto& root : SongCore::API::Loading::GetRootCustomWIPLevelPaths())
                if(IsPathUnder(path, root)) return true;
            return false;
        }

        SongLibraryGroup OfficialGroup(GlobalNamespace::BeatmapLevelPack* pack)
        {
            std::string identity;
            if(pack)
            {
                if(pack->__cordl_internal_get_packID())
                    identity += std::string(pack->__cordl_internal_get_packID());
                if(pack->__cordl_internal_get_packName())
                    identity += " " + std::string(pack->__cordl_internal_get_packName());
            }
            identity = Lower(identity);
            return identity.find("ost") != std::string::npos ||
                   identity.find("extras") != std::string::npos ||
                   identity.find("camellia") != std::string::npos
                ? SongLibraryGroup::Ost
                : SongLibraryGroup::Dlc;
        }

        bool MatchesFilter(
            SongLibraryFilter filter,
            const SongLibraryItem& item,
            const VideoDescriptor& descriptor)
        {
            switch(filter)
            {
                case SongLibraryFilter::Custom: return item.group == SongLibraryGroup::Custom;
                case SongLibraryFilter::Wip: return item.group == SongLibraryGroup::Wip;
                case SongLibraryFilter::Ost: return item.group == SongLibraryGroup::Ost;
                case SongLibraryFilter::Dlc: return item.group == SongLibraryGroup::Dlc;
                case SongLibraryFilter::Video:
                    return descriptor.CanPlay() || descriptor.CanDownload();
                default: return true;
            }
        }

        void SetActive(UnityEngine::Component* component, bool active)
        {
            if(component) component->get_gameObject()->SetActive(active);
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

        // Browser layer: the list receives almost the entire panel. Its 5.5
        // unit rows show roughly ten tracks at once while retaining the native
        // Beat Saber level-list appearance and scroll controls.
        browserTitle_ = BSML::Lite::CreateText(
            controller, "Video Library", 4.2f, {0, 37}, {55, 6});
        browserStorage_ = BSML::Lite::CreateText(
            controller, "", 2.4f, {0, 33}, {55, 4});
        searchInput_ = BSML::Lite::CreateStringSetting(
            controller, "Search Maps", "", {0, 27}, [this](StringW value) {
                search_ = std::string(value);
                RebuildVisibleRows();
            });
        filterPreviousButton_ = BSML::Lite::CreateUIButton(
            controller, "<", UnityEngine::Vector2{-22, 18.5f}, UnityEngine::Vector2{9, 7}, [this]() { ChangeFilter(-1); });
        filterText_ = BSML::Lite::CreateText(
            controller, FilterNames[0], 3.0f, {0, 18.5f}, {36, 6});
        filterNextButton_ = BSML::Lite::CreateUIButton(
            controller, ">", UnityEngine::Vector2{22, 18.5f}, UnityEngine::Vector2{9, 7}, [this]() { ChangeFilter(1); });
        list_ = BSML::Lite::CreateScrollableList(
            controller, {0, -11.5f}, {55, 52}, [this](int row) { SelectRow(row); });
        if(list_)
        {
            list_->set_listStyle(BSML::CustomListTableData::ListStyle::List);
            list_->cellSize = 5.0f;
            list_->expandCell = true;
        }

        // Editor layer: every child is initially hidden and cannot intercept
        // pointer events from the search field or list underneath it.
        backToListButton_ = BSML::Lite::CreateUIButton(
            controller, "< Back to Song List", UnityEngine::Vector2{0, 36}, UnityEngine::Vector2{45, 7}, [this]() { ShowBrowser(); });
        detailTitle_ = BSML::Lite::CreateText(
            controller, "", 3.8f, {0, 28}, {55, 9});
        detailText_ = BSML::Lite::CreateText(
            controller, "", 2.6f, {0, 21}, {55, 6});
        urlInput_ = BSML::Lite::CreateStringSetting(
            controller, "YouTube URL", "", {0, 13}, [this](StringW value) {
                url_ = std::string(value);
            });
        offsetSetting_ = BSML::Lite::CreateIncrementSetting(
            controller, "Start Offset", 2, 0.25f, 0.0f,
            -60.0f, 60.0f, {0, 4}, [this](float value) {
                offset_ = value;
                if(selected_) VideoLibrary::Instance().UpdateTiming(
                    std::string(selected_->levelID), VideoOrigin::User, offset_, rate_);
            });
        rateSetting_ = BSML::Lite::CreateIncrementSetting(
            controller, "Playback Speed", 2, 0.05f, 1.0f,
            0.05f, 8.0f, {0, -4}, [this](float value) {
                rate_ = value;
                if(selected_) VideoLibrary::Instance().UpdateTiming(
                    std::string(selected_->levelID), VideoOrigin::User, offset_, rate_);
            });
        downloadButton_ = BSML::Lite::CreateUIButton(
            controller, "Download Video", UnityEngine::Vector2{-14, -14}, UnityEngine::Vector2{27, 7},
            [this]() { StartOrCancelDownload(); });
        fitButton_ = BSML::Lite::CreateUIButton(
            controller, "Fit to Song", UnityEngine::Vector2{14, -14}, UnityEngine::Vector2{22, 7}, [this]() { FitToSong(); });
        removeButton_ = BSML::Lite::CreateUIButton(
            controller, "Remove User Video", UnityEngine::Vector2{0, -23}, UnityEngine::Vector2{31, 7},
            [this]() { RemoveOverride(); });
        detailStorage_ = BSML::Lite::CreateText(
            controller, "", 2.4f, {0, -34}, {55, 5});

        for(auto* text : {browserTitle_, browserStorage_, filterText_, detailTitle_, detailText_, detailStorage_})
            if(text) text->set_alignment(TMPro::TextAlignmentOptions::Center);

        RebuildCatalog();
        ShowBrowser();
        Refresh();
    }

    void VideoLibraryMenu::RebuildCatalog()
    {
        catalog_.clear();
        std::unordered_set<std::string> ids;
        auto add = [&](GlobalNamespace::BeatmapLevel* level, SongLibraryGroup group) {
            if(level && level->levelID && ids.emplace(std::string(level->levelID)).second)
                catalog_.push_back({level, group});
        };

        auto* container = BSML::Helpers::GetDiContainer();
        auto* model = container
            ? container->Resolve<GlobalNamespace::BeatmapLevelsModel*>()
            : nullptr;
        auto* repository = model
            ? model->__cordl_internal_get__allExistingBeatmapLevelsRepository()
            : nullptr;
        if(repository)
        {
            for(auto* pack : repository->__cordl_internal_get__beatmapLevelPacks())
            {
                if(!pack) continue;
                for(auto* level : pack->__cordl_internal_get_beatmapLevels())
                {
                    auto* custom = level && level->levelID
                        ? SongCore::API::Loading::GetLevelByLevelID(std::string(level->levelID))
                        : nullptr;
                    add(level, custom
                        ? (IsWip(custom) ? SongLibraryGroup::Wip : SongLibraryGroup::Custom)
                        : OfficialGroup(pack));
                }
            }
        }

        // SongCore is authoritative for custom and WIP songs. Supplementing
        // the base repository prevents its async refresh timing from hiding
        // tracks when this menu is opened immediately after game startup.
        for(auto* custom : SongCore::API::Loading::GetAllLevels())
            add(custom, IsWip(custom) ? SongLibraryGroup::Wip : SongLibraryGroup::Custom);

        std::sort(catalog_.begin(), catalog_.end(), [](const auto& left, const auto& right) {
            return Lower(std::string(left.level->songName)) <
                   Lower(std::string(right.level->songName));
        });
        std::array<int, 4> groupCounts{};
        for(const auto& item : catalog_)
            ++groupCounts[static_cast<int>(item.group)];
        PaperLogger.info(
            "Video library catalog: {} total ({} custom, {} WIP, {} OST, {} DLC)",
            catalog_.size(), groupCounts[0], groupCounts[1], groupCounts[2], groupCounts[3]);
        RebuildVisibleRows();
    }

    void VideoLibraryMenu::RebuildVisibleRows()
    {
        visible_.clear();
        if(!list_) return;
        list_->data->Clear();
        const auto query = Lower(search_);
        for(auto& item : catalog_)
        {
            auto* level = item.level;
            const auto name = level->songName ? std::string(level->songName) : "Unknown Song";
            const auto author = level->songAuthorName
                ? std::string(level->songAuthorName) : std::string{};
            const auto descriptor = VideoLibrary::Instance().Describe(level);
            if(!MatchesFilter(filter_, item, descriptor) ||
               (!query.empty() && Lower(name + " " + author).find(query) == std::string::npos))
                continue;
            visible_.push_back(&item);
            const std::string videoState = descriptor.hasUserOverride ? "User video" :
                descriptor.CanPlay() ? "Video ready" :
                descriptor.CanDownload() ? "Download available" : "No video";
            list_->data->Add(BSML::CustomCellInfo::construct(
                name, author.empty() ? videoState : author + " | " + videoState));
        }
        if(browserTitle_)
            browserTitle_->set_text("Video Library (" + std::to_string(visible_.size()) + ")");
        if(browserStorage_)
            browserStorage_->set_text(StorageLabel(
                VideoLibrary::Instance().LibraryBytes(), VideoLibrary::Instance().FreeBytes()));
        if(list_->tableView)
        {
            list_->tableView->ReloadData();
            list_->tableView->ScrollToCellWithIdx(0, HMUI::TableView::ScrollPositionType::Beginning, false);
        }
    }

    void VideoLibraryMenu::ChangeFilter(int direction)
    {
        constexpr int count = static_cast<int>(FilterNames.size());
        const int next = (static_cast<int>(filter_) + direction + count) % count;
        filter_ = static_cast<SongLibraryFilter>(next);
        if(filterText_) filterText_->set_text(FilterNames[next]);
        RebuildVisibleRows();
    }

    void VideoLibraryMenu::SelectRow(int row)
    {
        if(row < 0 || row >= static_cast<int>(visible_.size())) return;
        selected_ = visible_[row]->level;
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        url_ = descriptor.downloadUrl.value_or("");
        offset_ = descriptor.playableConfig ? descriptor.playableConfig->offsetSeconds : 0.0;
        rate_ = descriptor.playableConfig ? descriptor.playableConfig->playbackRate : 1.0;
        if(urlInput_) urlInput_->SetText(url_);
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        ShowEditor();
        RefreshDetails();
        StartSelectedPreview();
    }

    void VideoLibraryMenu::ShowBrowser()
    {
        editorVisible_ = false;
        SetEditorVisible(false);
        SetBrowserVisible(true);
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Stop();
        ScreenPreview::Instance().ActivateCurrentState();
        RebuildVisibleRows();
    }

    void VideoLibraryMenu::ShowEditor()
    {
        editorVisible_ = true;
        SetBrowserVisible(false);
        SetEditorVisible(true);
    }

    void VideoLibraryMenu::SetBrowserVisible(bool visible)
    {
        SetActive(browserTitle_, visible);
        SetActive(browserStorage_, visible);
        SetActive(searchInput_, visible);
        SetActive(filterPreviousButton_, visible);
        SetActive(filterText_, visible);
        SetActive(filterNextButton_, visible);
        SetActive(list_, visible);
    }

    void VideoLibraryMenu::SetEditorVisible(bool visible)
    {
        SetActive(backToListButton_, visible);
        SetActive(detailTitle_, visible);
        SetActive(detailText_, visible);
        SetActive(urlInput_, visible);
        SetActive(offsetSetting_, visible);
        SetActive(rateSetting_, visible);
        SetActive(downloadButton_, visible);
        SetActive(fitButton_, visible);
        SetActive(removeButton_, visible);
        SetActive(detailStorage_, visible);
    }

    void VideoLibraryMenu::StartOrCancelDownload()
    {
        auto& downloader = DownloadManager::Instance();
        if(downloader.Snapshot().Active()) { downloader.Cancel(); return; }
        if(!selected_ || url_.empty())
        {
            if(detailText_) detailText_->set_text("Enter a YouTube URL first.");
            return;
        }
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
        RefreshDetails();
        StartSelectedPreview();
    }

    void VideoLibraryMenu::FitToSong()
    {
        if(!selected_ || selected_->songDuration <= 0.0f) return;
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        if(!descriptor.playableConfig || descriptor.playableConfig->declaredDurationSeconds <= 0.0)
        {
            if(detailText_) detailText_->set_text("Video duration is not available yet.");
            return;
        }
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
        if(detailStorage_) detailStorage_->set_text(StorageLabel(
            VideoLibrary::Instance().LibraryBytes(), VideoLibrary::Instance().FreeBytes()));
        if(!selected_ || !detailText_) return;
        if(detailTitle_)
        {
            const auto name = selected_->songName ? std::string(selected_->songName) : "Unknown Song";
            const auto author = selected_->songAuthorName
                ? std::string(selected_->songAuthorName) : std::string{};
            detailTitle_->set_text(author.empty() ? name : name + "\n" + author);
        }
        const auto download = DownloadManager::Instance().Snapshot();
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        const bool thisDownload = download.levelId == std::string(selected_->levelID);
        detailText_->set_text(thisDownload && download.state != DownloadState::Idle
            ? download.message
            : descriptor.hasUserOverride ? "User video active" :
              descriptor.CanPlay() ? "Mapper video ready" :
              descriptor.CanDownload() ? "Mapper video available to download" :
              "Paste a YouTube URL to add a video");
        if(downloadButton_) BSML::Lite::SetButtonText(
            downloadButton_, download.Active() ? "Pause Download" :
            descriptor.hasUserOverride ? "Replace Video" : "Download Video");
        if(removeButton_) removeButton_->set_interactable(descriptor.hasUserOverride);
        if(fitButton_) fitButton_->set_interactable(descriptor.CanPlay());
        if(thisDownload && download.state == DownloadState::Completed &&
           !PlaybackSession::Instance().IsLibraryPreviewActive())
            StartSelectedPreview();
    }

    void VideoLibraryMenu::StartSelectedPreview()
    {
        auto& playback = PlaybackSession::Instance();
        playback.Stop();
        if(!selected_ || !VideoLibrary::Instance().Describe(selected_).CanPlay())
        {
            ScreenPreview::Instance().ActivateCurrentState();
            return;
        }
        ScreenPreview::Instance().Suspend();
        playback.Prepare(selected_);
        playback.Start(PlaybackContext::LibraryPreview);
        previewStartedAt_ = UnityEngine::Time::get_realtimeSinceStartup();
    }

    void VideoLibraryMenu::Tick()
    {
        if(!active_) return;
        if(++tickCounter_ >= 30)
        {
            tickCounter_ = 0;
            if(editorVisible_) RefreshDetails();
        }
        auto& playback = PlaybackSession::Instance();
        if(editorVisible_ && selected_ &&
           VideoLibrary::Instance().Describe(selected_).CanPlay() &&
           !playback.IsLibraryPreviewActive())
            StartSelectedPreview();
        if(playback.IsLibraryPreviewActive())
            playback.Tick(UnityEngine::Time::get_realtimeSinceStartup() - previewStartedAt_);
    }

    void VideoLibraryMenu::Refresh()
    {
        if(!controller_) return;
        active_ = true;
        RebuildCatalog();
        if(editorVisible_) RefreshDetails();
        if(!Settings::Instance().ModEnabled())
            DownloadManager::Instance().Cancel();
    }

    void VideoLibraryMenu::Deactivate()
    {
        active_ = false;
        DownloadManager::Instance().Cancel();
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Stop();
    }
}
