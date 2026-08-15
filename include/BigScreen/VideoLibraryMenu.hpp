// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "BigScreen/VideoLibrary.hpp"
#include "beatsaber-hook/shared/utils/typedefs.h"

namespace BSML {
    class ClickableText;
    class CustomListTableData;
    class IncrementSetting;
    class ModalView;
    class SliderSetting;
    class ToggleSetting;
}
namespace GlobalNamespace {
    class BeatmapLevel;
    class IPreviewMediaData;
    class SongPreviewPlayer;
}
namespace HMUI { class HoverHint; class ImageView; class InputFieldView; class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace System::Threading::Tasks { template<class TResult> class Task_1; }
namespace UnityEngine { class AudioClip; class AudioSource; class GameObject; class Sprite; }
namespace UnityEngine::UI { class Button; }

namespace BigScreen {
    enum class SongLibraryGroup { Custom, Wip, Ost, Dlc };
    enum class SongLibraryFilter { All, Custom, Wip, Ost, Dlc, Video };

    struct SongLibraryItem {
        GlobalNamespace::BeatmapLevel* level = nullptr;
        SongLibraryGroup group = SongLibraryGroup::Ost;
    };

    /// Owns the contents of two independent right-panel view controllers: a
    /// song browser and a focused editor. The parent flow coordinator swaps
    /// controllers during navigation, so hidden controls can never overlap or
    /// intercept input from the page currently visible to the player.
    class VideoLibraryMenu final {
    public:
        static VideoLibraryMenu& Instance();
        void CreateUi(
            HMUI::ViewController* browserController,
            HMUI::ViewController* editorController,
            std::function<void(bool showEditor)> navigate,
            std::function<void(GlobalNamespace::BeatmapLevel*)> browseLocalVideo);
        /// Releases every scene-owned UI/media reference before MenuCore
        /// replaces the menu hierarchy.
        void ForgetUi();
        void Refresh();
        void Tick(GlobalNamespace::SongPreviewPlayer* songPreviewPlayer);
        void Deactivate();
        void StopActivePreview();
        void RefreshDisplaySettings();
        /// Synchronizes the existing child editor after the center-screen file
        /// browser has committed a new user-owned video assignment.
        void LocalVideoAssignmentChanged(const std::string& fileName);

    private:
        VideoLibraryMenu() = default;
        void RebuildCatalog();
        void RebuildVisibleRows(bool preserveScrollPosition = false);
        void SelectRow(int row);
        void ShowBrowser();
        void ShowEditor();
        void ChangeFilter(int direction);
        void BeginUrlProbe();
        void StartOrCancelDownload();
        /// Handles one of the exact resolution buttons populated by the URL
        /// probe. The height is captured from the latest probe result rather
        /// than inferred from the global playback-resolution setting.
        void DownloadResolutionPressed(std::size_t buttonIndex);
        void RequestResolutionDownload(int height);
        void ConfirmPendingResolutionDownload();
        void StartResolutionDownload(int height);
        void PasteUrlFromClipboard();
        void SearchSelectedSongOnYouTube();
        void RefreshLocalVideoStatus();
        void RemoveOverride();
        bool ApplyFitToSong(bool reportStatus);
        bool SaveTiming();
        /// Keeps mapper-authored addresses visually distinct from addresses
        /// the user typed or pasted without changing whether the field can be
        /// edited. Mapper metadata is muted gray; user input is full white.
        void RefreshUrlTextColor();
        void RefreshDetails();
        void ClearThumbnail();
        void RefreshVisibleVideoThumbnails();
        void StartSelectedPreview();
        void RequestSelectedAudio();
        void TogglePreviewPlayback();
        void SeekPreview(float songTimeSeconds);
        /// Starts the song audition at previewSongTime_. Normal user actions
        /// rebuild the video session; an end-of-song loop reuses the already
        /// open decoder and seeks its external clock back to zero.
        void StartPreviewAudio();
        void LoopPreviewPlayback();
        void StopPreviewAudio(bool returnToMenuMusic);
        /// Clears Unity audio wrappers whose managed objects survived after
        /// their native AudioClip/AudioSource was destroyed. The next Tick
        /// can reload the selected song without treating this expected Unity
        /// lifecycle race as a fatal failure of the entire mod menu.
        void RecoverInvalidPreviewAudio(const char* context);
        void EnforcePausedPreviewAudio();
        void ResetPreviewClock(double songTimeSeconds);
        double AdvancePreviewClock(double rawAudioSongTimeSeconds);
        void RefreshPlaybackControls();
        void JumpToLetter(char letter);
        VideoOrigin SelectedVideoOrigin() const;

