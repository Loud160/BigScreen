// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "BigScreen/VideoEditorNoticeModel.hpp"
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
    class AudioClipAsyncLoader;
    class BeatmapLevel;
    class IBeatmapLevelData;
    class IPreviewMediaData;
    class SongPreviewPlayer;
    struct LoadBeatmapLevelDataResult;
}
namespace HMUI { class HoverHint; class ImageView; class InputFieldView; class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace System::Threading::Tasks { template<class TResult> class Task_1; }
namespace UnityEngine { class AudioClip; class AudioSource; class GameObject; class Sprite; }
namespace UnityEngine::UI { class Button; class VerticalLayoutGroup; }

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
            std::function<void(GlobalNamespace::BeatmapLevel*)> browseLocalVideo,
            std::function<void(GlobalNamespace::BeatmapLevel*)> openThumbnailPicker,
            bool activate = true);
        /// Releases every scene-owned UI/media reference before MenuCore
        /// replaces the menu hierarchy.
        void ForgetUi();
        void Refresh();
        /// Builds the retained song model without activating preview playback.
        /// Metadata is warmed in small Unity-thread slices so a large library
        /// cannot turn either application startup or first menu entry into one
        /// long synchronous pause. Returns true when rows are ready to present.
        bool PrewarmCatalogStep(std::size_t descriptorBudget);
        /// Invalidates the retained song catalog after an explicit SongCore
        /// refresh. Ordinary menu re-entry reuses the proven catalog instead
        /// of re-reading every map and rebuilding every row on Unity's thread.
        void RequestCatalogRefresh();
        void Tick(GlobalNamespace::SongPreviewPlayer* songPreviewPlayer);
        void Deactivate();
        void StopActivePreview();
        void RefreshDisplaySettings();
        /// Opens the editor for an installed level by stable level ID. The
        /// optional navigation step is suppressed during a flow's first
        /// activation so HMUI can receive the editor as its initial right-side
        /// controller instead of replacing an uninitialized controller stack.
        bool OpenEditorForLevelId(
            std::string_view levelId,
            bool navigateToEditor = true);
        /// Synchronizes the existing child editor after the center-screen file
        /// browser has committed a new user-owned video assignment.
        void LocalVideoAssignmentChanged(const std::string& fileName);
        /// Reloads thumbnail surfaces after the center-screen picker replaced
        /// the map's PNG in place. The sprite cache is keyed by path, so the
        /// stale decode must be evicted before the same path is shown again.
        void LocalThumbnailChanged(const std::string& thumbnailPath);

    private:
        enum class EditorTransferKind { None, Probe, Download };

        VideoLibraryMenu() = default;
        void BeginCatalogRebuild();
        void RebuildCatalog();
        void RebuildVisibleRows(bool preserveScrollPosition = false);
        void SelectRow(int row);
        void SelectLevel(
            GlobalNamespace::BeatmapLevel* level,
            bool navigateToEditor);
        void ShowBrowser();
        void ShowEditor();
        void ChangeFilter(int direction);
        void BeginUrlProbe();
        void StartOrCancelDownload();
        /// Handles one of the exact resolution buttons populated by the URL
        /// probe. The height is captured from the latest probe result rather
        /// rather than inferred from any separate playback conversion tier.
        void DownloadResolutionPressed(std::size_t buttonIndex);
        void RequestResolutionDownload(int height);
        void ConfirmPendingResolutionDownload();
        void StartResolutionDownload(int height);
        void PasteUrlFromClipboard();
        void SearchSelectedSongOnYouTube();
        void RefreshSelectedMapperMetadata();
        void RefreshLocalVideoStatus();
        /// Handles the two explicit confirmation choices. Unlinking always
        /// removes the assignment only; deletion additionally removes the
        /// active physical file after VideoLibrary validates its ownership
        /// boundary.
        void RemoveOverride(bool deleteFile);
        bool ApplyFitToSong();
        bool SaveTiming();
        void OpenEditorNotice();
        void CloseEditorNotice();
        std::optional<VideoEditorNoticeModel::RevisionToken> PublishEditorNotice(
            std::string message);
        void PublishPreviewNotice(std::string message);
        void ClearPreviewNotice();
        void BeginEditorTransferNotice(
            EditorTransferKind kind,
            std::string initialMessage);
        void CancelEditorTransferNotice();
        void PollEditorTransferNotice();
        void CreateEditorNoticeSurface();
        void DestroyEditorNoticeSurface();
        void QueueEditorNoticePaint();
        /// This is the replacement's only TextMeshPro write site. Events only
        /// queue a paint; the next main-thread frame applies it after BSML has
        /// finished changing controller visibility and layout.
        void PaintEditorNotice();
        /// Keeps mapper-authored addresses visually distinct from addresses
        /// the user typed or pasted without changing whether the field can be
        /// edited. Mapper metadata is muted gray; user input is full white.
        void RefreshUrlTextColor();
        void RefreshDetails();
        void ClearThumbnail();
        /// Rebinds every visible recycled cell from its current table index.
        /// Big Screen owns both the custom video thumbnail and the text/layout
        /// presentation, so refreshing only the thumbnail can leave retained
        /// HMUI cells displaying another row's song until pointer hover.
        void RefreshVisibleRowPresentation();
        void StartSelectedPreview();
        void RequestSelectedAudio();
        /// Releases the reference-counted full-song audio acquired for an
        /// official OST/DLC map. Custom maps keep using their existing preview
        /// provider because SongCore exposes the complete song through it.
        void ReleaseOfficialSongAudio();
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
        /// Arms a non-blocking decoder-only head start. The video worker keeps
        /// filling its bounded read-ahead queue while the song clock remains
        /// stationary, and Tick releases audio after the short deadline.
        void BeginPreviewPreRoll();
        void ClearPreviewPreRoll();
        bool PreviewPreRollComplete() const;
        void ResetPreviewClock(double songTimeSeconds);
        double AdvancePreviewClock(double rawAudioSongTimeSeconds);
        void RefreshPlaybackControls();
        void JumpToLetter(char letter);
        VideoOrigin SelectedVideoOrigin() const;

        HMUI::ViewController* browserController_ = nullptr;
        HMUI::ViewController* editorController_ = nullptr;
        std::function<void(bool)> navigate_;
        std::function<void(GlobalNamespace::BeatmapLevel*)> browseLocalVideo_;
        std::function<void(GlobalNamespace::BeatmapLevel*)> openThumbnailPicker_;
        // Only a task launched by this menu may be cancelled when the menu is
        // disabled or closed. Updater, showcase, and song-screen jobs share the
        // downloader singleton but have independent lifetimes.
        std::string ownedDownloadLevelId_;
        // Keeps the editor refreshing until a menu-owned download has finished
        // both its worker operation and its short library.json publication.
        // This is operational state only; it does not retain or render text.
        std::string pendingDownloadRefreshLevelId_;
        // Terminal probe/failure/cancellation progress remains visible until a
        // new editor action begins, independently from any status-text model.
        std::string terminalDownloadProgressLevelId_;
        VideoEditorNoticeModel editorNoticeModel_;
        VideoEditorNoticeModel::VisitToken editorNoticeVisit_;
        VideoEditorNoticeModel::TransferToken editorTransferNotice_;
        VideoEditorNoticeModel::RevisionToken previewNoticeRevision_;
        EditorTransferKind editorTransferKind_ = EditorTransferKind::None;
        bool editorTransferCancellationRequested_ = false;
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
        UnityEngine::UI::VerticalLayoutGroup* operationStatusHost_ = nullptr;
        TMPro::TextMeshProUGUI* operationStatusText_ = nullptr;
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
        // Second, deliberately separate confirmation shown only before a
        // LOCAL file is permanently deleted. Downloads stay one-step because
        // they can be fetched again; a user's own MP4 cannot.
        BSML::ModalView* deleteLocalConfirmModal_ = nullptr;
        TMPro::TextMeshProUGUI* deleteLocalConfirmText_ = nullptr;
        // The second destructive confirmation is intentionally bound to the
        // exact assignment displayed when it opened. Revalidate both values
        // before deletion so a background refresh cannot delete a replacement.
        std::string pendingLocalDeleteLevelId_;
        std::filesystem::path pendingLocalDeletePath_;
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
        UnityEngine::UI::Button* mapperRefreshButton_ = nullptr;
        UnityEngine::UI::Button* searchYouTubeButton_ = nullptr;
        UnityEngine::UI::Button* pasteUrlButton_ = nullptr;
        UnityEngine::UI::Button* checkUrlButton_ = nullptr;
        UnityEngine::UI::Button* showFileBrowserButton_ = nullptr;
        UnityEngine::UI::Button* setThumbnailButton_ = nullptr;
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
        UnityEngine::UI::Button* unlinkVideoButton_ = nullptr;
        UnityEngine::UI::Button* deleteVideoButton_ = nullptr;
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
        // Official Beat Saber maps deliberately expose only their short menu
        // audition through IPreviewMediaData. Load their level data first and
        // then ask AudioClipAsyncLoader for the full gameplay song; otherwise
        // the synchronized library preview loops after roughly eight seconds.
        System::Threading::Tasks::Task_1<GlobalNamespace::LoadBeatmapLevelDataResult>*
            levelDataLoadTask_ = nullptr;
        System::Threading::Tasks::Task_1<UnityW<UnityEngine::AudioClip>>* audioLoadTask_ = nullptr;
        GlobalNamespace::AudioClipAsyncLoader* officialSongAudioLoader_ = nullptr;
        GlobalNamespace::IBeatmapLevelData* officialSongLevelData_ = nullptr;
        UnityW<UnityEngine::AudioClip> previewAudioClip_ = nullptr;
        UnityW<UnityEngine::AudioSource> previewAudioSource_ = nullptr;
        SongLibraryFilter filter_ = SongLibraryFilter::All;
        std::string search_;
        std::string url_;
        std::string loadedThumbnailPath_;
        std::string probedUrl_;
        std::string completedVideoThumbnailIdentity_;
        std::string refreshedDownloadIdentity_;
        std::string audioLoadLevelId_;
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
        // Initial Play and every automatic loop wait briefly before releasing
        // audio. This is deliberately menu-preview state rather than decoder
        // state: gameplay already prewarms during its scene transition.
        bool previewPreRollPending_ = false;
        double previewPreRollReadyRealtime_ = 0.0;
        bool previewMeasurementStarted_ = false;
        bool previewClockValid_ = false;
        double smoothedPreviewSongTime_ = 0.0;
        double previewClockRealtime_ = 0.0;
        bool suppressScrubberCallback_ = false;
        float scrubberFollowResumeTime_ = 0.0f;
        bool suppressTimingCallbacks_ = false;
        bool suppressUrlCallback_ = false;
        bool mapperProvidedUrl_ = false;
        bool editorNoticePaintPending_ = false;
        int editorNoticePaintAfterFrame_ = -1;
        int tickCounter_ = 0;
        int thumbnailTickCounter_ = 0;
        int playbackControlsTickCounter_ = 0;
        bool periodicDownloadWasActive_ = false;
        // HMUI completes right-panel activation after ShowBrowser returns. A
        // reload performed while the controller is still inactive can be
        // overwritten by the final active layout. Retain the exact scroll
        // position and perform one definitive reload only after the browser
        // reports active.
        bool browserTableReloadPending_ = false;
        float browserReturnScrollPosition_ = 0.0f;
        bool catalogRefreshRequested_ = true;
        bool catalogPrewarmModelReady_ = false;
        std::size_t catalogPrewarmIndex_ = 0;
        int pendingDownloadHeight_ = 0;
    };
}
