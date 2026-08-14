#include "BigScreen/VideoLibraryMenu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/ErrorManager.hpp"
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
#include "HMUI/ScrollView.hpp"
#include "HMUI/TableCell.hpp"
#include "HMUI/TableView.hpp"
#include "System/Threading/CancellationToken.hpp"
#include "System/Threading/Tasks/Task_1.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/AndroidJavaClass.hpp"
#include "UnityEngine/AndroidJavaObject.hpp"
#include "UnityEngine/AudioClip.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/GUIUtility.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ColorBlock.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/Graphic.hpp"
#include "UnityEngine/UI/GridLayoutGroup.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Lists.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/ClickableText.hpp"
#include "bsml/shared/BSML/Components/CustomListTableData.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "bsml/shared/Helpers/delegates.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils-exceptions.hpp"
#include "main.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

namespace BigScreen {
    namespace {
        constexpr std::array<std::string_view, 6> FilterNames{
            "Show All Maps", "Custom Maps", "WIP Maps",
            "OST Maps", "DLC Maps", "Maps With Video"
        };
        // Keep the UI slider normalized and translate its position to song
        // time. One thousand positions are comfortably finer than a controller
        // can place the handle in VR and work for songs of any duration.
        constexpr float PreviewScrubIncrement = 0.001f;
        constexpr float PreviewScrubFollowDelay = 0.25f;

