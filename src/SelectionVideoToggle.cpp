#include "BigScreen/SelectionVideoToggle.hpp"

#include <algorithm>
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
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        void PlaceTopBarToggle(
            BSML::ToggleSetting* setting,
            std::string_view objectName,
            float horizontalPosition)
        {
            if(!setting)
                return;

            setting->get_gameObject()->set_name(objectName);
            if(auto* layout = setting->GetComponent<UnityEngine::UI::LayoutElement*>())
                layout->set_preferredWidth(42.0f);

            auto rect = setting->get_transform().cast<UnityEngine::RectTransform>();
            if(rect)
            {
                // Put both global controls back on Beat Saber's top strip, at
                // the same height as Solo Play and the navigation bar. Unlike
                // the original top-row implementation, each toggle receives a
                // separate compact root, so their invisible hit areas cannot
                // overlap and steal clicks from one another.
                rect->set_anchorMin({0.5f, 0.5f});
                rect->set_anchorMax({0.5f, 0.5f});
                rect->set_pivot({0.5f, 0.5f});
                rect->set_anchoredPosition({horizontalPosition, 28.0f});
                rect->set_sizeDelta({42.0f, 8.0f});
            }

            if(setting->text)
            {
                setting->text->set_fontSize(3.25f);
                auto labelRect =
                    setting->text->get_transform().cast<UnityEngine::RectTransform>();
                if(labelRect)
                {
                    // Keep the label and switch together inside that compact
                    // root. The two roots sit side by side across the top row.
                    labelRect->set_anchorMin({0.5f, 0.5f});
                    labelRect->set_anchorMax({0.5f, 0.5f});
                    labelRect->set_pivot({0.5f, 0.5f});
                    labelRect->set_anchoredPosition({-8.0f, 0.0f});
                    labelRect->set_sizeDelta({22.0f, 8.0f});
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
                    switchRect->set_anchoredPosition({11.0f, 0.0f});
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
        if(!detailView || previewUi_ || inMapUi_ || layoutUi_)
            return;

        const auto& settings = Settings::Instance();
        inMapEnabled_ = settings.VideoEnabled();

        // The controls are global preferences rather than properties of the
        // selected song. Create both unconditionally and place their compact
        // roots side by side on the permanent song-selection header.
        previewUi_ = BSML::Lite::CreateToggle(
            detailView,
            "Preview Video",
            settings.MenuPreviewEnabled(),
            UnityEngine::Vector2{-18.0f, 28.0f},
            [this](bool value)
            {
                PreviewToggleChanged(value);
            });
        inMapUi_ = BSML::Lite::CreateToggle(
            detailView,
            "Video In Map",
            inMapEnabled_,
            UnityEngine::Vector2{28.0f, 28.0f},
            [this](bool value)
            {
                InMapToggleChanged(value);
            });
        layoutUi_ = BSML::Lite::CreateIncrementSetting(
            detailView,
            "Screen Layout",
            0,
            1.0f,
            static_cast<float>(settings.ActiveScreenLayout() + 1),
            1.0f,
            3.0f,
            UnityEngine::Vector2{-57.0f, 28.0f},
            [](float value)
            {
                auto& settings = Settings::Instance();
                settings.SetActiveScreenLayout(
                    std::clamp(static_cast<int>(value) - 1, 0, 2));
                auto& playback = PlaybackSession::Instance();
                const bool restartPreview = playback.IsMenuPreviewActive();
                playback.RefreshDisplaySettings();
                if(restartPreview)
                    playback.Start(PlaybackContext::MenuPreview);
            });
        if(layoutUi_)
        {
            auto rect = layoutUi_->get_transform().cast<UnityEngine::RectTransform>();
            rect->set_anchorMin({0.5f, 0.5f});
            rect->set_anchorMax({0.5f, 0.5f});
            rect->set_pivot({0.5f, 0.5f});
            rect->set_anchoredPosition({-57.0f, 28.0f});
            rect->set_sizeDelta({34.0f, 8.0f});
        }
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

        PlaceTopBarToggle(previewUi_, "Big Screen Preview Video Toggle", -18.0f);
        PlaceTopBarToggle(inMapUi_, "Big Screen Video In Map Toggle", 28.0f);
        BSML::Lite::AddHoverHint(
            previewUi_,
            "Plays the selected song's video on the song-selection screen.");
        BSML::Lite::AddHoverHint(
            inMapUi_,
            "Shows the selected song's video while playing the map.");
        BSML::Lite::AddHoverHint(
            layoutUi_,
            "Selects one of your three saved screen layouts for previews and gameplay.");
        RefreshUi();
        PaperLogger.info("Created top-row Preview Video and Video In Map controls");
    }

    void SelectionVideoToggle::ForgetUi()
    {
        // The menu scene owns the actual object and destroys it normally. Drop
        // our native pointer during StandardLevelDetailView.OnDestroy so a
        // later menu scene can construct a fresh control safely.
        previewUi_ = nullptr;
        inMapUi_ = nullptr;
        layoutUi_ = nullptr;
        downloadButton_ = nullptr;
        downloadStatus_ = nullptr;
    }

    void SelectionVideoToggle::SongSelectionShown()
    {
        RefreshUi();

        // Returning from gameplay does not necessarily restart Beat Saber's
        // audio preview for the still-selected song. Defer the video restart
        // until SongPreviewPlayer confirms that the non-default song clip is
        // actually audible; otherwise Big Screen can visibly replay a silent
        // video while Beat Saber remains on its normal menu soundtrack.
        auto& playback = PlaybackSession::Instance();
        resumeWhenSongAudioStarts_ =
            selectedLevelHasVideo_ &&
            inMapEnabled_ &&
            IsMenuPreviewEnabled() &&
            playback.HasPreparedVideo();
        resumeWaitReported_ = false;
    }

    void SelectionVideoToggle::SongSelectionHidden()
    {
        resumeWhenSongAudioStarts_ = false;
        resumeWaitReported_ = false;
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

        resumeWhenSongAudioStarts_ = false;
        resumeWaitReported_ = false;
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
            resumeWhenSongAudioStarts_ = false;
            resumeWaitReported_ = false;
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
        resumeWhenSongAudioStarts_ = false;
        resumeWaitReported_ = false;
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

    void SelectionVideoToggle::TickSongPreview(
        double songTimeSeconds,
        bool selectedSongAudioIsAudible)
    {
        auto& playback = PlaybackSession::Instance();

        if(resumeWhenSongAudioStarts_)
        {
            if(!selectedLevelHasVideo_ ||
               !inMapEnabled_ ||
               !IsMenuPreviewEnabled() ||
               !playback.HasPreparedVideo())
            {
                resumeWhenSongAudioStarts_ = false;
                resumeWaitReported_ = false;
                return;
            }

            if(!selectedSongAudioIsAudible)
            {
                if(!resumeWaitReported_)
                {
                    PaperLogger.info(
                        "Waiting for Beat Saber song-preview audio before resuming the menu video");
                    resumeWaitReported_ = true;
                }
                return;
            }

            playback.Start(PlaybackContext::MenuPreview);
            resumeWhenSongAudioStarts_ = false;
            resumeWaitReported_ = false;
            PaperLogger.info("Resumed menu video with Beat Saber's song-preview audio");
        }

        // Drive the video only while the selected song clip is genuinely
        // playing. This prevents a muted/faded/default menu channel from
        // advancing the video independently after a gameplay transition.
        if(playback.IsMenuPreviewActive() && selectedSongAudioIsAudible)
            playback.Tick(songTimeSeconds);
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
            request.fitToSong = descriptor.mapperDefinition->fitToSong;
            request.blackDuringLeadIn = descriptor.mapperDefinition->blackDuringLeadIn;
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
        if(!previewUi_ && !inMapUi_ && !layoutUi_)
            return;

        const auto& settings = Settings::Instance();
        inMapEnabled_ = settings.VideoEnabled();

        // SetIsOnWithoutNotify prevents selection refreshes from masquerading
        // as a user click and reopening a decoder that is already running.
        SetToggleWithoutNotification(inMapUi_, inMapEnabled_);
        SetToggleWithoutNotification(previewUi_, settings.MenuPreviewEnabled());
        if(previewUi_)
            previewUi_->set_interactable(inMapEnabled_);
        if(layoutUi_)
        {
            layoutUi_->set_Value(
                static_cast<float>(settings.ActiveScreenLayout() + 1));
            layoutUi_->set_interactable(settings.ModEnabled());
        }

        // Visibility deliberately does not inspect selectedLevelHasVideo_: both
        // switches control all songs the user subsequently browses. Only the
        // master mod switch removes them from Beat Saber's song screen.
        if(inMapUi_)
            inMapUi_->get_gameObject()->SetActive(settings.ModEnabled());
        if(previewUi_)
            previewUi_->get_gameObject()->SetActive(settings.ModEnabled());
        if(layoutUi_)
            layoutUi_->get_gameObject()->SetActive(settings.ModEnabled());
        TickDownloadUi();
    }
}
