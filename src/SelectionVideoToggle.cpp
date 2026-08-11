#include "BigScreen/SelectionVideoToggle.hpp"

#include "BigScreen/PlaybackSession.hpp"
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
        // The anchored location sits at the lower-right edge of the parameter
        // area in Beat Saber 1.37 and leaves the Play/Practice buttons clear.
        ui_ = BSML::Lite::CreateToggle(
            detailView,
            "Video",
            enabled_,
            UnityEngine::Vector2{36.0f, -24.0f},
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
            enabled_ = true;
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
        enabled_ = true;
        playback.Prepare(customLevel);
        selectedLevelHasVideo_ = playback.HasPreparedVideo();
        RefreshUi();

        if(selectedLevelHasVideo_ && IsMenuPreviewEnabled())
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
        ui_->get_gameObject()->SetActive(selectedLevelHasVideo_);
    }
}
