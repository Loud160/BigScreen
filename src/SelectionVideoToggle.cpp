#include "BigScreen/SelectionVideoToggle.hpp"

#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/Settings.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/UI/Toggle.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "main.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

namespace BigScreen {
    SelectionVideoToggle& SelectionVideoToggle::Instance()
    {
        static SelectionVideoToggle control;
        return control;
    }

    void SelectionVideoToggle::CreateUi(
        GlobalNamespace::StandardLevelDetailView* detailView)
    {
        if(!detailView || ui_)
            return;

        // StandardLevelDetailView owns the characteristic, difficulty, level
        // statistics, and Play controls. Parenting here keeps Video adjacent to
        // the choice it affects instead of hiding it in a separate mod screen.
        // Keep the control centered horizontally within the detail view. The
        // first 1.37 build placed its center near the panel's right edge; that
        // left the Video label visible but clipped the actual switch. A centered
        // anchor keeps the complete BSML control inside the panel at every
        // supported UI scale while remaining below the difficulty controls.
        ui_ = BSML::Lite::CreateToggle(
            detailView,
            "Video",
            enabled_,
            UnityEngine::Vector2{0.0f, -24.0f},
            [this](bool value)
            {
                ToggleChanged(value);
            });

        if(!ui_)
        {
            PaperLogger.error("Could not create the song-selection Video toggle");
            return;
        }

        ui_->get_gameObject()->set_name("Big Screen Video Toggle");
        if(ui_->text)
            ui_->text->set_fontSize(3.5f);
        BSML::Lite::AddHoverHint(
            ui_,
            "Show this song's map video in the menu and during play.");
        RefreshUi();
        PaperLogger.info("Created complete song-detail Video toggle at centered anchor");
    }

    void SelectionVideoToggle::ForgetUi()
    {
        // The menu scene owns the actual object and destroys it normally. Drop
        // our native pointer during StandardLevelDetailView.OnDestroy so a
        // later menu scene can construct a fresh control safely.
        ui_ = nullptr;
    }

    void SelectionVideoToggle::LevelSelected(
        bool isCustom,
        const std::string& levelId,
        SongCore::SongLoader::CustomBeatmapLevel* customLevel)
    {
        auto& playback = PlaybackSession::Instance();

        if(!isCustom || !customLevel)
        {
            selectedLevelId_ = levelId;
            selectedLevelHasVideo_ = false;
            enabled_ = Settings::Instance().VideoEnabledByDefault();
            playback.Prepare(nullptr);
            RefreshUi();
            return;
        }

        // SongCore also raises its selection event when difficulty changes.
        // Preserve the user's switch choice and the active preview for that
        // case; only a genuinely different level resets Video to on.
        if(levelId == selectedLevelId_)
        {
            RefreshUi();
            return;
        }

        selectedLevelId_ = levelId;
        enabled_ = Settings::Instance().VideoEnabledByDefault();
        playback.Prepare(customLevel);
        selectedLevelHasVideo_ = playback.HasPreparedVideo();
        RefreshUi();

        if(selectedLevelHasVideo_ && enabled_ && IsMenuPreviewEnabled())
            playback.Start(PlaybackContext::MenuPreview);
    }

    void SelectionVideoToggle::ApplyDefaultVideoEnabled(bool enabled)
    {
        enabled_ = enabled;
        RefreshUi();

        if(!selectedLevelHasVideo_)
            return;

        auto& playback = PlaybackSession::Instance();
        if(!enabled_)
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
            selectedLevelHasVideo_ = false;
            enabled_ = Settings::Instance().VideoEnabledByDefault();
            playback.Prepare(nullptr);
        }
        else if(selectedLevelHasVideo_ && enabled_ && IsMenuPreviewEnabled())
        {
            playback.Start(PlaybackContext::MenuPreview);
        }
        RefreshUi();
    }

    void SelectionVideoToggle::MenuPreviewPreferenceChanged()
    {
        auto& playback = PlaybackSession::Instance();
        if(!IsMenuPreviewEnabled())
        {
            // A preview preference change must never stop gameplay if a mod
            // menu is opened by another mod while a replay is active.
            if(playback.IsMenuPreviewActive())
                playback.Stop();
            return;
        }

        if(selectedLevelHasVideo_ && enabled_ && playback.HasPreparedVideo())
            playback.Start(PlaybackContext::MenuPreview);
    }

    bool SelectionVideoToggle::IsEnabledForSelectedLevel() const
    {
        return selectedLevelHasVideo_ && enabled_;
    }

    void SelectionVideoToggle::ToggleChanged(bool enabled)
    {
        if(!selectedLevelHasVideo_)
            return;

        enabled_ = enabled;
        PaperLogger.info(
            "Selected song video switched {}",
            enabled_ ? "on" : "off");
        auto& playback = PlaybackSession::Instance();
        if(!enabled_)
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
        if(!ui_)
            return;

        // SetIsOnWithoutNotify prevents selection refreshes from masquerading
        // as a user click and reopening a decoder that is already running.
        ui_->currentValue = enabled_;
        if(ui_->toggle)
            ui_->toggle->SetIsOnWithoutNotify(enabled_);
        ui_->get_gameObject()->SetActive(
            Settings::Instance().ModEnabled() && selectedLevelHasVideo_);
    }
}
