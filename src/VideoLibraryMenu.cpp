// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/VideoLibraryMenu.hpp"
#include "BigScreen/MenuModal.hpp"
#include "BigScreen/UiSettingsUtility.hpp"
#include "BigScreen/UiUtility.hpp"
#include "BigScreen/Utility.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
#include "BigScreen/DiagnosticSessionLogger.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "GlobalNamespace/AudioClipAsyncLoader.hpp"
#include "GlobalNamespace/AudioClipAsyncLoaderExtensions.hpp"
#include "GlobalNamespace/AudioHelpers.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BeatmapLevelDataVersion.hpp"
#include "GlobalNamespace/BeatmapLevelPack.hpp"
#include "GlobalNamespace/BeatmapLevelsModel.hpp"
#include "GlobalNamespace/BeatmapLevelsRepository.hpp"
#include "GlobalNamespace/IBeatmapLevelData.hpp"
#include "GlobalNamespace/LevelListTableCell.hpp"
#include "GlobalNamespace/LoadBeatmapLevelDataResult.hpp"
#include "GlobalNamespace/IPreviewMediaData.hpp"
#include "GlobalNamespace/PerceivedLoudnessPerLevelModel.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerSensitivityFlag.hpp"
#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "HMUI/InputFieldView.hpp"
#include "HMUI/HoverHint.hpp"
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
#include "UnityEngine/TextAnchor.hpp"
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
        using UiUtility::EnsureLayout;
        using UiUtility::SetToggleWithoutNotification;

        constexpr std::array<std::string_view, 6> FilterNames{
            "Show All Maps", "Custom Maps", "WIP Maps",
            "OST Maps", "DLC Maps", "Maps With Video"
        };
        // Keep the UI slider normalized and translate its position to song
        // time. One thousand positions are comfortably finer than a controller
        // can place the handle in VR and work for songs of any duration.
        constexpr float PreviewScrubIncrement = 0.001f;
        constexpr float PreviewScrubFollowDelay = 0.25f;
        // A quarter second is long enough to build a useful reserve at 60 FPS
        // without making the menu transport feel delayed. This does not sleep
        // or block Unity; Tick continues feeding the stationary decoder clock.
        constexpr double PreviewDecoderPreRollSeconds = 0.25;
        constexpr std::string_view StorageMetricLineHeight = "70%";
        std::string StorageMetricText(
            std::string_view heading,
            std::string_view value)
        {
            // TextMeshPro's line-height tag controls the actual baseline
            // distance used by this font. This is deterministic on-headset,
            // unlike small lineSpacing values that are visually rounded away.
            return "<line-height=" + std::string(StorageMetricLineHeight) + ">" +
                std::string(heading) + "\n" + std::string(value) +
                "</line-height>";
        }
        constexpr std::string_view MapperTimingLockedHint =
            "This timing comes from the map author's Cinema configuration. Download or assign the video before changing it.";
        constexpr std::string_view FitTimingHint =
            "Automatically calculates playback speed so the video ends with the song after Video Playback Offset is applied. Changing the offset recalculates the fitted speed; turning Fit to Song off restores normal 1.00x playback.";
        constexpr std::string_view RateTimingHint =
            "Controls how quickly the video advances. 1.00 is normal speed; lower values slow it down and higher values speed it up. Fit to Song manages this value automatically when enabled.";
        constexpr std::string_view OffsetTimingHint =
            "Aligns the video with the song. Negative values wait before showing video frame zero; positive values begin farther into the video.";
        constexpr std::string_view LeadInTimingHint =
            "Controls the waiting time created by a negative Video Playback Offset. On shows a solid black screen; off keeps the screen hidden until the video begins.";

        void SetBrightButtonLabel(
            UnityEngine::UI::Button* button,
            float fontSize)
        {
            if(!button)
                return;
            BSML::Lite::SetButtonTextSize(button, fontSize);
            if(auto* text = button->get_gameObject()
                   ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
                text->set_color(UnityEngine::Color::get_white());
        }

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

        /// Supplies the timing values that a Video Library download should
        /// inherit. A playable user/mapper assignment is authoritative while
        /// it exists. With no MP4 present, retain the mapper's Cinema timing
        /// as hidden download defaults instead of resetting the request to
        /// zero merely because playback controls are intentionally concealed.
        const MapVideoConfig* EditorTimingConfig(
            const VideoDescriptor& descriptor)
        {
            if(descriptor.playableConfig)
                return &*descriptor.playableConfig;
            if(descriptor.mapperDefinition)
                return &*descriptor.mapperDefinition;
            return nullptr;
        }

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

        std::string TransferFailureNotice(const DownloadSnapshot& transfer)
        {
            const std::string code = transfer.errorCode.empty()
                ? "BS-DL-FAILED-001"
                : transfer.errorCode;
            const auto presentation = CoreLogic::DescribeDownloadFailure(
                code,
                {},
                transfer.metadataOnly);
            return code + ": " + presentation.shortReason;
        }

        std::string ActiveTransferNotice(
            const DownloadSnapshot& transfer,
            bool metadataOnly,
            bool cancellationRequested)
        {
            if(cancellationRequested)
                return metadataOnly
                    ? "Stopping the YouTube link check..."
                    : "Stopping the video download...";
            if(metadataOnly)
                return "Checking YouTube link...";
            if(transfer.state == DownloadState::Preparing &&
               transfer.containerPreparation && transfer.totalBytes > 0)
            {
                const auto percent = static_cast<int>(std::clamp(
                    100.0 * transfer.downloadedBytes / transfer.totalBytes,
                    0.0,
                    100.0));
                return "Preparing video for playback (" +
                    std::to_string(percent) + "%).";
            }
            if(transfer.state != DownloadState::Downloading)
                return "Starting video download...";

            std::ostringstream message;
            message << "Downloading video: "
                    << Utility::FormatMegabytes(transfer.downloadedBytes);
            if(transfer.totalBytes > 0)
            {
                const auto percent = static_cast<int>(std::clamp(
                    100.0 * transfer.downloadedBytes / transfer.totalBytes,
                    0.0,
                    100.0));
                message << " / " << Utility::FormatMegabytes(transfer.totalBytes)
                        << " (" << percent << "%)";
            }
            if(transfer.speedBytesPerSecond > 0.0)
                message << " | " << std::fixed << std::setprecision(1)
                        << transfer.speedBytesPerSecond / 1048576.0 << " MB/s";
            if(transfer.etaSeconds > 0.0)
            {
                const auto seconds = static_cast<int>(transfer.etaSeconds);
                message << " | " << seconds / 60 << ':'
                        << std::setfill('0') << std::setw(2) << seconds % 60
                        << " left";
            }
            return message.str();
        }

        std::string FinishedTransferNotice(
            const DownloadSnapshot& transfer,
            bool metadataOnly)
        {
            if(transfer.state == DownloadState::Failed)
                return TransferFailureNotice(transfer);
            if(transfer.state == DownloadState::Cancelled)
                return metadataOnly
                    ? "YouTube link check cancelled."
                    : "Video download cancelled.";
            if(metadataOnly && transfer.state == DownloadState::ProbeCompleted)
                return "Video download options are ready.";
            if(!metadataOnly && transfer.state == DownloadState::Completed)
                return "Video download complete.";
            return metadataOnly
                ? "YouTube link check finished."
                : "Video downloader task finished.";
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

        bool IsWip(SongCore::SongLoader::CustomBeatmapLevel* level)
        {
            if(!level) return false;
            const auto path = std::filesystem::path(level->get_customLevelPath());
            for(const auto& root : SongCore::API::Loading::GetRootCustomWIPLevelPaths())
                if(Utility::IsPathInside(path, root)) return true;
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

        // Keep the timing controls at their normal interactive height. Native
        // BSML setting rows include unused top/bottom breathing room inside
        // that height, so a small negative group spacing removes only that
        // visible dead space. Shrinking the rows themselves clips labels,
        // arrows, switches, and handles.
        constexpr float TimingControlHeight = 8.0f;
        constexpr float TimingControlSpacing = -2.0f;
        constexpr float TimingControlCount = 4.0f;

        void EnforceTimingControlHeight(UnityEngine::Component* component)
        {
            ConfigureLayout(
                component, -1.0f, TimingControlHeight, 1.0f);
            if(auto* layout = EnsureLayout(component))
                layout->set_minHeight(TimingControlHeight);
        }

        void StyleToggleRow(
            BSML::ToggleSetting* setting,
            std::string_view label)
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

            // ToggleSetting is cloned from Beat Saber's full-width settings
            // screen. In this narrower side controller, its NameText can wind
            // up behind the custom background or outside the row's mask.
            // Explicitly restore the label after styling and render it above
            // the background while retaining the native switch on the right.
            if(setting->text)
            {
                setting->text->set_text(std::string(label));
                setting->text->set_color(UnityEngine::Color::get_white());
                setting->text->set_enableWordWrapping(false);
                setting->text->set_enableAutoSizing(true);
                setting->text->set_fontSizeMin(2.2f);
                setting->text->set_fontSizeMax(3.0f);
                setting->text->set_maxVisibleLines(1);
                setting->text->set_overflowMode(
                    TMPro::TextOverflowModes::Ellipsis);
                setting->text->set_alignment(
                    TMPro::TextAlignmentOptions::MidlineLeft);
                setting->text->get_transform()->SetAsLastSibling();
            }
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
        FailedVideoThumbnailLoads.clear();
        RowVideoThumbnails.clear();
        VideoThumbnailUseCounter = 0;
        try
        {
            if(UnityW<UnityEngine::Sprite>::isAlive(loadedThumbnailSprite_))
                UnityEngine::Object::Destroy(loadedThumbnailSprite_);
        }
        catch(...)
        {
            // The prior menu scene may already own/destroy the sprite.
        }
        *this = VideoLibraryMenu{};
    }

    void VideoLibraryMenu::CreateUi(
        HMUI::ViewController* browserController,
        HMUI::ViewController* editorController,
        std::function<void(bool showEditor)> navigate,
        std::function<void(GlobalNamespace::BeatmapLevel*)> browseLocalVideo,
        std::function<void(GlobalNamespace::BeatmapLevel*)> openThumbnailPicker,
        bool activate)
    {
        if(!browserController || !editorController)
            return;
        browserController_ = browserController;
        editorController_ = editorController;
        navigate_ = std::move(navigate);
        browseLocalVideo_ = std::move(browseLocalVideo);
        openThumbnailPicker_ = std::move(openThumbnailPicker);
        timingRows_.clear();
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
            }
        }
        auto* numericButton = BSML::Lite::CreateClickableText(
            alphabet, "#", TMPro::FontStyles::Normal, 2.35f,
            {0.0f, 0.0f}, {3.5f, 3.35f}, [this]() { JumpToLetter('#'); });
        numericButton->set_alignment(TMPro::TextAlignmentOptions::Center);

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
                [this](int row)
                {
                    // Keep a Unity/IL2CPP failure inside Big Screen's normal
                    // recovery boundary. If this exception escapes, the
                    // custom-types delegate wrapper aborts the entire game.
                    ErrorManager::Instance().Guard(
                        "opening a Video Library song",
                        [this, row]() { SelectRow(row); });
                });
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
                            // This is the normal native-list path. Match the
                            // fallback list's exception boundary so a stale
                            // Unity object cannot escape through BSML and turn
                            // a recoverable menu failure into SIGABRT.
                            ErrorManager::Instance().Guard(
                                "opening a Video Library song",
                                [this, row]() { SelectRow(row); });
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

        auto* editorHeader = BSML::Lite::CreateHorizontalLayoutGroup(editorRoot);
        ConfigureGroup(editorHeader);
        editorHeader->set_spacing(0.8f);
        ConfigureLayout(editorHeader, 54.0f, 7.0f, 1.0f);
        backToListButton_ = BSML::Lite::CreateUIButton(
            editorHeader, "< Back to Song List", {0.0f, 0.0f}, {45.0f, 7.0f}, [this]() { ShowBrowser(); });
        ConfigureLayout(backToListButton_, 45.0f, 7.0f, 0.0f);
        BSML::Lite::SetButtonTextSize(backToListButton_, 3.1f);
        mapperRefreshButton_ = BSML::Lite::CreateUIButton(
            editorHeader,
            "↻",
            {0.0f, 0.0f},
            {8.2f, 7.0f},
            [this]()
            {
                ErrorManager::Instance().Guard(
                    "refreshing mapper video settings",
                    [this]() { RefreshSelectedMapperMetadata(); });
            });
        ConfigureLayout(mapperRefreshButton_, 8.2f, 7.0f, 0.0f);
        // Match the reset glyphs used beside Screen Layout and Performance.
        // Only the symbol grows; the established compact header button and
        // Back to Song List spacing remain unchanged.
        SetBrightButtonLabel(mapperRefreshButton_, 6.0f);
        BSML::Lite::AddHoverHint(
            mapperRefreshButton_,
            "Reloads this map's Cinema JSON or playlist metadata from Quest storage. Use this after copying an edited file back to the Quest.");

        // Place editor rows directly on the full-panel root. The BSML
        // ScrollView template carries its own narrow viewport geometry, which
        // cannot inherit the complete right-side panel width reliably. The
        // complete form fits in this controller, so an intermediate viewport
        // only reduces usable space without providing a layout benefit.
        const BSML::Lite::TransformWrapper editorBody(editorRoot);

        // Keep song and artist on one compact line. The right-side controller
        // has a fixed visible height, so this recovers room for the always-
        // visible Video Storage group without introducing a scroll container.
        auto* titleTopSpacer = BSML::Lite::CreateText(editorBody, "", 1.0f);
        ConfigureLayout(titleTopSpacer, -1.0f, 0.2f, 1.0f);
        auto* titleActionRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
        ConfigureGroup(titleActionRow);
        titleActionRow->set_spacing(0.7f);
        ConfigureLayout(titleActionRow, 54.0f, 5.8f, 1.0f);
        if(auto* rowLayout = EnsureLayout(titleActionRow))
        {
            rowLayout->set_minWidth(54.0f);
            rowLayout->set_minHeight(5.8f);
        }

        // The title consumes all space not reserved for the fixed search
        // action and auto-sizes before masking long song/artist combinations.
        detailTitle_ = BSML::Lite::CreateText(
            titleActionRow, "", 3.3f);
        ConfigureLayout(detailTitle_, 0.0f, 5.8f, 1.0f);
        detailTitle_->set_enableWordWrapping(false);
        detailTitle_->set_enableAutoSizing(true);
        detailTitle_->set_fontSizeMin(2.2f);
        detailTitle_->set_fontSizeMax(3.3f);
        detailTitle_->set_overflowMode(TMPro::TextOverflowModes::Masking);
        detailTitle_->set_maxVisibleLines(1);

        searchYouTubeButton_ = BSML::Lite::CreateUIButton(
            titleActionRow,
            "Search YouTube",
            {0.0f, 0.0f},
            {16.5f, 5.7f},
            [this]() { SearchSelectedSongOnYouTube(); });
        ConfigureLayout(searchYouTubeButton_, 16.5f, 5.7f, 0.0f);
        // It is created after the title for straightforward pointer setup;
        // reorder it inside the horizontal group so the action occupies the
        // left edge and the song/artist block receives the remaining width.
        searchYouTubeButton_->get_transform()->SetAsFirstSibling();
        SetBrightButtonLabel(searchYouTubeButton_, 2.2f);
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
        ConfigureLayout(titleBottomSpacer, -1.0f, 0.3f, 1.0f);

        // Keep local-video management to one compact row in the child editor.
        // Browsing is intentionally moved to the wide center screen so long
        // filenames and folder navigation do not resize or crowd this panel.
        auto* localRow = BSML::Lite::CreateHorizontalLayoutGroup(editorBody);
        ConfigureGroup(localRow);
        localRow->set_spacing(0.7f);
        ConfigureLayout(localRow, 54.0f, 8.0f, 1.0f);
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
        SetBrightButtonLabel(showFileBrowserButton_, 2.25f);
        BSML::Lite::AddHoverHint(
            showFileBrowserButton_,
            "Opens the Quest file browser at this map's folder. Built-in songs start in Big Screen's Video Import folder. You can navigate anywhere in shared storage and assign an 8-bit SDR H.264/H.265 MP4 or VP8/VP9 WebM video up to 1440p.");
        setThumbnailButton_ = BSML::Lite::CreateUIButton(
            localRow,
            "Set Thumbnail",
            {0.0f, 0.0f},
            {18.0f, 7.5f},
            [this]()
            {
                if(selected_ && openThumbnailPicker_)
                    openThumbnailPicker_(selected_);
            });
        ConfigureLayout(setThumbnailButton_, 18.0f, 7.5f, 0.0f);
        SetBrightButtonLabel(setThumbnailButton_, 2.25f);
        BSML::Lite::AddHoverHint(
            setThumbnailButton_,
            "Opens a frame picker for this map's local video. Scrub to any moment, step frame by frame, and save that exact frame as the map's thumbnail. It can be re-picked at any time.");
        localVideoStatusText_ = BSML::Lite::CreateText(
            localRow, "", 2.45f);
        ConfigureLayout(localVideoStatusText_, 0.0f, 7.5f, 1.0f);
        localVideoStatusText_->set_alignment(
            TMPro::TextAlignmentOptions::MidlineLeft);
        localVideoStatusText_->set_enableWordWrapping(false);
        localVideoStatusText_->set_overflowMode(
            TMPro::TextOverflowModes::Ellipsis);
        localVideoStatusText_->get_gameObject()->SetActive(false);

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
        SetBrightButtonLabel(pasteUrlButton_, 2.45f);
        urlInput_ = BSML::Lite::CreateStringSetting(
            urlEntryRow, "YouTube URL", "", [this](StringW value) {
                url_ = Trim(std::string(value));
                if(!suppressUrlCallback_)
                {
                    terminalDownloadProgressLevelId_.clear();
                    mapperProvidedUrl_ = false;
                    RefreshUrlTextColor();
                    BeginUrlProbe();
                }
            });
        ConfigureLayout(urlInput_, 0.0f, 8.0f, 1.0f);
        urlInputText_ = urlInput_->__cordl_internal_get__textView().ptr();
        RefreshUrlTextColor();
        checkUrlButton_ = BSML::Lite::CreateUIButton(
            urlEntryRow,
            "Check",
            {0.0f, 0.0f},
            {10.0f, 7.5f},
            [this]() { BeginUrlProbe(); });
        ConfigureLayout(checkUrlButton_, 10.0f, 7.5f, 0.0f);
        SetBrightButtonLabel(checkUrlButton_, 2.35f);
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
        urlPreviewRow->set_childAlignment(UnityEngine::TextAnchor::MiddleLeft);
        ConfigureLayout(urlPreviewRow, -1.0f, 10.5f, 1.0f);
        if(auto* previewRowLayout = EnsureLayout(urlPreviewRow))
            previewRowLayout->set_minHeight(10.5f);
        urlThumbnail_ = BSML::Lite::CreateImage(
            urlPreviewRow,
            BSML::Utilities::ImageResources::GetBlankSprite());
        // Keep the established preview artwork large enough to identify the
        // video at a glance. Resolution choices consume horizontal space, not
        // the thumbnail's height or aspect ratio.
        ConfigureLayout(urlThumbnail_, 17.0f, 9.2f, 0.0f);
        urlThumbnail_->set_color({0.08f, 0.10f, 0.13f, 0.85f});
        urlThumbnail_->set_preserveAspect(true);

        // The probe can expose as many as four exact source tiers. Give the
        // entire remaining row to those choices instead of retaining the old
        // single centered Download Video button and decorative balance image.
        // This is the visible half of the explicit-resolution contract: the
        // global output limiter never filters these source download choices.
        // Keep all four choices in one row, but use an explicit action verb so
        // the values cannot be mistaken for passive metadata. The wider native
        // backgrounds and single-line labels remain readable on Quest.
        auto* downloadChoices = BSML::Lite::CreateHorizontalLayoutGroup(urlPreviewRow);
        ConfigureGroup(downloadChoices);
        downloadChoices->set_spacing(0.8f);
        downloadChoices->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        ConfigureLayout(downloadChoices, 0.0f, 9.2f, 1.0f);
        downloadTierButtons_.clear();
        displayedDownloadHeights_.clear();
        for(std::size_t index = 0; index < 4; ++index)
        {
            auto* button = BSML::Lite::CreateUIButton(
                downloadChoices,
                "DOWNLOAD 480p",
                {0.0f, 0.0f},
                {19.0f, 7.5f},
                [this, index]() { DownloadResolutionPressed(index); });
            // Every visible choice is equally flexible, so one to four
            // buttons always share the whole row at one uniform size: the
            // widest label ("DOWNLOAD 1440p") sets the floor, and any
            // remaining row width is distributed evenly instead of leaving
            // the group cramped against the thumbnail with clipped text.
            ConfigureLayout(button, 19.0f, 7.5f, 1.0f);
            if(auto* buttonLayout = EnsureLayout(button))
                buttonLayout->set_minWidth(19.0f);
            SetBrightButtonLabel(button, 2.9f);
            if(auto* tierText = button->get_gameObject()
                   ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            {
                tierText->set_enableWordWrapping(false);
                tierText->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
                // Never render a truncated price-list of resolutions: if a
                // label would still overflow its equal share, shrink the font
                // slightly instead of cutting off the trailing "p".
                tierText->set_enableAutoSizing(true);
                tierText->set_fontSizeMin(2.1f);
                tierText->set_fontSizeMax(2.9f);
            }
            BSML::Lite::AddHoverHint(
                button,
                "Downloads this exact resolution and assigns it to the selected song. Big Screen plays the selected file at its native resolution.");
            button->get_gameObject()->SetActive(false);
            downloadTierButtons_.push_back(button);
        }
        downloadButton_ = BSML::Lite::CreateUIButton(
            downloadChoices, "Download Video", {0.0f, 0.0f}, {20.0f, 7.5f},
            [this]() { StartOrCancelDownload(); });
        ConfigureLayout(downloadButton_, 20.0f, 7.5f, 0.0f);
        SetBrightButtonLabel(downloadButton_, 2.45f);
        downloadButtonText_ = downloadButton_->get_gameObject()
            ->GetComponentInChildren<TMPro::TextMeshProUGUI*>();
        BSML::Lite::AddHoverHint(
            downloadButton_,
            "Downloads the selected source tier and assigns it to this song. Resolutions through 1080p use H.264 MP4; 1440p uses VP9 WebM and requires hardware decoding. While active, the same button can pause the download.");
        downloadButton_->get_gameObject()->SetActive(false);
        // Preserve the thumbnail/action row geometry while the real button is
        // hidden. Swapping this transparent slot out when validation succeeds
        // prevents the thumbnail from jumping sideways as the button appears.
        auto* downloadButtonPlaceholder = BSML::Lite::CreateImage(
            downloadChoices,
            BSML::Utilities::ImageResources::GetBlankSprite());
        downloadButtonPlaceholder->set_color({0.0f, 0.0f, 0.0f, 0.0f});
        downloadButtonPlaceholder->set_raycastTarget(false);
        downloadButtonPlaceholder_ = downloadButtonPlaceholder->get_gameObject();
        BSML::Lite::AddHoverHint(
            urlInput_,
            "Enter a normal youtube.com video address or a youtu.be Share link. Big Screen checks the link before the Download Video button appears.");
        BSML::Lite::AddHoverHint(
            pasteUrlButton_,
            "Pastes a YouTube address from the Quest clipboard and checks whether the video can be downloaded.");

        downloadConfirmModal_ = BSML::Lite::CreateModal(
            editorController,
            {76.0f, 42.0f},
            [this]() {
                pendingDownloadHeight_ = 0;
                DiagnosticSessionLogger::Instance().DownloadEvent(
                    "resolution_cancelled", "VideoLibraryMenu");
                DiagnosticSessionLogger::Instance().EndDownloadSession(
                    "cancelled_before_transfer");
            },
            true);
        downloadConfirmationText_ = BSML::Lite::CreateText(
            downloadConfirmModal_,
            "",
            TMPro::FontStyles::Normal,
            3.0f,
            {0.0f, 5.0f},
            {68.0f, 24.0f});
        downloadConfirmationText_->set_enableWordWrapping(true);
        downloadConfirmationText_->set_enableAutoSizing(true);
        downloadConfirmationText_->set_fontSizeMin(2.45f);
        downloadConfirmationText_->set_fontSizeMax(3.0f);
        downloadConfirmationText_->set_overflowMode(
            TMPro::TextOverflowModes::Ellipsis);
        downloadConfirmationText_->set_alignment(
            TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            downloadConfirmModal_->get_transform(),
            "Cancel",
            {20.0f, -31.0f},
            {23.0f, 8.0f},
            [this]()
            {
                pendingDownloadHeight_ = 0;
                DiagnosticSessionLogger::Instance().DownloadEvent(
                    "resolution_cancelled", "VideoLibraryMenu");
                DiagnosticSessionLogger::Instance().EndDownloadSession(
                    "cancelled_before_transfer");
                if(downloadConfirmModal_)
                    downloadConfirmModal_->Hide();
            });
        confirmDownloadButton_ = BSML::Lite::CreateUIButton(
            downloadConfirmModal_->get_transform(),
            "Download",
            {54.0f, -31.0f},
            {29.0f, 8.0f},
            [this]() { ConfirmPendingResolutionDownload(); });

        // Only this fixed-height host belongs to the long-lived controller.
        // Each selected-map visit creates its own TextMeshPro child inside the
        // host and destroys that child on exit. A CanvasRenderer mesh from one
        // map therefore cannot reappear when another map changes the layout.
        operationStatusHost_ =
            BSML::Lite::CreateVerticalLayoutGroup(editorBody);
        ConfigureGroup(operationStatusHost_);
        operationStatusHost_->set_childForceExpandWidth(true);
        ConfigureLayout(operationStatusHost_, -1.0f, 5.5f, 1.0f);
        if(auto* statusHostLayout = EnsureLayout(operationStatusHost_))
            statusHostLayout->set_minHeight(5.5f);

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

        // Keep all four synchronization settings in one full-width group. The
        // group overlaps the unused row margins while each native control
        // retains its complete height. This is intentionally different from the old
        // one-control wrapper that caused Fit to Song to size differently.
        auto* timingControlsGroup =
            BSML::Lite::CreateVerticalLayoutGroup(editorBody);
        ConfigureGroup(timingControlsGroup);
        timingControlsGroup->set_spacing(TimingControlSpacing);
        timingControlsGroup->set_childForceExpandWidth(true);
        const float timingControlsHeight =
            (TimingControlHeight * TimingControlCount) +
            (TimingControlSpacing * (TimingControlCount - 1.0f));
        ConfigureLayout(timingControlsGroup, -1.0f, timingControlsHeight, 1.0f);
        if(auto* timingLayout = EnsureLayout(timingControlsGroup))
            timingLayout->set_minHeight(timingControlsHeight);
        const BSML::Lite::TransformWrapper timingControlsBody(timingControlsGroup);

        fitToggle_ = BSML::Lite::CreateToggle(
            timingControlsBody,
            "Fit to Song",
            false,
            [this](bool enabled)
            {
                if(suppressTimingCallbacks_) return;
                fitToSong_ = enabled;
                if(enabled)
                {
                    if(!ApplyFitToSong())
                    {
                        fitToSong_ = false;
                        suppressTimingCallbacks_ = true;
                        SetToggleWithoutNotification(fitToggle_, false);
                        suppressTimingCallbacks_ = false;
                    }
                }
                else
                {
                    // Fit to Song owns the calculated rate. Returning to manual
                    // mode must start from the neutral 1.00x baseline rather
                    // than leaving a fitted value such as 0.98x that makes the
                    // next 0.05 arrow step land on an unexpected 1.03x.
                    rate_ = 1.0;
                    if(!SaveTiming())
                        return;
                    suppressTimingCallbacks_ = true;
                    if(rateSetting_) rateSetting_->set_Value(1.0f);
                    suppressTimingCallbacks_ = false;
                    terminalDownloadProgressLevelId_.clear();
                    StartSelectedPreview();
                    RefreshDetails();
                    PublishEditorNotice(
                        "Fit to Song disabled. Playback speed reset to 1.00x.");
                }
            });
        EnforceTimingControlHeight(fitToggle_);
        StyleToggleRow(fitToggle_, "Fit to Song");
        fitTimingHint_ = BSML::Lite::AddHoverHint(
            fitToggle_,
            std::string(FitTimingHint));

        rateSetting_ = BSML::Lite::CreateIncrementSetting(
            timingControlsBody, "Playback Speed", 2, 0.05f, 1.0f,
            0.05f, 8.0f, {0, 0}, [this](float value) {
                if(suppressTimingCallbacks_) return;
                rate_ = value;
                if(SaveTiming())
                {
                    terminalDownloadProgressLevelId_.clear();
                    StartSelectedPreview();
                    RefreshDetails();
                    std::ostringstream message;
                    message << std::fixed << std::setprecision(2)
                            << "Playback speed saved: " << rate_ << "x.";
                    PublishEditorNotice(message.str());
                }
            });
        EnforceTimingControlHeight(rateSetting_);
        rateTimingHint_ = BSML::Lite::AddHoverHint(
            rateSetting_,
            std::string(RateTimingHint));

        offsetSetting_ = BSML::Lite::CreateIncrementSetting(
            timingControlsBody, "Video Playback Offset", 2, 0.25f, 0.0f,
            -60.0f, 60.0f, {0, 0}, [this](float value) {
                if(suppressTimingCallbacks_) return;
                offset_ = value;
                if(fitToSong_)
                    ApplyFitToSong();
                else if(SaveTiming())
                {
                    terminalDownloadProgressLevelId_.clear();
                    StartSelectedPreview();
                    RefreshDetails();
                    std::ostringstream message;
                    message << std::fixed << std::setprecision(2);
                    if(offset_ < 0.0)
                        message << "Video delayed " << -offset_
                                << " seconds; lead-in is "
                                << (blackDuringLeadIn_ ? "black" : "transparent")
                                << '.';
                    else if(offset_ > 0.0)
                        message << "Video skips " << offset_
                                << " seconds at song start.";
                    else
                        message << "Video starts with the song.";
                    PublishEditorNotice(message.str());
                }
            });
        EnforceTimingControlHeight(offsetSetting_);
        offsetTimingHint_ = BSML::Lite::AddHoverHint(
            offsetSetting_,
            std::string(OffsetTimingHint));

        blackLeadInToggle_ = BSML::Lite::CreateToggle(
            timingControlsBody,
            "Lead-In Background",
            false,
            [this](bool enabled)
            {
                if(suppressTimingCallbacks_) return;
                blackDuringLeadIn_ = enabled;
                if(SaveTiming())
                {
                    terminalDownloadProgressLevelId_.clear();
                    StartSelectedPreview();
                    RefreshDetails();
                    PublishEditorNotice(enabled
                        ? "Lead-in background set to black."
                        : "Lead-in background set to transparent.");
                }
            });
        EnforceTimingControlHeight(blackLeadInToggle_);
        StyleToggleRow(blackLeadInToggle_, "Lead-In Background");
        leadInTimingHint_ = BSML::Lite::AddHoverHint(
            blackLeadInToggle_,
            std::string(LeadInTimingHint));
        // RefreshDetails hides the complete group so its reserved height also
        // collapses when no downloaded or local video is available.
        timingRows_.push_back(timingControlsGroup->get_gameObject());

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
        ConfigureLayout(playbackPanel, -1.0f, 12.5f, 1.0f);
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

        // Preserve the original title-row allocation so the playback controls
        // below do not move, but render the title independently across the
        // panel's top edge. Roughly two thirds of the glyph height sits above
        // the rounded background and one third overlaps it like a section tab.
        auto* playbackTitleSpacer = BSML::Lite::CreateText(
            playbackBody, "", 1.0f);
        // Fix the top allocation at the measured on-headset position. The
        // flexible spacer below Playback absorbs any parent height variation,
        // preventing Unity from expanding this spacer and undoing the offset.
        ConfigureLayout(playbackTitleSpacer, -1.0f, 3.60f, 0.0f);
        auto* playbackGroupTitle = BSML::Lite::CreateText(
            playbackPanel->get_transform(),
            "Playback Position",
            3.0f);
        if(auto* titleLayout = EnsureLayout(playbackGroupTitle))
            titleLayout->set_ignoreLayout(true);
        playbackGroupTitle->set_alignment(TMPro::TextAlignmentOptions::Center);
        playbackGroupTitle->set_enableWordWrapping(false);
        if(auto titleRect = playbackGroupTitle->get_transform()
               .cast<UnityEngine::RectTransform>())
        {
            titleRect->set_anchorMin({0.0f, 1.0f});
            titleRect->set_anchorMax({1.0f, 1.0f});
            titleRect->set_pivot({0.5f, 0.5f});
            titleRect->set_anchoredPosition({0.0f, 0.5f});
            titleRect->set_sizeDelta({-2.0f, 3.0f});
            titleRect->SetAsLastSibling();
        }

        auto* playbackRow = BSML::Lite::CreateHorizontalLayoutGroup(playbackBody);
        ConfigureGroup(playbackRow);
        // Preserve the established left/right edge padding and transport gap.
        // Only the visual track below is made thinner; changing this row was
        // what shifted the Play button and the scrubber's right edge.
        playbackRow->set_spacing(1.25f);
        playbackRow->set_padding(UnityEngine::RectOffset::New_ctor(1, 1, 0, 0));
        // The transport button is shorter than its row. Center every child on
        // the row's vertical axis so the button sits evenly within the rounded
        // Playback Position background instead of hugging one edge.
        playbackRow->set_childAlignment(UnityEngine::TextAnchor::MiddleLeft);
        ConfigureLayout(playbackRow, -1.0f, 7.8f, 1.0f);
        auto* playbackBottomSpacer = BSML::Lite::CreateText(
            playbackBody, "", 1.0f);
        ConfigureLayout(playbackBottomSpacer, -1.0f, 0.0f, 1.0f, 1.0f);
        // Keep the row and scrubber untouched. A dedicated transport column
        // uses asymmetric fractional spacers around the 5.8-unit button. The
        // 0.75 top / 1.25 bottom split fills the 7.8-unit row exactly and moves
        // only the button 0.25 units above its mathematically centered position.
        auto* transportColumn = BSML::Lite::CreateVerticalLayoutGroup(playbackRow);
        ConfigureGroup(transportColumn);
        transportColumn->set_spacing(0.0f);
        transportColumn->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        ConfigureLayout(transportColumn, 8.5f, 7.8f, 0.0f);
        auto* transportTopSpacer = BSML::Lite::CreateText(transportColumn, "", 1.0f);
        ConfigureLayout(transportTopSpacer, 8.5f, 0.75f, 0.0f);
        playPauseButton_ = BSML::Lite::CreateUIButton(
            transportColumn,
            "▶",
            "PlayButton",
            {0.0f, 0.0f},
            {8.5f, 6.0f},
            [this]() { TogglePreviewPlayback(); });
        // The stock button sprite extends slightly beyond its LayoutElement.
        // Leave enough inset for that visual edge to remain inside the rounded
        // playback panel while retaining a generous VR pointer target.
        ConfigureLayout(playPauseButton_, 8.5f, 5.8f, 0.0f);
        auto* transportBottomSpacer = BSML::Lite::CreateText(transportColumn, "", 1.0f);
        ConfigureLayout(transportBottomSpacer, 8.5f, 1.25f, 0.0f);
        BSML::Lite::SetButtonTextSize(playPauseButton_, 3.6f);
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

        // Removing a video offers two deliberately distinct operations. The
        // safe Unlink action changes only library metadata; Delete additionally
        // removes the active physical file. Keeping both behind one modal makes
        // the destructive difference explicit without adding another permanent
        // button to an already dense storage row.
        removeConfirmModal_ = BSML::Lite::CreateModal(
            editorController,
            {72.0f, 38.0f},
            nullptr,
            true);
        removeConfirmationText_ = BSML::Lite::CreateText(
            removeConfirmModal_,
            "Remove this assigned video?",
            TMPro::FontStyles::Normal,
            3.3f,
            {0.0f, 4.0f},
            {64.0f, 22.0f});
        removeConfirmationText_->set_enableWordWrapping(true);
        removeConfirmationText_->set_enableAutoSizing(true);
        removeConfirmationText_->set_fontSizeMin(2.9f);
        removeConfirmationText_->set_fontSizeMax(3.3f);
        removeConfirmationText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        removeConfirmationText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        auto* cancelRemoveButton = BSML::Lite::CreateUIButton(
            removeConfirmModal_->get_transform(),
            "Cancel",
            {10.0f, -31.0f},
            {18.0f, 8.0f},
            [this]()
            {
                if(removeConfirmModal_)
                    removeConfirmModal_->Hide();
                pendingLocalDeleteLevelId_.clear();
                pendingLocalDeletePath_.clear();
            });
        ConfigureLayout(cancelRemoveButton, 18.0f, 8.0f, 0.0f);
        unlinkVideoButton_ = BSML::Lite::CreateUIButton(
            removeConfirmModal_->get_transform(),
            "Unlink",
            {36.0f, -31.0f},
            {18.0f, 8.0f},
            [this]()
            {
                if(removeConfirmModal_)
                    removeConfirmModal_->Hide();
                RemoveOverride(false);
            });
        ConfigureLayout(unlinkVideoButton_, 18.0f, 8.0f, 0.0f);
        BSML::Lite::SetButtonTextSize(unlinkVideoButton_, 2.3f);
        deleteVideoButton_ = BSML::Lite::CreateUIButton(
            removeConfirmModal_->get_transform(),
            "<color=#FF3838>Delete File</color>",
            {62.0f, -31.0f},
            {18.0f, 8.0f},
            [this]()
            {
                if(removeConfirmModal_)
                    removeConfirmModal_->Hide();
                // A download can simply be fetched again, so it keeps the
                // established single confirmation. A local file is the user's
                // own irreplaceable MP4/WebM: interpose one explicit warning
                // naming the file before anything touches the filesystem.
                const auto descriptor = selected_
                    ? VideoLibrary::Instance().Describe(selected_)
                    : VideoDescriptor{};
                const bool activeLocalFile =
                    descriptor.userOverrideIsMapLocal ||
                    descriptor.userOverrideIsImported ||
                    descriptor.userOverrideIsExternal ||
                    (!descriptor.hasUserOverride &&
                     descriptor.hasMapperLocalFile);
                if(activeLocalFile && deleteLocalConfirmModal_)
                {
                    pendingLocalDeleteLevelId_ = selected_ && selected_->levelID
                        ? std::string(selected_->levelID) : std::string{};
                    pendingLocalDeletePath_ = descriptor.playableConfig
                        ? descriptor.playableConfig->videoPath.lexically_normal()
                        : std::filesystem::path{};
                    if(deleteLocalConfirmText_)
                        deleteLocalConfirmText_->set_text(
                            "Permanently delete this video file from your Quest?\n\n" +
                            descriptor.activeMapFileName.value_or(
                                "The assigned local video") +
                            "\n\nThe file will be gone for good - Big Screen cannot restore it and it is not re-downloadable.");
                    ShowModalInFront(deleteLocalConfirmModal_);
                    return;
                }
                RemoveOverride(true);
            });
        ConfigureLayout(deleteVideoButton_, 18.0f, 8.0f, 0.0f);
        BSML::Lite::SetButtonTextSize(deleteVideoButton_, 2.3f);
        if(auto* confirmText = deleteVideoButton_->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            confirmText->set_color({1.0f, 0.22f, 0.22f, 1.0f});

        // Final safeguard for the only unrecoverable choice in this menu.
        deleteLocalConfirmModal_ = BSML::Lite::CreateModal(
            editorController,
            {72.0f, 38.0f},
            nullptr,
            true);
        deleteLocalConfirmText_ = BSML::Lite::CreateText(
            deleteLocalConfirmModal_,
            "Permanently delete this video file from your Quest?",
            TMPro::FontStyles::Normal,
            3.3f,
            {0.0f, 4.0f},
            {64.0f, 22.0f});
        deleteLocalConfirmText_->set_enableWordWrapping(true);
        deleteLocalConfirmText_->set_enableAutoSizing(true);
        deleteLocalConfirmText_->set_fontSizeMin(2.7f);
        deleteLocalConfirmText_->set_fontSizeMax(3.3f);
        deleteLocalConfirmText_->set_overflowMode(
            TMPro::TextOverflowModes::Ellipsis);
        deleteLocalConfirmText_->set_alignment(
            TMPro::TextAlignmentOptions::Center);
        auto* keepLocalFileButton = BSML::Lite::CreateUIButton(
            deleteLocalConfirmModal_->get_transform(),
            "Keep the File",
            {14.0f, -31.0f},
            {21.0f, 8.0f},
            [this]()
            {
                if(deleteLocalConfirmModal_)
                    deleteLocalConfirmModal_->Hide();
                pendingLocalDeleteLevelId_.clear();
                pendingLocalDeletePath_.clear();
            });
        ConfigureLayout(keepLocalFileButton, 21.0f, 8.0f, 0.0f);
        BSML::Lite::SetButtonTextSize(keepLocalFileButton, 2.3f);
        auto* confirmDeleteLocalButton = BSML::Lite::CreateUIButton(
            deleteLocalConfirmModal_->get_transform(),
            "<color=#FF3838>Delete Forever</color>",
            {44.0f, -31.0f},
            {21.0f, 8.0f},
            [this]()
            {
                if(deleteLocalConfirmModal_)
                    deleteLocalConfirmModal_->Hide();
                const auto current = selected_
                    ? VideoLibrary::Instance().Describe(selected_)
                    : VideoDescriptor{};
                const bool sameAssignment = selected_ && selected_->levelID &&
                    std::string(selected_->levelID) == pendingLocalDeleteLevelId_ &&
                    current.playableConfig &&
                    current.playableConfig->videoPath.lexically_normal() ==
                        pendingLocalDeletePath_;
                pendingLocalDeleteLevelId_.clear();
                pendingLocalDeletePath_.clear();
                if(!sameAssignment)
                {
                    terminalDownloadProgressLevelId_.clear();
                    RefreshDetails();
                    PublishEditorNotice(
                        "The assigned video changed. Nothing was deleted.");
                    return;
                }
                RemoveOverride(true);
            });
        ConfigureLayout(confirmDeleteLocalButton, 21.0f, 8.0f, 0.0f);
        BSML::Lite::SetButtonTextSize(confirmDeleteLocalButton, 2.3f);
        if(auto* deleteForeverText = confirmDeleteLocalButton->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            deleteForeverText->set_color(UnityEngine::Color::get_white());

        auto* storageSpacer = BSML::Lite::CreateText(editorBody, "", 1.0f);
        ConfigureLayout(storageSpacer, -1.0f, 0.8f, 1.0f);
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
        // Keep this preferred-only: a hard minimum steals height from Playback
        // in Beat Saber's fixed side controller. The compact title allocation
        // below moves the values inside this existing backing instead.
        ConfigureLayout(storagePanel, -1.0f, 17.5f, 1.0f);
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
        // Reducing only the title's allocation raises it slightly and moves
        // the value row upward, containing its descenders without making the
        // panel larger or changing any interactive control.
        ConfigureLayout(storageGroupTitle, -1.0f, 3.4f, 1.0f);
        storageGroupTitle->set_alignment(TMPro::TextAlignmentOptions::Center);

        // The button occupies the far-right edge. Local Videos participates in
        // this layout only when one or more MP4 files physically exist in the
        // selected map folder, so ordinary maps do not gain an empty column.
        auto* storageRow = BSML::Lite::CreateHorizontalLayoutGroup(storageBody);
        ConfigureGroup(storageRow);
        storageRow->set_spacing(0.6f);
        ConfigureLayout(storageRow, -1.0f, 7.0f, 1.0f);
        detailMapStorage_ = BSML::Lite::CreateText(
            storageRow, StorageMetricText("Downloaded Video", "0.0 MB"), 2.15f);
        ConfigureLayout(detailMapStorage_, 0.0f, 7.0f, 1.0f);
        detailLocalStorage_ = BSML::Lite::CreateText(
            storageRow, StorageMetricText("Local Videos", "0.0 MB"), 2.15f);
        ConfigureLayout(detailLocalStorage_, 0.0f, 7.0f, 1.0f);
        detailLibraryStorage_ = BSML::Lite::CreateText(
            storageRow, StorageMetricText("All Downloads", "0.0 MB"), 2.15f);
        ConfigureLayout(detailLibraryStorage_, 0.0f, 7.0f, 1.0f);
        detailFreeStorage_ = BSML::Lite::CreateText(
            storageRow, StorageMetricText("Free Space", "0.0 MB"), 2.15f);
        ConfigureLayout(detailFreeStorage_, 0.0f, 7.0f, 1.0f);
        // Explicitly preserve rich-text parsing for the line-height tag. The
        // columns retain their existing horizontal spacing and widths.
        for(auto* storageText : {
                detailMapStorage_, detailLocalStorage_, detailLibraryStorage_,
                detailFreeStorage_})
            if(storageText) storageText->set_richText(true);
        removeButton_ = BSML::Lite::CreateUIButton(
            storageRow,
            "<color=#FF3838>Remove Video</color>",
            {0.0f, 0.0f},
            {13.5f, 6.5f},
            [this]()
            {
                if(removeConfirmModal_)
                    ShowModalInFront(removeConfirmModal_);
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
            "Removes this song's assigned video after confirmation. Big Screen downloads are deleted; map-folder, Video Import, and other local files are only unassigned and remain on the Quest.");

        for(auto* text : {
                browserTitle_, browserStorage_, filterText_, detailTitle_,
                detailMapStorage_, detailLocalStorage_, detailLibraryStorage_,
                detailFreeStorage_, playbackTimeText_})
            if(text) text->set_alignment(TMPro::TextAlignmentOptions::Center);

        editorVisible_ = false;
        // Menu prewarming constructs this inactive hierarchy before Big Screen
        // owns the foreground. Do not claim song-preview ownership or rebuild
        // the catalog until the coordinator is actually presented.
        if(activate)
            Refresh();
    }

    void VideoLibraryMenu::BeginCatalogRebuild()
    {
        const auto rebuildStarted = std::chrono::steady_clock::now();
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
                for(auto* level : pack->__cordl_internal_get__beatmapLevels())
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
            "Video library catalog rebuilt in {} ms: {} total ({} custom, {} WIP, {} OST, {} DLC)",
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - rebuildStarted).count(),
            catalog_.size(), groupCounts[0], groupCounts[1], groupCounts[2], groupCounts[3]);
        catalogPrewarmModelReady_ = true;
        catalogPrewarmIndex_ = 0;
    }

    bool VideoLibraryMenu::PrewarmCatalogStep(
        std::size_t descriptorBudget)
    {
        if(!browserController_ || !editorController_)
            return false;
        if(!catalogRefreshRequested_)
            return true;
        if(!catalogPrewarmModelReady_)
            BeginCatalogRebuild();

        // Describe() performs the map-local Cinema JSON and managed-assignment
        // lookup on its first call, then retains the result in VideoLibrary.
        // Warming only a few entries per frame preserves Unity ownership while
        // avoiding the roughly 800 ms first-open burst observed with 578 maps.
        descriptorBudget = std::max<std::size_t>(descriptorBudget, 1);
        const auto end = std::min(
            catalogPrewarmIndex_ + descriptorBudget,
            catalog_.size());
        while(catalogPrewarmIndex_ < end)
        {
            // One malformed mapper file must not pin the incremental cursor or
            // prevent an unrelated Configure Video deep-link from opening.
            // Advance ownership before parsing, then isolate any unexpected
            // filesystem/parser exception to this one catalog entry. Describe
            // already records ordinary rejected Cinema JSON as a map-specific
            // diagnostic and returns the usable non-mapper state.
            auto* level = catalog_[catalogPrewarmIndex_++].level;
            try
            {
                VideoLibrary::Instance().Describe(level);
            }
            catch(const std::exception& exception)
            {
                PaperLogger.error(
                    "Skipped one video-library descriptor during incremental catalog preparation: {}",
                    exception.what());
            }
            catch(...)
            {
                PaperLogger.error(
                    "Skipped one video-library descriptor during incremental catalog preparation");
            }
        }
        if(catalogPrewarmIndex_ < catalog_.size())
            return false;

        catalogRefreshRequested_ = false;
        catalogPrewarmModelReady_ = false;
        catalogPrewarmIndex_ = 0;
        const auto rowsStarted = std::chrono::steady_clock::now();
        RebuildVisibleRows();
        PaperLogger.info(
            "Video library catalog prewarming completed; retained rows finalized in {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - rowsStarted).count());
        return true;
    }

    void VideoLibraryMenu::RebuildCatalog()
    {
        BeginCatalogRebuild();
        // Explicit refreshes occur after SongCore changes the installed map
        // model. Almost every descriptor is already cached; completing this
        // uncommon invalidation atomically keeps the visible table consistent.
        while(!PrewarmCatalogStep(
            std::max<std::size_t>(catalog_.size(), 1)))
        {
        }
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
        DiagnosticSessionLogger::Instance().MenuEvent(
            "library_filter_changed", "VideoLibraryMenu", {
                {"filter", std::string(FilterNames[next])}});
        if(filterText_) filterText_->set_text(
            "Filter: " + std::string(FilterNames[next]));
        RebuildVisibleRows();
    }

    void VideoLibraryMenu::SelectRow(int row)
    {
        if(row < 0 || row >= static_cast<int>(visible_.size())) return;
        SelectLevel(visible_[row]->level, true);
    }

    bool VideoLibraryMenu::OpenEditorForLevelId(
        std::string_view levelId,
        bool navigateToEditor)
    {
        if(levelId.empty())
            return false;

        const auto item = std::find_if(
            catalog_.begin(),
            catalog_.end(),
            [levelId](const SongLibraryItem& candidate)
            {
                return candidate.level && candidate.level->levelID &&
                    std::string(candidate.level->levelID) == levelId;
            });
        if(item == catalog_.end())
        {
            PaperLogger.warn(
                "Could not deep-link Big Screen's video editor: level '{}' was not in the installed-song catalog",
                std::string(levelId));
            return false;
        }

        SelectLevel(item->level, navigateToEditor);
        return true;
    }

    void VideoLibraryMenu::SelectLevel(
        GlobalNamespace::BeatmapLevel* level,
        bool navigateToEditor)
    {
        if(!level)
            return;
        // Selection is a hard notice-lifecycle boundary even if a caller
        // reaches this method without first navigating through ShowBrowser.
        // Remove the preceding map's renderer and invalidate every token before
        // selected_ can refer to the next map.
        CloseEditorNotice();
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
        selected_ = level;
        DiagnosticSessionLogger::Instance().MenuEvent(
            "song_selected", "VideoLibraryMenu", {
                {"levelId", selected_ && selected_->levelID
                    ? std::string(selected_->levelID) : ""},
                {"songName", selected_ && selected_->songName
                    ? std::string(selected_->songName) : "Unknown Song"}});
        PaperLogger.debug(
            "Opening video editor for '{}' ({})",
            selected_ && selected_->songName
                ? std::string(selected_->songName) : std::string("Unknown Song"),
            selected_ && selected_->levelID
                ? std::string(selected_->levelID) : std::string("no level id"));
        previewSongTime_ = 0.0;
        pendingDownloadRefreshLevelId_.clear();
        terminalDownloadProgressLevelId_.clear();
        if(selected_ && selected_->levelID)
        {
            const auto download = DownloadManager::Instance().Snapshot();
            if(DownloadManager::Instance().OperationInProgress() &&
               download.levelId == std::string(selected_->levelID))
                pendingDownloadRefreshLevelId_ = download.levelId;
        }
        ClearThumbnail();
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        const auto* timing = EditorTimingConfig(descriptor);
        url_ = descriptor.downloadUrl.value_or("");
        mapperProvidedUrl_ = descriptor.downloadUrl.has_value() &&
            descriptor.downloadOrigin == VideoOrigin::Mapper;
        offset_ = timing ? timing->offsetSeconds : 0.0;
        rate_ = timing ? timing->playbackRate : 1.0;
        fitToSong_ = timing ? timing->fitToSong : false;
        blackDuringLeadIn_ = timing ? timing->blackDuringLeadIn : false;
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
            RefreshUrlTextColor();
        }
        suppressTimingCallbacks_ = true;
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        SetToggleWithoutNotification(fitToggle_, fitToSong_);
        SetToggleWithoutNotification(blackLeadInToggle_, blackDuringLeadIn_);
        suppressTimingCallbacks_ = false;
        RefreshLocalVideoStatus();
        if(navigateToEditor)
            ShowEditor();
        else
            editorVisible_ = true;
        OpenEditorNotice();
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
        CloseEditorNotice();
        editorVisible_ = false;
        pendingDownloadRefreshLevelId_.clear();
        terminalDownloadProgressLevelId_.clear();
        StopPreviewAudio(true);
        // Returning from the selected-map child page relinquishes the video,
        // not only its audio. Stop closes the decoder and synchronously clears
        // its read-ahead queue, so the next selected map cannot inherit an old
        // frame reserve or keep the prior video's memory allocated.
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            PlaybackSession::Instance().Stop();
        if(navigate_) navigate_(false);
        ScreenPreview::Instance().ActivateUserLayout();
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
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice(
                "Select a song before checking a YouTube link.");
            RefreshDetails();
            return;
        }
        url_ = Trim(url_);
        if(!IsYouTubeUrl(url_))
        {
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice(url_.empty()
                ? "Enter a YouTube link first."
                : "Use a youtube.com or youtu.be link.");
            RefreshDetails();
            return;
        }

        std::string error;
        if(!DownloadManager::Instance().StartProbe(
            std::string(selected_->levelID),
            url_,
            error))
        {
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice(error.empty()
                ? "The YouTube link could not be checked."
                : error);
        }
        else
        {
            ownedDownloadLevelId_ = std::string(selected_->levelID);
            pendingDownloadRefreshLevelId_ = std::string(selected_->levelID);
            terminalDownloadProgressLevelId_.clear();
            // Bind the asynchronous probe result to the exact text currently
            // in the field. Clearing or replacing that text invalidates this
            // identity, preventing a completed older probe from restoring a
            // stale thumbnail during RefreshDetails.
            probedUrl_ = url_;
            BeginEditorTransferNotice(
                EditorTransferKind::Probe,
                "Checking YouTube link...");
        }
        RefreshDetails();
    }

    void VideoLibraryMenu::StartOrCancelDownload()
    {
        auto& downloader = DownloadManager::Instance();
        const auto current = downloader.Snapshot();
        if(!selected_)
        {
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice("Select a song before downloading a video.");
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
                if(!editorTransferNotice_)
                {
                    BeginEditorTransferNotice(
                        current.metadataOnly
                            ? EditorTransferKind::Probe
                            : EditorTransferKind::Download,
                        ActiveTransferNotice(
                            current,
                            current.metadataOnly,
                            false));
                }
                downloader.Cancel();
                CancelEditorTransferNotice();
            }
            else
            {
                terminalDownloadProgressLevelId_.clear();
                PublishEditorNotice(
                    "Another downloader task is already running.");
            }
            RefreshDetails();
            return;
        }
        // Retry and Resume keep the exact tier chosen before the transfer
        // failed or was cancelled. A direct/non-enumerated fallback uses the
        // historic 1080p request only when the probe supplied no tier list.
        const int requestedHeight = current.levelId == selectedLevelId &&
            current.requestedHeight > 0
                ? current.requestedHeight
                : 1080;
        RequestResolutionDownload(requestedHeight);
    }

    void VideoLibraryMenu::DownloadResolutionPressed(std::size_t buttonIndex)
    {
        if(buttonIndex >= displayedDownloadHeights_.size())
            return;
        RequestResolutionDownload(displayedDownloadHeights_[buttonIndex]);
    }

    void VideoLibraryMenu::RequestResolutionDownload(int height)
    {
        if(!selected_ || height < 1 || height > 1440)
            return;

        auto& diagnostics = DiagnosticSessionLogger::Instance();
        if(Settings::Instance().DetailedDiagnosticLoggingEnabled())
        {
            diagnostics.BeginDownloadSession({
                {"levelId", std::string(selected_->levelID)},
                {"songName", selected_->songName
                    ? std::string(selected_->songName) : "Unknown Song"},
                {"songAuthor", selected_->songAuthorName
                    ? std::string(selected_->songAuthorName) : ""},
                {"sourceUrl", url_},
                {"requestedHeight", std::to_string(height)}});
            diagnostics.DownloadEvent(
                "download_clicked", "VideoLibraryMenu", {
                    {"requestedHeight", std::to_string(height)}});
        }

        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        const bool localFile = descriptor.userOverrideIsMapLocal ||
            descriptor.userOverrideIsImported ||
            descriptor.userOverrideIsExternal ||
            (descriptor.hasMapperLocalFile && !descriptor.hasUserOverride);
        const bool needsWarning = height == 1440;
        const bool replacing = descriptor.CanPlay();
        if(!needsWarning && !replacing)
        {
            diagnostics.DownloadEvent(
                "resolution_selected", "VideoLibraryMenu", {
                    {"height", std::to_string(height)}});
            StartResolutionDownload(height);
            return;
        }

        pendingDownloadHeight_ = height;
        diagnostics.DownloadEvent(
            "resolution_dialog_opened", "VideoLibraryMenu", {
                {"height", std::to_string(height)},
                {"replacement", replacing ? "true" : "false"}});
        std::ostringstream message;
        message << "<b>Download " << height << "p video";
        if(replacing)
            message << " and replace the current assignment";
        message << "?</b>\n\n";
        if(needsWarning)
        {
            message << "1440p requires Hardware Video Decoding. Software decoding is not supported. If hardware decoding fails, Big Screen stops the video while the map continues.\n\n";
        }
        if(replacing)
        {
            if(descriptor.activeMapFileName)
                message << "Currently assigned: "
                        << *descriptor.activeMapFileName << "\n";
            message << "The current video remains available until the new download succeeds.";
            if(localFile)
                message << " Your local file will not be deleted; Big Screen only changes this song's assignment.";
        }
        if(downloadConfirmationText_)
            downloadConfirmationText_->set_text(message.str());
        if(confirmDownloadButton_)
            BSML::Lite::SetButtonText(
                confirmDownloadButton_,
                "Download " + std::to_string(height) + "p");
        if(downloadConfirmModal_)
            ShowModalInFront(downloadConfirmModal_);
    }

    void VideoLibraryMenu::ConfirmPendingResolutionDownload()
    {
        const int height = pendingDownloadHeight_;
        pendingDownloadHeight_ = 0;
        if(downloadConfirmModal_)
            downloadConfirmModal_->Hide();
        if(height > 0)
        {
            DiagnosticSessionLogger::Instance().DownloadEvent(
                "resolution_selected", "VideoLibraryMenu", {
                    {"height", std::to_string(height)}});
            StartResolutionDownload(height);
        }
    }

    void VideoLibraryMenu::StartResolutionDownload(int height)
    {
        auto& downloader = DownloadManager::Instance();
        if(!selected_)
            return;
        const auto selectedLevelId = std::string(selected_->levelID);
        url_ = Trim(url_);
        if(url_.empty())
        {
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice("Enter a YouTube link first.");
            RefreshDetails();
            return;
        }
        if(!IsYouTubeUrl(url_))
        {
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice("Use a youtube.com or youtu.be link.");
            RefreshDetails();
            return;
        }
        DownloadRequest request;
        request.levelId = selectedLevelId;
        request.songName = selected_->songName
            ? std::string(selected_->songName)
            : std::string("Unknown Song");
        request.songAuthor = selected_->songAuthorName
            ? std::string(selected_->songAuthorName)
            : std::string{};
        request.sourceUrl = url_;
        request.origin = VideoOrigin::User;
        request.explicitContentAllowed =
            UiUtility::ExplicitContentAllowed();
        request.offsetSeconds = offset_;
        request.playbackRate = rate_;
        request.fitToSong = fitToSong_;
        request.blackDuringLeadIn = blackDuringLeadIn_;
        request.requestedHeight = height;
        request.maximumSourceFps = Settings::Instance().PlaybackFpsLimit();
        std::string error;
        PaperLogger.info(
            "Download {}p button pressed for {}",
            height,
            selectedLevelId);
        if(!downloader.Start(std::move(request), error))
        {
            DiagnosticSessionLogger::Instance().DownloadEvent(
                "download_failed", "VideoLibraryMenu", {
                    {"stage", "start"}, {"message", error}});
            DiagnosticSessionLogger::Instance().EndDownloadSession(
                "failed_to_start");
            const std::string failureMessage = error.empty()
                ? "The download could not be started."
                : error;
            terminalDownloadProgressLevelId_.clear();
            PaperLogger.error(
                "Could not start download for {}: {}",
                selectedLevelId,
                failureMessage);
            ErrorManager::Instance().RecordError(
                "Starting a video download for " + selectedLevelId,
                failureMessage);
            PublishEditorNotice(failureMessage);
        }
        else
        {
            DiagnosticSessionLogger::Instance().DownloadEvent(
                "download_started", "VideoLibraryMenu", {
                    {"height", std::to_string(height)}});
            ownedDownloadLevelId_ = selectedLevelId;
            pendingDownloadRefreshLevelId_ = selectedLevelId;
            terminalDownloadProgressLevelId_.clear();
            // The same URL and tier can legitimately produce the same title,
            // byte count, and deterministic paths as the previous download.
            // Clear the per-transfer presentation identities when a new job
            // starts so its completion and replacement thumbnail are still
            // acknowledged exactly once.
            refreshedDownloadIdentity_.clear();
            completedVideoThumbnailIdentity_.clear();
            BeginEditorTransferNotice(
                EditorTransferKind::Download,
                "Starting video download...");
        }
        RefreshDetails();
    }

    void VideoLibraryMenu::PasteUrlFromClipboard()
    {
        if(!urlInput_) return;
        try
        {
            const auto clipboardValue = UnityEngine::GUIUtility::get_systemCopyBuffer();
            const auto clipboard = Trim(
                clipboardValue ? std::string(clipboardValue) : std::string{});
            if(clipboard.empty())
            {
                terminalDownloadProgressLevelId_.clear();
                PublishEditorNotice(
                    "The Quest clipboard is empty. Copy a YouTube link first.");
                RefreshDetails();
                return;
            }
            if(!IsWebUrl(clipboard))
            {
                terminalDownloadProgressLevelId_.clear();
                PublishEditorNotice(
                    "Clipboard text is not a valid web address.");
                RefreshDetails();
                return;
            }

            url_ = clipboard;
            mapperProvidedUrl_ = false;
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
            RefreshUrlTextColor();
            BeginUrlProbe();
        }
        catch(const std::exception& error)
        {
            PaperLogger.error("Could not read the Quest clipboard: {}", error.what());
            ErrorManager::Instance().RecordError(
                "Reading the Quest clipboard",
                error.what());
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice("The Quest clipboard could not be read.");
            RefreshDetails();
        }
    }

    void VideoLibraryMenu::RefreshSelectedMapperMetadata()
    {
        if(!selected_ || !selected_->levelID)
        {
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice(
                "Select a song before refreshing mapper settings.");
            RefreshDetails();
            return;
        }

        const std::string levelId(selected_->levelID);
        VideoLibrary::Instance().RefreshMapperMetadata(levelId);
        ClearThumbnail();

        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        const auto* timing = EditorTimingConfig(descriptor);
        url_ = descriptor.downloadUrl.value_or("");
        mapperProvidedUrl_ = descriptor.downloadUrl.has_value() &&
            descriptor.downloadOrigin == VideoOrigin::Mapper;
        offset_ = timing ? timing->offsetSeconds : 0.0;
        rate_ = timing ? timing->playbackRate : 1.0;
        fitToSong_ = timing ? timing->fitToSong : false;
        blackDuringLeadIn_ = timing ? timing->blackDuringLeadIn : false;

        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
            RefreshUrlTextColor();
        }
        suppressTimingCallbacks_ = true;
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        SetToggleWithoutNotification(fitToggle_, fitToSong_);
        SetToggleWithoutNotification(blackLeadInToggle_, blackDuringLeadIn_);
        suppressTimingCallbacks_ = false;

        terminalDownloadProgressLevelId_.clear();
        RefreshLocalVideoStatus();
        StartSelectedPreview();
        RefreshDetails();
        RefreshPlaybackControls();
        PublishEditorNotice(descriptor.mapperDefinition
            ? "Mapper video settings refreshed."
            : "This song has no Cinema mapper settings.");
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
            terminalDownloadProgressLevelId_.clear();
            PublishEditorNotice(
                "This song does not provide enough information to search.");
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
            terminalDownloadProgressLevelId_.clear();
            PaperLogger.info(
                "Opened YouTube search for '{}'",
                query);
            PublishEditorNotice("Opened YouTube search in Quest Browser.");
        }
        catch(const std::exception& error)
        {
            terminalDownloadProgressLevelId_.clear();
            PaperLogger.error(
                "Could not launch YouTube search '{}': {}",
                url,
                error.what());
            ErrorManager::Instance().RecordError(
                "Opening a YouTube search",
                error.what());
            PublishEditorNotice("Quest Browser could not open the search.");
        }
        catch(...)
        {
            terminalDownloadProgressLevelId_.clear();
            PaperLogger.error(
                "Could not launch YouTube search '{}'",
                url);
            ErrorManager::Instance().RecordError(
                "Opening a YouTube search",
                "Unknown native exception");
            PublishEditorNotice("Quest Browser could not open the search.");
        }
        RefreshDetails();
    }

    void VideoLibraryMenu::RefreshLocalVideoStatus()
    {
        // The center-screen browser owns directory enumeration, probing, help,
        // and assignment. This side-panel row only reflects the active local
        // assignment and never repeats an FFmpeg scan.
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
        // Thumbnail picking follows the local video itself: any map with an
        // active local file can pick a frame, and can re-pick a different one
        // at any time. Downloads keep their own YouTube artwork instead.
        if(setThumbnailButton_)
            setThumbnailButton_->set_interactable(
                activeLocal && descriptor.playableConfig.has_value());
        if(localVideoStatusText_)
        {
            localVideoStatusText_->get_gameObject()->SetActive(activeLocal);
            if(activeLocal)
            {
                localVideoStatusText_->set_text(
                    "Local video: " +
                    descriptor.activeMapFileName.value_or("selected video"));
                localVideoStatusText_->set_color(
                    {0.20f, 1.0f, 0.36f, 1.0f});
            }
        }
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
        mapperProvidedUrl_ = false;
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText("");
            suppressUrlCallback_ = false;
            RefreshUrlTextColor();
        }
        ClearThumbnail();
        terminalDownloadProgressLevelId_.clear();
        DiagnosticSessionLogger::Instance().MenuEvent(
            "video_assigned", "LocalVideoBrowser", {
                {"levelId", std::string(selected_->levelID)},
                {"fileName", fileName},
                {"origin", "local"}});
        previewSongTime_ = 0.0;
        playWhenAudioReady_ = true;
        RefreshLocalVideoStatus();
        RequestSelectedAudio();
        StartSelectedPreview();
        if(IsAlive(previewAudioClip_))
            StartPreviewAudio();
        RefreshDetails();
        PublishEditorNotice("Local video assigned: " + fileName);
    }

    void VideoLibraryMenu::LocalThumbnailChanged(const std::string& thumbnailPath)
    {
        // The picker replaced the PNG at its deterministic path. Every cached
        // decode of that path is stale: evict the row-list sprite and force
        // the editor's large thumbnail to reload on the next RefreshDetails.
        EvictVideoThumbnail(thumbnailPath);
        if(loadedThumbnailPath_ == thumbnailPath && loadedThumbnailSprite_)
        {
            UnityEngine::Object::Destroy(loadedThumbnailSprite_);
            loadedThumbnailSprite_ = nullptr;
            loadedThumbnailPath_.clear();
        }
        terminalDownloadProgressLevelId_.clear();
        RefreshDetails();
        RebuildVisibleRows(true);
        PublishEditorNotice("Video thumbnail updated.");
    }

    void VideoLibraryMenu::RemoveOverride(bool deleteFile)
    {
        if(!selected_) return;
        const auto diagnosticLevelId = std::string(selected_->levelID);
        try
        {

        const auto removedDescriptor = VideoLibrary::Instance().Describe(selected_);
        const bool hasUserOverride = removedDescriptor.hasUserOverride;
        const bool hasMapperDownload = removedDescriptor.hasMapperDownload;
        const bool removingLocalMapFile =
            removedDescriptor.userOverrideIsMapLocal;
        const bool removingImportedFile =
            removedDescriptor.userOverrideIsImported;
        const bool removingExternalFile =
            removedDescriptor.userOverrideIsExternal;
        const bool activeLocalFile = removingLocalMapFile ||
            removingImportedFile || removingExternalFile ||
            (!hasUserOverride && removedDescriptor.hasMapperLocalFile);

        // Close the decoder before either unlinking or deleting. On Android an
        // open decoder/file descriptor can keep a removed path alive until the
        // session ends and can also race a new assignment made immediately.
        StopPreviewAudio(true);
        auto& playback = PlaybackSession::Instance();
        if(playback.IsLibraryPreviewActive())
            playback.Stop();

        auto& library = VideoLibrary::Instance();
        const auto levelId = std::string(selected_->levelID);
        const auto userThumbnailPath = library.AllocateThumbnailPath(
            levelId, VideoOrigin::User).string();
        const auto mapperThumbnailPath = library.AllocateThumbnailPath(
            levelId, VideoOrigin::Mapper).string();

        std::optional<std::filesystem::path> localPathToDelete;
        if(deleteFile && activeLocalFile)
        {
            if(!removedDescriptor.playableConfig)
            {
                terminalDownloadProgressLevelId_.clear();
                RefreshDetails();
                PublishEditorNotice("No removable video file was found.");
                return;
            }
            localPathToDelete =
                removedDescriptor.playableConfig->videoPath.lexically_normal();
        }

        bool removed = false;
        if(hasUserOverride)
            removed = library.RemoveUserOverride(
                levelId,
                deleteFile && !activeLocalFile);

        // Remove every fallback assignment as well, otherwise Unlink could
        // appear to do nothing when a mapper download or mapper-local file was
        // waiting underneath a user override. With Unlink, managed MP4 bytes
        // remain available to Show File Browser and Storage Maintenance.
        if(hasMapperDownload)
            removed = library.RemoveMapperDownload(
                levelId,
                deleteFile && !activeLocalFile) || removed;
        if(removedDescriptor.hasMapperLocalFile)
            removed = library.SuppressMapperLocalVideo(levelId) || removed;

        if(removed)
            DiagnosticSessionLogger::Instance().MenuEvent(
                deleteFile ? "video_deleted" : "video_unassigned",
                "VideoLibraryMenu", {{"levelId", diagnosticLevelId}});

        if(!removed)
        {
            terminalDownloadProgressLevelId_.clear();
            RefreshDetails();
            PublishEditorNotice("This song has no video assignment to remove.");
            return;
        }

        // Persist every unlink before touching a user-owned file. A full or
        // unavailable manifest can therefore abort safely without deleting
        // the only copy of the video. If filesystem deletion later fails,
        // restore the still-existing file assignment when possible.
        if(localPathToDelete)
        {
            std::string deleteError;
            if(!library.DeleteLocalVideoFile(*localPathToDelete, deleteError))
            {
                std::string restoreError;
                const bool restored = library.SetVideoFileOverride(
                    selected_, *localPathToDelete, restoreError);
                const std::string deletionFailureMessage = restored
                    ? (deleteError.empty()
                        ? "The local file could not be deleted. Its assignment was restored."
                        : deleteError + " Its assignment was restored.")
                    : "The local file could not be deleted and remains on the Quest, but its assignment could not be restored. Select it again with Show File Browser.";
                terminalDownloadProgressLevelId_.clear();
                ErrorManager::Instance().RecordError(
                    "Deleting a local video", deletionFailureMessage);
                RefreshDetails();
                PublishEditorNotice(deletionFailureMessage);
                return;
            }

            // The picked thumbnail came from the now-deleted local file.
            // Thumbnail cleanup is non-critical after the durable unlink and
            // must not turn a successful deletion into an uncaught UI error.
            const auto localThumbnailPath =
                library.LocalThumbnailPath(levelId).string();
            try
            {
                if(library.RemoveLocalThumbnail(levelId))
                    EvictVideoThumbnail(localThumbnailPath);
            }
            catch(const std::exception& exception)
            {
                ErrorManager::Instance().RecordError(
                    "removing a deleted video's thumbnail", exception.what());
            }
        }

        // Row sprites outlive the files from which they were decoded. Evict
        // both possible managed identities; this is harmless when one source
        // remains because the next refresh reloads its still-existing JPEG.
        EvictVideoThumbnail(userThumbnailPath);
        EvictVideoThumbnail(mapperThumbnailPath);

        // The thumbnail sprite is UI-owned and independent of the downloaded
        // MP4. Release the Unity texture and remove stale artwork even when the
        // video itself was merely unlinked.
        const auto download = DownloadManager::Instance().Snapshot();
        if(!activeLocalFile &&
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
        terminalDownloadProgressLevelId_.clear();
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        const auto* timing = EditorTimingConfig(descriptor);
        url_ = descriptor.downloadUrl.value_or("");
        mapperProvidedUrl_ = descriptor.downloadUrl.has_value() &&
            descriptor.downloadOrigin == VideoOrigin::Mapper;
        offset_ = timing ? timing->offsetSeconds : 0.0;
        rate_ = timing ? timing->playbackRate : 1.0;
        fitToSong_ = timing ? timing->fitToSong : false;
        blackDuringLeadIn_ = timing ? timing->blackDuringLeadIn : false;
        if(urlInput_)
        {
            suppressUrlCallback_ = true;
            urlInput_->SetText(url_);
            suppressUrlCallback_ = false;
            RefreshUrlTextColor();
        }
        suppressTimingCallbacks_ = true;
        if(offsetSetting_) offsetSetting_->set_Value(static_cast<float>(offset_));
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        SetToggleWithoutNotification(fitToggle_, fitToSong_);
        SetToggleWithoutNotification(blackLeadInToggle_, blackDuringLeadIn_);
        suppressTimingCallbacks_ = false;
        RefreshLocalVideoStatus();
        RefreshDetails();
        StartSelectedPreview();
        PublishEditorNotice(deleteFile
            ? "Video file deleted."
            : "Video unlinked from this song.");
        }
        catch(const std::exception& exception)
        {
            constexpr std::string_view failureMessage =
                "The video assignment could not be saved. The previous library state was restored.";
            terminalDownloadProgressLevelId_.clear();
            ErrorManager::Instance().ReportInternal(
                "removing a video assignment", exception.what());
            ErrorManager::Instance().ReportUserVisible(
                "Video change was not saved", std::string(failureMessage));
            RefreshDetails();
            PublishEditorNotice(std::string(failureMessage));
        }
        catch(...)
        {
            constexpr std::string_view failureMessage =
                "The video assignment could not be saved. The previous library state was restored.";
            terminalDownloadProgressLevelId_.clear();
            ErrorManager::Instance().ReportInternal(
                "removing a video assignment", "Unknown native exception");
            ErrorManager::Instance().ReportUserVisible(
                "Video change was not saved", std::string(failureMessage));
            RefreshDetails();
            PublishEditorNotice(std::string(failureMessage));
        }
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
            else if(Utility::IsRegularFile(*metadata->second.path) &&
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
        const auto restorePersistedTiming = [this]()
        {
            const auto descriptor =
                VideoLibrary::Instance().Describe(selected_);
            const auto* timing = EditorTimingConfig(descriptor);
            offset_ = timing ? timing->offsetSeconds : 0.0;
            rate_ = timing ? timing->playbackRate : 1.0;
            fitToSong_ = timing ? timing->fitToSong : false;
            blackDuringLeadIn_ = timing ? timing->blackDuringLeadIn : false;
            suppressTimingCallbacks_ = true;
            if(offsetSetting_)
                offsetSetting_->set_Value(static_cast<float>(offset_));
            if(rateSetting_)
                rateSetting_->set_Value(static_cast<float>(rate_));
            SetToggleWithoutNotification(fitToggle_, fitToSong_);
            SetToggleWithoutNotification(
                blackLeadInToggle_, blackDuringLeadIn_);
            suppressTimingCallbacks_ = false;
        };
        try
        {
            return VideoLibrary::Instance().UpdateTiming(
                std::string(selected_->levelID),
                SelectedVideoOrigin(),
                offset_,
                rate_,
                fitToSong_,
                blackDuringLeadIn_);
        }
        catch(const std::exception& exception)
        {
            constexpr std::string_view failureMessage =
                "Timing could not be saved. The previous values remain active.";
            terminalDownloadProgressLevelId_.clear();
            ErrorManager::Instance().ReportInternal(
                "saving video timing", exception.what());
            ErrorManager::Instance().ReportUserVisible(
                "Video timing was not saved", std::string(failureMessage));
            restorePersistedTiming();
            RefreshDetails();
            PublishEditorNotice(std::string(failureMessage));
            return false;
        }
        catch(...)
        {
            constexpr std::string_view failureMessage =
                "Timing could not be saved. The previous values remain active.";
            terminalDownloadProgressLevelId_.clear();
            ErrorManager::Instance().ReportInternal(
                "saving video timing", "Unknown native exception");
            ErrorManager::Instance().ReportUserVisible(
                "Video timing was not saved", std::string(failureMessage));
            restorePersistedTiming();
            RefreshDetails();
            PublishEditorNotice(std::string(failureMessage));
            return false;
        }
    }

    bool VideoLibraryMenu::ApplyFitToSong()
    {
        if(!selected_)
        {
            terminalDownloadProgressLevelId_.clear();
            RefreshDetails();
            PublishEditorNotice("Select a song before using Fit to Song.");
            return false;
        }
        if(selected_->songDuration <= 0.0f)
        {
            terminalDownloadProgressLevelId_.clear();
            RefreshDetails();
            PublishEditorNotice("Fit to Song needs a valid song duration.");
            return false;
        }
        const auto descriptor = VideoLibrary::Instance().Describe(selected_);
        if(!descriptor.playableConfig || descriptor.playableConfig->declaredDurationSeconds <= 0.0)
        {
            terminalDownloadProgressLevelId_.clear();
            RefreshDetails();
            PublishEditorNotice("Fit to Song needs a valid video duration.");
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
            terminalDownloadProgressLevelId_.clear();
            RefreshDetails();
            PublishEditorNotice("Fit to Song could not save the new speed.");
            return false;
        }
        suppressTimingCallbacks_ = true;
        if(rateSetting_) rateSetting_->set_Value(static_cast<float>(rate_));
        SetToggleWithoutNotification(fitToggle_, fitToSong_);
        suppressTimingCallbacks_ = false;
        StartSelectedPreview();
        terminalDownloadProgressLevelId_.clear();
        RefreshDetails();
        std::ostringstream message;
        message << std::fixed << std::setprecision(2)
                << "Fit to Song enabled: " << rate_ << "x.";
        PublishEditorNotice(message.str());
        return true;
    }

    void VideoLibraryMenu::OpenEditorNotice()
    {
        const std::string levelId = selected_ && selected_->levelID
            ? std::string(selected_->levelID)
            : std::string{};
        editorNoticeVisit_ = editorNoticeModel_.Enter(levelId);
        editorTransferNotice_ = {};
        previewNoticeRevision_ = {};
        editorTransferKind_ = EditorTransferKind::None;
        editorTransferCancellationRequested_ = false;
        CreateEditorNoticeSurface();
        QueueEditorNoticePaint();

        // Attach only to work that is genuinely running. DownloadManager keeps
        // its last terminal snapshot for diagnostics and Retry/Resume, but a
        // retained result is not an event in this newly opened editor visit.
        const auto transfer = DownloadManager::Instance().Snapshot();
        if(!levelId.empty() &&
           DownloadManager::Instance().OperationInProgress() &&
           transfer.levelId == levelId)
        {
            BeginEditorTransferNotice(
                transfer.metadataOnly
                    ? EditorTransferKind::Probe
                    : EditorTransferKind::Download,
                ActiveTransferNotice(
                    transfer,
                    transfer.metadataOnly,
                    false));
        }
    }

    void VideoLibraryMenu::CloseEditorNotice()
    {
        // This is deliberately unconditional. Map exit must erase the model's
        // retained string even if a preceding exception left the controller's
        // local visit token inconsistent with the model.
        editorNoticeModel_.Reset();
        editorNoticeVisit_ = {};
        editorTransferNotice_ = {};
        previewNoticeRevision_ = {};
        editorTransferKind_ = EditorTransferKind::None;
        editorTransferCancellationRequested_ = false;
        pendingDownloadRefreshLevelId_.clear();
        terminalDownloadProgressLevelId_.clear();
        editorNoticePaintPending_ = false;
        editorNoticePaintAfterFrame_ = -1;
        DestroyEditorNoticeSurface();
    }

    std::optional<VideoEditorNoticeModel::RevisionToken>
    VideoLibraryMenu::PublishEditorNotice(std::string message)
    {
        if(!selected_ || !selected_->levelID || !editorNoticeVisit_)
            return std::nullopt;
        terminalDownloadProgressLevelId_.clear();
        auto revision = editorNoticeModel_.Publish(
            editorNoticeVisit_,
            std::string(selected_->levelID),
            std::move(message));
        if(revision)
        {
            // Publish() deliberately supersedes any transfer notice that was
            // waiting for the throttled UI poll. Mirror that retirement in
            // the controller so the old token is never polled again.
            editorTransferNotice_ = {};
            editorTransferKind_ = EditorTransferKind::None;
            editorTransferCancellationRequested_ = false;
            QueueEditorNoticePaint();
        }
        return revision;
    }

    void VideoLibraryMenu::PublishPreviewNotice(std::string message)
    {
        if(auto revision = PublishEditorNotice(std::move(message)))
            previewNoticeRevision_ = *revision;
    }

    void VideoLibraryMenu::ClearPreviewNotice()
    {
        if(!previewNoticeRevision_)
            return;
        const bool cleared =
            editorNoticeModel_.ClearIfCurrent(previewNoticeRevision_);
        previewNoticeRevision_ = {};
        if(cleared)
            QueueEditorNoticePaint();
    }

    void VideoLibraryMenu::BeginEditorTransferNotice(
        EditorTransferKind kind,
        std::string initialMessage)
    {
        if(!selected_ || !selected_->levelID || !editorNoticeVisit_ ||
           kind == EditorTransferKind::None)
            return;

        // A newly accepted downloader operation supersedes any transfer token
        // left between the worker's terminal publication and this UI tick.
        // Retiring that token without reading its snapshot prevents it from
        // producing a late result after the new operation begins.
        if(editorTransferNotice_)
            editorNoticeModel_.ForgetTransfer(editorTransferNotice_);
        editorTransferNotice_ = {};
        editorTransferKind_ = EditorTransferKind::None;
        editorTransferCancellationRequested_ = false;

        auto token = editorNoticeModel_.BeginTransfer(
            editorNoticeVisit_, std::string(selected_->levelID));
        if(!token)
            return;
        editorTransferNotice_ = *token;
        editorTransferKind_ = kind;
        terminalDownloadProgressLevelId_.clear();
        if(editorNoticeModel_.PublishTransfer(
               editorTransferNotice_, std::move(initialMessage)))
            QueueEditorNoticePaint();
    }

    void VideoLibraryMenu::CancelEditorTransferNotice()
    {
        if(!editorTransferNotice_)
            return;
        editorTransferCancellationRequested_ = true;
        const bool metadataOnly =
            editorTransferKind_ == EditorTransferKind::Probe;
        const auto transfer = DownloadManager::Instance().Snapshot();
        if(editorNoticeModel_.PublishTransfer(
               editorTransferNotice_,
               ActiveTransferNotice(transfer, metadataOnly, true)))
            QueueEditorNoticePaint();
    }

    void VideoLibraryMenu::PollEditorTransferNotice()
    {
        if(!editorVisible_ || !selected_ || !selected_->levelID ||
           !editorTransferNotice_)
            return;

        const std::string levelId(selected_->levelID);
        const auto transfer = DownloadManager::Instance().Snapshot();
        if(transfer.levelId != levelId)
        {
            const bool cleared =
                editorNoticeModel_.AbandonTransfer(editorTransferNotice_);
            editorTransferNotice_ = {};
            editorTransferKind_ = EditorTransferKind::None;
            editorTransferCancellationRequested_ = false;
            if(cleared)
                QueueEditorNoticePaint();
            return;
        }

        const bool metadataOnly =
            editorTransferKind_ == EditorTransferKind::Probe;
        if(!metadataOnly &&
           VideoLibrary::Instance().BackgroundCommitInProgress())
        {
            if(editorNoticeModel_.PublishTransfer(
                   editorTransferNotice_, "Saving downloaded video..."))
                QueueEditorNoticePaint();
            return;
        }

        if(DownloadManager::Instance().OperationInProgress())
        {
            if(editorNoticeModel_.PublishTransfer(
                   editorTransferNotice_,
                   ActiveTransferNotice(
                       transfer,
                       metadataOnly,
                       editorTransferCancellationRequested_)))
                QueueEditorNoticePaint();
            return;
        }

        if(editorNoticeModel_.FinishTransfer(
               editorTransferNotice_,
               FinishedTransferNotice(transfer, metadataOnly)))
            QueueEditorNoticePaint();
        editorTransferNotice_ = {};
        editorTransferKind_ = EditorTransferKind::None;
        editorTransferCancellationRequested_ = false;
    }

    void VideoLibraryMenu::CreateEditorNoticeSurface()
    {
        DestroyEditorNoticeSurface();
        if(!operationStatusHost_)
            return;

        operationStatusText_ =
            BSML::Lite::CreateText(operationStatusHost_, "", 2.35f);
        ConfigureLayout(operationStatusText_, -1.0f, 5.5f, 1.0f);
        operationStatusText_->set_enableWordWrapping(false);
        operationStatusText_->set_enableAutoSizing(true);
        operationStatusText_->set_fontSizeMin(1.9f);
        operationStatusText_->set_fontSizeMax(2.35f);
        operationStatusText_->set_overflowMode(
            TMPro::TextOverflowModes::Ellipsis);
        operationStatusText_->set_alignment(
            TMPro::TextAlignmentOptions::Center);
    }

    void VideoLibraryMenu::DestroyEditorNoticeSurface()
    {
        auto* surface = operationStatusText_;
        operationStatusText_ = nullptr;
        if(!surface)
            return;

        // Destroy is deferred by Unity until the end of the frame. Disable the
        // old object first so its cached CanvasRenderer mesh cannot overlap a
        // newly created map-owned surface during that frame.
        try
        {
            if(auto object = surface->get_gameObject())
            {
                object->SetActive(false);
                UnityEngine::Object::Destroy(object);
            }
        }
        catch(...)
        {
            // Native state and the cached pointer were already cleared above.
            // A scene transition may have destroyed the Unity object first;
            // never let that prevent the remainder of menu teardown.
            PaperLogger.warn(
                "Status-label surface was already unavailable during map exit");
        }
    }

    void VideoLibraryMenu::QueueEditorNoticePaint()
    {
        if(!editorVisible_ || !operationStatusText_ || !editorNoticeVisit_)
            return;
        if(!editorNoticePaintPending_)
            editorNoticePaintAfterFrame_ = UnityEngine::Time::get_frameCount();
        editorNoticePaintPending_ = true;
    }

    void VideoLibraryMenu::PaintEditorNotice()
    {
        if(!editorNoticePaintPending_ || !operationStatusText_ ||
           !editorVisible_ || !editorNoticeVisit_ ||
           UnityEngine::Time::get_frameCount() <= editorNoticePaintAfterFrame_)
            return;
        const std::string levelId = selected_ && selected_->levelID
            ? std::string(selected_->levelID)
            : std::string{};
        editorNoticePaintPending_ = false;
        editorNoticePaintAfterFrame_ = -1;
        operationStatusText_->set_text(std::string(
            editorNoticeModel_.Current(editorNoticeVisit_, levelId)));
    }

    void VideoLibraryMenu::RefreshUrlTextColor()
    {
        if(!urlInputText_)
            return;

        // A muted mapper address communicates that it came from the map's
        // metadata. As soon as the player types or pastes a replacement, the
        // same editable field becomes full white so ownership of the value is
        // clear without introducing another label or consuming panel space.
        urlInputText_->set_color(
            mapperProvidedUrl_
                ? UnityEngine::Color{0.66f, 0.68f, 0.72f, 1.0f}
                : UnityEngine::Color::get_white());
    }

    void VideoLibraryMenu::RefreshDetails()
    {
        if(auto notice = DownloadManager::Instance().TakeDownloadNotice())
        {
            ErrorManager::Instance().ReportUserVisible(
                notice->title, notice->message);
        }
        auto& library = VideoLibrary::Instance();
        auto& downloader = DownloadManager::Instance();
        const auto download = downloader.Snapshot();
        const std::string selectedLevelId = selected_ && selected_->levelID
            ? std::string(selected_->levelID)
            : std::string{};
        const bool thisDownload = !selectedLevelId.empty() &&
            download.levelId == selectedLevelId;
        const bool downloadOperationInProgress =
            downloader.OperationInProgress();
        if(thisDownload && downloadOperationInProgress)
            pendingDownloadRefreshLevelId_ = selectedLevelId;

        if(library.BackgroundCommitInProgress())
        {
            // Keep Unity responsive while the downloader persists library.json.
            // The previous complete layout remains in place until the short
            // background transaction publishes its final snapshot.
            return;
        }
        const auto libraryBytes = library.LibraryBytes();
        const auto freeBytes = library.FreeBytes();
        if(detailLibraryStorage_)
            detailLibraryStorage_->set_text(
                StorageMetricText(
                    "All Downloads", Utility::FormatStorageSize(libraryBytes)));
        if(detailFreeStorage_)
            detailFreeStorage_->set_text(
                StorageMetricText(
                    "Free Space", Utility::FormatStorageSize(freeBytes)));
        if(!selected_)
        {
            if(detailMapStorage_)
                detailMapStorage_->set_text(
                    StorageMetricText("Downloaded Video", "0.0 MB"));
            if(detailLocalStorage_)
                detailLocalStorage_->get_gameObject()->SetActive(false);
            if(storageSpacer_)
                storageSpacer_->SetActive(false);
            if(storagePanel_)
                storagePanel_->SetActive(false);
            return;
        }
        const auto descriptor = library.Describe(selected_);
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
                StorageMetricText(
                    "Downloaded Video",
                    Utility::FormatStorageSize(downloadedVideoBytes)));
        if(detailLocalStorage_)
        {
            detailLocalStorage_->set_text(
                StorageMetricText(
                    "Local Videos", Utility::FormatStorageSize(localVideoBytes)));
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
            detailTitle_->set_text(
                author.empty() ? name : name + " — " + author);
        }
        if(removeConfirmationText_)
        {
            const bool activeLocalFile = descriptor.userOverrideIsMapLocal ||
                descriptor.userOverrideIsImported ||
                descriptor.userOverrideIsExternal ||
                (!descriptor.hasUserOverride && descriptor.hasMapperLocalFile);
            removeConfirmationText_->set_text(
                activeLocalFile
                    ? "Remove this local video assignment?\n\nUnlink keeps the video file on the Quest and only stops this map from using it. Delete File permanently removes it. You can also delete it later with the Quest file browser."
                    : "Remove this downloaded video assignment?\n\nUnlink keeps the downloaded file on the Quest. Delete Video permanently removes it. An unlinked download can be reassigned with Show File Browser or removed later with Storage Maintenance.");
            if(deleteVideoButton_)
                if(auto* label = deleteVideoButton_->get_gameObject()
                       ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
                {
                    label->set_text(activeLocalFile
                        ? "<color=#FF3838>Delete File</color>"
                        : "<color=#FF3838>Delete Video</color>");
                    label->set_color(UnityEngine::Color::get_white());
                }
        }
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
                Utility::IsRegularFile(*descriptor.thumbnailPath))
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
        // A retained downloader snapshot is durable diagnostic/retry state,
        // not proof that this editor observed the operation. Consume the
        // non-text refresh latch once the worker and library publication have
        // both finished. Only probe/failure/cancellation outcomes retain their
        // progress bar; a completed video is represented by its assignment.
        if(thisDownload && !downloadOperationInProgress &&
           pendingDownloadRefreshLevelId_ == selectedLevelId)
        {
            // A URL may be edited while its earlier probe is still running.
            // The downloader correctly rejects the overlapping replacement,
            // but its old terminal probe snapshot must not overwrite the
            // newer validation message. Full transfers remain map-scoped
            // because their completion actually replaces that map's video.
            const bool terminalMatchesCurrentRequest =
                !download.metadataOnly ||
                (!probedUrl_.empty() && url_ == probedUrl_);
            const bool retainsTerminalProgress = terminalMatchesCurrentRequest &&
                (download.state == DownloadState::ProbeCompleted ||
                 download.state == DownloadState::Failed ||
                 download.state == DownloadState::Cancelled);
            if(retainsTerminalProgress)
                terminalDownloadProgressLevelId_ = selectedLevelId;
            else
                terminalDownloadProgressLevelId_.clear();
            pendingDownloadRefreshLevelId_.clear();
        }

        // A completed YouTube replacement immediately removes the old local
        // file's active highlight without opening the decoder. This identity
        // exists only for refreshing assignment/thumbnail state; it does not
        // own the operation-status label.
        if(thisDownload && download.state == DownloadState::Completed)
        {
            const auto completedIdentity =
                download.levelId + "|" + download.title + "|" +
                std::to_string(download.totalBytes);
            if(refreshedDownloadIdentity_ != completedIdentity)
            {
                refreshedDownloadIdentity_ = completedIdentity;
                RefreshLocalVideoStatus();
            }
        }

        if(downloadProgressTrack_ && downloadProgressFill_)
        {
            // Metadata lookup has no byte total, so it uses a pulsing fill.
            // Once yt-dlp starts transferring the MP4, the same bar switches
            // to an exact byte ratio and changes color for terminal outcomes.
            // A completed file is already represented by the active-video
            // status and must not leave a frozen full progress bar behind.
            // Probe results and failures remain visible because their next
            // action (choose a tier, retry, or resume) is still on this page.
            const bool showTerminalProgress =
                terminalDownloadProgressLevelId_ == selectedLevelId &&
                (download.state == DownloadState::ProbeCompleted ||
                 download.state == DownloadState::Failed ||
                 download.state == DownloadState::Cancelled);
            const bool showProgress = thisDownload &&
                ((downloadOperationInProgress && download.Active()) ||
                 showTerminalProgress);
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
                  download.state == DownloadState::Cancelled ||
                  download.state == DownloadState::Failed);
            // A completed transfer has already replaced/assigned the active
            // file. Keeping the prior probe's tier list visible made the
            // editor look as though the same video still needed downloading,
            // especially when a local override had just been replaced. Hide
            // every download action immediately; a later URL check can expose
            // fresh replacement choices again.
            const bool showTierButtons = validatedProbe &&
                !download.availableHeights.empty();
            const bool showDownloadButton = validatedTransferState ||
                (validatedProbe && download.availableHeights.empty());
            downloadButton_->get_gameObject()->SetActive(showDownloadButton);
            displayedDownloadHeights_.clear();
            if(showTierButtons)
            {
                const auto count = std::min(
                    download.availableHeights.size(),
                    downloadTierButtons_.size());
                displayedDownloadHeights_.assign(
                    download.availableHeights.begin(),
                    download.availableHeights.begin() + count);
            }
            for(std::size_t index = 0;
                index < downloadTierButtons_.size(); ++index)
            {
                auto* tierButton = downloadTierButtons_[index];
                if(!tierButton)
                    continue;
                const bool visible = index < displayedDownloadHeights_.size();
                tierButton->get_gameObject()->SetActive(visible);
                if(visible)
                {
                    const int height = displayedDownloadHeights_[index];
                    BSML::Lite::SetButtonText(
                        tierButton,
                        "DOWNLOAD " + std::to_string(height) + "p");
                    if(auto* tierText = tierButton->get_gameObject()
                           ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
                        tierText->set_color(UnityEngine::Color::get_white());
                    tierButton->set_interactable(true);
                }
            }
            if(downloadButtonPlaceholder_)
                downloadButtonPlaceholder_->SetActive(
                    !showDownloadButton && !showTierButtons);

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
        const bool mapperTimingWaitingForVideo = !descriptor.CanPlay() &&
            descriptor.mapperDefinition.has_value();
        // A replacement download deliberately leaves the previous assigned
        // file intact until validation and atomic publication finish. Do not
        // expose timing or playback controls for that preserved old file while
        // the new transfer is downloading/preparing: it makes an unfinished
        // replacement look ready and lets preview start against the wrong
        // media generation.
        const bool videoTransferPending = thisDownload &&
            downloadOperationInProgress &&
            !download.metadataOnly &&
            (download.state == DownloadState::Preparing ||
             download.state == DownloadState::Downloading);
        for(auto* row : timingRows_)
            if(row) row->SetActive(
                !videoTransferPending &&
                (descriptor.CanPlay() || mapperTimingWaitingForVideo));
        for(auto* row : videoOnlyRows_)
            if(row) row->SetActive(
                descriptor.CanPlay() && !videoTransferPending);
        if(offsetSetting_) offsetSetting_->set_interactable(
            descriptor.CanPlay() && !videoTransferPending);
        if(rateSetting_) rateSetting_->set_interactable(
            descriptor.CanPlay() && !videoTransferPending && !fitToSong_);
        if(fitToggle_) fitToggle_->set_interactable(
            descriptor.CanPlay() && !videoTransferPending);
        if(blackLeadInToggle_) blackLeadInToggle_->set_interactable(
            descriptor.CanPlay() && !videoTransferPending);
        if(playbackScrubber_) playbackScrubber_->set_interactable(
            descriptor.CanPlay() && !videoTransferPending);
        if(playPauseButton_) playPauseButton_->set_interactable(
            descriptor.CanPlay() && !videoTransferPending);
        if(removeButton_) removeButton_->set_interactable(
            !videoTransferPending &&
            (descriptor.hasUserOverride || descriptor.hasMapperDownload ||
             descriptor.hasMapperLocalFile));
        const auto timingHint = mapperTimingWaitingForVideo
            ? std::string(MapperTimingLockedHint)
            : std::string{};
        if(fitTimingHint_) fitTimingHint_->set_text(
            mapperTimingWaitingForVideo ? timingHint : std::string(FitTimingHint));
        if(rateTimingHint_) rateTimingHint_->set_text(
            mapperTimingWaitingForVideo ? timingHint : std::string(RateTimingHint));
        if(offsetTimingHint_) offsetTimingHint_->set_text(
            mapperTimingWaitingForVideo ? timingHint : std::string(OffsetTimingHint));
        if(leadInTimingHint_) leadInTimingHint_->set_text(
            mapperTimingWaitingForVideo ? timingHint : std::string(LeadInTimingHint));
        // Completion deliberately does not auto-open the new decoder. The
        // explicit Play button starts preview initialization after file and
        // manifest publication are fully separate from this Unity update.
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
        DiagnosticSessionLogger::Instance().MenuEvent(
            "preview_prepared", "SystemNormalization", {
                {"levelId", std::string(selected_->levelID)},
                {"songTime", std::to_string(previewSongTime_)}});
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
        if((audioLoadTask_ || levelDataLoadTask_) &&
           audioLoadLevelId_ == levelId)
            return;

        ReleaseOfficialSongAudio();
        previewAudioClip_ = nullptr;
        previewAudioSource_ = nullptr;
        audioLoadLevelId_ = levelId;

        // SongCore's custom preview provider returns the complete song file,
        // which is why custom-map previews already run to the real song end.
        // Official OST/DLC BeatmapLevel objects instead expose only the short
        // menu audition. Resolve the full level data and its gameplay audio
        // asynchronously so no disk/asset loading blocks Beat Saber's UI.
        if(!SongCore::API::Loading::GetLevelByLevelID(levelId))
        {
            try
            {
                auto* container = BSML::Helpers::GetDiContainer();
                auto* model = container
                    ? container->Resolve<GlobalNamespace::BeatmapLevelsModel*>()
                    : nullptr;
                officialSongAudioLoader_ = container
                    ? container->Resolve<GlobalNamespace::AudioClipAsyncLoader*>()
                    : nullptr;
                if(!model || !officialSongAudioLoader_)
                {
                    officialSongAudioLoader_ = nullptr;
                    terminalDownloadProgressLevelId_.clear();
                    PublishPreviewNotice(
                        "Beat Saber could not provide full song audio.");
                    return;
                }

                levelDataLoadTask_ = model->LoadBeatmapLevelDataAsync(
                    selected_->levelID,
                    // Packaged OST and DLC levels expose their gameplay song
                    // through the original level-data variant. The alternate
                    // NoEnvironmentKeywords variant is used by newer gameplay
                    // setup paths, but requesting it here makes the official
                    // loader return LoadBeatmapLevelDataResult::Error before
                    // AudioClipAsyncLoader can acquire the full song clip.
                    GlobalNamespace::BeatmapLevelDataVersion::Original,
                    // CORDL's generated empty C++ constructor does not clear
                    // CancellationToken::_source. Passing CancellationToken{}
                    // therefore forwards an indeterminate managed pointer and
                    // crashes when Beat Saber's asset loader checks whether
                    // cancellation was requested. Always obtain .NET's real,
                    // zero-source CancellationToken.None value instead.
                    System::Threading::CancellationToken::get_None());
                if(!levelDataLoadTask_)
                {
                    officialSongAudioLoader_ = nullptr;
                    terminalDownloadProgressLevelId_.clear();
                    PublishPreviewNotice(
                        "Beat Saber could not start loading this song.");
                }
                else
                {
                    // Opening a map preloads its audio opportunistically but
                    // is not itself a user operation. Keep a fresh editor's
                    // notice blank; the Play action publishes a loading notice
                    // if this task has not completed by the time it is needed.
                    PaperLogger.info(
                        "Loading full official song audio for Video Library preview: '{}'",
                        levelId);
                }
            }
            catch(const std::exception& error)
            {
                levelDataLoadTask_ = nullptr;
                officialSongAudioLoader_ = nullptr;
                terminalDownloadProgressLevelId_.clear();
                PublishPreviewNotice(
                    "Beat Saber could not load this song's full audio.");
                PaperLogger.error(
                    "Could not start full official-song audio load for '{}': {}",
                    levelId,
                    error.what());
            }
            return;
        }

        previewMediaData_ = selected_->__cordl_internal_get_previewMediaData();
        if(!previewMediaData_)
        {
            terminalDownloadProgressLevelId_.clear();
            PublishPreviewNotice("This song does not provide preview audio.");
            return;
        }

        // Beat Saber 1.40.8 owns preview-audio cancellation internally and no
        // longer accepts a caller-provided CancellationToken.
        audioLoadTask_ = previewMediaData_->GetPreviewAudioClip();
        if(!audioLoadTask_)
        {
            terminalDownloadProgressLevelId_.clear();
            PublishPreviewNotice(
                "Beat Saber could not start loading preview audio.");
        }
        // Successful background preload remains silent. TogglePreviewPlayback
        // reports the wait only when the player explicitly presses Play.
    }

    void VideoLibraryMenu::ReleaseOfficialSongAudio()
    {
        auto* loader = officialSongAudioLoader_;
        auto* levelData = officialSongLevelData_;
        officialSongAudioLoader_ = nullptr;
        officialSongLevelData_ = nullptr;
        levelDataLoadTask_ = nullptr;
        if(!loader || !levelData)
            return;

        try
        {
            // LoadSong uses Beat Saber's reference-counted audio cache. Match
            // every successful acquisition when the editor changes songs or
            // closes so full OST/DLC clips do not accumulate in Quest memory.
            GlobalNamespace::AudioClipAsyncLoaderExtensions::UnloadSong(
                loader,
                levelData);
        }
        catch(const std::exception& error)
        {
            // Audio teardown is best-effort and must never strand the player
            // inside the mod menu merely because Beat Saber already discarded
            // a level-data object during a scene transition.
            PaperLogger.warn(
                "Could not release official full-song preview audio: {}",
                error.what());
        }
    }

    void VideoLibraryMenu::TogglePreviewPlayback()
    {
        if(!selected_ || !VideoLibrary::Instance().Describe(selected_).CanPlay())
            return;

        bool completedPlaybackNeedsRestart = false;

        if(playWhenVideoReady_)
        {
            // A second press while the decoder is preparing is a stop request,
            // just like pressing Pause after ordinary playback has begun.
            playWhenVideoReady_ = false;
            ClearPreviewPreRoll();
            previewPaused_ = true;
            previewClockValid_ = false;
            DiagnosticSessionLogger::Instance().MenuEvent(
                "preview_paused", "VideoLibraryMenu", {
                    {"reason", "preparing_cancelled"}});
            ClearPreviewNotice();
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
                ClearPreviewPreRoll();
                previewClockValid_ = false;
                DiagnosticSessionLogger::Instance().MenuEvent(
                    "preview_paused", "VideoLibraryMenu", {
                        {"songTime", std::to_string(previewSongTime_)}});
                RefreshPlaybackControls();
                return;
            }

            // The clip can finish between UI ticks. Normalize that stale
            // "playing" flag here and continue into the explicit EOF restart
            // path. Unpausing an exhausted AudioSource cannot revive either
            // its audio or the drained video decoder.
            previewPlaying_ = false;
            previewPaused_ = false;
            completedPlaybackNeedsRestart = true;
        }

        const double duration = std::max(0.0f, selected_->songDuration);
        if(duration > 0.0 && previewSongTime_ >= duration - 0.01)
        {
            previewSongTime_ = 0.0;
            completedPlaybackNeedsRestart = true;
        }

        // Decoder work begins (or continues from selection-time prewarming)
        // immediately, but the song clock cannot advance until this deadline.
        // The same path is used for first Play and an explicit resume.
        BeginPreviewPreRoll();
        RequestSelectedAudio();
        if(!IsAlive(previewAudioClip_) || !IsAlive(songPreviewPlayer_))
        {
            playWhenAudioReady_ = true;
            terminalDownloadProgressLevelId_.clear();
            RefreshDetails();
            PublishPreviewNotice("Loading song audio...");
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
            if(!playback.SynchronizedAudioReady(previewSongTime_) ||
               !PreviewPreRollComplete())
            {
                playWhenVideoReady_ = true;
                playWhenAudioReady_ = false;
                terminalDownloadProgressLevelId_.clear();
                RefreshDetails();
                RefreshPlaybackControls();
                PublishPreviewNotice(
                    "Preparing synchronized video preview...");
                return;
            }
            if(!previewMeasurementStarted_ && playback.FirstFrameUploaded())
            {
                playback.BeginLibraryPreviewMeasurement(previewSongTime_);
                previewMeasurementStarted_ = true;
            }
            previewAudioSource_->set_time(static_cast<float>(previewSongTime_));
            songPreviewPlayer_->UnPauseCurrentChannel();
            ClearPreviewPreRoll();
            ResetPreviewClock(previewSongTime_);
            previewPlaying_ = true;
            previewPaused_ = false;
            playWhenAudioReady_ = false;
            playWhenVideoReady_ = false;
            DiagnosticSessionLogger::Instance().MenuEvent(
                "preview_started", "VideoLibraryMenu", {
                    {"mode", "resume"},
                    {"songTime", std::to_string(previewSongTime_)}});
            ClearPreviewNotice();
            RefreshPlaybackControls();
            return;
        }

        if(completedPlaybackNeedsRestart)
        {
            auto& playback = PlaybackSession::Instance();
            if(playback.IsLibraryPreviewActive())
                playback.RestartLibraryPreview(previewSongTime_);
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
            terminalDownloadProgressLevelId_.clear();
            RefreshDetails();
            RefreshPlaybackControls();
            PublishPreviewNotice("Video preview could not be prepared.");
            return;
        }
        playback.Tick(previewSongTime_);
        if(!playback.SynchronizedAudioReady(previewSongTime_) ||
           !PreviewPreRollComplete())
        {
            previewPlaying_ = false;
            playWhenAudioReady_ = false;
            playWhenVideoReady_ = true;
            terminalDownloadProgressLevelId_.clear();
            RefreshDetails();
            RefreshPlaybackControls();
            PublishPreviewNotice(
                "Preparing synchronized video preview...");
            return;
        }
        if(!previewMeasurementStarted_ && playback.FirstFrameUploaded())
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
            // SongPreviewPlayer interprets a positive duration as a request to
            // begin returning to Beat Saber's default menu music before that
            // duration expires. The transition begins early enough for its
            // configured fade-out, which can release Big Screen's clock source
            // two or three seconds before the song ends. A zero duration turns
            // off that internal timer. Big Screen detects the real clip/song
            // boundary in Tick and explicitly starts the next synchronized
            // loop, so there must be only one owner of end-of-preview timing.
            0.0f,
            nullptr);
        previewAudioSource_ = ActiveSongAudioSource(songPreviewPlayer_);
        if(IsAlive(previewAudioSource_))
            previewAudioSource_->set_time(static_cast<float>(previewSongTime_));
        ClearPreviewPreRoll();
        ResetPreviewClock(previewSongTime_);
        previewPlaying_ = true;
        previewPaused_ = false;
        playWhenAudioReady_ = false;
        playWhenVideoReady_ = false;
        DiagnosticSessionLogger::Instance().MenuEvent(
            "preview_started", "VideoLibraryMenu", {
                {"mode", "play"},
                {"songTime", std::to_string(previewSongTime_)}});
        ClearPreviewNotice();
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
        // Treat every loop as a fresh start. Restart clears the old reserve;
        // this timer lets the worker rebuild it before audio advances again.
        BeginPreviewPreRoll();
        scrubberFollowResumeTime_ = 0.0f;
        RefreshPlaybackControls();

        // Keep the Unity screen alive, but explicitly begin a new decoder
        // presentation pass. This clears EOF and stale mailbox/readiness state
        // before StartPreviewAudio asks whether synchronized audio may begin.
        // If a Quest MediaCodec implementation cannot resume after its flushed
        // seek, PlaybackSession performs one bounded decoder-only reopen.
        auto& playback = PlaybackSession::Instance();
        if(playback.IsLibraryPreviewActive())
            playback.RestartLibraryPreview(previewSongTime_);
        StartPreviewAudio();
        DiagnosticSessionLogger::Instance().MenuEvent(
            "preview_looped", "SystemNormalization");
        PaperLogger.info("Looped Video Library preview to the beginning");
    }

    void VideoLibraryMenu::StopPreviewAudio(bool returnToMenuMusic)
    {
        ClearPreviewNotice();
        // Clear ownership before calling back into Unity. A flow transition can
        // destroy SongPreviewPlayer, AudioSource, or AudioClip between frames.
        // If CrossfadeToDefault then throws, no later menu tick can see stale
        // media state and attempt to use it again.
        auto player = songPreviewPlayer_;
        auto clip = previewAudioClip_;
        playWhenAudioReady_ = false;
        playWhenVideoReady_ = false;
        ClearPreviewPreRoll();
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

        // Release only after SongPreviewPlayer has relinquished the active
        // channel. Unloading first could invalidate the AudioClip while Unity
        // is still crossfading away from it.
        ReleaseOfficialSongAudio();

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
        ReleaseOfficialSongAudio();
        previewPlaying_ = false;
        previewPaused_ = false;
        playWhenAudioReady_ = shouldResume;
        playWhenVideoReady_ = false;
        ClearPreviewPreRoll();
        previewClockValid_ = false;
        terminalDownloadProgressLevelId_.clear();
        PublishPreviewNotice(shouldResume
            ? "Beat Saber replaced the preview audio. Reloading it..."
            : "Beat Saber replaced the preview audio. Press Play to reload.");
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

    void VideoLibraryMenu::BeginPreviewPreRoll()
    {
        if(previewPreRollPending_)
            return;
        previewPreRollPending_ = true;
        previewPreRollReadyRealtime_ =
            static_cast<double>(UnityEngine::Time::get_realtimeSinceStartup()) +
            PreviewDecoderPreRollSeconds;
    }

    void VideoLibraryMenu::ClearPreviewPreRoll()
    {
        previewPreRollPending_ = false;
        previewPreRollReadyRealtime_ = 0.0;
    }

    bool VideoLibraryMenu::PreviewPreRollComplete() const
    {
        return !previewPreRollPending_ ||
            static_cast<double>(UnityEngine::Time::get_realtimeSinceStartup()) >=
                previewPreRollReadyRealtime_;
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
        DiagnosticSessionLogger::Instance().MenuEvent(
            "preview_seeked", "VideoLibraryMenu", {
                {"songTime", std::to_string(previewSongTime_)}});

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
        {
            // A scrub is an explicit seek, even when the user moves less than
            // the ordinary clock-discontinuity threshold. Restart invalidates
            // and empties every prefetched YUV frame before the new position
            // is requested, preventing old queued pictures from flashing or
            // delaying the selected timeline position.
            playback.RestartLibraryPreview(previewSongTime_);
            playback.Tick(previewSongTime_);
        }
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

        // The hierarchy is prewarmed and retained, but its map rows are not a
        // permanent startup snapshot. Resolve a small descriptor slice on each
        // safe browser frame after SongCore reports a completed refresh. If the
        // editor is open, keep its selected BeatmapLevel stable and defer until
        // the user backs out instead of replacing table objects underneath
        // active callbacks. This keeps large libraries from blocking Unity's
        // UI thread when Big Screen first appears.
        if(catalogRefreshRequested_ && !editorVisible_)
            PrewarmCatalogStep(8);

        const auto ownedTask = DownloadManager::Instance().Snapshot();
        if(!ownedTask.Active() && !ownedDownloadLevelId_.empty() &&
           ownedTask.levelId == ownedDownloadLevelId_)
            ownedDownloadLevelId_.clear();

        // Quest recording, activity focus changes, and SongPreviewPlayer
        // crossfades can destroy the native AudioClip/AudioSource while the
        // IL2CPP wrapper remains non-null. Never dereference that stale wrapper
        // and never escalate this recoverable media transition to the global
        // menu circuit breaker.
        if((previewAudioClip_.unsafePtr() && !IsAlive(previewAudioClip_)) ||
           (previewAudioSource_.unsafePtr() && !IsAlive(previewAudioSource_)))
            RecoverInvalidPreviewAudio("menu update");
        if(editorVisible_ && playWhenAudioReady_ &&
           !IsAlive(previewAudioClip_) && !audioLoadTask_ &&
           !levelDataLoadTask_)
            RequestSelectedAudio();
        if(!editorVisible_ && ++thumbnailTickCounter_ >= 9)
        {
            thumbnailTickCounter_ = 0;
            RefreshVisibleVideoThumbnails();
        }

        try
        {
          if(editorVisible_ && levelDataLoadTask_ &&
             levelDataLoadTask_->get_IsCompleted())
          {
            auto* completedTask = levelDataLoadTask_;
            levelDataLoadTask_ = nullptr;
            const bool selectionStillMatches = selected_ && selected_->levelID &&
                audioLoadLevelId_ == std::string(selected_->levelID);
            if(completedTask->get_IsCompletedSuccessfully() &&
               selectionStillMatches && officialSongAudioLoader_)
            {
                const auto result = completedTask->get_Result();
                if(!result.isError && result.beatmapLevelData)
                {
                    officialSongLevelData_ = result.beatmapLevelData;
                    audioLoadTask_ =
                        GlobalNamespace::AudioClipAsyncLoaderExtensions::LoadSong(
                            officialSongAudioLoader_,
                            officialSongLevelData_);
                    if(!audioLoadTask_)
                    {
                        terminalDownloadProgressLevelId_.clear();
                        PublishPreviewNotice(
                            "Beat Saber could not start loading full song audio.");
                        ReleaseOfficialSongAudio();
                        playWhenAudioReady_ = false;
                    }
                }
                else
                {
                    PaperLogger.error(
                        "Official full-song level-data load failed for '{}': "
                        "resultError={}, levelDataPresent={}",
                        audioLoadLevelId_,
                        result.isError,
                        result.beatmapLevelData != nullptr);
                    terminalDownloadProgressLevelId_.clear();
                    PublishPreviewNotice(
                        "Beat Saber could not load this song's level data.");
                    officialSongAudioLoader_ = nullptr;
                    playWhenAudioReady_ = false;
                }
            }
            else if(selectionStillMatches)
            {
                PaperLogger.error(
                    "Official full-song level-data task failed for '{}': "
                    "completedSuccessfully={}, canceled={}, faulted={}",
                    audioLoadLevelId_,
                    completedTask->get_IsCompletedSuccessfully(),
                    completedTask->get_IsCanceled(),
                    completedTask->get_IsFaulted());
                terminalDownloadProgressLevelId_.clear();
                PublishPreviewNotice(
                    "Beat Saber could not load this song's level data.");
                officialSongAudioLoader_ = nullptr;
                playWhenAudioReady_ = false;
            }
            RefreshDetails();
          }

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
                {
                    terminalDownloadProgressLevelId_.clear();
                    PublishPreviewNotice(
                        "Beat Saber returned no audio for this song.");
                }
                else
                {
                    PaperLogger.info(
                        "Video Library audio ready for '{}' ({:.2f}s clip, {:.2f}s song)",
                        audioLoadLevelId_,
                        previewAudioClip_->get_length(),
                        selected_->songDuration);
                    if(playWhenAudioReady_)
                        StartPreviewAudio();
                    else
                        ClearPreviewNotice();
                }
            }
            else
            {
                terminalDownloadProgressLevelId_.clear();
                PublishPreviewNotice(
                    "Beat Saber could not load this song's audio.");
                playWhenAudioReady_ = false;
                ReleaseOfficialSongAudio();
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
                if(playback.SynchronizedAudioReady(previewSongTime_) &&
                   PreviewPreRollComplete())
                {
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
                auto& playback = PlaybackSession::Instance();
                if(playback.IsLibraryPreviewActive())
                {
                    playback.Tick(previewSongTime_);
                    // A transparent/black lead-in starts audio before a video
                    // frame exists. Establish the diagnostics baseline only
                    // once the first visible picture arrives, preserving the
                    // same untimed-prewarm exclusion used by ordinary starts.
                    if(!previewMeasurementStarted_ &&
                       playback.FirstFrameUploaded())
                    {
                        playback.BeginLibraryPreviewMeasurement(previewSongTime_);
                        previewMeasurementStarted_ = true;
                    }
                }
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
            // Keep the throttled progress redraw alive through C++ container
            // preparation and manifest publication, not merely while the
            // Python-authored snapshot reports an active transfer state.
            const bool downloadActive =
                DownloadManager::Instance().OperationInProgress();
            if(editorVisible_ &&
               (downloadActive || periodicDownloadWasActive_ ||
                 !pendingDownloadRefreshLevelId_.empty() ||
                 static_cast<bool>(editorTransferNotice_)))
            {
                RefreshDetails();
                PollEditorTransferNotice();
            }
            periodicDownloadWasActive_ = downloadActive;
        }
        // Apply the latest event only after this frame's controller activation,
        // sibling SetActive calls, and downloader refresh have completed. TMP
        // then builds the mesh against the final layout on the following frame.
        PaintEditorNotice();
    }

    void VideoLibraryMenu::Refresh()
    {
        if(!browserController_ || !editorController_) return;
        // Claim playback before any retained selection or download state is
        // refreshed. SongPreviewPlayer continues ticking behind this flow;
        // without an explicit owner it can restart the previous song-menu
        // video during the Stop/Prepare gap while a file is replaced.
        PlaybackSession::Instance().SetLibraryPreviewOwnershipActive(true);
        active_ = true;
        // Beat Saber and SongCore retain their level objects for the menu
        // session. A first activation with hundreds of maps must not synchronously
        // parse any unrelated descriptor before a Configure Video deep-link is
        // honored. Build only the inexpensive pointer model here; Tick or the
        // hidden main-menu warmer advances descriptor slices afterward.
        if(catalogRefreshRequested_ && !catalogPrewarmModelReady_)
            BeginCatalogRebuild();
        if(editorVisible_) RefreshDetails();
        if(!Settings::Instance().ModEnabled())
        {
            const auto download = DownloadManager::Instance().Snapshot();
            if(download.Active() && !ownedDownloadLevelId_.empty() &&
               download.levelId == ownedDownloadLevelId_)
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
        CloseEditorNotice();
        editorVisible_ = false;
        pendingDownloadRefreshLevelId_.clear();
        terminalDownloadProgressLevelId_.clear();
        const std::string selectedLevelId =
            selected_ && selected_->levelID
                ? std::string(selected_->levelID)
                : std::string{};
        // Decoder shutdown is deliberately independent from Unity audio and
        // download cleanup. One subsystem throwing must not leave another
        // subsystem's background thread alive across a scene transition.
        try { StopPreviewAudio(true); }
        catch(const std::exception& error)
        {
            PaperLogger.error("Preview audio teardown failed during deactivation: {}", error.what());
        }
        // Downloads are owned by the process-wide manager, not this retained
        // view. Let a transfer finish after the player closes Big Screen; the
        // worker publishes its result without touching Unity UI objects.
        try
        {
            if(PlaybackSession::Instance().IsLibraryPreviewActive())
                PlaybackSession::Instance().Stop();
            PlaybackSession::Instance().ClearPreparedPreviewForLevel(
                selectedLevelId);
        }
        catch(const std::exception& error)
        {
            PaperLogger.error("Video decoder teardown failed during deactivation: {}", error.what());
        }
        // Release ownership only after the decoder and prepared selection have
        // been cleared. A new ordinary song-menu preview may start on the next
        // SongPreviewPlayer tick, but it can no longer resurrect the file that
        // this editor just removed or replaced.
        PlaybackSession::Instance().SetLibraryPreviewOwnershipActive(false);
    }

    void VideoLibraryMenu::RequestCatalogRefresh()
    {
        catalogRefreshRequested_ = true;
        // A SongCore refresh can arrive while startup prewarming is between
        // slices. Discard the partial index so the rebuilt model and its level
        // pointers all come from one completed SongCore generation.
        catalogPrewarmModelReady_ = false;
        catalogPrewarmIndex_ = 0;
    }

    void VideoLibraryMenu::StopActivePreview()
    {
        DiagnosticSessionLogger::Instance().MenuEvent(
            "preview_stopped", "VideoLibraryMenu");
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
        try { ScreenPreview::Instance().ActivateUserLayout(); }
        catch(const std::exception& error)
        {
            PaperLogger.error("Could not restore the settings preview: {}", error.what());
        }
    }
}
