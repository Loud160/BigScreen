#include "BigScreen/VideoLibraryMenu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
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
#include "HMUI/ImageView.hpp"
#include "HMUI/TableView.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/GUIUtility.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/GridLayoutGroup.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Lists.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/ClickableText.hpp"
#include "bsml/shared/BSML/Components/CustomListTableData.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/Helpers/delegates.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
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

        std::string Trim(std::string value)
        {
            const auto isWhitespace = [](unsigned char character) {
                return std::isspace(character) != 0;
            };
            value.erase(
                value.begin(),
                std::find_if_not(value.begin(), value.end(), isWhitespace));
            value.erase(
                std::find_if_not(value.rbegin(), value.rend(), isWhitespace).base(),
                value.end());
            return value;
        }

        bool IsWebUrl(const std::string& value)
        {
            const auto lowered = Lower(value);
            return lowered.starts_with("https://") || lowered.starts_with("http://");
        }

        bool IsYouTubeUrl(const std::string& value)
        {
            const auto lowered = Lower(Trim(value));
            const auto scheme = lowered.find("://");
            if(scheme == std::string::npos ||
               (lowered.substr(0, scheme) != "http" &&
                lowered.substr(0, scheme) != "https"))
                return false;
            const auto hostStart = scheme + 3;
            const auto hostEnd = lowered.find_first_of("/:?#", hostStart);
            auto host = lowered.substr(hostStart, hostEnd - hostStart);
            const auto at = host.rfind('@');
            if(at != std::string::npos) host.erase(0, at + 1);
            const auto isDomain = [&host](std::string_view domain) {
                return host == domain ||
                    (host.size() > domain.size() &&
                     host.ends_with(std::string(".") + std::string(domain)));
            };
            return isDomain("youtube.com") ||
                   isDomain("youtu.be") ||
                   isDomain("youtube-nocookie.com");
        }

        std::string Megabytes(std::uint64_t bytes)
        {
            std::ostringstream text;
            text << std::fixed << std::setprecision(1)
                 << bytes / 1048576.0 << " MB";
            return text.str();
        }

        std::string DownloadStatus(const DownloadSnapshot& download)
        {
            if(download.state != DownloadState::Downloading)
                return download.message;

            std::ostringstream text;
            text << "Downloading  " << Megabytes(download.downloadedBytes);
            if(download.totalBytes)
            {
                const auto percent = static_cast<int>(std::clamp(
                    100.0 * download.downloadedBytes / download.totalBytes,
                    0.0,
                    100.0));
                text << " / " << Megabytes(download.totalBytes)
                     << "  (" << percent << "%)";
            }
            if(download.speedBytesPerSecond > 0.0)
                text << "  |  " << std::fixed << std::setprecision(1)
                     << download.speedBytesPerSecond / 1048576.0 << " MB/s";
            if(download.etaSeconds > 0.0)
            {
                const auto seconds = static_cast<int>(download.etaSeconds);
                text << "  |  " << seconds / 60 << ':'
                     << std::setfill('0') << std::setw(2) << seconds % 60
                     << " left";
            }
            return text.str();
        }

        std::string StorageLabel(std::uint64_t used, std::uint64_t free)
        {
            std::ostringstream text;
            text << std::fixed << std::setprecision(1)
                 << "Videos " << used / 1073741824.0 << " GB  |  Free "
                 << free / 1073741824.0 << " GB";
            return text.str();
        }

        std::string BrowserSummary(
            std::size_t count,
            std::uint64_t used,
            std::uint64_t free)
        {
            std::ostringstream text;
            text << count << " maps  |  " << std::fixed << std::setprecision(1)
                 << used / 1073741824.0 << " GB videos  |  "
                 << free / 1073741824.0 << " GB free";
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

        UnityEngine::UI::LayoutElement* EnsureLayout(UnityEngine::Component* component)
        {
            if(!component) return nullptr;
            auto object = component->get_gameObject();
            auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>();
            return layout ? layout : object->AddComponent<UnityEngine::UI::LayoutElement*>();
        }

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

        template<class TLayout>
        void ConfigureGroup(TLayout* group, bool vertical)
        {
            if(!group) return;
            group->set_spacing(0.45f);
            group->set_childControlWidth(true);
            group->set_childControlHeight(true);
            group->set_childForceExpandWidth(false);
            group->set_childForceExpandHeight(false);
        }

        void StretchToPanel(UnityEngine::RectTransform* transform)
        {
            if(!transform) return;
            transform->set_anchorMin({0.0f, 0.0f});
            transform->set_anchorMax({1.0f, 1.0f});
            transform->set_pivot({0.5f, 0.5f});
            transform->set_anchoredPosition({0.0f, 0.0f});
            transform->set_sizeDelta({-4.0f, -3.0f});
        }

        UnityEngine::GameObject* ConstructLayout(
            std::string_view markup,
            UnityEngine::Transform* parent,
            const std::string& id)
        {
            auto parser = BSML::parse_and_construct(markup, parent, nullptr);
            if(!parser || !parser->parserParams) return nullptr;
            const auto& matches = parser->parserParams->GetObjectsWithTag(id);
            return matches.empty() ? nullptr : matches.front();
        }
    }

    VideoLibraryMenu& VideoLibraryMenu::Instance()
    {
        static VideoLibraryMenu menu;
        return menu;
    }

    void VideoLibraryMenu::CreateUi(
        HMUI::ViewController* browserController,
        HMUI::ViewController* editorController,
        std::function<void(bool showEditor)> navigate)
    {
        if(!browserController || !editorController) return;
        browserController_ = browserController;
        editorController_ = editorController;
        navigate_ = std::move(navigate);

        // Qounters-style pages use one layout-owned content tree per view
        // controller. Keeping browser and editor controls on separate
        // controllers removes the hidden hit targets and partial-page remnants
        // caused by the earlier absolute-position overlay.
        auto* browserRoot = BSML::Lite::CreateVerticalLayoutGroup(browserController);
        ConfigureGroup(browserRoot, true);
        browserRoot->set_childForceExpandWidth(true);
        StretchToPanel(browserRoot->get_rectTransform());

        browserTitle_ = BSML::Lite::CreateText(browserRoot, "Video Library", 4.8f);
        ConfigureLayout(browserTitle_, -1.0f, 5.8f, 1.0f);
        browserStorage_ = BSML::Lite::CreateText(browserRoot, "", 2.35f);
        ConfigureLayout(browserStorage_, -1.0f, 3.8f, 1.0f);
        searchInput_ = BSML::Lite::CreateStringSetting(
            browserRoot, "Search Maps", "", [this](StringW value) {
                search_ = std::string(value);
                RebuildVisibleRows();
            });
        ConfigureLayout(searchInput_, -1.0f, 8.0f, 1.0f);

        // A rounded panel visually binds the arrows and value into one
        // selector. This is the same BSML background treatment used for
        // grouped controls elsewhere in Beat Saber's mod menus.
        auto* filterPanel = ConstructLayout(
            "<horizontal tags='big-screen-filter' bg='round-rect-panel' "
            "pad-left='1' pad-right='1'/>",
            browserRoot->get_transform(),
            "big-screen-filter");
        auto* filterRow = filterPanel
            ? filterPanel->GetComponent<UnityEngine::UI::HorizontalLayoutGroup*>()
            : BSML::Lite::CreateHorizontalLayoutGroup(browserRoot);
        ConfigureGroup(filterRow, false);
        ConfigureLayout(filterRow, -1.0f, 7.0f, 1.0f);
        filterPreviousButton_ = BSML::Lite::CreateUIButton(
            filterRow, "<", {0.0f, 0.0f}, {8.0f, 7.0f}, [this]() { ChangeFilter(-1); });
        ConfigureLayout(filterPreviousButton_, 8.0f, 7.0f);
        BSML::Lite::SetButtonTextSize(filterPreviousButton_, 3.4f);
        filterText_ = BSML::Lite::CreateText(
            filterRow, "Filter: Show All Maps", 3.0f);
        ConfigureLayout(filterText_, 33.0f, 7.0f, 1.0f);
        filterNextButton_ = BSML::Lite::CreateUIButton(
            filterRow, ">", {0.0f, 0.0f}, {8.0f, 7.0f}, [this]() { ChangeFilter(1); });
        ConfigureLayout(filterNextButton_, 8.0f, 7.0f);
        BSML::Lite::SetButtonTextSize(filterNextButton_, 3.4f);

        auto* listRow = BSML::Lite::CreateHorizontalLayoutGroup(browserRoot);
        ConfigureGroup(listRow, false);
        listRow->set_spacing(0.8f);
        ConfigureLayout(listRow, -1.0f, -1.0f, 1.0f, 1.0f);
        // The compact two-column rail is deliberately the first child in this
        // row, placing it to the left of the songs. Its order is A/N, B/O, ...
        // M/Z, followed by # for titles beginning with punctuation or numbers.
        auto* alphabet = BSML::Lite::CreateGridLayoutGroup(listRow);
        alphabet->set_constraint(UnityEngine::UI::GridLayoutGroup::Constraint::FixedColumnCount);
        alphabet->set_constraintCount(2);
        alphabet->set_cellSize({3.5f, 3.35f});
        alphabet->set_spacing({0.15f, 0.15f});
        ConfigureLayout(alphabet, 7.3f, 50.0f, 0.0f, 1.0f);
        alphabetButtons_.clear();
        for(int row = 0; row < 13; ++row)
        {
            for(const char letter : {static_cast<char>('A' + row), static_cast<char>('N' + row)})
            {
                auto* button = BSML::Lite::CreateClickableText(
                    alphabet,
                    std::string(1, letter),
                    TMPro::FontStyles::Normal,
                    2.35f,
                    {0.0f, 0.0f},
                    {3.5f, 3.35f},
                    [this, letter]() { JumpToLetter(letter); });
                button->set_alignment(TMPro::TextAlignmentOptions::Center);
                alphabetButtons_.push_back(button);
            }
        }
        auto* numericButton = BSML::Lite::CreateClickableText(
            alphabet, "#", TMPro::FontStyles::Normal, 2.35f,
            {0.0f, 0.0f}, {3.5f, 3.35f}, [this]() { JumpToLetter('#'); });
        numericButton->set_alignment(TMPro::TextAlignmentOptions::Center);
        alphabetButtons_.push_back(numericButton);

        // Construct the list through BSML's standard show-scrollbar path.
        // Unlike CreateScrollableList's detached top/bottom carets, this uses
        // Beat Saber's native right-edge scroll indicator and page arrows.
        auto* listObject = ConstructLayout(
            "<list tags='big-screen-song-list' show-scrollbar='true' "
            "stick-scrolling='true' list-style='List'/>",
            listRow->get_transform(),
            "big-screen-song-list");
        list_ = listObject
            ? listObject->GetComponent<BSML::CustomListTableData*>()
            : nullptr;
        if(!list_)
        {
            PaperLogger.error("Could not construct the native video library list");
            list_ = BSML::Lite::CreateScrollableList(
                listRow, {0.0f, 0.0f}, {46.0f, 50.0f},
                [this](int row) { SelectRow(row); });
        }
        if(list_)
        {
            list_->set_listStyle(BSML::CustomListTableData::ListStyle::List);
            list_->expandCell = true;
            // Reserve eight units after the table itself for the native
            // right-edge scrollbar installed by show-scrollbar.
            ConfigureLayout(list_, 38.0f, 50.0f, 0.0f, 1.0f);
            if(listObject)
            {
                list_->tableView->add_didSelectCellWithIdxEvent(
                    BSML::MakeSystemAction(
                        std::function<void(UnityW<HMUI::TableView>, int)>(
                        [this](UnityW<HMUI::TableView>, int row)
                        {
                            SelectRow(row);
                        })));
            }
        }

        auto* editorRoot = BSML::Lite::CreateVerticalLayoutGroup(editorController);
        ConfigureGroup(editorRoot, true);
        editorRoot->set_childForceExpandWidth(true);
        StretchToPanel(editorRoot->get_rectTransform());

        backToListButton_ = BSML::Lite::CreateUIButton(
            editorRoot, "< Back to Song List", {0.0f, 0.0f}, {45.0f, 7.0f}, [this]() { ShowBrowser(); });
        ConfigureLayout(backToListButton_, -1.0f, 7.0f, 1.0f);
        BSML::Lite::SetButtonTextSize(backToListButton_, 3.1f);
        detailTitle_ = BSML::Lite::CreateText(
            editorRoot, "", 3.6f);
        ConfigureLayout(detailTitle_, -1.0f, 7.5f, 1.0f);
        urlInput_ = BSML::Lite::CreateStringSetting(
            editorRoot, "YouTube URL", "", [this](StringW value) {
                url_ = Trim(std::string(value));
                transientStatus_.clear();
                if(!suppressUrlCallback_)
                    BeginUrlProbe();
            });
        ConfigureLayout(urlInput_, -1.0f, 8.0f, 1.0f);

        // Keep the address field at the panel's full width. Recognition art
        // belongs in the following action row, where it cannot reduce the
        // amount of the pasted URL that remains visible and editable.
        auto* urlPreviewRow = BSML::Lite::CreateHorizontalLayoutGroup(editorRoot);
        ConfigureGroup(urlPreviewRow, false);
        urlPreviewRow->set_spacing(0.45f);
        ConfigureLayout(urlPreviewRow, -1.0f, 9.5f, 1.0f);
        urlThumbnail_ = BSML::Lite::CreateImage(
            urlPreviewRow,
            BSML::Utilities::ImageResources::GetBlankSprite());
        ConfigureLayout(urlThumbnail_, 15.0f, 8.4f, 0.0f);
        urlThumbnail_->set_color({0.08f, 0.10f, 0.13f, 0.85f});
        urlThumbnail_->set_preserveAspect(true);
        pasteUrlButton_ = BSML::Lite::CreateUIButton(
            urlPreviewRow,
            "Paste URL",
            {0.0f, 0.0f},
            {15.5f, 7.5f},
            [this]() { PasteUrlFromClipboard(); });
        ConfigureLayout(pasteUrlButton_, 15.5f, 7.5f, 1.0f);
        BSML::Lite::SetButtonTextSize(pasteUrlButton_, 2.6f);
        downloadButton_ = BSML::Lite::CreateUIButton(
            urlPreviewRow, "Download Video", {0.0f, 0.0f}, {18.5f, 7.5f},
            [this]() { StartOrCancelDownload(); });
        ConfigureLayout(downloadButton_, 18.5f, 7.5f, 1.0f);
        BSML::Lite::SetButtonTextSize(downloadButton_, 2.45f);
        BSML::Lite::AddHoverHint(
            urlInput_,
            "Accepts youtube.com links and youtu.be Share links.");
        BSML::Lite::AddHoverHint(
            pasteUrlButton_,
            "Pastes a youtube.com or youtu.be address from the Quest clipboard.");

        // Status and progress belong directly below the controls they describe.
        // This keeps recognition errors and active download feedback visually
        // attached to the thumbnail/download row instead of the song heading.
        detailText_ = BSML::Lite::CreateText(
            editorRoot, "", 2.35f);
        ConfigureLayout(detailText_, -1.0f, 7.0f, 1.0f);
        downloadProgressTrack_ = BSML::Lite::CreateImage(
            editorRoot,
            BSML::Utilities::ImageResources::GetBlankSprite());
        ConfigureLayout(downloadProgressTrack_, -1.0f, 2.2f, 1.0f);
        downloadProgressTrack_->set_color({0.08f, 0.10f, 0.13f, 0.85f});
        downloadProgressTrack_->set_preserveAspect(false);
        downloadProgressFill_ = BSML::Lite::CreateImage(
            downloadProgressTrack_->get_transform(),
            BSML::Utilities::ImageResources::GetBlankSprite());
        downloadProgressFill_->set_color({0.10f, 0.75f, 1.0f, 1.0f});
        downloadProgressFill_->set_preserveAspect(false);
        if(auto fillRect = downloadProgressFill_->get_transform().cast<UnityEngine::RectTransform>())
        {
            fillRect->set_anchorMin({0.0f, 0.0f});
            fillRect->set_anchorMax({0.0f, 1.0f});
            fillRect->set_pivot({0.0f, 0.5f});
            fillRect->set_anchoredPosition({0.0f, 0.0f});
            fillRect->set_sizeDelta({0.0f, -0.35f});
        }
        downloadProgressTrack_->get_gameObject()->SetActive(false);
        offsetSetting_ = BSML::Lite::CreateIncrementSetting(
            editorRoot, "Start Offset", 2, 0.25f, 0.0f,
            -60.0f, 60.0f, {0, 0}, [this](float value) {
                offset_ = value;
                if(selected_ && VideoLibrary::Instance().Describe(selected_).CanPlay() &&
                   VideoLibrary::Instance().UpdateTiming(
                    std::string(selected_->levelID), SelectedVideoOrigin(), offset_, rate_))
                    StartSelectedPreview();
            });
        ConfigureLayout(offsetSetting_, -1.0f, 8.0f, 1.0f);
        rateSetting_ = BSML::Lite::CreateIncrementSetting(
            editorRoot, "Playback Speed", 2, 0.05f, 1.0f,
            0.05f, 8.0f, {0, 0}, [this](float value) {
                rate_ = value;
                if(selected_ && VideoLibrary::Instance().Describe(selected_).CanPlay() &&
                   VideoLibrary::Instance().UpdateTiming(
                    std::string(selected_->levelID), SelectedVideoOrigin(), offset_, rate_))
                    StartSelectedPreview();
            });
        ConfigureLayout(rateSetting_, -1.0f, 8.0f, 1.0f);

        fitButton_ = BSML::Lite::CreateUIButton(
            editorRoot, "Fit to Song", {0.0f, 0.0f}, {31.0f, 7.0f},
            [this]() { FitToSong(); });
        ConfigureLayout(fitButton_, -1.0f, 7.0f, 1.0f);
        BSML::Lite::SetButtonTextSize(fitButton_, 2.8f);
        removeButton_ = BSML::Lite::CreateUIButton(
            editorRoot, "Remove User Video", {0.0f, 0.0f}, {31.0f, 7.0f},
            [this]() { RemoveOverride(); });
        ConfigureLayout(removeButton_, -1.0f, 7.0f, 1.0f);
        BSML::Lite::SetButtonTextSize(removeButton_, 2.8f);
        detailStorage_ = BSML::Lite::CreateText(
            editorRoot, "", 2.3f);
        ConfigureLayout(detailStorage_, -1.0f, 3.0f, 1.0f);

        for(auto* text : {browserTitle_, browserStorage_, filterText_, detailTitle_, detailText_, detailStorage_})
            if(text) text->set_alignment(TMPro::TextAlignmentOptions::Center);

        RebuildCatalog();
        editorVisible_ = false;
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
            browserTitle_->set_text("Video Library");
        if(browserStorage_)
            browserStorage_->set_text(BrowserSummary(
                visible_.size(),
                VideoLibrary::Instance().LibraryBytes(),
                VideoLibrary::Instance().FreeBytes()));
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
        if(filterText_) filterText_->set_text(
            "Filter: " + std::string(FilterNames[next]));
        RebuildVisibleRows();
    }

    void VideoLibraryMenu::SelectRow(int row)
    {
        if(row < 0 || row >= static_cast<int>(visible_.size())) return;
        selected_ = visible_[row]->level;
        transientStatus_.clear();
        loadedThumbnailPath_.clear();
        if(loadedThumbnailSprite_)
        {
            UnityEngine::Object::Destroy(loadedThumbnailSprite_);
            loadedThumbnailSprite_ = nullptr;
        }
        if(urlThumbnail_)
        {
            urlThumbnail_->set_sprite(
                BSML::Utilities::ImageResources::GetBlankSprite());
            urlThumbnail_->set_color({0.08f, 0.10f, 0.13f, 0.85f});
        }
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        url_ = descriptor.downloadUrl.value_or("");
        offset_ = descriptor.playableConfig ? descriptor.playableConfig->offsetSeconds : 0.0;
        rate_ = descriptor.playableConfig ? descriptor.playableConfig->playbackRate : 1.0;
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
        }
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        ShowEditor();
        RefreshDetails();
        StartSelectedPreview();
    }

    void VideoLibraryMenu::ShowBrowser()
    {
        editorVisible_ = false;
        if(navigate_) navigate_(false);
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Stop();
        ScreenPreview::Instance().ActivateCurrentState();
        RebuildVisibleRows();
    }

    void VideoLibraryMenu::ShowEditor()
    {
        editorVisible_ = true;
        if(navigate_) navigate_(true);
    }

    void VideoLibraryMenu::JumpToLetter(char letter)
    {
        if(!list_ || !list_->tableView || visible_.empty()) return;
        for(std::size_t index = 0; index < visible_.size(); ++index)
        {
            auto* level = visible_[index] ? visible_[index]->level : nullptr;
            const std::string name = level && level->songName
                ? std::string(level->songName)
                : std::string{};
            if(name.empty()) continue;
            const auto first = static_cast<unsigned char>(name.front());
            const bool alpha = std::isalpha(first) != 0;
            const char normalized = alpha
                ? static_cast<char>(std::toupper(first))
                : '#';
            if(normalized == letter)
            {
                list_->tableView->ScrollToCellWithIdx(
                    static_cast<int>(index),
                    HMUI::TableView::ScrollPositionType::Beginning,
                    true);
                return;
            }
        }
    }

    VideoOrigin VideoLibraryMenu::SelectedVideoOrigin() const
    {
        if(!selected_) return VideoOrigin::User;
        return VideoLibrary::Instance().Describe(selected_).hasUserOverride
            ? VideoOrigin::User
            : VideoOrigin::Mapper;
    }

    void VideoLibraryMenu::BeginUrlProbe()
    {
        loadedThumbnailPath_.clear();
        if(loadedThumbnailSprite_)
        {
            UnityEngine::Object::Destroy(loadedThumbnailSprite_);
            loadedThumbnailSprite_ = nullptr;
        }
        if(urlThumbnail_)
        {
            urlThumbnail_->set_sprite(
                BSML::Utilities::ImageResources::GetBlankSprite());
            urlThumbnail_->set_color({0.08f, 0.10f, 0.13f, 0.85f});
        }
        if(!selected_)
        {
            transientStatus_ = "Select a song before checking a video URL.";
            RefreshDetails();
            return;
        }
        url_ = Trim(url_);
        if(!IsYouTubeUrl(url_))
        {
            transientStatus_ = url_.empty()
                ? "Paste a youtube.com or youtu.be URL first."
                : "That is not a recognized YouTube URL. Use youtube.com or youtu.be.";
            RefreshDetails();
            return;
        }

        std::string error;
        if(!DownloadManager::Instance().StartProbe(
            std::string(selected_->levelID),
            url_,
            error))
        {
            transientStatus_ = error.empty()
                ? "The YouTube URL could not be checked."
                : error;
        }
        else
        {
            transientStatus_.clear();
        }
        RefreshDetails();
    }

    void VideoLibraryMenu::StartOrCancelDownload()
    {
        auto& downloader = DownloadManager::Instance();
        const auto current = downloader.Snapshot();
        if(!selected_)
        {
            transientStatus_ = "Select a song before downloading a video.";
            RefreshDetails();
            return;
        }
        const auto selectedLevelId = std::string(selected_->levelID);
        if(current.metadataOnly && current.levelId == selectedLevelId &&
           current.state == DownloadState::Failed)
        {
            BeginUrlProbe();
            return;
        }
        if(current.Active())
        {
            if(current.levelId == selectedLevelId)
            {
                downloader.Cancel();
                transientStatus_ = "Stopping download...";
            }
            else
            {
                transientStatus_ = "Another downloader task is already running.";
            }
            RefreshDetails();
            return;
        }
        url_ = Trim(url_);
        if(url_.empty())
        {
            transientStatus_ = "Paste a youtube.com or youtu.be URL first.";
            RefreshDetails();
            return;
        }
        if(!IsYouTubeUrl(url_))
        {
            transientStatus_ =
                "That is not a recognized YouTube URL. Use youtube.com or youtu.be.";
            RefreshDetails();
            return;
        }
        DownloadRequest request{
            std::string(selected_->levelID),
            std::string(selected_->songName),
            selected_->songAuthorName ? std::string(selected_->songAuthorName) : std::string{},
            url_, VideoOrigin::User, ExplicitAllowed(), offset_, rate_};
        std::string error;
        PaperLogger.info("Download button pressed for {}", selectedLevelId);
        if(!downloader.Start(std::move(request), error))
        {
            transientStatus_ = error.empty()
                ? "The download could not be started."
                : error;
            PaperLogger.error(
                "Could not start download for {}: {}",
                selectedLevelId,
                transientStatus_);
        }
        else
        {
            transientStatus_.clear();
        }
        RefreshDetails();
    }

    void VideoLibraryMenu::PasteUrlFromClipboard()
    {
        if(!urlInput_ || !detailText_) return;
        try
        {
            const auto clipboardValue = UnityEngine::GUIUtility::get_systemCopyBuffer();
            const auto clipboard = Trim(
                clipboardValue ? std::string(clipboardValue) : std::string{});
            if(clipboard.empty())
            {
                detailText_->set_text(
                    "The Quest clipboard is empty. Copy a video link first.");
                return;
            }
            if(!IsWebUrl(clipboard))
            {
                detailText_->set_text(
                    "Clipboard text is not a valid http or https URL.");
                return;
            }

            url_ = clipboard;
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
            BeginUrlProbe();
        }
        catch(const std::exception& error)
        {
            PaperLogger.error("Could not read the Quest clipboard: {}", error.what());
            detailText_->set_text("Could not read the Quest clipboard.");
        }
    }

    void VideoLibraryMenu::RemoveOverride()
    {
        if(!selected_) return;
        VideoLibrary::Instance().RemoveUserOverride(std::string(selected_->levelID), true);
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        url_ = descriptor.downloadUrl.value_or("");
        offset_ = descriptor.playableConfig
            ? descriptor.playableConfig->offsetSeconds
            : 0.0;
        rate_ = descriptor.playableConfig
            ? descriptor.playableConfig->playbackRate
            : 1.0;
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
        }
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        RefreshDetails();
        StartSelectedPreview();
    }

    void VideoLibraryMenu::FitToSong()
    {
        if(!selected_)
        {
            transientStatus_ = "Select a song before using Fit to Song.";
            RefreshDetails();
            return;
        }
        if(selected_->songDuration <= 0.0f)
        {
            transientStatus_ = "Song duration is unavailable, so Fit to Song could not run.";
            RefreshDetails();
            return;
        }
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        if(!descriptor.playableConfig || descriptor.playableConfig->declaredDurationSeconds <= 0.0)
        {
            transientStatus_ = "Video duration is unavailable, so Fit to Song could not run.";
            RefreshDetails();
            return;
        }

        const auto requestedRate =
            descriptor.playableConfig->declaredDurationSeconds / selected_->songDuration;
        rate_ = std::clamp(requestedRate, 0.05, 8.0);
        if(!VideoLibrary::Instance().UpdateTiming(
            std::string(selected_->levelID), SelectedVideoOrigin(), offset_, rate_))
        {
            transientStatus_ = "Fit to Song could not save the new playback speed.";
            RefreshDetails();
            return;
        }
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        StartSelectedPreview();

        // Report the concrete result so the button never appears inert. Include
        // the limit when an unusually mismatched video requires a speed outside
        // the range supported by the playback controls.
        std::ostringstream status;
        status << std::fixed << std::setprecision(2)
               << "Fit applied: playback speed set to " << rate_ << "x";
        if(std::abs(requestedRate - rate_) > 0.0001)
            status << " (limited from " << requestedRate << "x)";
        status << ".";
        transientStatus_ = status.str();
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
        if(transientStatus_ == "Stopping download..." && !download.Active())
            transientStatus_.clear();
        // The background probe writes a complete JPEG before publishing its
        // path. Decode it only after that terminal status, and retain the
        // sprite until another URL or song is selected so periodic UI ticks
        // never allocate duplicate Unity textures.
        if(thisDownload && download.state == DownloadState::ProbeCompleted &&
           !download.thumbnailPath.empty() && urlThumbnail_)
        {
            const auto thumbnailIdentity =
                download.thumbnailPath + "|" + download.title;
            if(loadedThumbnailPath_ != thumbnailIdentity &&
               std::filesystem::is_regular_file(download.thumbnailPath))
            {
                try
                {
                    auto* sprite = BSML::Lite::FileToSprite(download.thumbnailPath);
                    if(sprite)
                    {
                        if(loadedThumbnailSprite_)
                            UnityEngine::Object::Destroy(loadedThumbnailSprite_);
                        loadedThumbnailSprite_ = sprite;
                        loadedThumbnailPath_ = thumbnailIdentity;
                        urlThumbnail_->set_sprite(sprite);
                        urlThumbnail_->set_color(UnityEngine::Color::get_white());
                    }
                }
                catch(const std::exception& error)
                {
                    PaperLogger.warn(
                        "Could not display the YouTube thumbnail: {}",
                        error.what());
                }
            }
        }
        detailText_->set_text(!transientStatus_.empty()
            ? transientStatus_
            : thisDownload && download.state != DownloadState::Idle
                ? DownloadStatus(download)
            : descriptor.hasUserOverride ? "User video active" :
              descriptor.CanPlay() ? "Mapper video ready" :
              descriptor.CanDownload() ? "Mapper video available to download" :
              "Paste a youtube.com or youtu.be URL to add a video");
        if(downloadProgressTrack_ && downloadProgressFill_)
        {
            // Metadata lookup has no byte total, so it uses a pulsing fill.
            // Once yt-dlp starts transferring the MP4, the same bar switches
            // to an exact byte ratio and changes color for terminal outcomes.
            const bool showProgress = thisDownload &&
                download.state != DownloadState::Idle;
            downloadProgressTrack_->get_gameObject()->SetActive(showProgress);
            if(showProgress)
            {
                float progress = 0.0f;
                if(download.state == DownloadState::Completed ||
                   download.state == DownloadState::ProbeCompleted)
                    progress = 1.0f;
                else if(download.state == DownloadState::Failed ||
                        download.state == DownloadState::Cancelled)
                    progress = 0.04f;
                else if(download.Active() && download.totalBytes)
                    progress = std::clamp(
                        static_cast<float>(download.downloadedBytes) /
                            static_cast<float>(download.totalBytes),
                        0.0f,
                        1.0f);
                else if(download.Active())
                    progress = 0.12f + 0.68f * std::abs(std::sin(
                        UnityEngine::Time::get_realtimeSinceStartup() * 1.8f));

                downloadProgressFill_->set_color(
                    (download.state == DownloadState::Completed ||
                     download.state == DownloadState::ProbeCompleted)
                        ? UnityEngine::Color{0.20f, 0.90f, 0.42f, 1.0f}
                        : download.state == DownloadState::Failed
                            ? UnityEngine::Color{0.95f, 0.22f, 0.20f, 1.0f}
                            : download.state == DownloadState::Cancelled
                                ? UnityEngine::Color{0.95f, 0.65f, 0.12f, 1.0f}
                                : UnityEngine::Color{0.10f, 0.75f, 1.0f, 1.0f});
                if(auto fillRect = downloadProgressFill_->get_transform().cast<UnityEngine::RectTransform>())
                    fillRect->set_anchorMax({progress, 1.0f});
            }
        }
        if(downloadButton_) BSML::Lite::SetButtonText(
            downloadButton_, thisDownload && download.state == DownloadState::Probing
                ? "Checking URL..." :
            thisDownload && download.Active() ? "Cancel Download" :
            thisDownload && download.metadataOnly && download.state == DownloadState::Failed
                ? "Check URL Again" :
            thisDownload && download.state == DownloadState::Failed
                ? "Retry Download" :
            thisDownload && download.state == DownloadState::Cancelled
                ? "Resume Download" :
            descriptor.hasUserOverride ? "Replace Video" : "Download Video");
        if(downloadButton_)
            downloadButton_->set_interactable(
                (thisDownload && download.Active() &&
                    download.state != DownloadState::Probing) ||
                (!download.Active() && !url_.empty()));
        if(offsetSetting_) offsetSetting_->set_interactable(descriptor.CanPlay());
        if(rateSetting_) rateSetting_->set_interactable(descriptor.CanPlay());
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
        if(!browserController_ || !editorController_) return;
        active_ = true;
        RebuildCatalog();
        if(editorVisible_) RefreshDetails();
        if(!Settings::Instance().ModEnabled())
            DownloadManager::Instance().Cancel();
    }

    void VideoLibraryMenu::Deactivate()
    {
        active_ = false;
        editorVisible_ = false;
        DownloadManager::Instance().Cancel();
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Stop();
    }
}
