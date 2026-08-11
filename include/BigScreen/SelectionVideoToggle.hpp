#pragma once

#include <string>

namespace BSML {
    class ToggleSetting;
}

namespace GlobalNamespace {
    class StandardLevelDetailView;
}

namespace SongCore::SongLoader {
    class CustomBeatmapLevel;
}

namespace BigScreen {
    /// Owns the persistent Video switch shown at the top-right of Beat Saber's
    /// song-selection screen.
    ///
    /// Its state is global rather than per-song: scrolling the song list never
    /// changes the switch or unexpectedly starts a sequence of new previews.
    class SelectionVideoToggle final {
    public:
        static SelectionVideoToggle& Instance();

        void CreateUi(GlobalNamespace::StandardLevelDetailView* detailView);
        void ForgetUi();

        void LevelSelected(
            bool isCustom,
            const std::string& levelId,
            SongCore::SongLoader::CustomBeatmapLevel* customLevel);

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

        void ToggleChanged(bool enabled);
        void RefreshUi();

        BSML::ToggleSetting* ui_ = nullptr;
        std::string selectedLevelId_;
        bool selectedLevelHasVideo_ = false;
        bool enabled_ = true;
    };
}
