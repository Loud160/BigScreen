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
    /// Owns the small, contextual Video switch shown beside a song's normal
    /// difficulty controls.
    ///
    /// This is intentionally selection state, not a global mod preference. A
    /// mapper-provided video starts enabled whenever the user selects a new
    /// song, and the choice survives difficulty changes for that same song.
    class SelectionVideoToggle final {
    public:
        static SelectionVideoToggle& Instance();

        void CreateUi(GlobalNamespace::StandardLevelDetailView* detailView);
        void ForgetUi();

        void LevelSelected(
            bool isCustom,
            const std::string& levelId,
            SongCore::SongLoader::CustomBeatmapLevel* customLevel);

        /// Applies a changed global default to the currently selected song.
        /// Future selections read the same value directly from Settings.
        void ApplyDefaultVideoEnabled(bool enabled);

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
