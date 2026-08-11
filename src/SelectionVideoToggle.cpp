#include "BigScreen/SelectionVideoToggle.hpp"

#include <iomanip>
#include <sstream>

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerSensitivityFlag.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/Toggle.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        void PlaceRightSideToggle(
            BSML::ToggleSetting* setting,
            std::string_view objectName,
            float verticalPosition)
        {
            if(!setting)
                return;

            setting->get_gameObject()->set_name(objectName);
            if(auto* layout = setting->GetComponent<UnityEngine::UI::LayoutElement*>())
                layout->set_preferredWidth(50.0f);

            auto rect = setting->get_transform().cast<UnityEngine::RectTransform>();
            if(rect)
            {
                // The BSML toggle template normally stretches across the full
                // song-detail view. Two such roots at the same position overlap
                // and the upper one intercepts pointer events for both visible
                // switches. Give each control its own compact right-side hit
                // area and a separate vertical row below Solo Play instead.
                rect->set_anchorMin({0.5f, 0.5f});
                rect->set_anchorMax({0.5f, 0.5f});
                rect->set_pivot({0.5f, 0.5f});
                rect->set_anchoredPosition({20.0f, verticalPosition});
                rect->set_sizeDelta({50.0f, 8.0f});
            }

            if(setting->text)
            {
                setting->text->set_fontSize(3.25f);
                auto labelRect =
                    setting->text->get_transform().cast<UnityEngine::RectTransform>();
                if(labelRect)
                {
                    // Each label occupies the left portion of its compact row,
                    // with the switch immediately to its right.
                    labelRect->set_anchorMin({0.5f, 0.5f});
                    labelRect->set_anchorMax({0.5f, 0.5f});
                    labelRect->set_pivot({0.5f, 0.5f});
                    labelRect->set_anchoredPosition({0.0f, 0.0f});
                    labelRect->set_sizeDelta({24.0f, 8.0f});
                }
            }

            if(auto switchTransform = setting->get_transform()->Find("SwitchView"))
            {
                auto switchRect = switchTransform.cast<UnityEngine::RectTransform>();
                if(switchRect)
                {
                    switchRect->set_anchorMin({0.5f, 0.5f});
                    switchRect->set_anchorMax({0.5f, 0.5f});
                    switchRect->set_pivot({0.5f, 0.5f});
                    switchRect->set_anchoredPosition({19.0f, 0.0f});
                }
            }
        }

        void SetToggleWithoutNotification(BSML::ToggleSetting* setting, bool value)
        {
            if(!setting)
                return;
            setting->currentValue = value;
            if(setting->toggle)
                setting->toggle->SetIsOnWithoutNotify(value);
        }

        bool ExplicitContentAllowed()
        {
            auto* container = BSML::Helpers::GetDiContainer();
            auto* model = container
                ? container->Resolve<GlobalNamespace::PlayerDataModel*>()
                : nullptr;
            auto* data = model ? model->get_playerData() : nullptr;
            return data && data->get_desiredSensitivityFlag().value__ >=
                GlobalNamespace::PlayerSensitivityFlag::Explicit.value__;
        }

        std::string Megabytes(std::uint64_t bytes)
        {
            std::ostringstream text;
            text << std::fixed << std::setprecision(1)
                 << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MB";
            return text.str();
        }
    }

    SelectionVideoToggle& SelectionVideoToggle::Instance()
    {
        static SelectionVideoToggle control;
        return control;
    }

    void SelectionVideoToggle::CreateUi(
        GlobalNamespace::StandardLevelDetailView* detailView)
    {
        if(!detailView || previewUi_ || inMapUi_)
            return;

        const auto& settings = Settings::Instance();
        inMapEnabled_ = settings.VideoEnabled();

        // The controls are global preferences rather than properties of the
        // selected song. Create both unconditionally and keep them in separate
        // right-side rows so scrolling between any built-in or custom song
        // never changes their visibility or makes their hit areas overlap.
        previewUi_ = BSML::Lite::CreateToggle(
            detailView,
            "Preview Video",
            settings.MenuPreviewEnabled(),
            UnityEngine::Vector2{20.0f, 19.0f},
            [this](bool value)
            {
                PreviewToggleChanged(value);
            });
        inMapUi_ = BSML::Lite::CreateToggle(
            detailView,
            "Video In Map",
            inMapEnabled_,
            UnityEngine::Vector2{20.0f, 10.0f},
            [this](bool value)
            {
                InMapToggleChanged(value);
            });
        downloadButton_ = BSML::Lite::CreateUIButton(
            detailView,
            "Download Video",
            UnityEngine::Vector2{20.0f, 0.5f},
            UnityEngine::Vector2{31.0f, 7.0f},
            [this]() { DownloadButtonPressed(); });
        downloadStatus_ = BSML::Lite::CreateText(
            detailView,
            "",
            2.5f,
            UnityEngine::Vector2{20.0f, -5.5f},
            UnityEngine::Vector2{42.0f, 7.0f});
        if(downloadStatus_)
            downloadStatus_->set_alignment(TMPro::TextAlignmentOptions::Center);

        if(!previewUi_ || !inMapUi_)
        {
            PaperLogger.error("Could not create both song-selection video toggles");
            return;
        }

        PlaceRightSideToggle(previewUi_, "Big Screen Preview Video Toggle", 19.0f);
        PlaceRightSideToggle(inMapUi_, "Big Screen Video In Map Toggle", 10.0f);
        BSML::Lite::AddHoverHint(
            previewUi_,
            "Plays the selected song's video on the song-selection screen.");
        BSML::Lite::AddHoverHint(
            inMapUi_,
            "Shows the selected song's video while playing the map.");
        RefreshUi();
        PaperLogger.info("Created stacked Preview Video and Video In Map controls");
    }

    void SelectionVideoToggle::ForgetUi()
    {
        // The menu scene owns the actual object and destroys it normally. Drop
        // our native pointer during StandardLevelDetailView.OnDestroy so a
        // later menu scene can construct a fresh control safely.
        previewUi_ = nullptr;
        inMapUi_ = nullptr;
        downloadButton_ = nullptr;
        downloadStatus_ = nullptr;
    }

    void SelectionVideoToggle::SongSelectionShown()
    {
        // Stop() deliberately retains the parsed map configuration. If Beat
        // Saber returns to the same still-selected song, this common settings
        // path can rebuild the decoder and screen without rereading the map or
        // waiting for SongCore to raise another selection event.
        MenuPreviewPreferenceChanged();
    }

    void SelectionVideoToggle::SongSelectionHidden()
    {
        // yt-dlp retains its .part file. Returning to this song offers Resume
        // instead of wasting storage or network data.
        DownloadManager::Instance().Cancel();
        auto& playback = PlaybackSession::Instance();
        if(!playback.IsMenuPreviewActive())
            return;

        // Restrict cleanup to the menu context. The same view can be disabled
        // during the transition into gameplay, and a late disable callback
        // must never tear down a gameplay or Replay-owned screen.
        playback.Stop();
        PaperLogger.info("Stopped video preview because song selection was hidden");
    }

    void SelectionVideoToggle::LevelSelected(
        const std::string& levelId,
        GlobalNamespace::BeatmapLevel* level)
    {
        auto& playback = PlaybackSession::Instance();

        // SongCore also raises its selection event when difficulty changes.
        // Preserve the prepared decoder for a difficulty-only change. The
        // global switch itself never depends on which level is selected.
        if(levelId == selectedLevelId_)
        {
            RefreshUi();
            return;
        }

        selectedLevelId_ = levelId;
        selectedLevel_ = level;
        inMapEnabled_ = Settings::Instance().VideoEnabled();
        playback.Prepare(level);
        selectedLevelHasVideo_ = playback.HasPreparedVideo();
        RefreshUi();

        if(selectedLevelHasVideo_ && inMapEnabled_ && IsMenuPreviewEnabled())
            playback.Start(PlaybackContext::MenuPreview);
    }

    void SelectionVideoToggle::ApplyGlobalVideoEnabled(bool enabled)
    {
        inMapEnabled_ = enabled;
        RefreshUi();

        if(!selectedLevelHasVideo_)
            return;

        auto& playback = PlaybackSession::Instance();
        if(!inMapEnabled_)
            playback.Stop();
        else if(IsMenuPreviewEnabled())
            playback.Start(PlaybackContext::MenuPreview);
    }

    void SelectionVideoToggle::ModEnabledChanged(bool enabled)
    {
        auto& playback = PlaybackSession::Instance();
        if(!enabled)
        {
            // Clear the selected-map identity as well as stopping playback.
            // SongCore selections are intentionally ignored while disabled,
            // so retaining this data could resurrect the wrong song if the
            // user changes selection before re-enabling Big Screen.
            selectedLevelId_.clear();
            selectedLevel_ = nullptr;
            selectedLevelHasVideo_ = false;
            inMapEnabled_ = Settings::Instance().VideoEnabled();
            playback.Prepare(nullptr);
        }
        else if(selectedLevelHasVideo_ && inMapEnabled_ && IsMenuPreviewEnabled())
        {
            playback.Start(PlaybackContext::MenuPreview);
        }
        RefreshUi();
    }

    void SelectionVideoToggle::MenuPreviewPreferenceChanged()
    {
        RefreshUi();
        auto& playback = PlaybackSession::Instance();
        if(!IsMenuPreviewEnabled())
        {
            // A preview preference change must never stop gameplay if a mod
            // menu is opened by another mod while a replay is active.
            if(playback.IsMenuPreviewActive())
                playback.Stop();
            return;
        }

        if(selectedLevelHasVideo_ && inMapEnabled_ && playback.HasPreparedVideo())
            playback.Start(PlaybackContext::MenuPreview);
    }

    bool SelectionVideoToggle::IsEnabledForSelectedLevel() const
    {
        return selectedLevelHasVideo_ && inMapEnabled_;
    }

    void SelectionVideoToggle::DownloadButtonPressed()
    {
        auto& downloader = DownloadManager::Instance();
        const auto snapshot = downloader.Snapshot();
        if(snapshot.Active())
        {
            downloader.Cancel();
            return;
        }
        if(!selectedLevel_) return;

        const auto descriptor = VideoLibrary::Instance().Describe(selectedLevel_);
        if(!descriptor.downloadUrl) return;
        DownloadRequest request;
        request.levelId = descriptor.levelId;
        request.songName = descriptor.songName;
        request.songAuthor = descriptor.songAuthor;
        request.sourceUrl = *descriptor.downloadUrl;
        request.origin = descriptor.downloadOrigin;
        request.explicitContentAllowed = ExplicitContentAllowed();
        if(descriptor.mapperDefinition)
        {
            request.offsetSeconds = descriptor.mapperDefinition->offsetSeconds;
            request.playbackRate = descriptor.mapperDefinition->playbackRate;
        }
        std::string error;
        if(!downloader.Start(std::move(request), error) && downloadStatus_)
            downloadStatus_->set_text(error);
        TickDownloadUi();
    }

    void SelectionVideoToggle::TickDownloadUi()
    {
        if(!downloadButton_ || !downloadStatus_) return;
        const auto snapshot = DownloadManager::Instance().Snapshot();
        const bool forSelection = !snapshot.levelId.empty() &&
                                  snapshot.levelId == selectedLevelId_;
        const auto descriptor = selectedLevel_
            ? VideoLibrary::Instance().Describe(selectedLevel_)
            : VideoDescriptor{};
        const bool show = Settings::Instance().ModEnabled() &&
                          (descriptor.CanDownload() || forSelection);
        downloadButton_->get_gameObject()->SetActive(show && !descriptor.CanPlay());
        downloadStatus_->get_gameObject()->SetActive(show);
        if(!show) return;

        if(forSelection && snapshot.Active())
        {
            BSML::Lite::SetButtonText(downloadButton_, "Pause Download");
            downloadStatus_->set_text(snapshot.totalBytes
                ? Megabytes(snapshot.downloadedBytes) + " / " +
                    Megabytes(snapshot.totalBytes)
                : snapshot.message);
        }
        else if(forSelection && snapshot.state == DownloadState::Failed)
        {
            BSML::Lite::SetButtonText(downloadButton_, "Retry Download");
            downloadStatus_->set_text(snapshot.message);
        }
        else if(forSelection && snapshot.state == DownloadState::Cancelled)
        {
            BSML::Lite::SetButtonText(downloadButton_, "Resume Download");
            downloadStatus_->set_text(snapshot.message);
        }
        else if(descriptor.CanPlay())
        {
            downloadStatus_->set_text("Video ready");
            if(!selectedLevelHasVideo_)
            {
                PlaybackSession::Instance().Prepare(selectedLevel_);
                selectedLevelHasVideo_ = PlaybackSession::Instance().HasPreparedVideo();
                if(selectedLevelHasVideo_ && inMapEnabled_ && IsMenuPreviewEnabled())
                    PlaybackSession::Instance().Start(PlaybackContext::MenuPreview);
            }
        }
        else
        {
            BSML::Lite::SetButtonText(downloadButton_, "Download Video");
            downloadStatus_->set_text("This map has a video available");
        }
    }

    void SelectionVideoToggle::PreviewToggleChanged(bool enabled)
    {
        Settings::Instance().SetMenuPreviewEnabled(enabled);
        PaperLogger.info(
            "Song-selection video preview changed to {}",
            Settings::Instance().MenuPreviewEnabled() ? "on" : "off");

        // Use the same transition path as the main settings page so preview
        // playback stops immediately and the switch reflects dependencies.
        MenuPreviewPreferenceChanged();
    }

    void SelectionVideoToggle::InMapToggleChanged(bool enabled)
    {
        Settings::Instance().SetVideoEnabled(enabled);
        inMapEnabled_ = enabled;
        RefreshUi();
        PaperLogger.info(
            "Video-in-map switch changed to {}",
            inMapEnabled_ ? "on" : "off");

        // The switch remains useful even when the current song has no video;
        // in that case it simply controls the next video map the user selects.
        if(!selectedLevelHasVideo_)
            return;

        auto& playback = PlaybackSession::Instance();
        if(!inMapEnabled_)
        {
            // Stop immediately so the menu gives direct visual feedback and no
            // decoder thread remains active for a video the user disabled.
            playback.Stop();
        }
        else if(IsMenuPreviewEnabled())
        {
            // Stop preserves the parsed selection metadata, allowing a quick
            // re-enable without reading JSON or reopening the map selection.
            playback.Start(PlaybackContext::MenuPreview);
        }
    }

    void SelectionVideoToggle::RefreshUi()
    {
        if(!previewUi_ && !inMapUi_)
            return;

        const auto& settings = Settings::Instance();
        inMapEnabled_ = settings.VideoEnabled();

        // SetIsOnWithoutNotify prevents selection refreshes from masquerading
        // as a user click and reopening a decoder that is already running.
        SetToggleWithoutNotification(inMapUi_, inMapEnabled_);
        SetToggleWithoutNotification(previewUi_, settings.MenuPreviewEnabled());
        if(previewUi_)
            previewUi_->set_interactable(inMapEnabled_);

        // Visibility deliberately does not inspect selectedLevelHasVideo_: both
        // switches control all songs the user subsequently browses. Only the
        // master mod switch removes them from Beat Saber's song screen.
        if(inMapUi_)
            inMapUi_->get_gameObject()->SetActive(settings.ModEnabled());
        if(previewUi_)
            previewUi_->get_gameObject()->SetActive(settings.ModEnabled());
        TickDownloadUi();
    }
}
