#include "BigScreen/VideoLibraryMenu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "GlobalNamespace/AudioHelpers.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BeatmapLevelPack.hpp"
#include "GlobalNamespace/BeatmapLevelsModel.hpp"
#include "GlobalNamespace/BeatmapLevelsRepository.hpp"
#include "GlobalNamespace/LevelListTableCell.hpp"
#include "GlobalNamespace/IPreviewMediaData.hpp"
#include "GlobalNamespace/PerceivedLoudnessPerLevelModel.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerSensitivityFlag.hpp"
#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "HMUI/InputFieldView.hpp"
#include "HMUI/ImageView.hpp"
#include "HMUI/TableCell.hpp"
#include "HMUI/TableView.hpp"
#include "System/Threading/CancellationToken.hpp"
#include "System/Threading/Tasks/Task_1.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/AudioClip.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/GUIUtility.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/GridLayoutGroup.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"
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
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
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
        // Keep the UI slider normalized instead of assigning the song's
        // duration as its range. HMUI's TextSlider is a settings control, and
        // very large per-song ranges can feed unwanted preferred geometry back
        // into its parent layout. One thousand normalized positions are still
        // comfortably finer than a controller can place the handle in VR.
        constexpr float PreviewScrubIncrement = 0.001f;
        constexpr float PreviewScrubFollowDelay = 0.25f;

        // These sprites are created only from the configured video's cached
        // YouTube thumbnail. Beat Saber's song/album cover is deliberately not
        // consulted: a blank row therefore means that song has no video.
        std::unordered_map<std::string, UnityEngine::Sprite*> VideoThumbnailSprites;
        std::unordered_set<std::string> FailedVideoThumbnailLoads;

        struct RowVideoThumbnail {
            bool hasVideo = false;
            std::optional<std::string> sourceUrl;
            std::optional<std::filesystem::path> path;
        };
        std::unordered_map<std::string, RowVideoThumbnail> RowVideoThumbnails;

        void EvictVideoThumbnail(const std::string& path)
        {
            if(const auto cached = VideoThumbnailSprites.find(path);
               cached != VideoThumbnailSprites.end())
            {
                if(cached->second)
                    UnityEngine::Object::Destroy(cached->second);
                VideoThumbnailSprites.erase(cached);
            }
            FailedVideoThumbnailLoads.erase(path);
        }

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

        std::string PlaybackTime(double seconds)
        {
            const auto wholeSeconds = static_cast<int>(std::max(0.0, seconds));
            const int minutes = wholeSeconds / 60;
            const int remainder = wholeSeconds % 60;
            std::ostringstream text;
            text << minutes << ':' << std::setw(2) << std::setfill('0') << remainder;
            return text.str();
        }

        UnityEngine::AudioSource* ActiveSongAudioSource(
            GlobalNamespace::SongPreviewPlayer* player)
        {
            if(!player) return nullptr;
            const int channel = player->__cordl_internal_get__activeChannel();
            auto controllers = player->__cordl_internal_get__audioSourceControllers();
            if(!controllers || channel < 0 || channel >= controllers.size())
                return nullptr;
            auto* controller = controllers[channel];
            return controller
                ? controller->__cordl_internal_get_audioSource().ptr()
                : nullptr;
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
            text << count << " songs  |  " << std::fixed << std::setprecision(1)
                 << used / 1073741824.0 << " GB videos  |  "
                 << free / 1073741824.0 << " GB free";
            return text.str();
        }

        std::size_t DistinctSongCount(
            const std::vector<SongLibraryItem*>& rows)
        {
            std::unordered_set<std::string> songs;
            for(const auto* item : rows)
            {
                const auto* level = item ? item->level : nullptr;
                if(!level) continue;

                // Beat Saber can expose more than one repository row for the
                // same musical work (for example, difficulty-specific data).
                // Count by normalized title/artist rather than those rows.
                const std::string name = level->songName
                    ? Lower(std::string(level->songName))
                    : std::string{};
                const std::string author = level->songAuthorName
                    ? Lower(std::string(level->songAuthorName))
                    : std::string{};
                if(!name.empty())
                    songs.emplace(name + "\x1f" + author);
                else if(level->levelID)
                    songs.emplace(std::string(level->levelID));
            }
            return songs.size();
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

        void SetToggleWithoutNotification(
            BSML::ToggleSetting* setting,
            bool value)
        {
            if(!setting) return;
            setting->currentValue = value;
            if(setting->toggle)
                setting->toggle->SetIsOnWithoutNotify(value);
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
            list_->expandCell = false;
            // Alphabet rail + spacing + 42-unit list matches the full-width
            // filter control above. The list's own right edge still contains
            // Beat Saber's native scrollbar and page arrows.
            ConfigureLayout(list_, 42.0f, 50.0f, 0.0f, 1.0f);
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

        // Place editor rows directly on the full-panel root. The BSML
        // ScrollView template carries its own narrow viewport geometry, which
        // cannot inherit the complete right-side panel width reliably. The
        // complete form fits in this controller, so an intermediate viewport
        // only reduces usable space without providing a layout benefit.
        const BSML::Lite::TransformWrapper editorBody(editorRoot);

        detailTitle_ = BSML::Lite::CreateText(
            editorBody, "", 3.6f);
        ConfigureLayout(detailTitle_, -1.0f, 7.5f, 1.0f);
        urlInput_ = BSML::Lite::CreateStringSetting(
            editorBody, "YouTube URL", "", [this](StringW value) {
                url_ = Trim(std::string(value));
                transientStatus_.clear();
                if(!suppressUrlCallback_)
                    BeginUrlProbe();
            });
        ConfigureLayout(urlInput_, -1.0f, 8.0f, 1.0f);

        // Keep the address field at the panel's full width. Recognition art
        // belongs in the following action row, where it cannot reduce the
        // amount of the pasted URL that remains visible and editable.
        auto* urlPreviewRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
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
            editorBody, "", 2.35f);
        ConfigureLayout(detailText_, -1.0f, 7.0f, 1.0f);
        downloadProgressTrack_ = BSML::Lite::CreateImage(
            editorBody,
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

        // Automatic fit and lead-in appearance are persistent per-video
        // choices. Keeping them side by side avoids increasing the editor's
        // height while placing both policies before the numeric timing fields.
        auto* timingToggleRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
        ConfigureGroup(timingToggleRow, false);
        timingToggleRow->set_spacing(0.6f);
        ConfigureLayout(timingToggleRow, -1.0f, 8.0f, 1.0f);
        fitToggle_ = BSML::Lite::CreateToggle(
            timingToggleRow,
            "Fit to Song",
            false,
            [this](bool enabled)
            {
                if(suppressTimingCallbacks_) return;
                fitToSong_ = enabled;
                if(enabled)
                {
                    if(!ApplyFitToSong(true))
                    {
                        fitToSong_ = false;
                        suppressTimingCallbacks_ = true;
                        SetToggleWithoutNotification(fitToggle_, false);
                        suppressTimingCallbacks_ = false;
                    }
                }
                else if(SaveTiming())
                {
                    transientStatus_ = "Automatic song fitting disabled; playback speed is now manual.";
                    StartSelectedPreview();
                    RefreshDetails();
                }
            });
        ConfigureLayout(fitToggle_, -1.0f, 8.0f, 1.0f);
        BSML::Lite::AddHoverHint(
            fitToggle_,
            "Continuously adjusts playback speed so the video ends with the song after applying Start Offset.");
        blackLeadInToggle_ = BSML::Lite::CreateToggle(
            timingToggleRow,
            "Black Lead-In",
            false,
            [this](bool enabled)
            {
                if(suppressTimingCallbacks_) return;
                blackDuringLeadIn_ = enabled;
                if(SaveTiming())
                {
                    transientStatus_ = enabled
                        ? "Negative offset lead-in will use a solid black screen."
                        : "Negative offset lead-in will remain transparent.";
                    StartSelectedPreview();
                    RefreshDetails();
                }
            });
        ConfigureLayout(blackLeadInToggle_, -1.0f, 8.0f, 1.0f);
        BSML::Lite::AddHoverHint(
            blackLeadInToggle_,
            "Off keeps the screen fully transparent until video time reaches zero. On shows solid black during that delay.");

        offsetSetting_ = BSML::Lite::CreateIncrementSetting(
            editorBody, "Start Offset", 2, 0.25f, 0.0f,
            -60.0f, 60.0f, {0, 0}, [this](float value) {
                if(suppressTimingCallbacks_) return;
                offset_ = value;
                if(fitToSong_)
                    ApplyFitToSong(false);
                else if(SaveTiming())
                {
                    std::ostringstream status;
                    status << std::fixed << std::setprecision(2);
                    if(offset_ < 0.0)
                        status << "Video delayed by " << -offset_ << " seconds; lead-in is "
                               << (blackDuringLeadIn_ ? "black" : "transparent") << ".";
                    else if(offset_ > 0.0)
                        status << "Video skips forward " << offset_ << " seconds at song start.";
                    else
                        status << "Video starts at frame zero with the song.";
                    transientStatus_ = status.str();
                    StartSelectedPreview();
                    RefreshDetails();
                }
            });
        ConfigureLayout(offsetSetting_, -1.0f, 8.0f, 1.0f);
        BSML::Lite::AddHoverHint(
            offsetSetting_,
            "Negative values delay video frame zero; positive values skip forward into the video.");
        rateSetting_ = BSML::Lite::CreateIncrementSetting(
            editorBody, "Playback Speed", 2, 0.05f, 1.0f,
            0.05f, 8.0f, {0, 0}, [this](float value) {
                if(suppressTimingCallbacks_) return;
                rate_ = value;
                if(SaveTiming())
                {
                    std::ostringstream status;
                    status << std::fixed << std::setprecision(2)
                           << "Manual playback speed saved at " << rate_ << "x.";
                    transientStatus_ = status.str();
                    StartSelectedPreview();
                    RefreshDetails();
                }
            });
        ConfigureLayout(rateSetting_, -1.0f, 8.0f, 1.0f);

        playbackScrubber_ = BSML::Lite::CreateSliderSetting(
            editorBody,
            "Preview Position",
            PreviewScrubIncrement,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            false,
            {0.0f, 0.0f},
            [this](float value) {
                if(!suppressScrubberCallback_)
                {
                    // SongPreviewPlayer updates every frame. Give the player's
                    // laser drag ownership of the handle briefly so that clock
                    // following cannot pull it away between pointer updates.
                    scrubberFollowResumeTime_ =
                        UnityEngine::Time::get_realtimeSinceStartup() +
                        PreviewScrubFollowDelay;
                    const double duration = selected_
                        ? std::max(0.0f, selected_->songDuration)
                        : 0.0;
                    SeekPreview(static_cast<float>(value * duration));
                }
            });
        // Explicitly cap this settings prefab to the usable right-panel width.
        // Its internal TextSlider must not report a preferred width that can
        // expand the entire editor controller.
        ConfigureLayout(playbackScrubber_, 49.0f, 8.0f, 0.0f);

        auto* playbackRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
        ConfigureGroup(playbackRow, false);
        playbackRow->set_spacing(0.6f);
        ConfigureLayout(playbackRow, -1.0f, 7.0f, 1.0f);
        playPauseButton_ = BSML::Lite::CreateUIButton(
            playbackRow,
            "Play",
            {0.0f, 0.0f},
            {17.0f, 6.5f},
            [this]() { TogglePreviewPlayback(); });
        ConfigureLayout(playPauseButton_, 17.0f, 6.5f, 0.0f);
        BSML::Lite::SetButtonTextSize(playPauseButton_, 2.8f);
        playbackTimeText_ = BSML::Lite::CreateText(
            playbackRow,
            "0:00 / 0:00",
            2.8f);
        ConfigureLayout(playbackTimeText_, 29.0f, 6.5f, 1.0f);
        playbackTimeText_->set_alignment(TMPro::TextAlignmentOptions::Center);

        removeButton_ = BSML::Lite::CreateUIButton(
            editorBody, "Remove User Video", {0.0f, 0.0f}, {31.0f, 7.0f},
            [this]() { RemoveOverride(); });
        ConfigureLayout(removeButton_, -1.0f, 7.0f, 1.0f);
        BSML::Lite::SetButtonTextSize(removeButton_, 2.8f);
        detailStorage_ = BSML::Lite::CreateText(
            editorBody, "", 2.3f);
        ConfigureLayout(detailStorage_, -1.0f, 3.0f, 1.0f);

        for(auto* text : {browserTitle_, browserStorage_, filterText_, detailTitle_, detailText_, detailStorage_, playbackTimeText_})
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
        RowVideoThumbnails.clear();
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

            UnityEngine::Sprite* videoThumbnail = nullptr;
            if(level->levelID)
            {
                RowVideoThumbnails[std::string(level->levelID)] = {
                    descriptor.CanPlay() || descriptor.CanDownload(),
                    descriptor.downloadUrl,
                    descriptor.thumbnailPath};
            }
            if((descriptor.CanPlay() || descriptor.CanDownload()) &&
               descriptor.thumbnailPath)
            {
                const auto found = VideoThumbnailSprites.find(
                    descriptor.thumbnailPath->string());
                if(found != VideoThumbnailSprites.end())
                    videoThumbnail = found->second;
            }
            list_->data->Add(BSML::CustomCellInfo::construct(
                name,
                author.empty() ? videoState : author + " | " + videoState,
                videoThumbnail));
        }
        if(browserTitle_)
            browserTitle_->set_text("Video Library");
        if(browserStorage_)
            browserStorage_->set_text(BrowserSummary(
                DistinctSongCount(visible_),
                VideoLibrary::Instance().LibraryBytes(),
                VideoLibrary::Instance().FreeBytes()));
        if(list_->tableView)
        {
            // A TableView retains its selected index even when its data is
            // rebuilt. Clear it before reloading so returning from the child
            // editor leaves every row clickable, including the song the user
            // just edited.
            list_->tableView->ClearSelection();
            list_->tableView->ReloadData();
            list_->tableView->ScrollToCellWithIdx(0, HMUI::TableView::ScrollPositionType::Beginning, false);
            RefreshVisibleVideoThumbnails();
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
        StopPreviewAudio(true);
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Stop();
        selected_ = visible_[row]->level;
        previewSongTime_ = 0.0;
        transientStatus_.clear();
        ClearThumbnail();
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        url_ = descriptor.downloadUrl.value_or("");
        offset_ = descriptor.playableConfig ? descriptor.playableConfig->offsetSeconds : 0.0;
        rate_ = descriptor.playableConfig ? descriptor.playableConfig->playbackRate : 1.0;
        fitToSong_ = descriptor.playableConfig
            ? descriptor.playableConfig->fitToSong
            : false;
        blackDuringLeadIn_ = descriptor.playableConfig
            ? descriptor.playableConfig->blackDuringLeadIn
            : false;
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
        }
        suppressTimingCallbacks_ = true;
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        SetToggleWithoutNotification(fitToggle_, fitToSong_);
        SetToggleWithoutNotification(blackLeadInToggle_, blackDuringLeadIn_);
        suppressTimingCallbacks_ = false;
        ShowEditor();
        RequestSelectedAudio();
        RefreshDetails();
        StartSelectedPreview();
        RefreshPlaybackControls();
    }

    void VideoLibraryMenu::ShowBrowser()
    {
        editorVisible_ = false;
        StopPreviewAudio(true);
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
        ClearThumbnail();
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
            url_, VideoOrigin::User, ExplicitAllowed(), offset_, rate_,
            fitToSong_, blackDuringLeadIn_};
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
        auto& library = VideoLibrary::Instance();
        const auto levelId = std::string(selected_->levelID);
        const auto thumbnailPath = library.AllocateThumbnailPath(
            levelId, VideoOrigin::User).string();
        if(!library.RemoveUserOverride(levelId, true))
        {
            transientStatus_ = "No user video was available to remove.";
            RefreshDetails();
            return;
        }

        // The row sprite outlives the file it was decoded from, so explicitly
        // evict and destroy it when its user video is removed. If the song has
        // a mapper video underneath, that separate mapper thumbnail will be
        // selected the next time the browser is rebuilt.
        EvictVideoThumbnail(thumbnailPath);

        // The thumbnail sprite is UI-owned and independent of the downloaded
        // MP4. Delete the probe image associated with this song as well, then
        // release the Unity texture and restore the placeholder. Removing the
        // file also prevents the periodic refresh from recreating the sprite
        // from a stale completed-probe snapshot.
        const auto download = DownloadManager::Instance().Snapshot();
        if(download.levelId == std::string(selected_->levelID) &&
           !download.thumbnailPath.empty())
        {
            std::error_code thumbnailError;
            std::filesystem::remove(download.thumbnailPath, thumbnailError);
            if(thumbnailError)
            {
                PaperLogger.warn(
                    "Could not delete removed video's thumbnail '{}': {}",
                    download.thumbnailPath,
                    thumbnailError.message());
            }
        }
        ClearThumbnail();
        transientStatus_ = "User video removed.";
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        url_ = descriptor.downloadUrl.value_or("");
        offset_ = descriptor.playableConfig
            ? descriptor.playableConfig->offsetSeconds
            : 0.0;
        rate_ = descriptor.playableConfig
            ? descriptor.playableConfig->playbackRate
            : 1.0;
        fitToSong_ = descriptor.playableConfig
            ? descriptor.playableConfig->fitToSong
            : false;
        blackDuringLeadIn_ = descriptor.playableConfig
            ? descriptor.playableConfig->blackDuringLeadIn
            : false;
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
        }
        suppressTimingCallbacks_ = true;
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        SetToggleWithoutNotification(fitToggle_, fitToSong_);
        SetToggleWithoutNotification(blackLeadInToggle_, blackDuringLeadIn_);
        suppressTimingCallbacks_ = false;
        RefreshDetails();
        StartSelectedPreview();
    }

    void VideoLibraryMenu::ClearThumbnail()
    {
        loadedThumbnailPath_.clear();
        if(loadedThumbnailSprite_)
        {
            UnityEngine::Object::Destroy(loadedThumbnailSprite_);
            loadedThumbnailSprite_ = nullptr;
        }
        if(urlThumbnail_)
        {
            // Keep the reserved layout space stable so Paste and Download do
            // not jump sideways merely because a thumbnail was cleared.
            urlThumbnail_->set_sprite(
                BSML::Utilities::ImageResources::GetBlankSprite());
            urlThumbnail_->set_color({0.08f, 0.10f, 0.13f, 0.85f});
        }
    }

    void VideoLibraryMenu::RefreshVisibleVideoThumbnails()
    {
        if(!list_ || !list_->tableView || !list_->data)
            return;

        auto* cells = list_->tableView->__cordl_internal_get__visibleCells();
        if(!cells) return;

        for(int cellIndex = 0; cellIndex < cells->get_Count(); ++cellIndex)
        {
            auto cell = cells->get_Item(cellIndex);
            auto levelCell = cell
                .try_cast<GlobalNamespace::LevelListTableCell>()
                .value_or(nullptr);
            if(!levelCell) continue;

            const int row = levelCell->get_idx();
            if(row < 0 || row >= static_cast<int>(visible_.size()) ||
               row >= list_->data->get_Count())
                continue;

            auto* item = visible_[row];
            auto* level = item ? item->level : nullptr;
            if(!level || !level->levelID) continue;
            const std::string levelId(level->levelID);
            const auto metadata = RowVideoThumbnails.find(levelId);
            const bool hasVideo = metadata != RowVideoThumbnails.end() &&
                metadata->second.hasVideo;
            auto coverImage = levelCell->__cordl_internal_get__coverImage();

            // Reused Beat Saber cells may still contain the icon from a prior
            // row. Hide that image object completely for songs without video,
            // which also guarantees album art can never leak into this list.
            if(coverImage)
            {
                coverImage->get_gameObject()->SetActive(hasVideo);
                coverImage->set_preserveAspect(true);
                auto coverRect = coverImage->get_rectTransform();
                coverRect->set_anchorMin({1.0f, 0.5f});
                coverRect->set_anchorMax({1.0f, 0.5f});
                coverRect->set_pivot({1.0f, 0.5f});
                coverRect->set_anchoredPosition({-1.0f, 0.0f});
                coverRect->set_sizeDelta({7.2f, 7.2f});
                coverImage->set_sprite(
                    BSML::Utilities::ImageResources::GetBlankSprite());
                coverImage->set_color({1.0f, 1.0f, 1.0f, 0.0f});
            }
            if(auto nameText = levelCell->__cordl_internal_get__songNameText())
            {
                auto rect = nameText->get_rectTransform();
                rect->set_anchorMin({0.0f, 0.5f});
                rect->set_anchorMax({1.0f, 0.5f});
                rect->set_pivot({0.0f, 0.5f});
                rect->set_anchoredPosition({1.2f, 1.7f});
                rect->set_sizeDelta({hasVideo ? -11.0f : -2.0f, 3.5f});
            }
            if(auto authorText = levelCell->__cordl_internal_get__songAuthorText())
            {
                auto rect = authorText->get_rectTransform();
                rect->set_anchorMin({0.0f, 0.5f});
                rect->set_anchorMax({1.0f, 0.5f});
                rect->set_pivot({0.0f, 0.5f});
                rect->set_anchoredPosition({1.2f, -1.8f});
                rect->set_sizeDelta({hasVideo ? -11.0f : -2.0f, 3.0f});
            }

            auto* cellInfo = list_->data[row];
            if(!hasVideo || !metadata->second.path)
            {
                cellInfo->icon = nullptr;
                continue;
            }

            const std::string path = metadata->second.path->string();
            UnityEngine::Sprite* sprite = nullptr;
            if(const auto loaded = VideoThumbnailSprites.find(path);
               loaded != VideoThumbnailSprites.end())
            {
                sprite = loaded->second;
            }
            else if(std::filesystem::is_regular_file(*metadata->second.path) &&
                    !FailedVideoThumbnailLoads.contains(path))
            {
                try
                {
                    sprite = BSML::Lite::FileToSprite(path);
                    if(sprite)
                        VideoThumbnailSprites.emplace(path, sprite);
                    else
                        FailedVideoThumbnailLoads.emplace(path);
                }
                catch(const std::exception& error)
                {
                    FailedVideoThumbnailLoads.emplace(path);
                    PaperLogger.warn(
                        "Could not display video thumbnail '{}': {}",
                        path,
                        error.what());
                }
            }
            else if(metadata->second.sourceUrl &&
                    Settings::Instance().ModEnabled())
            {
                DownloadManager::Instance().QueueVideoThumbnail(
                    levelId,
                    *metadata->second.sourceUrl,
                    *metadata->second.path);
            }

            cellInfo->icon = sprite;
            if(sprite && coverImage)
            {
                coverImage->set_sprite(sprite);
                coverImage->set_color(UnityEngine::Color::get_white());
            }
        }
    }

    bool VideoLibraryMenu::SaveTiming()
    {
        if(!selected_ || !selected_->levelID)
            return false;
        return VideoLibrary::Instance().UpdateTiming(
            std::string(selected_->levelID),
            SelectedVideoOrigin(),
            offset_,
            rate_,
            fitToSong_,
            blackDuringLeadIn_);
    }

    bool VideoLibraryMenu::ApplyFitToSong(bool reportStatus)
    {
        if(!selected_)
        {
            transientStatus_ = "Select a song before enabling Fit to Song.";
            RefreshDetails();
            return false;
        }
        if(selected_->songDuration <= 0.0f)
        {
            transientStatus_ = "Song duration is unavailable, so Fit to Song could not run.";
            RefreshDetails();
            return false;
        }
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        if(!descriptor.playableConfig || descriptor.playableConfig->declaredDurationSeconds <= 0.0)
        {
            transientStatus_ = "Video duration is unavailable, so Fit to Song could not run.";
            RefreshDetails();
            return false;
        }

        // Media time is songTime * rate + offset. Solving that expression for
        // the rate that reaches the last video frame at the last song frame
        // gives (videoDuration - offset) / songDuration. A -2 second offset
        // therefore adds two seconds of lead-in to the fitted timeline exactly
        // as the user expects, while preserving the common end point.
        const auto requestedRate =
            (descriptor.playableConfig->declaredDurationSeconds - offset_) /
            selected_->songDuration;
        rate_ = std::clamp(requestedRate, 0.05, 8.0);
        if(!SaveTiming())
        {
            transientStatus_ = "Fit to Song could not save the new playback speed.";
            RefreshDetails();
            return false;
        }
        suppressTimingCallbacks_ = true;
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        SetToggleWithoutNotification(fitToggle_, fitToSong_);
        suppressTimingCallbacks_ = false;
        StartSelectedPreview();

        std::ostringstream status;
        status << std::fixed << std::setprecision(2)
               << (reportStatus ? "Automatic fit enabled: " : "Automatic fit updated: ")
               << "playback speed " << rate_
               << "x with " << offset_ << "s offset";
        if(std::abs(requestedRate - rate_) > 0.0001)
            status << " (limited from " << requestedRate << "x)";
        status << ".";
        transientStatus_ = status.str();
        RefreshDetails();
        return true;
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
        // A replacement can reuse the same deterministic file name. Evict the
        // prior decoded sprite once the new download publishes its thumbnail,
        // otherwise Unity would continue showing the old video's pixels even
        // though the JPEG on disk has changed.
        if(thisDownload && download.state == DownloadState::Completed &&
           !download.thumbnailPath.empty())
        {
            const auto completedIdentity =
                download.levelId + "|" + download.thumbnailPath + "|" + download.title;
            if(completedVideoThumbnailIdentity_ != completedIdentity)
            {
                EvictVideoThumbnail(download.thumbnailPath);
                completedVideoThumbnailIdentity_ = completedIdentity;
            }
        }

        // Prefer a just-probed thumbnail while the user is choosing a URL;
        // otherwise show the durable thumbnail belonging to the active video.
        // This gives the child editor the same video identity as the browser
        // after restarting the game, without ever falling back to album art.
        std::string detailThumbnailPath;
        std::string detailThumbnailIdentity;
        if(thisDownload && download.state == DownloadState::ProbeCompleted &&
           !download.thumbnailPath.empty())
        {
            detailThumbnailPath = download.thumbnailPath;
            detailThumbnailIdentity = download.thumbnailPath + "|" + download.title;
        }
        else if(descriptor.thumbnailPath &&
                std::filesystem::is_regular_file(*descriptor.thumbnailPath))
        {
            detailThumbnailPath = descriptor.thumbnailPath->string();
            detailThumbnailIdentity = detailThumbnailPath;
        }
        if(urlThumbnail_ && !detailThumbnailPath.empty() &&
           loadedThumbnailPath_ != detailThumbnailIdentity)
        {
            try
            {
                auto* sprite = BSML::Lite::FileToSprite(detailThumbnailPath);
                if(sprite)
                {
                    if(loadedThumbnailSprite_)
                        UnityEngine::Object::Destroy(loadedThumbnailSprite_);
                    loadedThumbnailSprite_ = sprite;
                    loadedThumbnailPath_ = detailThumbnailIdentity;
                    urlThumbnail_->set_sprite(sprite);
                    urlThumbnail_->set_color(UnityEngine::Color::get_white());
                }
            }
            catch(const std::exception& error)
            {
                PaperLogger.warn(
                    "Could not display the video thumbnail: {}",
                    error.what());
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
        if(rateSetting_) rateSetting_->set_interactable(
            descriptor.CanPlay() && !fitToSong_);
        if(fitToggle_) fitToggle_->set_interactable(descriptor.CanPlay());
        if(blackLeadInToggle_) blackLeadInToggle_->set_interactable(descriptor.CanPlay());
        if(playbackScrubber_) playbackScrubber_->set_interactable(descriptor.CanPlay());
        if(playPauseButton_) playPauseButton_->set_interactable(descriptor.CanPlay());
        if(removeButton_) removeButton_->set_interactable(descriptor.hasUserOverride);
        if(thisDownload && download.state == DownloadState::Completed)
        {
            const auto completedIdentity =
                download.levelId + "|" + download.title + "|" +
                std::to_string(download.totalBytes);
            if(autoPlayedDownloadIdentity_ != completedIdentity)
            {
                autoPlayedDownloadIdentity_ = completedIdentity;
                previewSongTime_ = 0.0;
                playWhenAudioReady_ = true;
                RequestSelectedAudio();
                StartSelectedPreview();
                if(previewAudioClip_)
                    StartPreviewAudio();
            }
        }
        RefreshPlaybackControls();
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
        if(playback.IsLibraryPreviewActive())
            playback.Tick(previewSongTime_);
    }

    void VideoLibraryMenu::RequestSelectedAudio()
    {
        if(!selected_ || !selected_->levelID)
            return;
        const std::string levelId(selected_->levelID);
        if(previewAudioClip_ && audioLoadLevelId_ == levelId)
            return;
        if(audioLoadTask_ && audioLoadLevelId_ == levelId)
            return;

        previewAudioClip_ = nullptr;
        previewAudioSource_ = nullptr;
        previewMediaData_ = selected_->__cordl_internal_get_previewMediaData();
        audioLoadLevelId_ = levelId;
        if(!previewMediaData_)
        {
            transientStatus_ = "This song does not expose preview audio.";
            return;
        }

        audioLoadTask_ = previewMediaData_->GetPreviewAudioClip(
            System::Threading::CancellationToken::get_None());
        if(!audioLoadTask_)
            transientStatus_ = "Beat Saber could not start loading this song's audio.";
    }

    void VideoLibraryMenu::TogglePreviewPlayback()
    {
        if(!selected_ || !VideoLibrary::Instance().Describe(selected_).CanPlay())
            return;

        if(previewPlaying_)
        {
            if(songPreviewPlayer_ && previewAudioClip_ &&
               songPreviewPlayer_->get_activeAudioClip().ptr() == previewAudioClip_)
                songPreviewPlayer_->PauseCurrentChannel();
            previewPlaying_ = false;
            playWhenAudioReady_ = false;
            RefreshPlaybackControls();
            return;
        }

        const double duration = std::max(0.0f, selected_->songDuration);
        if(duration > 0.0 && previewSongTime_ >= duration - 0.01)
            previewSongTime_ = 0.0;

        RequestSelectedAudio();
        if(!previewAudioClip_ || !songPreviewPlayer_)
        {
            playWhenAudioReady_ = true;
            transientStatus_ = "Loading song audio for synchronized preview...";
            RefreshDetails();
            return;
        }

        // Resume the paused Beat Saber channel when it is still ours. If the
        // menu music or another preview reclaimed the channel, rebuild the
        // crossfade at the requested scrub position instead.
        if(previewAudioSource_ &&
           previewAudioSource_->get_clip().ptr() == previewAudioClip_ &&
           songPreviewPlayer_->get_activeAudioClip().ptr() == previewAudioClip_)
        {
            previewAudioSource_->set_time(static_cast<float>(previewSongTime_));
            songPreviewPlayer_->UnPauseCurrentChannel();
            previewPlaying_ = true;
            playWhenAudioReady_ = false;
            StartSelectedPreview();
            RefreshPlaybackControls();
            return;
        }
        StartPreviewAudio();
    }

    void VideoLibraryMenu::StartPreviewAudio()
    {
        if(!selected_ || !previewAudioClip_ || !songPreviewPlayer_)
        {
            playWhenAudioReady_ = true;
            return;
        }

        const double songDuration = std::max(0.0f, selected_->songDuration);
        const double clipDuration = std::max(0.0f, previewAudioClip_->get_length());
        const double availableDuration = songDuration > 0.0
            ? std::min(songDuration, clipDuration)
            : clipDuration;
        previewSongTime_ = std::clamp(
            previewSongTime_,
            0.0,
            std::max(0.0, availableDuration - 0.01));

        // Use Beat Saber's perceived-loudness model and SongPreviewPlayer so
        // this audition follows the user's music-volume/mixer settings rather
        // than creating an unregulated AudioSource of our own.
        float musicVolume = 1.0f;
        try
        {
            auto* container = BSML::Helpers::GetDiContainer();
            auto* loudness = container
                ? container->Resolve<GlobalNamespace::PerceivedLoudnessPerLevelModel*>()
                : nullptr;
            if(loudness)
                musicVolume = GlobalNamespace::AudioHelpers::DBToNormalizedVolume(
                    loudness->GetLoudnessCorrectionByLevelId(selected_->levelID));
        }
        catch(const std::exception& error)
        {
            PaperLogger.warn(
                "Could not resolve Beat Saber's preview loudness for '{}': {}",
                std::string(selected_->levelID),
                error.what());
        }

        songPreviewPlayer_->CrossfadeTo(
            previewAudioClip_,
            musicVolume,
            static_cast<float>(previewSongTime_),
            static_cast<float>(std::max(0.1, availableDuration - previewSongTime_)),
            nullptr);
        previewAudioSource_ = ActiveSongAudioSource(songPreviewPlayer_);
        if(previewAudioSource_)
            previewAudioSource_->set_time(static_cast<float>(previewSongTime_));
        previewPlaying_ = true;
        playWhenAudioReady_ = false;
        transientStatus_.clear();
        StartSelectedPreview();
        RefreshPlaybackControls();
    }

    void VideoLibraryMenu::StopPreviewAudio(bool returnToMenuMusic)
    {
        playWhenAudioReady_ = false;
        previewPlaying_ = false;
        if(returnToMenuMusic && songPreviewPlayer_ && previewAudioClip_ &&
           songPreviewPlayer_->get_activeAudioClip().ptr() == previewAudioClip_)
            songPreviewPlayer_->CrossfadeToDefault();
        previewAudioSource_ = nullptr;
        previewAudioClip_ = nullptr;
        audioLoadTask_ = nullptr;
        previewMediaData_ = nullptr;
        audioLoadLevelId_.clear();
        RefreshPlaybackControls();
    }

    void VideoLibraryMenu::SeekPreview(float songTimeSeconds)
    {
        if(!selected_ || !VideoLibrary::Instance().Describe(selected_).CanPlay())
            return;
        const double duration = std::max(0.0f, selected_->songDuration);
        previewSongTime_ = std::clamp(
            static_cast<double>(songTimeSeconds),
            0.0,
            duration);

        if(previewAudioSource_ && previewAudioClip_ &&
           previewAudioSource_->get_clip().ptr() == previewAudioClip_)
        {
            const double clipEnd = std::max(
                0.0f,
                previewAudioClip_->get_length() - 0.01f);
            previewAudioSource_->set_time(static_cast<float>(
                std::min(previewSongTime_, clipEnd)));
        }
        else if(previewPlaying_)
        {
            StartPreviewAudio();
        }

        auto& playback = PlaybackSession::Instance();
        if(!playback.IsLibraryPreviewActive())
            StartSelectedPreview();
        else
            playback.Tick(previewSongTime_);
        RefreshPlaybackControls();
    }

    void VideoLibraryMenu::RefreshPlaybackControls()
    {
        const double duration = selected_
            ? std::max(0.0f, selected_->songDuration)
            : 0.0;
        if(playbackTimeText_)
            playbackTimeText_->set_text(
                PlaybackTime(previewSongTime_) + " / " + PlaybackTime(duration));
        if(playPauseButton_)
            BSML::Lite::SetButtonText(
                playPauseButton_,
                playWhenAudioReady_ ? "Loading..." :
                    previewPlaying_ ? "Pause" : "Play");
        if(playbackScrubber_)
        {
            const double normalizedPosition = duration > 0.0
                ? std::clamp(previewSongTime_ / duration, 0.0, 1.0)
                : 0.0;
            const bool userStillScrubbing =
                UnityEngine::Time::get_realtimeSinceStartup() <
                scrubberFollowResumeTime_;
            if(!userStillScrubbing &&
               std::abs(playbackScrubber_->get_Value() - normalizedPosition) >
                   PreviewScrubIncrement * 0.4)
            {
                suppressScrubberCallback_ = true;
                playbackScrubber_->set_Value(static_cast<float>(normalizedPosition));
                suppressScrubberCallback_ = false;
            }
        }
    }

    void VideoLibraryMenu::Tick(
        GlobalNamespace::SongPreviewPlayer* songPreviewPlayer)
    {
        if(!active_) return;
        songPreviewPlayer_ = songPreviewPlayer;
        if(!editorVisible_)
            RefreshVisibleVideoThumbnails();

        if(editorVisible_ && audioLoadTask_ && audioLoadTask_->get_IsCompleted())
        {
            auto* completedTask = audioLoadTask_;
            audioLoadTask_ = nullptr;
            if(completedTask->get_IsCompletedSuccessfully() && selected_ &&
               selected_->levelID &&
               audioLoadLevelId_ == std::string(selected_->levelID))
            {
                auto clip = completedTask->get_Result();
                previewAudioClip_ = clip ? clip.ptr() : nullptr;
                if(!previewAudioClip_)
                    transientStatus_ = "Beat Saber returned no audio for this song.";
                else if(playWhenAudioReady_)
                    StartPreviewAudio();
            }
            else
            {
                transientStatus_ = "Beat Saber could not load this song's audio.";
                playWhenAudioReady_ = false;
            }
            RefreshDetails();
        }

        if(editorVisible_ && previewPlaying_ && previewAudioClip_)
        {
            if(!previewAudioSource_ ||
               previewAudioSource_->get_clip().ptr() != previewAudioClip_)
                previewAudioSource_ = ActiveSongAudioSource(songPreviewPlayer_);

            if(previewAudioSource_ &&
               previewAudioSource_->get_clip().ptr() == previewAudioClip_ &&
               previewAudioSource_->get_isPlaying())
            {
                previewSongTime_ = previewAudioSource_->get_time();
                if(PlaybackSession::Instance().IsLibraryPreviewActive())
                    PlaybackSession::Instance().Tick(previewSongTime_);
            }
            else
            {
                previewPlaying_ = false;
                playWhenAudioReady_ = false;
            }
            RefreshPlaybackControls();
        }
        if(++tickCounter_ >= 30)
        {
            tickCounter_ = 0;
            if(editorVisible_) RefreshDetails();
        }
    }

    void VideoLibraryMenu::Refresh()
    {
        if(!browserController_ || !editorController_) return;
        active_ = true;
        RebuildCatalog();
        if(editorVisible_) RefreshDetails();
        if(!Settings::Instance().ModEnabled())
        {
            DownloadManager::Instance().Cancel();
            StopPreviewAudio(true);
            if(PlaybackSession::Instance().IsLibraryPreviewActive())
                PlaybackSession::Instance().Stop();
        }
    }

    void VideoLibraryMenu::Deactivate()
    {
        active_ = false;
        editorVisible_ = false;
        StopPreviewAudio(true);
        DownloadManager::Instance().Cancel();
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Stop();
    }
}
