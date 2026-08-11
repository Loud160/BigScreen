#pragma once

#include <string>

namespace BSML {
    class ToggleSetting;
}

namespace GlobalNamespace {
    class BeatmapLevel;
    class StandardLevelDetailView;
}

namespace TMPro {
    class TextMeshProUGUI;
}

namespace UnityEngine::UI {
    class Button;
}

namespace BigScreen {
    /// Owns the persistent Preview Video and Video In Map switches shown at
    /// the top-right of Beat Saber's song-selection screen.
    ///
    /// Its state is global rather than per-song: scrolling the song list never
    /// changes the switch or unexpectedly starts a sequence of new previews.
    class SelectionVideoToggle final {
    public:
        static SelectionVideoToggle& Instance();

        void CreateUi(GlobalNamespace::StandardLevelDetailView* detailView);
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

        bool IsEnabledForSelectedLevel() const;

    private:
        SelectionVideoToggle() = default;

        void PreviewToggleChanged(bool enabled);
        void InMapToggleChanged(bool enabled);
        void DownloadButtonPressed();
        void RefreshUi();

        BSML::ToggleSetting* previewUi_ = nullptr;
        BSML::ToggleSetting* inMapUi_ = nullptr;
        UnityEngine::UI::Button* downloadButton_ = nullptr;
        TMPro::TextMeshProUGUI* downloadStatus_ = nullptr;
        GlobalNamespace::BeatmapLevel* selectedLevel_ = nullptr;
        std::string selectedLevelId_;
        bool selectedLevelHasVideo_ = false;
        bool inMapEnabled_ = true;
        bool resumeWhenSongAudioStarts_ = false;
        bool resumeWaitReported_ = false;
    };
}
