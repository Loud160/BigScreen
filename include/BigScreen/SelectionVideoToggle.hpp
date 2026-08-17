// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "BigScreen/VideoLibrary.hpp"

namespace BSML {
    class FloatingScreen;
    class IncrementSetting;
    class ModalView;
    class ToggleSetting;
}

namespace GlobalNamespace {
    class BeatmapLevel;
    class MissionLevelDetailViewController;
    class StandardLevelDetailView;
}

namespace TMPro {
    class TextMeshProUGUI;
}

namespace UnityEngine {
    class Component;
    class GameObject;
}

namespace UnityEngine::UI {
    class Button;
}

namespace BigScreen {
    /// Owns the persistent Preview Video, Video In Map, and Screen Layout
    /// controls on a floating row above Beat Saber's top title/back screen.
    ///
    /// Its state is global rather than per-song: scrolling the song list never
    /// changes the switch or unexpectedly starts a sequence of new previews.
    class SelectionVideoToggle final {
    public:
        static SelectionVideoToggle& Instance();

        void CreateUi(GlobalNamespace::StandardLevelDetailView* detailView);
        /// Creates the same global top-row controls used by Solo while omitting
        /// the Solo-only Cinema download row. Campaign owns a different detail
        /// controller, but both screens share the same ScreenSystem title bar.
        void CreateCampaignUi(
            GlobalNamespace::MissionLevelDetailViewController* detailView);
        /// Shows or hides only Campaign's copy of the shared global controls.
        /// Campaign has no song-preview transport or Cinema download strip to
        /// resume, cancel, or otherwise mutate at these lifecycle boundaries.
        void CampaignSelectionShown();
        void CampaignSelectionHidden();
        void ForgetUi();

        /// Reapplies the current preview preference when Beat Saber makes the
        /// song-detail panel visible again. Playback is resumed only when the
        /// selected map still has a prepared video and all global switches
        /// permit it.
        void SongSelectionShown();

        /// Stops only a menu preview when the song-detail panel is hidden.
        /// Beat Saber can retain this view instead of destroying it while it
        /// returns home or presents another flow coordinator, so OnDestroy is
        /// not a reliable cleanup boundary by itself.
        void SongSelectionHidden();

        void LevelSelected(
            const std::string& levelId,
            GlobalNamespace::BeatmapLevel* level);

        void TickDownloadUi();

        /// Synchronizes an active menu video to Beat Saber's audible selected-
        /// song preview. A return from gameplay is held here until the song
        /// clip, rather than the default menu clip, is actually playing.
        void TickSongPreview(double songTimeSeconds, bool selectedSongAudioIsAudible);

        /// Mirrors a changed global state into this control and immediately
        /// applies it to the currently selected video map, if there is one.
        void ApplyGlobalVideoEnabled(bool enabled);

        /// Tears down all selection/video state when the master mod switch is
        /// off and restores normal selection handling when it is turned on.
        void ModEnabledChanged(bool enabled);

        /// Starts or stops only menu-preview playback after its independent
        /// preference changes; gameplay playback is intentionally untouched.
        void MenuPreviewPreferenceChanged();

        /// Mirrors a layout change made from Big Screen's main settings page
        /// into the compact selector on Beat Saber's song-selection screen.
        /// Both controls edit the same saved setting; this method only redraws
        /// the second UI surface and never writes another preference value.
        void ScreenLayoutPreferenceChanged();

        bool IsEnabledForSelectedLevel() const;

    private:
        SelectionVideoToggle() = default;

        void PreviewToggleChanged(bool enabled);
        void InMapToggleChanged(bool enabled);
        void LayoutSelectorChanged(float value);
        void DownloadButtonPressed();
        void OpenResolutionDialog();
        void RefreshResolutionDialog();
        void ResolutionButtonPressed(std::size_t buttonIndex);
        void RequestResolutionDownload(int height);
        void ConfirmPendingResolutionDownload();
        void StartResolutionDownload(int height);
        void ReportDownloadFailure(
            const std::string& detail,
            bool metadataCheck = false);
        void CreateTopControls(
            UnityEngine::Component* anchor,
            bool requireTopScreen);
        void BringHeaderControlsToFront();
        /// Anchors the floating canvas just above ScreenSystem's top screen,
        /// with a detail-view coordinate only as a compatibility fallback.
        /// Runs again when song selection is shown because the hierarchy's
        /// final transforms are not guaranteed during construction.
        void PositionControlsRow();
        void RefreshUi();

        // Slim self-raycasting canvas hosting the three global controls above
        // the stock title bar. Owned by this class, not by the detail view.
        BSML::FloatingScreen* controlsScreen_ = nullptr;
        // Generic Component anchor shared by StandardLevelDetailView (Solo)
        // and MissionLevelDetailViewController (Campaign). It is used only to
        // locate the active ScreenSystem and its stock top title screen.
        UnityEngine::Component* controlsAnchor_ = nullptr;
        // Campaign's detail transform is not in the same coordinate space as
        // Solo's fallback anchor. Until its ScreenSystem top screen is ready,
        // keep the panel hidden and retry rather than placing it far offscreen.
        bool controlsRequireTopScreen_ = false;
        bool controlsPositionPending_ = false;
        bool controlsVisibleRequested_ = false;
        BSML::ToggleSetting* previewUi_ = nullptr;
        BSML::ToggleSetting* inMapUi_ = nullptr;
        BSML::IncrementSetting* layoutUi_ = nullptr;
        // Cinema-parity download row between the difficulty selector and the
        // Play/Practice buttons: cloned native background, italic status
        // label, and the Download/Cancel/Retry button on one centered row.
        UnityEngine::GameObject* downloadRow_ = nullptr;
        UnityEngine::UI::Button* downloadButton_ = nullptr;
        TMPro::TextMeshProUGUI* downloadStatus_ = nullptr;
        BSML::ModalView* resolutionModal_ = nullptr;
        TMPro::TextMeshProUGUI* resolutionModalText_ = nullptr;
        std::vector<UnityEngine::UI::Button*> resolutionButtons_;
        std::vector<int> displayedResolutionHeights_;
        UnityEngine::UI::Button* confirmResolutionButton_ = nullptr;
        GlobalNamespace::BeatmapLevel* selectedLevel_ = nullptr;
        std::string selectedLevelId_;
        // A failed worker remains in a terminal state until another task is
        // started. Remember the exact failure already presented so the
        // per-frame song-menu refresh cannot spam identical dialogs.
        std::string reportedDownloadFailure_;
        bool selectedLevelHasVideo_ = false;
        bool inMapEnabled_ = true;
        bool resumeWhenSongAudioStarts_ = false;
        bool resumeWaitReported_ = false;
        float nextDownloadUiRefreshTime_ = 0.0f;
        // Only cancel a downloader task when this song-selection surface was
        // the owner that started it. Downloads begun in Big Screen's library
        // must survive the stock song panel being hidden.
        std::string ownedDownloadLevelId_;
        std::string probedDownloadUrl_;
        int pendingDownloadHeight_ = 0;
        bool resolutionModalOpen_ = false;
        // Map metadata is immutable while one song remains selected. Keeping
        // its descriptor here avoids reparsing Cinema JSON and probing files
        // ten times per second merely to redraw an unchanged download button.
        VideoDescriptor selectedDescriptor_;
    };
}