        // These sprites are created only from the configured video's cached
        // YouTube thumbnail. Beat Saber's song/album cover is deliberately not
        // consulted: a blank row therefore means that song has no video.
        struct CachedVideoThumbnail {
            UnityEngine::Sprite* sprite = nullptr;
            std::uint64_t lastUse = 0;
        };
        constexpr std::size_t MaximumCachedVideoThumbnails = 64;
        std::uint64_t VideoThumbnailUseCounter = 0;
        std::unordered_map<std::string, CachedVideoThumbnail> VideoThumbnailSprites;
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
                if(cached->second.sprite)
                    UnityEngine::Object::Destroy(cached->second.sprite);
                VideoThumbnailSprites.erase(cached);
            }
            FailedVideoThumbnailLoads.erase(path);
        }

        UnityEngine::Sprite* FindCachedVideoThumbnail(const std::string& path)
        {
            const auto found = VideoThumbnailSprites.find(path);
            if(found == VideoThumbnailSprites.end())
                return nullptr;
            found->second.lastUse = ++VideoThumbnailUseCounter;
            return found->second.sprite;
        }

        void CacheVideoThumbnail(
            const std::string& path,
            UnityEngine::Sprite* sprite)
        {
            if(!sprite)
                return;
            if(VideoThumbnailSprites.size() >= MaximumCachedVideoThumbnails)
            {
                const auto oldest = std::min_element(
                    VideoThumbnailSprites.begin(),
                    VideoThumbnailSprites.end(),
                    [](const auto& left, const auto& right)
                    {
                        return left.second.lastUse < right.second.lastUse;
                    });
                if(oldest != VideoThumbnailSprites.end())
                {
                    if(oldest->second.sprite)
                        UnityEngine::Object::Destroy(oldest->second.sprite);
                    FailedVideoThumbnailLoads.erase(oldest->first);
                    VideoThumbnailSprites.erase(oldest);
                }
            }
            VideoThumbnailSprites.insert_or_assign(
                path,
                CachedVideoThumbnail{sprite, ++VideoThumbnailUseCounter});
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
            if(!UnityW<GlobalNamespace::SongPreviewPlayer>::isAlive(player))
                return nullptr;
            const int channel = player->__cordl_internal_get__activeChannel();
            auto controllers = player->__cordl_internal_get__audioSourceControllers();
            if(!controllers || channel < 0 ||
               channel >= static_cast<int>(controllers.size()))
                return nullptr;
            auto* controller = controllers[channel];
            if(!controller)
                return nullptr;
            auto source = controller->__cordl_internal_get_audioSource();
            return source ? source.unsafePtr() : nullptr;
        }

        bool ActiveSongClipMatches(
            GlobalNamespace::SongPreviewPlayer* player,
            UnityEngine::AudioClip* expected)
        {
            if(!UnityW<GlobalNamespace::SongPreviewPlayer>::isAlive(player) ||
               !UnityW<UnityEngine::AudioClip>::isAlive(expected))
                return false;
            const auto active = player->get_activeAudioClip();
            return active && active.unsafePtr() == expected;
        }

        bool IsAlive(UnityW<UnityEngine::AudioClip> clip)
        {
            return UnityW<UnityEngine::AudioClip>::isAlive(clip.unsafePtr());
        }

        bool IsAlive(UnityW<UnityEngine::AudioSource> source)
        {
            return UnityW<UnityEngine::AudioSource>::isAlive(source.unsafePtr());
        }

        bool IsAlive(UnityW<GlobalNamespace::SongPreviewPlayer> player)
        {
            return UnityW<GlobalNamespace::SongPreviewPlayer>::isAlive(
                player.unsafePtr());
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

        std::string EncodeUrlQuery(const std::string& value)
        {
            // Encode UTF-8 bytes directly. YouTube accepts percent-encoded
            // UTF-8, so titles and artist names outside ASCII remain intact.
            constexpr char Hex[] = "0123456789ABCDEF";
            std::string encoded;
            encoded.reserve(value.size() * 3);
            for(const unsigned char character : value)
            {
                if(std::isalnum(character) || character == '-' ||
                   character == '_' || character == '.' || character == '~')
                {
                    encoded.push_back(static_cast<char>(character));
                }
                else if(character == ' ')
                {
                    encoded.push_back('+');
                }
                else
                {
                    encoded.push_back('%');
                    encoded.push_back(Hex[character >> 4]);
                    encoded.push_back(Hex[character & 0x0F]);
                }
            }
            return encoded;
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

        std::string StorageSize(std::uint64_t bytes)
        {
            std::ostringstream text;
            text << std::fixed << std::setprecision(1);
            if(bytes < 1073741824ULL)
                text << bytes / 1048576.0 << " MB";
            else
                text << bytes / 1073741824.0 << " GB";
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

        void StyleToggleRow(BSML::ToggleSetting* setting)
        {
            if(!setting || !setting->toggle) return;
            auto* background = BSML::Lite::CreateImage(
                setting->get_transform(),
                BSML::Utilities::ImageResources::GetBlankSprite());
            background->set_color(UnityEngine::Color::get_white());
            background->set_preserveAspect(false);
            background->set_raycastTarget(false);
            if(auto rect = background->get_transform().cast<UnityEngine::RectTransform>())
            {
                rect->set_anchorMin({0.0f, 0.0f});
                rect->set_anchorMax({1.0f, 1.0f});
                rect->set_pivot({0.5f, 0.5f});
                rect->set_anchoredPosition({0.0f, 0.0f});
                rect->set_sizeDelta({-0.4f, -0.35f});
                rect->SetAsFirstSibling();
            }

            // Reuse Toggle's normal Selectable transition so hovering the
            // switch brightens the complete row just like Beat Saber's native
            // settings. AnimatedSwitchView continues to render the on/off
            // state above this non-interactive background.
            setting->toggle->set_targetGraphic(background);
            setting->toggle->set_transition(
                UnityEngine::UI::Selectable::Transition::ColorTint);
            auto colors = setting->toggle->get_colors();
            colors.set_normalColor({0.18f, 0.20f, 0.23f, 0.92f});
            colors.set_highlightedColor({0.38f, 0.41f, 0.46f, 0.96f});
            colors.set_pressedColor({0.48f, 0.52f, 0.58f, 1.0f});
            colors.set_selectedColor({0.30f, 0.33f, 0.38f, 0.96f});
            colors.set_disabledColor({0.12f, 0.13f, 0.15f, 0.55f});
            setting->toggle->set_colors(colors);
        }

        template<class TLayout>
        void ConfigureGroup(TLayout* group)
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

    void VideoLibraryMenu::ForgetUi()
    {
        // The old hierarchy may already be gone, so do not dereference or
        // Destroy its cached objects here. Clear native ownership wholesale;
        // the replacement flow will rebuild every control and media reference.
        for(auto& [identity, cached] : VideoThumbnailSprites)
        {
            (void)identity;
            try
            {
                if(UnityW<UnityEngine::Sprite>::isAlive(cached.sprite))
                    UnityEngine::Object::Destroy(cached.sprite);
            }
            catch(...)
            {
                // The old scene may already have destroyed this sprite.
            }
        }
        VideoThumbnailSprites.clear();
        VideoThumbnailUseCounter = 0;
        *this = VideoLibraryMenu{};
    }

    void VideoLibraryMenu::CreateUi(
        HMUI::ViewController* browserController,
        HMUI::ViewController* editorController,
        std::function<void(bool showEditor)> navigate,
        std::function<void(GlobalNamespace::BeatmapLevel*)> browseLocalVideo)
    {
        if(!browserController || !editorController) return;
        browserController_ = browserController;
        editorController_ = editorController;
        navigate_ = std::move(navigate);
        browseLocalVideo_ = std::move(browseLocalVideo);
        videoOnlyRows_.clear();

        // Qounters-style pages use one layout-owned content tree per view
        // controller. Keeping browser and editor controls on separate
        // controllers removes the hidden hit targets and partial-page remnants
        // caused by the earlier absolute-position overlay.
        auto* browserRoot = BSML::Lite::CreateVerticalLayoutGroup(browserController);
        ConfigureGroup(browserRoot);
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
        ConfigureGroup(filterRow);
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
        ConfigureGroup(listRow);
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
            ErrorManager::Instance().RecordError(
                "Creating the Video Library song list",
                "Beat Saber did not create the native list control");
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
            if(list_->tableView)
            {
                // The browser controller is disabled while its child editor
                // occupies the right panel. TableView normally scrolls to row
                // zero when it is enabled again, which conflicts with the
                // position retained by an animated A-Z jump and can make a
                // recycled cell display a different row from its click index.
                // Search and filter changes already reset the list explicitly,
                // so controller activation should preserve the current row.
                list_->tableView->__cordl_internal_set__scrollToTopOnEnable(false);
            }
            if(listObject && list_->tableView)
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
        ConfigureGroup(editorRoot);
        editorRoot->set_childForceExpandWidth(true);
        // CreateVerticalLayoutGroup installs a ContentSizeFitter that normally
        // derives the container width from whichever children are currently
        // active. Video-only rows therefore made the editor wider when shown
        // and narrower when hidden. Disable horizontal fitting and let the
        // stretched RectTransform inherit the real side-panel width instead.
        if(auto* fitter = editorRoot->get_gameObject()
               ->GetComponent<UnityEngine::UI::ContentSizeFitter*>())
            fitter->set_horizontalFit(
                UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
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

        // Keep the proven single TMPro title object: Beat Saber's side-panel
        // layout can collapse adjacent standalone text rows even while their
        // LayoutElements retain space. Explicit padding and reduced line
        // spacing keep both song and artist visible without either line
        // crossing the Back divider or the first local-file row.
        auto* titleTopSpacer = BSML::Lite::CreateText(editorBody, "", 1.0f);
        ConfigureLayout(titleTopSpacer, -1.0f, 0.5f, 1.0f);
        auto* titleActionRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
        ConfigureGroup(titleActionRow);
        titleActionRow->set_spacing(0.7f);
        ConfigureLayout(titleActionRow, 54.0f, 7.5f, 1.0f);
        if(auto* rowLayout = EnsureLayout(titleActionRow))
        {
            rowLayout->set_minWidth(54.0f);
            rowLayout->set_minHeight(7.5f);
        }

        // The combined row spans both the song and artist lines. The title
        // consumes all space not reserved for the fixed search action and
        // auto-sizes before masking, preserving two readable lines without
        // drawing beneath the button.
        detailTitle_ = BSML::Lite::CreateText(
            titleActionRow, "", 3.3f);
        ConfigureLayout(detailTitle_, 0.0f, 7.5f, 1.0f);
        detailTitle_->set_enableWordWrapping(false);
        detailTitle_->set_enableAutoSizing(true);
        detailTitle_->set_fontSizeMin(2.2f);
        detailTitle_->set_fontSizeMax(3.3f);
        detailTitle_->set_overflowMode(TMPro::TextOverflowModes::Masking);
        detailTitle_->set_maxVisibleLines(2);
        detailTitle_->set_lineSpacing(-16.0f);

        searchYouTubeButton_ = BSML::Lite::CreateUIButton(
            titleActionRow,
            "Search YouTube",
            {0.0f, 0.0f},
            {16.5f, 7.4f},
            [this]() { SearchSelectedSongOnYouTube(); });
        ConfigureLayout(searchYouTubeButton_, 16.5f, 7.4f, 0.0f);
        // It is created after the title for straightforward pointer setup;
        // reorder it inside the horizontal group so the action occupies the
        // left edge and the song/artist block receives the remaining width.
        searchYouTubeButton_->get_transform()->SetAsFirstSibling();
        BSML::Lite::SetButtonTextSize(searchYouTubeButton_, 2.2f);
        if(auto* searchText = searchYouTubeButton_->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
        {
            searchText->set_enableWordWrapping(false);
            searchText->set_enableAutoSizing(true);
            searchText->set_fontSizeMin(1.7f);
            searchText->set_fontSizeMax(2.2f);
            searchText->set_overflowMode(TMPro::TextOverflowModes::Masking);
            searchText->set_maxVisibleLines(1);
        }
        BSML::Lite::AddHoverHint(
            searchYouTubeButton_,
            "Opens YouTube in the Quest browser and searches for this song and artist.");
        auto* titleBottomSpacer = BSML::Lite::CreateText(editorBody, "", 1.0f);
        ConfigureLayout(titleBottomSpacer, -1.0f, 0.7f, 1.0f);

        // Keep local-video management to one compact row in the child editor.
        // Browsing is intentionally moved to the wide center screen so long
        // filenames and folder navigation do not resize or crowd this panel.
        auto* localRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
        ConfigureGroup(localRow);
        localRow->set_spacing(0.7f);
        ConfigureLayout(localRow, 54.0f, 8.0f, 1.0f);
        localVideoListContent_ = localRow->get_gameObject();
        showFileBrowserButton_ = BSML::Lite::CreateUIButton(
            localRow,
            "Show File Browser",
            {0.0f, 0.0f},
            {20.0f, 7.5f},
            [this]()
            {
                if(selected_ && browseLocalVideo_)
                    browseLocalVideo_(selected_);
            });
        ConfigureLayout(showFileBrowserButton_, 20.0f, 7.5f, 0.0f);
        BSML::Lite::SetButtonTextSize(showFileBrowserButton_, 2.25f);
        BSML::Lite::AddHoverHint(
            showFileBrowserButton_,
            "Opens the Quest file browser at this map's folder. Built-in songs start in Big Screen's Video Import folder. You can navigate anywhere in shared storage and assign a compatible H.264 MP4.");
        localVideoStatusText_ = BSML::Lite::CreateText(
            localRow, "", 2.45f);
        ConfigureLayout(localVideoStatusText_, 0.0f, 7.5f, 1.0f);
        localVideoStatusText_->set_alignment(
            TMPro::TextAlignmentOptions::MidlineLeft);
        localVideoStatusText_->set_enableWordWrapping(false);
        localVideoStatusText_->set_overflowMode(
            TMPro::TextOverflowModes::Ellipsis);
        localVideoStatusText_->get_gameObject()->SetActive(false);

        localVideoHelpModal_ = BSML::Lite::CreateModal(
            editorController,
            {64.0f, 34.0f},
            nullptr,
            true);
        localVideoHelpText_ = BSML::Lite::CreateText(
            localVideoHelpModal_,
            "",
            TMPro::FontStyles::Normal,
            {0.0f, 4.0f});
        localVideoHelpText_->set_fontSize(2.8f);
        localVideoHelpText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        auto* closeLocalHelp = BSML::Lite::CreateUIButton(
            localVideoHelpModal_->get_transform(),
            "Close",
            {32.0f, -27.0f},
            {22.0f, 7.0f},
            [this]()
            {
                if(localVideoHelpModal_)
                    localVideoHelpModal_->Hide();
            });
        ConfigureLayout(closeLocalHelp, 22.0f, 7.0f, 0.0f);

        auto* urlEntryRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
        ConfigureGroup(urlEntryRow);
        urlEntryRow->set_spacing(0.6f);
        ConfigureLayout(urlEntryRow, -1.0f, 8.0f, 1.0f);
        if(auto* urlEntryLayout = EnsureLayout(urlEntryRow))
            urlEntryLayout->set_minHeight(8.0f);
        pasteUrlButton_ = BSML::Lite::CreateUIButton(
            urlEntryRow,
            "Paste URL",
            {0.0f, 0.0f},
            {13.0f, 7.5f},
            [this]() { PasteUrlFromClipboard(); });
        ConfigureLayout(pasteUrlButton_, 13.0f, 7.5f, 0.0f);
        BSML::Lite::SetButtonTextSize(pasteUrlButton_, 2.45f);
        pasteUrlButtonText_ = pasteUrlButton_->get_gameObject()
            ->GetComponentInChildren<TMPro::TextMeshProUGUI*>();
        if(pasteUrlButtonText_)
            pasteUrlButtonText_->set_color(UnityEngine::Color::get_white());
        urlInput_ = BSML::Lite::CreateStringSetting(
            urlEntryRow, "YouTube URL", "", [this](StringW value) {
                url_ = Trim(std::string(value));
                transientStatus_.clear();
                if(!suppressUrlCallback_)
                    BeginUrlProbe();
            });
        ConfigureLayout(urlInput_, 0.0f, 8.0f, 1.0f);
        checkUrlButton_ = BSML::Lite::CreateUIButton(
            urlEntryRow,
            "Check",
            {0.0f, 0.0f},
            {10.0f, 7.5f},
            [this]() { BeginUrlProbe(); });
        ConfigureLayout(checkUrlButton_, 10.0f, 7.5f, 0.0f);
        BSML::Lite::SetButtonTextSize(checkUrlButton_, 2.35f);
        BSML::Lite::AddHoverHint(
            checkUrlButton_,
            "Checks the displayed YouTube address and enables Download Video when the video is available. Use this for a mapper-provided address that has not been checked yet.");
        // The stock input field is almost indistinguishable from the menu
        // behind it on the side screen. Add a low-opacity, very light gray
        // plate behind only the editable URL control; keeping it non-raycast
        // means the field retains its complete clickable area.
        auto* urlFieldBackground = BSML::Lite::CreateImage(
            urlInput_->get_transform(),
            BSML::Utilities::ImageResources::GetBlankSprite());
        urlFieldBackground->set_color({0.88f, 0.90f, 0.93f, 0.24f});
        urlFieldBackground->set_preserveAspect(false);
        urlFieldBackground->set_raycastTarget(false);
        if(auto backgroundRect = urlFieldBackground->get_transform()
               .cast<UnityEngine::RectTransform>())
        {
            backgroundRect->set_anchorMin({0.0f, 0.0f});
            backgroundRect->set_anchorMax({1.0f, 1.0f});
            backgroundRect->set_pivot({0.5f, 0.5f});
            backgroundRect->set_anchoredPosition({0.0f, 0.0f});
            backgroundRect->set_sizeDelta({-0.35f, -0.35f});
            backgroundRect->SetAsFirstSibling();
        }

        // Keep the address field at the panel's full width. Recognition art
        // belongs in the following action row, where it cannot reduce the
        // amount of the pasted URL that remains visible and editable.
        auto* urlRowSpacer = BSML::Lite::CreateText(editorBody, "", 1.0f);
        ConfigureLayout(urlRowSpacer, -1.0f, 0.7f, 1.0f);
        if(auto* spacerLayout = EnsureLayout(urlRowSpacer))
            spacerLayout->set_minHeight(0.7f);
        auto* urlPreviewRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
        ConfigureGroup(urlPreviewRow);
        urlPreviewRow->set_spacing(0.45f);
        ConfigureLayout(urlPreviewRow, -1.0f, 9.0f, 1.0f);
        if(auto* previewRowLayout = EnsureLayout(urlPreviewRow))
            previewRowLayout->set_minHeight(9.0f);
        urlThumbnail_ = BSML::Lite::CreateImage(
            urlPreviewRow,
            BSML::Utilities::ImageResources::GetBlankSprite());
        ConfigureLayout(urlThumbnail_, 15.0f, 7.7f, 0.0f);
        urlThumbnail_->set_color({0.08f, 0.10f, 0.13f, 0.85f});
        urlThumbnail_->set_preserveAspect(true);
        downloadButton_ = BSML::Lite::CreateUIButton(
            urlPreviewRow, "Download Video", {0.0f, 0.0f}, {18.5f, 7.5f},
            [this]() { StartOrCancelDownload(); });
        ConfigureLayout(downloadButton_, 18.5f, 7.5f, 0.0f);
        BSML::Lite::SetButtonTextSize(downloadButton_, 2.45f);
        downloadButtonText_ = downloadButton_->get_gameObject()
            ->GetComponentInChildren<TMPro::TextMeshProUGUI*>();
        BSML::Lite::AddHoverHint(
            downloadButton_,
            "Downloads the checked YouTube video as an H.264 MP4 and assigns it to this song. While active, the same button can pause the download.");
        downloadButton_->get_gameObject()->SetActive(false);
        // Preserve the thumbnail/action row geometry while the real button is
        // hidden. Swapping this transparent slot out when validation succeeds
        // prevents the thumbnail from jumping sideways as the button appears.
        auto* downloadButtonPlaceholder = BSML::Lite::CreateImage(
            urlPreviewRow,
            BSML::Utilities::ImageResources::GetBlankSprite());
        downloadButtonPlaceholder->set_color({0.0f, 0.0f, 0.0f, 0.0f});
        downloadButtonPlaceholder->set_raycastTarget(false);
        ConfigureLayout(downloadButtonPlaceholder, 18.5f, 7.5f, 0.0f);
        downloadButtonPlaceholder_ = downloadButtonPlaceholder->get_gameObject();
        auto* thumbnailBalance = BSML::Lite::CreateImage(
            urlPreviewRow,
            BSML::Utilities::ImageResources::GetBlankSprite());
        thumbnailBalance->set_color({0.0f, 0.0f, 0.0f, 0.0f});
        thumbnailBalance->set_raycastTarget(false);
        ConfigureLayout(thumbnailBalance, 15.0f, 8.4f, 0.0f);
        BSML::Lite::AddHoverHint(
            urlInput_,
            "Enter a normal youtube.com video address or a youtu.be Share link. Big Screen checks the link before the Download Video button appears.");
        BSML::Lite::AddHoverHint(
            pasteUrlButton_,
            "Pastes a YouTube address from the Quest clipboard and checks whether the video can be downloaded.");

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

        // ToggleSetting is a full-width settings-row prefab. Keep Fit to Song
        // on its own row, followed by the two numeric timing controls. The
        // lead-in appearance belongs directly beneath the offset it describes.
        auto* timingToggleObject = ConstructLayout(
            "<vertical tags='big-screen-timing-toggles' spacing='0.2' "
            "horizontal-fit='Unconstrained'/>",
            editorRoot->get_transform(),
            "big-screen-timing-toggles");
        auto* timingToggleColumn = timingToggleObject
            ? timingToggleObject->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()
            : BSML::Lite::CreateVerticalLayoutGroup(editorBody);
        ConfigureGroup(timingToggleColumn);
        timingToggleColumn->set_spacing(0.2f);
        timingToggleColumn->set_childForceExpandWidth(true);
        ConfigureLayout(timingToggleColumn, -1.0f, 7.8f, 1.0f);
        fitToggle_ = BSML::Lite::CreateToggle(
            timingToggleColumn,
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
        ConfigureLayout(fitToggle_, -1.0f, 7.8f, 1.0f);
        StyleToggleRow(fitToggle_);
        BSML::Lite::AddHoverHint(
            fitToggle_,
            "Automatically calculates playback speed so the video ends with the song after Video Playback Offset is applied. Changing the offset recalculates the fitted speed.");
        videoOnlyRows_.push_back(timingToggleColumn->get_gameObject());

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
        videoOnlyRows_.push_back(rateSetting_->get_gameObject());
        BSML::Lite::AddHoverHint(
            rateSetting_,
            "Controls how quickly the video advances. 1.00 is normal speed; lower values slow it down and higher values speed it up. Fit to Song manages this value automatically when enabled.");

        offsetSetting_ = BSML::Lite::CreateIncrementSetting(
            editorBody, "Video Playback Offset", 2, 0.25f, 0.0f,
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
        videoOnlyRows_.push_back(offsetSetting_->get_gameObject());
        BSML::Lite::AddHoverHint(
            offsetSetting_,
            "Aligns the video with the song. Negative values wait before showing video frame zero; positive values begin farther into the video.");

        blackLeadInToggle_ = BSML::Lite::CreateToggle(
            editorBody,
            "Lead-In Background",
            false,
            [this](bool enabled)
            {
                if(suppressTimingCallbacks_) return;
                blackDuringLeadIn_ = enabled;
                if(SaveTiming())
                {
                    transientStatus_ = enabled
                        ? "Video lead-in will use a solid black background."
                        : "Video lead-in will remain transparent.";
                    StartSelectedPreview();
                    RefreshDetails();
                }
            });
        ConfigureLayout(blackLeadInToggle_, -1.0f, 7.8f, 1.0f);
        StyleToggleRow(blackLeadInToggle_);
        BSML::Lite::AddHoverHint(
            blackLeadInToggle_,
            "Controls the waiting time created by a negative Video Playback Offset. On shows a solid black screen; off keeps the screen hidden until the video begins.");
        videoOnlyRows_.push_back(blackLeadInToggle_->get_gameObject());

        // Visually separate audition controls from settings that permanently
        // alter video synchronization. The title, scrubber, transport button,
        // and clock all belong to this single bordered playback group.
        auto* playbackPanelObject = ConstructLayout(
            "<vertical tags='big-screen-playback-panel' bg='round-rect-panel' "
            "pad-left='0.6' pad-right='0.6' pad-top='0.35' pad-bottom='0.35' "
            "spacing='0.15' horizontal-fit='Unconstrained'/>",
            editorRoot->get_transform(),
            "big-screen-playback-panel");
        auto* playbackPanel = playbackPanelObject
            ? playbackPanelObject->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()
            : BSML::Lite::CreateVerticalLayoutGroup(editorBody);
        ConfigureGroup(playbackPanel);
        playbackPanel->set_spacing(0.15f);
        playbackPanel->set_childForceExpandWidth(true);
        ConfigureLayout(playbackPanel, -1.0f, 11.2f, 1.0f);
        videoOnlyRows_.push_back(playbackPanel->get_gameObject());
        auto* playbackPanelBackground = playbackPanel->get_gameObject()
            ->GetComponent<HMUI::ImageView*>();
        if(playbackPanelBackground)
        {
            playbackPanelBackground->set_gradient(false);
            playbackPanelBackground->set_color(
                {0.22f, 0.25f, 0.30f, 0.90f});
        }
        const BSML::Lite::TransformWrapper playbackBody(playbackPanel);

        auto* playbackGroupTitle = BSML::Lite::CreateText(
            playbackBody,
            "Playback Position",
            3.0f);
        ConfigureLayout(playbackGroupTitle, -1.0f, 2.8f, 1.0f);
        playbackGroupTitle->set_alignment(TMPro::TextAlignmentOptions::Center);

        auto* playbackRow = BSML::Lite::CreateHorizontalLayoutGroup(playbackBody);
        ConfigureGroup(playbackRow);
        // Preserve the established left/right edge padding and transport gap.
        // Only the visual track below is made thinner; changing this row was
        // what shifted the Play button and the scrubber's right edge.
        playbackRow->set_spacing(1.25f);
        playbackRow->set_padding(UnityEngine::RectOffset::New_ctor(1, 1, 0, 0));
        ConfigureLayout(playbackRow, -1.0f, 7.8f, 1.0f);
        playPauseButton_ = BSML::Lite::CreateUIButton(
            playbackRow,
            "▶",
            "PlayButton",
            {0.0f, 0.0f},
            {10.0f, 7.6f},
            [this]() { TogglePreviewPlayback(); });
        ConfigureLayout(playPauseButton_, 10.0f, 7.4f, 0.0f);
        BSML::Lite::SetButtonTextSize(playPauseButton_, 4.0f);
        auto transportColors = playPauseButton_->get_colors();
        transportColors.set_normalColor({0.05f, 0.55f, 0.90f, 1.0f});
        transportColors.set_highlightedColor({0.20f, 0.75f, 1.0f, 1.0f});
        transportColors.set_pressedColor({0.03f, 0.35f, 0.65f, 1.0f});
        transportColors.set_selectedColor({0.10f, 0.65f, 0.95f, 1.0f});
        playPauseButton_->set_transition(
            UnityEngine::UI::Selectable::Transition::ColorTint);
        playPauseButton_->set_colors(transportColors);
        if(auto target = playPauseButton_->get_targetGraphic())
            target->set_color(UnityEngine::Color::get_white());
        BSML::Lite::AddHoverHint(
            playPauseButton_,
            "Plays or pauses this song and video together so you can check synchronization before entering the map.");

        playbackScrubber_ = BSML::Lite::CreateSliderSetting(
            playbackRow,
            "",
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
        playbackScrubber_->formatter = [this](float normalized) -> StringW
        {
            const double duration = selected_
                ? std::max(0.0f, selected_->songDuration)
                : 0.0;
            return PlaybackTime(normalized * duration) + " / " +
                PlaybackTime(duration);
        };
        playbackTimeText_ = playbackScrubber_->text;
        if(playbackTimeText_)
        {
            playbackTimeText_->set_fontSize(2.4f);
            playbackTimeText_->set_alignment(TMPro::TextAlignmentOptions::Center);
            playbackTimeText_->set_color(UnityEngine::Color::get_black());
            // SliderSetting normally positions its value for a labeled
            // settings row. Stretch the combined current/total time over the
            // complete slider allocation so it remains centered on the bar.
            if(auto timeRect = playbackTimeText_->get_transform()
                   .cast<UnityEngine::RectTransform>())
            {
                timeRect->set_anchorMin({0.0f, 0.0f});
                timeRect->set_anchorMax({1.0f, 1.0f});
                timeRect->set_pivot({0.5f, 0.5f});
                timeRect->set_anchoredPosition({0.0f, 0.0f});
                timeRect->set_sizeDelta({0.0f, 0.0f});
                timeRect->SetAsLastSibling();
            }
        }

        // The stock settings slider reserves a fixed 52-unit control on the
        // right because it normally shares a row with a label. This playback
        // bar has its own group heading, so remove the empty label and stretch
        // the actual draggable slider across the flexible row allocation.
        if(auto title = playbackScrubber_->get_transform()->Find("Title"))
            title->get_gameObject()->SetActive(false);
        if(playbackScrubber_->slider)
        {
            auto sliderRect = playbackScrubber_->slider->get_transform()
                .cast<UnityEngine::RectTransform>();
            sliderRect->set_anchorMin({0.0f, 0.0f});
            sliderRect->set_anchorMax({1.0f, 1.0f});
            sliderRect->set_pivot({0.5f, 0.5f});
            // The slider root keeps a generous invisible hit area. Only the
            // light-gray track and visible handle are shortened below.
            sliderRect->set_anchoredPosition({0.0f, 0.0f});
            sliderRect->set_sizeDelta({0.0f, 0.0f});

            // The stock slider tints its complete track as the Selectable's
            // hover target. Preserve that graphic as a fixed light-gray bar,
            // then retarget ColorTint to the movable handle alone so pointing
            // at the control turns only the handle black.
            auto sliderTrack = playbackScrubber_->slider->get_targetGraphic();
            constexpr float scrubberTrackHeight = 2.5f;
            constexpr float scrubberHandleHeight =
                scrubberTrackHeight * 0.80f;
            if(sliderTrack)
            {
                sliderTrack->set_color({0.82f, 0.84f, 0.87f, 0.92f});
                if(auto trackRect = sliderTrack->get_transform()
                       .cast<UnityEngine::RectTransform>())
                {
                    trackRect->set_anchorMin({0.0f, 0.5f});
                    trackRect->set_anchorMax({1.0f, 0.5f});
                    trackRect->set_pivot({0.5f, 0.5f});
                    trackRect->set_anchoredPosition({0.0f, 0.0f});
                    trackRect->set_sizeDelta({0.0f, scrubberTrackHeight});
                }
            }

            UnityEngine::UI::Graphic* sliderHandle = nullptr;
            if(auto handleRect = playbackScrubber_->slider->get_handleRect())
            {
                // Keep the thumb visually substantial without making it
                // taller than the track: exactly 80 percent of track height.
                handleRect->set_anchorMin(
                    {handleRect->get_anchorMin().x, 0.5f});
                handleRect->set_anchorMax(
                    {handleRect->get_anchorMax().x, 0.5f});
                handleRect->set_pivot({0.5f, 0.5f});
                handleRect->set_anchoredPosition(
                    {handleRect->get_anchoredPosition().x, 0.0f});
                handleRect->set_sizeDelta(
                    {std::max(2.0f, handleRect->get_sizeDelta().x),
                     scrubberHandleHeight});
                sliderHandle = handleRect->get_gameObject()
                    ->GetComponentInChildren<UnityEngine::UI::Graphic*>();
            }
            if(sliderHandle)
            {
                sliderHandle->set_color(UnityEngine::Color::get_white());
                playbackScrubber_->slider->set_targetGraphic(sliderHandle);
            }
            auto sliderColors = playbackScrubber_->slider->get_colors();
            const UnityEngine::Color restingHandle{
                0.05f, 0.62f, 0.95f, 1.0f};
            sliderColors.set_normalColor(restingHandle);
            sliderColors.set_highlightedColor(UnityEngine::Color::get_black());
            sliderColors.set_pressedColor(UnityEngine::Color::get_black());
            sliderColors.set_selectedColor(restingHandle);
            playbackScrubber_->slider->set_colors(sliderColors);

            playbackScrubberFill_ = BSML::Lite::CreateImage(
                sliderTrack
                    ? sliderTrack->get_transform()
                    : playbackScrubber_->slider->get_transform(),
                BSML::Utilities::ImageResources::GetBlankSprite());
            playbackScrubberFill_->set_color({0.05f, 0.62f, 0.95f, 0.82f});
            playbackScrubberFill_->set_preserveAspect(false);
            playbackScrubberFill_->set_raycastTarget(false);
            if(auto fillRect = playbackScrubberFill_->get_transform()
                   .cast<UnityEngine::RectTransform>())
            {
                fillRect->set_anchorMin({0.0f, 0.0f});
                fillRect->set_anchorMax({0.0f, 1.0f});
                fillRect->set_pivot({0.0f, 0.5f});
                fillRect->set_anchoredPosition({0.0f, 0.0f});
                fillRect->set_sizeDelta({0.0f, -0.25f});
                fillRect->SetAsFirstSibling();
            }
        }

        // Play/Pause reserves the left edge; the scrubber consumes every
        // remaining unit of row width. Its native centered value text now
        // serves as the current/total playback clock directly on the bar.
        ConfigureLayout(playbackScrubber_, 0.0f, 6.7f, 1.0f);
        BSML::Lite::AddHoverHint(
            playbackScrubber_,
            "Drag to another point in the song and video preview. The time shown on the bar is the current song position and total song length.");

        // Deleting an override also deletes its downloaded media, thumbnail,
        // and saved timing. Require an explicit second action so an imprecise
        // controller click cannot remove those files immediately.
        removeConfirmModal_ = BSML::Lite::CreateModal(
            editorController,
            {64.0f, 32.0f},
            nullptr,
            true);
        removeConfirmationText_ = BSML::Lite::CreateText(
            removeConfirmModal_,
            "Remove this assigned video?\nThe downloaded video and its timing settings will be deleted.",
            TMPro::FontStyles::Normal,
            3.3f,
            {0.0f, 4.0f},
            {56.0f, 16.0f});
        removeConfirmationText_->set_enableWordWrapping(true);
        removeConfirmationText_->set_enableAutoSizing(true);
        removeConfirmationText_->set_fontSizeMin(2.9f);
        removeConfirmationText_->set_fontSizeMax(3.3f);
        removeConfirmationText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        removeConfirmationText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        auto* cancelRemoveButton = BSML::Lite::CreateUIButton(
            removeConfirmModal_->get_transform(),
            "Cancel",
            {18.0f, -25.0f},
            {20.0f, 8.0f},
            [this]()
            {
                if(removeConfirmModal_)
                    removeConfirmModal_->Hide();
            });
        ConfigureLayout(cancelRemoveButton, 20.0f, 8.0f, 0.0f);
        auto* confirmRemoveButton = BSML::Lite::CreateUIButton(
            removeConfirmModal_->get_transform(),
            "<color=#FF3838>Remove Video</color>",
            {46.0f, -25.0f},
            {20.0f, 8.0f},
            [this]()
            {
                if(removeConfirmModal_)
                    removeConfirmModal_->Hide();
                RemoveOverride();
            });
        ConfigureLayout(confirmRemoveButton, 20.0f, 8.0f, 0.0f);
        if(auto* confirmText = confirmRemoveButton->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            confirmText->set_color({1.0f, 0.22f, 0.22f, 1.0f});

        auto* storageSpacer = BSML::Lite::CreateText(editorBody, "", 1.0f);
        ConfigureLayout(storageSpacer, -1.0f, 2.0f, 1.0f);
        storageSpacer_ = storageSpacer->get_gameObject();

        // Match the playback area's visual hierarchy: a full-width rounded
        // panel names the section, then keeps all three capacity values and
        // the destructive action together inside one bordered region.
        auto* storagePanelObject = ConstructLayout(
            "<vertical tags='big-screen-storage-panel' bg='round-rect-panel' "
            "pad-left='0.8' pad-right='0.8' pad-top='0.5' pad-bottom='0.5' "
            "spacing='0.3' horizontal-fit='Unconstrained'/>",
            editorRoot->get_transform(),
            "big-screen-storage-panel");
        auto* storagePanel = storagePanelObject
            ? storagePanelObject->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()
            : BSML::Lite::CreateVerticalLayoutGroup(editorBody);
        ConfigureGroup(storagePanel);
        storagePanel->set_spacing(0.3f);
        storagePanel->set_childForceExpandWidth(true);
        ConfigureLayout(storagePanel, -1.0f, 12.5f, 1.0f);
        storagePanel_ = storagePanel->get_gameObject();
        if(playbackPanelBackground)
            if(auto* storagePanelBackground = storagePanel->get_gameObject()
                   ->GetComponent<HMUI::ImageView*>())
            {
                storagePanelBackground->set_gradient(false);
                storagePanelBackground->set_color(
                    playbackPanelBackground->get_color());
            }
        const BSML::Lite::TransformWrapper storageBody(storagePanel);

        auto* storageGroupTitle = BSML::Lite::CreateText(
            storageBody,
            "Video Storage",
            3.0f);
        ConfigureLayout(storageGroupTitle, -1.0f, 3.8f, 1.0f);
        storageGroupTitle->set_alignment(TMPro::TextAlignmentOptions::Center);

        // The button occupies the far-right edge. Local Videos participates in
        // this layout only when one or more MP4 files physically exist in the
        // selected map folder, so ordinary maps do not gain an empty column.
        auto* storageRow = BSML::Lite::CreateHorizontalLayoutGroup(storageBody);
        ConfigureGroup(storageRow);
        storageRow->set_spacing(0.6f);
        ConfigureLayout(storageRow, -1.0f, 7.0f, 1.0f);
        detailMapStorage_ = BSML::Lite::CreateText(
            storageRow, "Downloaded Video\n0.0 MB", 2.15f);
        ConfigureLayout(detailMapStorage_, 0.0f, 7.0f, 1.0f);
        detailLocalStorage_ = BSML::Lite::CreateText(
            storageRow, "Local Videos\n0.0 MB", 2.15f);
        ConfigureLayout(detailLocalStorage_, 0.0f, 7.0f, 1.0f);
        detailLibraryStorage_ = BSML::Lite::CreateText(
            storageRow, "All Downloads\n0.0 MB", 2.15f);
        ConfigureLayout(detailLibraryStorage_, 0.0f, 7.0f, 1.0f);
        detailFreeStorage_ = BSML::Lite::CreateText(
            storageRow, "Free Space\n0.0 MB", 2.15f);
        ConfigureLayout(detailFreeStorage_, 0.0f, 7.0f, 1.0f);
        removeButton_ = BSML::Lite::CreateUIButton(
            storageRow,
            "<color=#FF3838>Remove Video</color>",
            {0.0f, 0.0f},
            {13.5f, 6.5f},
            [this]()
            {
                if(removeConfirmModal_)
                    removeConfirmModal_->Show();
            });
        ConfigureLayout(removeButton_, 13.5f, 6.5f, 0.0f);
        BSML::Lite::SetButtonTextSize(removeButton_, 2.15f);
        if(auto* removeText = removeButton_->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
        {
            removeText->set_richText(true);
            removeText->set_color(UnityEngine::Color::get_white());
        }
        auto removeColors = removeButton_->get_colors();
        removeColors.set_normalColor({0.18f, 0.20f, 0.23f, 0.96f});
        removeColors.set_highlightedColor({0.38f, 0.41f, 0.46f, 1.0f});
        removeColors.set_pressedColor({0.50f, 0.53f, 0.58f, 1.0f});
        removeColors.set_selectedColor({0.30f, 0.33f, 0.38f, 1.0f});
        removeButton_->set_transition(
            UnityEngine::UI::Selectable::Transition::ColorTint);
        removeButton_->set_colors(removeColors);
        if(auto target = removeButton_->get_targetGraphic())
            target->set_color(UnityEngine::Color::get_white());
        BSML::Lite::AddHoverHint(
            removeButton_,
            "Removes the current user video assignment after confirmation. Downloaded Big Screen videos are deleted; map-folder and Video Import files are only unassigned and remain on the Quest.");

        for(auto* text : {
                browserTitle_, browserStorage_, filterText_, detailTitle_,
                detailText_, detailMapStorage_, detailLocalStorage_, detailLibraryStorage_,
                detailFreeStorage_, playbackTimeText_})
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

        // Startup recovery waits for this complete catalog because level IDs
        // are the only safe way to reconnect deterministic managed filenames.
        std::vector<GlobalNamespace::BeatmapLevel*> installedLevels;
        installedLevels.reserve(catalog_.size());
        for(const auto& item : catalog_)
            installedLevels.push_back(item.level);
        VideoLibrary::Instance().RecoverManagedFiles(installedLevels);

        std::sort(catalog_.begin(), catalog_.end(), [](const auto& left, const auto& right) {
            // Resolve every tie so rebuilding the catalog cannot reshuffle
            // songs that share a title. Table cells and the backing model both
            // use this index, so their order must be completely deterministic.
            const auto leftName = Lower(left.level && left.level->songName
                ? std::string(left.level->songName) : std::string{});
            const auto rightName = Lower(right.level && right.level->songName
                ? std::string(right.level->songName) : std::string{});
            if(leftName != rightName) return leftName < rightName;

            const auto leftArtist = Lower(left.level && left.level->songAuthorName
                ? std::string(left.level->songAuthorName) : std::string{});
            const auto rightArtist = Lower(right.level && right.level->songAuthorName
                ? std::string(right.level->songAuthorName) : std::string{});
            if(leftArtist != rightArtist) return leftArtist < rightArtist;

            const auto leftId = left.level && left.level->levelID
                ? std::string(left.level->levelID) : std::string{};
            const auto rightId = right.level && right.level->levelID
                ? std::string(right.level->levelID) : std::string{};
            return leftId < rightId;
        });
        std::array<int, 4> groupCounts{};
        for(const auto& item : catalog_)
            ++groupCounts[static_cast<int>(item.group)];
        PaperLogger.info(
            "Video library catalog: {} total ({} custom, {} WIP, {} OST, {} DLC)",
            catalog_.size(), groupCounts[0], groupCounts[1], groupCounts[2], groupCounts[3]);
        RebuildVisibleRows();
    }

    void VideoLibraryMenu::RebuildVisibleRows(bool preserveScrollPosition)
    {
        auto* tableView = list_ ? list_->tableView : nullptr;
        auto* scrollView = tableView ? tableView->get_scrollView().ptr() : nullptr;
        if(preserveScrollPosition && scrollView)
        {
            // Selecting a song can hide the browser before an animated letter
            // jump reaches its destination. Freeze it at the displayed position
            // before replacing data so it cannot keep moving recycled cells
            // after the browser controller becomes visible again.
            scrollView->ScrollTo(scrollView->get_position(), false);
        }

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
                videoThumbnail = FindCachedVideoThumbnail(
                    descriptor.thumbnailPath->string());
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
        if(tableView)
        {
            // A TableView retains its selected index even when its data is
            // rebuilt. Clear it before reloading so returning from the child
            // editor leaves every row clickable, including the song the user
            // just edited.
            tableView->ClearSelection();
            if(preserveScrollPosition)
            {
                // Returning from the editor does not intentionally change the
                // sort. Reload the metadata and the visible cells as one native
                // operation while retaining the current letter/scroll position.
                tableView->ReloadDataKeepingPosition();
            }
            else
            {
                // A new search, filter, or catalog intentionally begins at row
                // zero and also cancels any previous animated scroll.
                tableView->ReloadData();
                if(!visible_.empty())
                    tableView->ScrollToCellWithIdx(
                        0,
                        HMUI::TableView::ScrollPositionType::Beginning,
                        false);
            }
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
        if(list_ && list_->tableView)
        {
            // Freeze a possibly animated A-Z jump before hiding the browser.
            // The exact displayed position can then be restored safely when
            // the user returns from the child editor.
            if(auto* scrollView = list_->tableView->get_scrollView().ptr())
                scrollView->ScrollTo(scrollView->get_position(), false);
        }
        StopPreviewAudio(true);
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Stop();
        selected_ = visible_[row]->level;
        PaperLogger.debug(
            "Opening video editor row {} for '{}' ({})",
            row,
            selected_ && selected_->songName
                ? std::string(selected_->songName) : std::string("Unknown Song"),
            selected_ && selected_->levelID
                ? std::string(selected_->levelID) : std::string("no level id"));
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
        RefreshLocalVideoFiles();
        ShowEditor();
        RequestSelectedAudio();
        // A mapper URL has already been supplied on the user's behalf. Probe
        // it automatically so the hidden Download Video action can appear
        // once yt-dlp confirms that the source is actually available.
        if(!descriptor.CanPlay() && descriptor.CanDownload() &&
           IsYouTubeUrl(url_))
            BeginUrlProbe();
        else
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
        RebuildVisibleRows(true);
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
        if(!selected_ || !selected_->levelID)
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
            // Bind the asynchronous probe result to the exact text currently
            // in the field. Clearing or replacing that text invalidates this
            // identity, preventing a completed older probe from restoring a
            // stale thumbnail during RefreshDetails.
            probedUrl_ = url_;
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
            selected_->songName ? std::string(selected_->songName) : std::string("Unknown Song"),
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
            ErrorManager::Instance().RecordError(
                "Starting a video download for " + selectedLevelId,
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
            ErrorManager::Instance().RecordError(
                "Reading the Quest clipboard",
                error.what());
            detailText_->set_text("Could not read the Quest clipboard.");
        }
    }

    void VideoLibraryMenu::SearchSelectedSongOnYouTube()
    {
        if(!selected_)
            return;

        const std::string song = selected_->songName
            ? std::string(selected_->songName)
            : std::string{};
        const std::string artist = selected_->songAuthorName
            ? std::string(selected_->songAuthorName)
            : std::string{};
        const auto query = Trim(song + (artist.empty() ? "" : " " + artist));
        if(query.empty())
        {
            transientStatus_ =
                "This map does not provide a song or artist name to search.";
            RefreshDetails();
            return;
        }

        const auto url =
            "https://www.youtube.com/results?search_query=" +
            EncodeUrlQuery(query);
        try
        {
            // Opening another Quest activity backgrounds Beat Saber. Stop the
            // editor audition first so audio cannot continue underneath the
            // browser or resume from an ambiguous point when the player
            // returns to the game.
            StopPreviewAudio(true);
            if(PlaybackSession::Instance().IsLibraryPreviewActive())
                PlaybackSession::Instance().Stop();

            // Application.OpenURL returns successfully on this Quest firmware
            // but never asks Android to launch an activity. Build the standard
            // ACTION_VIEW intent explicitly through Unity's Java bridge. This
            // remains browser-agnostic; Android currently resolves it to the
            // installed Quest Browser rather than hard-coding that package.
            const StringW urlString(url);
            const StringW actionString("android.intent.action.VIEW");
            auto* uriClass = UnityEngine::AndroidJavaClass::New_ctor(
                "android.net.Uri");
            auto* uriArguments = Array<::System::Object*>::New({
                reinterpret_cast<::System::Object*>(
                    static_cast<Il2CppString*>(urlString))});
            auto* uri = uriClass->CallStatic<
                UnityEngine::AndroidJavaObject*>(
                    "parse",
                    uriArguments);

            ::ArrayW<::System::Object*, ::Array<::System::Object*>*>
                intentArguments = Array<::System::Object*>::New({
                reinterpret_cast<::System::Object*>(
                    static_cast<Il2CppString*>(actionString)),
                static_cast<::System::Object*>(uri)});
            auto* intent = UnityEngine::AndroidJavaObject::New_ctor(
                "android.content.Intent",
                intentArguments);

            auto* unityPlayer = UnityEngine::AndroidJavaClass::New_ctor(
                "com.unity3d.player.UnityPlayer");
            auto* activity = unityPlayer->GetStatic<
                UnityEngine::AndroidJavaObject*>("currentActivity");
            if(!activity || !intent)
                throw std::runtime_error(
                    "Unity did not expose its Android activity or intent");
            auto* activityArguments = Array<::System::Object*>::New({
                static_cast<::System::Object*>(intent)});
            activity->Call("startActivity", activityArguments);

            // Release Java local references after Android has accepted the
            // activity request. The browser owns its copied Intent/Uri state.
            activity->Dispose();
            unityPlayer->Dispose();
            intent->Dispose();
            uri->Dispose();
            uriClass->Dispose();
            transientStatus_ = "Opened YouTube search in the Quest browser.";
            PaperLogger.info(
                "Opened YouTube search for '{}'",
                query);
        }
        catch(const std::exception& error)
        {
            transientStatus_ =
                "Quest could not open the YouTube search in a browser.";
            PaperLogger.error(
                "Could not launch YouTube search '{}': {}",
                url,
                error.what());
            ErrorManager::Instance().RecordError(
                "Opening a YouTube search",
                error.what());
        }
        catch(...)
        {
            transientStatus_ =
                "Quest could not open the YouTube search in a browser.";
            PaperLogger.error(
                "Could not launch YouTube search '{}'",
                url);
            ErrorManager::Instance().RecordError(
                "Opening a YouTube search",
                "Unknown native exception");
        }
        RefreshDetails();
    }

    void VideoLibraryMenu::RefreshLocalVideoFiles()
    {
        // The center-screen browser owns directory enumeration and probing.
        // The side editor reads only the active assignment, avoiding repeated
        // FFmpeg file probes every time its details refresh.
        localVideoFiles_.clear();
        localVideoImported_.clear();
        RebuildLocalVideoRows();
    }

    void VideoLibraryMenu::RebuildLocalVideoRows()
    {
        if(!localVideoListContent_)
            return;

        const auto descriptor = selected_
            ? VideoLibrary::Instance().Describe(selected_)
            : VideoDescriptor{};
        const bool userLocal = descriptor.hasUserOverride &&
            (descriptor.userOverrideIsMapLocal ||
             descriptor.userOverrideIsImported ||
             descriptor.userOverrideIsExternal);
        const bool activeLocal = userLocal ||
            (!descriptor.hasUserOverride && descriptor.hasMapperLocalFile);
        if(showFileBrowserButton_)
            showFileBrowserButton_->set_interactable(selected_ != nullptr);
        if(localVideoStatusText_)
        {
            localVideoStatusText_->get_gameObject()->SetActive(activeLocal);
            if(activeLocal)
            {
                localVideoStatusText_->set_text(
                    "Local video: " +
                    descriptor.activeMapFileName.value_or("selected MP4"));
                localVideoStatusText_->set_color(
                    {0.20f, 1.0f, 0.36f, 1.0f});
            }
        }
    }

    void VideoLibraryMenu::SetLocalVideo(std::size_t index)
    {
        if(!selected_ || index >= localVideoFiles_.size())
            return;
        const auto& file = localVideoFiles_[index];
        const bool imported = index < localVideoImported_.size() &&
            localVideoImported_[index];
        if(!file.compatible)
        {
            ShowLocalVideoHelp(index);
            return;
        }

        // Close the decoder before replacing a managed YouTube override. The
        // selected local MP4 itself remains user-owned and is never moved or
        // deleted by this operation.
        StopPreviewAudio(true);
        auto& playback = PlaybackSession::Instance();
        if(playback.IsLibraryPreviewActive())
            playback.Stop();

        std::string error;
        const bool assigned = imported
            ? VideoLibrary::Instance().SetImportedVideoOverride(
                selected_, file.fileName, error)
            : VideoLibrary::Instance().SetLocalVideoOverride(
                selected_, file.fileName, error);
        if(!assigned)
        {
            transientStatus_ = error.empty()
                ? "The local video could not be assigned."
                : error;
            RefreshLocalVideoFiles();
            RefreshDetails();
            return;
        }

        EvictVideoThumbnail(
            VideoLibrary::Instance().AllocateThumbnailPath(
                std::string(selected_->levelID), VideoOrigin::User).string());

        // A newly assigned file starts with neutral timing, exactly like a new
        // user override. The same controls below can then tune it and persist
        // those values in library.json without altering the map-folder MP4.
        offset_ = 0.0;
        rate_ = 1.0;
        fitToSong_ = false;
        blackDuringLeadIn_ = false;
        suppressTimingCallbacks_ = true;
        if(offsetSetting_) offsetSetting_->set_Value(0.0f);
        if(rateSetting_) rateSetting_->set_Value(1.0f);
        SetToggleWithoutNotification(fitToggle_, false);
        SetToggleWithoutNotification(blackLeadInToggle_, false);
        suppressTimingCallbacks_ = false;

        url_.clear();
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText("");
            suppressUrlCallback_ = false;
        }
        ClearThumbnail();
        transientStatus_ = imported
            ? "Imported video assigned: " + file.fileName
            : "Local video assigned: " + file.fileName;
        previewSongTime_ = 0.0;
        playWhenAudioReady_ = true;
        RefreshLocalVideoFiles();
        RequestSelectedAudio();
        StartSelectedPreview();
        if(IsAlive(previewAudioClip_))
            StartPreviewAudio();
        RefreshDetails();
    }

    void VideoLibraryMenu::ShowLocalVideoHelp(std::size_t index)
    {
        if(index >= localVideoFiles_.size() ||
           !localVideoHelpModal_ || !localVideoHelpText_)
            return;
        const auto& file = localVideoFiles_[index];
        localVideoHelpText_->set_text(
            ((index < localVideoImported_.size() && localVideoImported_[index])
                ? "Video Import: " : "") + file.fileName + "\n\n" +
            (file.problem.empty()
                ? "This MP4 is not compatible with Big Screen. Use H.264/AVC video at 1080p or lower inside a valid MP4 container."
                : file.problem));
        localVideoHelpModal_->Show();
    }

    void VideoLibraryMenu::LocalVideoAssignmentChanged(
        const std::string& fileName)
    {
        if(!selected_)
            return;

        // The file browser commits a fresh user override with neutral timing.
        // Mirror that durable state into the already-visible editor, then
        // restart both preview clocks from the beginning so the same timing,
        // scrubber, and Fit to Song controls work without reopening the song.
        StopPreviewAudio(true);
        auto& playback = PlaybackSession::Instance();
        if(playback.IsLibraryPreviewActive())
            playback.Stop();

        EvictVideoThumbnail(
            VideoLibrary::Instance().AllocateThumbnailPath(
                std::string(selected_->levelID), VideoOrigin::User).string());
        offset_ = 0.0;
        rate_ = 1.0;
        fitToSong_ = false;
        blackDuringLeadIn_ = false;
        suppressTimingCallbacks_ = true;
        if(offsetSetting_) offsetSetting_->set_Value(0.0f);
        if(rateSetting_) rateSetting_->set_Value(1.0f);
        SetToggleWithoutNotification(fitToggle_, false);
        SetToggleWithoutNotification(blackLeadInToggle_, false);
        suppressTimingCallbacks_ = false;

        url_.clear();
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText("");
            suppressUrlCallback_ = false;
        }
        ClearThumbnail();
        transientStatus_ = "Local video assigned: " + fileName;
        previewSongTime_ = 0.0;
        playWhenAudioReady_ = true;
        RefreshLocalVideoFiles();
        RequestSelectedAudio();
        StartSelectedPreview();
        if(IsAlive(previewAudioClip_))
            StartPreviewAudio();
        RefreshDetails();
    }

    void VideoLibraryMenu::RemoveOverride()
    {
        if(!selected_) return;

        const auto removedDescriptor = VideoLibrary::Instance().Describe(selected_);
        const bool removingLocalMapFile =
            removedDescriptor.userOverrideIsMapLocal;
        const bool removingImportedFile =
            removedDescriptor.userOverrideIsImported;
        const bool removingExternalFile =
            removedDescriptor.userOverrideIsExternal;

        // Stop both clocks before changing the active assignment. Managed
        // downloads may be deleted after their decoder closes; map-folder MP4s
        // are only unregistered and always remain untouched.
        StopPreviewAudio(true);
        auto& playback = PlaybackSession::Instance();
        if(playback.IsLibraryPreviewActive())
            playback.Stop();

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
        if(!removingLocalMapFile && !removingImportedFile &&
           !removingExternalFile &&
           download.levelId == std::string(selected_->levelID) &&
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
        transientStatus_ = removingLocalMapFile
            ? "Local video assignment removed. The MP4 remains in the map folder."
            : removingImportedFile
                ? "Imported video assignment removed. The MP4 remains in Video Import."
            : removingExternalFile
                ? "Local video assignment removed. The MP4 remains in its original folder."
                : "Downloaded user video removed.";
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
        RefreshLocalVideoFiles();
        RefreshDetails();
        StartSelectedPreview();
    }

    void VideoLibraryMenu::ClearThumbnail()
    {
        loadedThumbnailPath_.clear();
        probedUrl_.clear();
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
            if(auto* loaded = FindCachedVideoThumbnail(path))
                sprite = loaded;
            else if(std::filesystem::is_regular_file(*metadata->second.path) &&
                    !FailedVideoThumbnailLoads.contains(path))
            {
                try
                {
                    sprite = BSML::Lite::FileToSprite(path);
                    if(sprite)
                        CacheVideoThumbnail(path, sprite);
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
        const auto libraryBytes = VideoLibrary::Instance().LibraryBytes();
        const auto freeBytes = VideoLibrary::Instance().FreeBytes();
        if(detailLibraryStorage_)
            detailLibraryStorage_->set_text(
                "All Downloads\n" + StorageSize(libraryBytes));
        if(detailFreeStorage_)
            detailFreeStorage_->set_text(
                "Free Space\n" + StorageSize(freeBytes));
        if(!selected_ || !detailText_)
        {
            if(detailMapStorage_)
                detailMapStorage_->set_text("Downloaded Video\n0.0 MB");
            if(detailLocalStorage_)
                detailLocalStorage_->get_gameObject()->SetActive(false);
            if(storageSpacer_)
                storageSpacer_->SetActive(false);
            if(storagePanel_)
                storagePanel_->SetActive(false);
            return;
        }
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        const auto downloadedVideoBytes = selected_->levelID
            ? VideoLibrary::Instance().ManagedBytesForLevel(
                std::string(selected_->levelID))
            : 0;
        const bool userLocal = descriptor.hasUserOverride &&
            (descriptor.userOverrideIsMapLocal ||
             descriptor.userOverrideIsImported ||
             descriptor.userOverrideIsExternal);
        const bool hasLocalVideos = userLocal ||
            (!descriptor.hasUserOverride && descriptor.hasMapperLocalFile);
        std::uint64_t localVideoBytes = 0;
        if(hasLocalVideos && descriptor.playableConfig)
        {
            std::error_code sizeError;
            localVideoBytes = std::filesystem::file_size(
                descriptor.playableConfig->videoPath, sizeError);
            if(sizeError) localVideoBytes = 0;
        }
        const bool showStorage = descriptor.CanPlay() ||
            downloadedVideoBytes > 0 || hasLocalVideos;
        if(detailMapStorage_)
            detailMapStorage_->set_text(
                "Downloaded Video\n" + StorageSize(downloadedVideoBytes));
        if(detailLocalStorage_)
        {
            detailLocalStorage_->set_text(
                "Local Videos\n" + StorageSize(localVideoBytes));
            detailLocalStorage_->get_gameObject()->SetActive(hasLocalVideos);
        }
        if(storageSpacer_)
            storageSpacer_->SetActive(showStorage);
        if(storagePanel_)
            storagePanel_->SetActive(showStorage);
        if(detailTitle_)
        {
            const auto name = selected_->songName ? std::string(selected_->songName) : "Unknown Song";
            const auto author = selected_->songAuthorName
                ? std::string(selected_->songAuthorName) : std::string{};
            detailTitle_->set_text(author.empty() ? name : name + "\n" + author);
        }
        if(removeConfirmationText_)
        {
            removeConfirmationText_->set_text(
                descriptor.userOverrideIsMapLocal
                    ? "Unassign this local video?\n\nBig Screen will remove the assignment and its timing settings. The MP4 file will remain unchanged in the map folder."
                    : descriptor.userOverrideIsImported
                        ? "Unassign this imported video?\n\nBig Screen will remove the assignment and its timing settings. The MP4 file will remain unchanged in the Video Import folder."
                    : descriptor.userOverrideIsExternal
                        ? "Unassign this local video?\n\nBig Screen will remove the assignment and its timing settings. The MP4 file will remain unchanged in its current Quest folder."
                    : "Remove this downloaded video?\n\nThe downloaded MP4 and its timing settings will be deleted from Big Screen storage.");
        }
        const auto download = DownloadManager::Instance().Snapshot();
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
           !download.thumbnailPath.empty() && !probedUrl_.empty() &&
           url_ == probedUrl_)
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
            : descriptor.userOverrideIsMapLocal
                ? "Local map video active: " +
                    descriptor.activeMapFileName.value_or("selected MP4")
            : descriptor.userOverrideIsImported
                ? "Imported video active: " +
                    descriptor.activeMapFileName.value_or("selected MP4")
            : descriptor.userOverrideIsExternal
                ? "Local video active: " +
                    descriptor.activeMapFileName.value_or("selected MP4")
            : descriptor.hasUserOverride ? "Downloaded user video active" :
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
        {
            const bool currentUrlWasProbed = thisDownload &&
                !probedUrl_.empty() && url_ == probedUrl_;
            const bool validatedProbe = currentUrlWasProbed &&
                download.state == DownloadState::ProbeCompleted;
            const bool validatedTransferState = currentUrlWasProbed &&
                !download.metadataOnly &&
                (download.state == DownloadState::Preparing ||
                 download.state == DownloadState::Downloading ||
                 download.state == DownloadState::Completed ||
                 download.state == DownloadState::Cancelled ||
                 download.state == DownloadState::Failed);
            const bool showDownloadButton = validatedProbe ||
                validatedTransferState;
            downloadButton_->get_gameObject()->SetActive(showDownloadButton);
            if(downloadButtonPlaceholder_)
                downloadButtonPlaceholder_->SetActive(!showDownloadButton);

            const bool downloadInteractable = showDownloadButton &&
                download.state != DownloadState::Probing;
            downloadButton_->set_interactable(downloadInteractable);

            // The native button prefab uses a slightly muted label even when
            // enabled. Make an actionable download unambiguously bright while
            // retaining a subdued label when there is no valid URL to use.
            if(downloadButtonText_)
                downloadButtonText_->set_color(
                    downloadInteractable
                        ? UnityEngine::Color::get_white()
                        : UnityEngine::Color{0.62f, 0.62f, 0.62f, 1.0f});
        }
        if(checkUrlButton_)
        {
            // The explicit action is primarily for mapper-populated URLs, but
            // it also gives pasted/typed addresses a deterministic retry path.
            // Do not let a second metadata task replace an active transfer.
            checkUrlButton_->set_interactable(
                IsYouTubeUrl(url_) && !(thisDownload && download.Active()));
        }
        for(auto* row : videoOnlyRows_)
            if(row) row->SetActive(descriptor.CanPlay());
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
                // A completed YouTube download atomically replaces any local
                // assignment. Refresh the file rows once so the old filename
                // immediately loses its green active state.
                RefreshLocalVideoFiles();
                previewSongTime_ = 0.0;
                playWhenAudioReady_ = true;
                RequestSelectedAudio();
                StartSelectedPreview();
                if(IsAlive(previewAudioClip_))
                    StartPreviewAudio();
            }
        }
        RefreshPlaybackControls();
    }

    void VideoLibraryMenu::StartSelectedPreview()
    {
        // A timing/display change can rebuild the preview while its audition is
        // running. Pause the owned audio channel before discarding the warmed
        // decoder, then resume only after the replacement session has uploaded
        // its first synchronized picture.
        const bool resumeAfterPrewarm = previewPlaying_;
        if(resumeAfterPrewarm && IsAlive(previewAudioSource_) &&
           IsAlive(previewAudioClip_) && IsAlive(songPreviewPlayer_) &&
           previewAudioSource_->get_clip().unsafePtr() == previewAudioClip_.unsafePtr() &&
           ActiveSongClipMatches(songPreviewPlayer_, previewAudioClip_))
        {
            songPreviewPlayer_->PauseCurrentChannel();
            previewPlaying_ = false;
            previewPaused_ = true;
        }

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
        previewMeasurementStarted_ = false;
        ResetPreviewClock(previewSongTime_);
        if(playback.IsLibraryPreviewActive())
        {
            playback.Tick(previewSongTime_);
            if(resumeAfterPrewarm)
                playWhenVideoReady_ = true;
        }
    }

    void VideoLibraryMenu::RequestSelectedAudio()
    {
        if(!selected_ || !selected_->levelID)
            return;
        const std::string levelId(selected_->levelID);
        if(IsAlive(previewAudioClip_) && audioLoadLevelId_ == levelId)
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

        if(playWhenVideoReady_)
        {
            // A second press while the decoder is preparing is a stop request,
            // just like pressing Pause after ordinary playback has begun.
            playWhenVideoReady_ = false;
            previewPaused_ = true;
            previewClockValid_ = false;
            transientStatus_.clear();
            RefreshPlaybackControls();
            return;
        }

        if(previewPlaying_)
        {
            const bool channelIsStillPlaying = IsAlive(previewAudioSource_) &&
                IsAlive(previewAudioClip_) &&
                previewAudioSource_->get_clip().unsafePtr() == previewAudioClip_.unsafePtr() &&
                previewAudioSource_->get_isPlaying();
            if(channelIsStillPlaying && IsAlive(songPreviewPlayer_) &&
               ActiveSongClipMatches(songPreviewPlayer_, previewAudioClip_))
            {
                songPreviewPlayer_->PauseCurrentChannel();
                previewPlaying_ = false;
                previewPaused_ = true;
                playWhenAudioReady_ = false;
                previewClockValid_ = false;
                RefreshPlaybackControls();
                return;
            }

            // The clip can finish between UI ticks. Normalize that stale
            // "playing" flag here and continue into the normal start path.
            previewPlaying_ = false;
            previewPaused_ = false;
        }

        const double duration = std::max(0.0f, selected_->songDuration);
        if(duration > 0.0 && previewSongTime_ >= duration - 0.01)
            previewSongTime_ = 0.0;

        RequestSelectedAudio();
        if(!IsAlive(previewAudioClip_) || !IsAlive(songPreviewPlayer_))
        {
            playWhenAudioReady_ = true;
            transientStatus_ = "Loading song audio for synchronized preview...";
            RefreshDetails();
            return;
        }

        // Resume the paused Beat Saber channel when it is still ours. If the
        // menu music or another preview reclaimed the channel, rebuild the
        // crossfade at the requested scrub position instead.
        if(previewPaused_ && IsAlive(previewAudioSource_) &&
           previewAudioSource_->get_clip().unsafePtr() == previewAudioClip_.unsafePtr() &&
           ActiveSongClipMatches(songPreviewPlayer_, previewAudioClip_))
        {
            auto& playback = PlaybackSession::Instance();
            if(!playback.IsLibraryPreviewActive())
                StartSelectedPreview();
            if(playback.IsLibraryPreviewActive())
                playback.Tick(previewSongTime_);
            if(!playback.FirstFrameUploaded())
            {
                playWhenVideoReady_ = true;
                playWhenAudioReady_ = false;
                transientStatus_ = "Preparing synchronized video preview...";
                RefreshDetails();
                RefreshPlaybackControls();
                return;
            }
            if(!previewMeasurementStarted_)
            {
                playback.BeginLibraryPreviewMeasurement(previewSongTime_);
                previewMeasurementStarted_ = true;
            }
            previewAudioSource_->set_time(static_cast<float>(previewSongTime_));
            songPreviewPlayer_->UnPauseCurrentChannel();
            ResetPreviewClock(previewSongTime_);
            previewPlaying_ = true;
            previewPaused_ = false;
            playWhenAudioReady_ = false;
            playWhenVideoReady_ = false;
            transientStatus_.clear();
            RefreshPlaybackControls();
            return;
        }
        StartPreviewAudio();
    }

    void VideoLibraryMenu::StartPreviewAudio()
    {
        if(!selected_ || !IsAlive(previewAudioClip_) ||
           !IsAlive(songPreviewPlayer_))
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

        // Match gameplay's decoder-before-audio ordering. Selecting the song
        // normally leaves a warmed LibraryPreview session waiting at this
        // timestamp. If that session was lost, rebuild it now, request/upload
        // the target frame, and keep the transport stopped until it is ready.
        auto& playback = PlaybackSession::Instance();
        if(!playback.IsLibraryPreviewActive())
            StartSelectedPreview();
        if(!playback.IsLibraryPreviewActive())
        {
            playWhenAudioReady_ = false;
            playWhenVideoReady_ = false;
            transientStatus_ = "Video preview could not be prepared.";
            RefreshDetails();
            RefreshPlaybackControls();
            return;
        }
        playback.Tick(previewSongTime_);
        if(!playback.FirstFrameUploaded())
        {
            previewPlaying_ = false;
            playWhenAudioReady_ = false;
            playWhenVideoReady_ = true;
            transientStatus_ = "Preparing synchronized video preview...";
            RefreshDetails();
            RefreshPlaybackControls();
            return;
        }
        if(!previewMeasurementStarted_)
        {
            playback.BeginLibraryPreviewMeasurement(previewSongTime_);
            previewMeasurementStarted_ = true;
        }

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
        if(IsAlive(previewAudioSource_))
            previewAudioSource_->set_time(static_cast<float>(previewSongTime_));
        ResetPreviewClock(previewSongTime_);
        previewPlaying_ = true;
        previewPaused_ = false;
        playWhenAudioReady_ = false;
        playWhenVideoReady_ = false;
        transientStatus_.clear();
        RefreshPlaybackControls();
    }

    void VideoLibraryMenu::LoopPreviewPlayback()
    {
        if(!selected_ || !IsAlive(previewAudioClip_) ||
           !IsAlive(songPreviewPlayer_))
            return;

        // Reset the visible transport before asking Beat Saber to create the
        // next audio crossfade. This prevents one stale end-of-song frame from
        // leaving the scrubber at 100 percent if the new channel starts later
        // in this Unity update.
        previewSongTime_ = 0.0;
        ResetPreviewClock(previewSongTime_);
        previewPlaying_ = false;
        previewPaused_ = false;
        playWhenAudioReady_ = false;
        scrubberFollowResumeTime_ = 0.0f;
        RefreshPlaybackControls();

        // Keep FFmpeg and the Unity screen alive. PlaybackSession uses Beat
        // Saber's audio position as its external clock, so ticking zero makes
        // the decoder worker perform its normal backwards seek without a
        // close/reopen allocation cycle at every loop.
        StartPreviewAudio();
        PaperLogger.info("Looped Video Library preview to the beginning");
    }

    void VideoLibraryMenu::StopPreviewAudio(bool returnToMenuMusic)
    {
        // Clear ownership before calling back into Unity. A flow transition can
        // destroy SongPreviewPlayer, AudioSource, or AudioClip between frames.
        // If CrossfadeToDefault then throws, no later menu tick can see stale
        // media state and attempt to use it again.
        auto player = songPreviewPlayer_;
        auto clip = previewAudioClip_;
        playWhenAudioReady_ = false;
        playWhenVideoReady_ = false;
        previewMeasurementStarted_ = false;
        previewClockValid_ = false;
        previewPlaying_ = false;
        previewPaused_ = false;
        previewAudioSource_ = nullptr;
        previewAudioClip_ = nullptr;
        audioLoadTask_ = nullptr;
        previewMediaData_ = nullptr;
        audioLoadLevelId_.clear();

        try
        {
            if(returnToMenuMusic && player && clip &&
               ActiveSongClipMatches(player, clip))
                player->CrossfadeToDefault();
        }
        catch(const std::exception& error)
        {
            // Teardown must never prevent PlaybackSession::Stop() from joining
            // the FFmpeg worker. Logging is sufficient because the menu is
            // already leaving and showing another dialog here would be unsafe.
            PaperLogger.warn("Could not restore menu music during preview teardown: {}", error.what());
        }

        if(active_ && editorVisible_)
        {
            try { RefreshPlaybackControls(); }
            catch(const std::exception& error)
            {
                PaperLogger.warn("Could not refresh stopped preview controls: {}", error.what());
            }
        }
    }

    void VideoLibraryMenu::RecoverInvalidPreviewAudio(const char* context)
    {
        const bool shouldResume =
            previewPlaying_ || playWhenAudioReady_ || playWhenVideoReady_;
        previewAudioSource_ = nullptr;
        previewAudioClip_ = nullptr;
        audioLoadTask_ = nullptr;
        previewMediaData_ = nullptr;
        audioLoadLevelId_.clear();
        previewPlaying_ = false;
        previewPaused_ = false;
        playWhenAudioReady_ = shouldResume;
        playWhenVideoReady_ = false;
        previewClockValid_ = false;
        transientStatus_ = shouldResume
            ? "Beat Saber replaced the preview audio. Reloading it..."
            : "Beat Saber replaced the preview audio. Press Play to reload it.";
        PaperLogger.warn("Recovered invalid menu preview audio during {}", context);
        ErrorManager::Instance().RecordError(
            "Recovering menu preview audio",
            std::string(context) +
                ": Unity destroyed an AudioClip or AudioSource while its managed wrapper was retained.");
    }

    void VideoLibraryMenu::EnforcePausedPreviewAudio()
    {
        // Android suspends Beat Saber when the Meta system menu opens. On some
        // Quest firmware, Unity resumes an AudioSource as the activity regains
        // focus even when SongPreviewPlayer had explicitly paused that source.
        // Big Screen's transport state remains paused, so without this guard
        // the song can become audible while the video correctly stays frozen.
        if(!editorVisible_ || !previewPaused_ || !IsAlive(previewAudioClip_) ||
           !IsAlive(songPreviewPlayer_))
            return;

        // SongPreviewPlayer rotates through multiple sources while crossfading.
        // Resolve its current source again after an activity resume instead of
        // assuming the pointer captured before opening the Meta menu is still
        // the channel that Beat Saber considers active.
        if(!IsAlive(previewAudioSource_) ||
           previewAudioSource_->get_clip().unsafePtr() != previewAudioClip_.unsafePtr())
            previewAudioSource_ = ActiveSongAudioSource(songPreviewPlayer_);

        const bool bigScreenStillOwnsChannel =
            IsAlive(previewAudioSource_) &&
            previewAudioSource_->get_clip().unsafePtr() == previewAudioClip_.unsafePtr() &&
            ActiveSongClipMatches(songPreviewPlayer_, previewAudioClip_);
        if(!bigScreenStillOwnsChannel || !previewAudioSource_->get_isPlaying())
            return;

        // Restore the exact scrubber position retained when the user pressed
        // Pause, then pause the channel before this frame returns to Unity.
        // This avoids accumulating a small audio-only jump each time the Quest
        // system shell is opened and keeps video/audio aligned on the next Play.
        const float clipEnd = std::max(
            0.0f,
            previewAudioClip_->get_length() - 0.01f);
        previewAudioSource_->set_time(static_cast<float>(std::clamp(
            previewSongTime_,
            0.0,
            static_cast<double>(clipEnd))));
        songPreviewPlayer_->PauseCurrentChannel();
        PaperLogger.info(
            "Restored Big Screen's paused audio-preview state after the audio channel resumed");
    }

    void VideoLibraryMenu::ResetPreviewClock(double songTimeSeconds)
    {
        smoothedPreviewSongTime_ = std::max(0.0, songTimeSeconds);
        previewClockRealtime_ =
            static_cast<double>(UnityEngine::Time::get_realtimeSinceStartup());
        previewClockValid_ = true;
    }

    double VideoLibraryMenu::AdvancePreviewClock(double rawAudioSongTimeSeconds)
    {
        const double now =
            static_cast<double>(UnityEngine::Time::get_realtimeSinceStartup());
        if(!previewClockValid_)
        {
            ResetPreviewClock(rawAudioSongTimeSeconds);
            return smoothedPreviewSongTime_;
        }

        smoothedPreviewSongTime_ = CoreLogic::AdvanceSmoothedPreviewClock(
            smoothedPreviewSongTime_,
            rawAudioSongTimeSeconds,
            now - previewClockRealtime_);
        previewClockRealtime_ = now;
        return smoothedPreviewSongTime_;
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
        ResetPreviewClock(previewSongTime_);

        if(IsAlive(previewAudioSource_) && IsAlive(previewAudioClip_) &&
           previewAudioSource_->get_clip().unsafePtr() == previewAudioClip_.unsafePtr())
        {
            const bool sourceWasPlaying = previewAudioSource_->get_isPlaying();
            const double clipEnd = std::max(
                0.0f,
                previewAudioClip_->get_length() - 0.01f);
            previewAudioSource_->set_time(static_cast<float>(
                std::min(previewSongTime_, clipEnd)));
            if(previewPlaying_ && !sourceWasPlaying)
            {
                // Seeking a naturally completed AudioSource changes its time
                // but does not restart it. Mark the transport ready so the
                // next Play action uses StartPreviewAudio rather than Pause.
                previewPlaying_ = false;
                previewPaused_ = false;
            }
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
                (playWhenAudioReady_ || playWhenVideoReady_) ? "…" :
                    previewPlaying_ ? "Ⅱ" : "▶");
        if(playbackScrubber_)
        {
            const double normalizedPosition = duration > 0.0
                ? std::clamp(previewSongTime_ / duration, 0.0, 1.0)
                : 0.0;
            if(playbackScrubberFill_)
            {
                if(auto fillRect = playbackScrubberFill_->get_transform()
                       .cast<UnityEngine::RectTransform>())
                    fillRect->set_anchorMax({
                        static_cast<float>(normalizedPosition),
                        1.0f});
            }
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

        // Quest recording, activity focus changes, and SongPreviewPlayer
        // crossfades can destroy the native AudioClip/AudioSource while the
        // IL2CPP wrapper remains non-null. Never dereference that stale wrapper
        // and never escalate this recoverable media transition to the global
        // menu circuit breaker.
        if((previewAudioClip_.unsafePtr() && !IsAlive(previewAudioClip_)) ||
           (previewAudioSource_.unsafePtr() && !IsAlive(previewAudioSource_)))
            RecoverInvalidPreviewAudio("menu update");
        if(editorVisible_ && playWhenAudioReady_ &&
           !IsAlive(previewAudioClip_) && !audioLoadTask_)
            RequestSelectedAudio();
        if(!editorVisible_ && ++thumbnailTickCounter_ >= 9)
        {
            thumbnailTickCounter_ = 0;
            RefreshVisibleVideoThumbnails();
        }

        try
        {
          if(editorVisible_ && audioLoadTask_ && audioLoadTask_->get_IsCompleted())
          {
            auto* completedTask = audioLoadTask_;
            audioLoadTask_ = nullptr;
            if(completedTask->get_IsCompletedSuccessfully() && selected_ &&
               selected_->levelID &&
               audioLoadLevelId_ == std::string(selected_->levelID))
            {
                auto clip = completedTask->get_Result();
                previewAudioClip_ =
                    UnityW<UnityEngine::AudioClip>::isAlive(clip.unsafePtr())
                        ? clip.unsafePtr()
                        : nullptr;
                if(!IsAlive(previewAudioClip_))
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

        // SongPreviewPlayer::Update runs immediately before this Tick. That is
        // the point where Unity can revive a source after returning from the
        // Meta shell, so enforce Big Screen's paused transport after Beat Saber
        // has applied its own per-frame audio state.
          EnforcePausedPreviewAudio();

          // Poll the stationary external clock while FFmpeg prepares the first
          // frame. Audio begins only after that picture has reached Unity, so
          // the library session starts from the same ready state as gameplay.
          if(editorVisible_ && playWhenVideoReady_ &&
             IsAlive(previewAudioClip_))
          {
            auto& playback = PlaybackSession::Instance();
            if(playback.IsLibraryPreviewActive())
            {
                playback.Tick(previewSongTime_);
                if(playback.FirstFrameUploaded())
                {
                    playWhenVideoReady_ = false;
                    StartPreviewAudio();
                }
            }
          }

          if(editorVisible_ && previewPlaying_ && IsAlive(previewAudioClip_))
          {
            if(!IsAlive(previewAudioSource_) ||
               previewAudioSource_->get_clip().unsafePtr() != previewAudioClip_.unsafePtr())
                previewAudioSource_ = ActiveSongAudioSource(songPreviewPlayer_);

            if(IsAlive(previewAudioSource_) &&
               previewAudioSource_->get_clip().unsafePtr() == previewAudioClip_.unsafePtr() &&
               previewAudioSource_->get_isPlaying())
            {
                const double rawAudioSongTime = previewAudioSource_->get_time();
                previewSongTime_ = AdvancePreviewClock(rawAudioSongTime);
                if(CoreLogic::PreviewReachedLoopBoundary(
                       rawAudioSongTime,
                       selected_ ? selected_->songDuration : 0.0,
                       previewAudioClip_->get_length()))
                {
                    LoopPreviewPlayback();
                    return;
                }
                if(PlaybackSession::Instance().IsLibraryPreviewActive())
                    PlaybackSession::Instance().Tick(previewSongTime_);
            }
            else
            {
                // AudioSource reports false both for a user-paused channel and
                // for one that naturally exhausted its clip. Explicit pauses
                // are tracked separately; reaching the end must clear that
                // state so Play creates a fresh crossfade after scrubbing back
                // instead of trying to unpause an already-stopped source.
                const bool completedChannelStillBelongsToBigScreen =
                    IsAlive(previewAudioSource_) && IsAlive(songPreviewPlayer_) &&
                    previewAudioSource_->get_clip().unsafePtr() == previewAudioClip_.unsafePtr() &&
                    ActiveSongClipMatches(songPreviewPlayer_, previewAudioClip_);
                if(completedChannelStillBelongsToBigScreen ||
                   CoreLogic::PreviewReachedLoopBoundary(
                       previewSongTime_,
                       selected_ ? selected_->songDuration : 0.0,
                       previewAudioClip_->get_length(),
                       0.10))
                {
                    // The proactive boundary above normally loops before the
                    // source stops. This fallback covers a long Unity frame
                    // that crosses the final sample in one update.
                    LoopPreviewPlayback();
                    return;
                }
                previewPlaying_ = false;
                previewPaused_ = false;
                playWhenAudioReady_ = false;
                previewClockValid_ = false;
            }
            // TMP text, slider layout, and native setting notifications are UI
            // work, not part of the playback clock. Fifteen updates per second
            // remain visually fluid in VR while leaving the main thread free
            // to upload every decoded video picture.
            if(++playbackControlsTickCounter_ >= 6)
            {
                playbackControlsTickCounter_ = 0;
                RefreshPlaybackControls();
            }
          }
        }
        catch(const il2cpp_utils::RunMethodException& error)
        {
            // Unity can invalidate an object between an alive check and the
            // subsequent generated property call. Contain that narrow race in
            // the preview transport: keep the video decoder/menu alive and
            // let the next Tick request a fresh Beat Saber audio clip.
            PaperLogger.warn(
                "Recovered IL2CPP audio-preview race: {}", error.what());
            RecoverInvalidPreviewAudio("an audio property call");
            return;
        }
        if(++tickCounter_ >= 30)
        {
            tickCounter_ = 0;
            // RefreshDetails performs storage/filesystem queries and rewrites
            // most of the editor hierarchy. Keep it event-driven during normal
            // playback; poll only while a download is changing, plus once more
            // to render its terminal state.
            const bool downloadActive =
                DownloadManager::Instance().Snapshot().Active();
            if(editorVisible_ &&
               (downloadActive || periodicDownloadWasActive_))
                RefreshDetails();
            periodicDownloadWasActive_ = downloadActive;
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

    void VideoLibraryMenu::RefreshDisplaySettings()
    {
        if(!editorVisible_ || !selected_ ||
           !VideoLibrary::Instance().Describe(selected_).CanPlay())
            return;

        // Curvature changes the screen mesh, so the active surface must be
        // recreated. Reopen the library preview at its retained song time;
        // sending the change through ScreenPreview would replace the playing
        // frame with that settings preview's checkerboard pattern.
        StartSelectedPreview();
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Tick(previewSongTime_);
    }

    void VideoLibraryMenu::Deactivate()
    {
        active_ = false;
        editorVisible_ = false;
        // Decoder shutdown is deliberately independent from Unity audio and
        // download cleanup. One subsystem throwing must not leave another
        // subsystem's background thread alive across a scene transition.
        try { StopPreviewAudio(true); }
        catch(const std::exception& error)
        {
            PaperLogger.error("Preview audio teardown failed during deactivation: {}", error.what());
        }
        DownloadManager::Instance().Cancel();
        try
        {
            if(PlaybackSession::Instance().IsLibraryPreviewActive())
                PlaybackSession::Instance().Stop();
        }
        catch(const std::exception& error)
        {
            PaperLogger.error("Video decoder teardown failed during deactivation: {}", error.what());
        }
    }

    void VideoLibraryMenu::StopActivePreview()
    {
        // Storage maintenance is a review task, not another song-selection
        // state. Stop only media preview ownership here: downloads remain
        // active and the selected editor state remains intact for when the
        // player closes the storage panel.
        try { StopPreviewAudio(true); }
        catch(const std::exception& error)
        {
            PaperLogger.error("Preview audio teardown failed while leaving the library: {}", error.what());
        }
        try
        {
            if(PlaybackSession::Instance().IsLibraryPreviewActive())
                PlaybackSession::Instance().Stop();
        }
        catch(const std::exception& error)
        {
            PaperLogger.error("Video decoder teardown failed while leaving the library: {}", error.what());
        }
        try { ScreenPreview::Instance().ActivateCurrentState(); }
        catch(const std::exception& error)
        {
            PaperLogger.error("Could not restore the settings preview: {}", error.what());
        }
    }
}