        HMUI::ViewController* browserController_ = nullptr;
        HMUI::ViewController* editorController_ = nullptr;
        std::function<void(bool)> navigate_;
        std::function<void(GlobalNamespace::BeatmapLevel*)> browseLocalVideo_;
        // Only a task launched by this menu may be cancelled when the menu is
        // disabled or closed. Updater, showcase, and song-screen jobs share the
        // downloader singleton but have independent lifetimes.
        std::string ownedDownloadLevelId_;
        BSML::CustomListTableData* list_ = nullptr;
        HMUI::InputFieldView* searchInput_ = nullptr;
        HMUI::InputFieldView* urlInput_ = nullptr;
        BSML::IncrementSetting* offsetSetting_ = nullptr;
        BSML::IncrementSetting* rateSetting_ = nullptr;
        BSML::SliderSetting* playbackScrubber_ = nullptr;
        BSML::ToggleSetting* fitToggle_ = nullptr;
        BSML::ToggleSetting* blackLeadInToggle_ = nullptr;
        TMPro::TextMeshProUGUI* browserTitle_ = nullptr;
        TMPro::TextMeshProUGUI* browserStorage_ = nullptr;
        TMPro::TextMeshProUGUI* filterText_ = nullptr;
        TMPro::TextMeshProUGUI* detailTitle_ = nullptr;
        TMPro::TextMeshProUGUI* detailText_ = nullptr;
        TMPro::TextMeshProUGUI* detailMapStorage_ = nullptr;
        TMPro::TextMeshProUGUI* detailLocalStorage_ = nullptr;
        TMPro::TextMeshProUGUI* detailLibraryStorage_ = nullptr;
        TMPro::TextMeshProUGUI* detailFreeStorage_ = nullptr;
        TMPro::TextMeshProUGUI* playbackTimeText_ = nullptr;
        TMPro::TextMeshProUGUI* urlInputText_ = nullptr;
        TMPro::TextMeshProUGUI* downloadButtonText_ = nullptr;
        TMPro::TextMeshProUGUI* localVideoStatusText_ = nullptr;
        TMPro::TextMeshProUGUI* removeConfirmationText_ = nullptr;
        TMPro::TextMeshProUGUI* downloadConfirmationText_ = nullptr;
        HMUI::ImageView* downloadProgressTrack_ = nullptr;
        HMUI::ImageView* downloadProgressFill_ = nullptr;
        HMUI::ImageView* playbackScrubberFill_ = nullptr;
        HMUI::ImageView* urlThumbnail_ = nullptr;
        BSML::ModalView* removeConfirmModal_ = nullptr;
        BSML::ModalView* downloadConfirmModal_ = nullptr;
        UnityEngine::GameObject* storageSpacer_ = nullptr;
        UnityEngine::GameObject* storagePanel_ = nullptr;
        // Timing rows remain visible but locked when a mapper supplied Cinema
        // timing and its MP4 has not been downloaded yet. Transport rows have
        // no useful function without playable media and remain video-only.
        std::vector<UnityEngine::GameObject*> timingRows_;
        std::vector<UnityEngine::GameObject*> videoOnlyRows_;
        UnityEngine::UI::Button* filterPreviousButton_ = nullptr;
        UnityEngine::UI::Button* filterNextButton_ = nullptr;
        UnityEngine::UI::Button* backToListButton_ = nullptr;
        UnityEngine::UI::Button* searchYouTubeButton_ = nullptr;
        UnityEngine::UI::Button* pasteUrlButton_ = nullptr;
        UnityEngine::UI::Button* checkUrlButton_ = nullptr;
        UnityEngine::UI::Button* showFileBrowserButton_ = nullptr;
        UnityEngine::UI::Button* downloadButton_ = nullptr;
        UnityEngine::GameObject* downloadButtonPlaceholder_ = nullptr;
        // Four slots cover the normal 480/720/1080/1440 probe result. When a
        // source offers none of those, slot zero is reused for the one real
        // lower height (for example 360p) returned by the downloader.
        std::vector<UnityEngine::UI::Button*> downloadTierButtons_;
        std::vector<int> displayedDownloadHeights_;
        UnityEngine::UI::Button* confirmDownloadButton_ = nullptr;
        UnityEngine::UI::Button* playPauseButton_ = nullptr;
        UnityEngine::UI::Button* removeButton_ = nullptr;
        HMUI::HoverHint* fitTimingHint_ = nullptr;
        HMUI::HoverHint* rateTimingHint_ = nullptr;
        HMUI::HoverHint* offsetTimingHint_ = nullptr;
        HMUI::HoverHint* leadInTimingHint_ = nullptr;
        std::vector<SongLibraryItem> catalog_;
        std::vector<SongLibraryItem*> visible_;
        GlobalNamespace::BeatmapLevel* selected_ = nullptr;
        GlobalNamespace::IPreviewMediaData* previewMediaData_ = nullptr;
        // Unity can destroy menu audio objects during a flow transition while
        // their IL2CPP wrappers remain non-null. UnityW makes every truth test
        // validate m_CachedPtr, preventing stale references from being treated
        // as live merely because their managed pointer still has an address.
        UnityW<GlobalNamespace::SongPreviewPlayer> songPreviewPlayer_ = nullptr;
        System::Threading::Tasks::Task_1<UnityW<UnityEngine::AudioClip>>* audioLoadTask_ = nullptr;
        UnityW<UnityEngine::AudioClip> previewAudioClip_ = nullptr;
        UnityW<UnityEngine::AudioSource> previewAudioSource_ = nullptr;
        SongLibraryFilter filter_ = SongLibraryFilter::All;
        std::string search_;
        std::string url_;
        std::string transientStatus_;
        std::string loadedThumbnailPath_;
        std::string probedUrl_;
        std::string completedVideoThumbnailIdentity_;
        std::string audioLoadLevelId_;
        std::string autoPlayedDownloadIdentity_;
        UnityEngine::Sprite* loadedThumbnailSprite_ = nullptr;
        double offset_ = 0.0;
        double rate_ = 1.0;
        double previewSongTime_ = 0.0;
        bool fitToSong_ = false;
        bool blackDuringLeadIn_ = false;
        bool active_ = false;
        bool editorVisible_ = false;
        bool previewPlaying_ = false;
        bool previewPaused_ = false;
        bool playWhenAudioReady_ = false;
        // Opening FFmpeg and producing the first drawable picture must finish
        // before the audition clock begins. Otherwise the decoder chases audio
        // that is already advancing and the diagnostics correctly report the
        // resulting media-timestamp gaps as skipped frames.
        bool playWhenVideoReady_ = false;
        bool previewMeasurementStarted_ = false;
        bool previewClockValid_ = false;
        double smoothedPreviewSongTime_ = 0.0;
        double previewClockRealtime_ = 0.0;
        bool suppressScrubberCallback_ = false;
        float scrubberFollowResumeTime_ = 0.0f;
        bool suppressTimingCallbacks_ = false;
        bool suppressUrlCallback_ = false;
        bool mapperProvidedUrl_ = false;
        int tickCounter_ = 0;
        int thumbnailTickCounter_ = 0;
        int playbackControlsTickCounter_ = 0;
        bool periodicDownloadWasActive_ = false;
        int pendingDownloadHeight_ = 0;
    };
}
