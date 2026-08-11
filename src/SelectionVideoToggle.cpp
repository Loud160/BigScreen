#include "BigScreen/SelectionVideoToggle.hpp"

#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/Settings.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
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

        enabled_ = Settings::Instance().VideoEnabled();

        // StandardLevelDetailView spans the complete song screen. Keep the
        // template's full-width root so neither child can be clipped, then move
        // its label and switch together into the top-right corner at the same
        // height as Beat Saber's mode/Back navigation row.
        ui_ = BSML::Lite::CreateToggle(
            detailView,
            "Video",
            enabled_,
            UnityEngine::Vector2{0.0f, 28.0f},
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

        if(auto* layout = ui_->GetComponent<UnityEngine::UI::LayoutElement*>())
            layout->set_preferredWidth(90.0f);
        auto rect = ui_->get_transform().cast<UnityEngine::RectTransform>();
        if(rect)
            rect->set_anchoredPosition({0.0f, 28.0f});

        if(ui_->text)
        {
            ui_->text->set_fontSize(3.5f);
            auto labelRect = ui_->text->get_transform().cast<UnityEngine::RectTransform>();
            if(labelRect)
            {
                labelRect->set_anchorMin({0.5f, 0.5f});
                labelRect->set_anchorMax({0.5f, 0.5f});
                labelRect->set_pivot({0.5f, 0.5f});
                labelRect->set_anchoredPosition({25.0f, 0.0f});
                labelRect->set_sizeDelta({16.0f, 8.0f});
            }
        }

        if(auto switchTransform = ui_->get_transform()->Find("SwitchView"))
        {
            auto switchRect = switchTransform.cast<UnityEngine::RectTransform>();
            if(switchRect)
            {
                switchRect->set_anchorMin({0.5f, 0.5f});
                switchRect->set_anchorMax({0.5f, 0.5f});
                switchRect->set_pivot({0.5f, 0.5f});
                switchRect->set_anchoredPosition({39.0f, 0.0f});
            }
        }
        BSML::Lite::AddHoverHint(
            ui_,
            "Global Big Screen video switch. Its state stays fixed while you scroll songs.");
        RefreshUi();
        PaperLogger.info("Created global Video toggle at the top-right of song selection");
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
            enabled_ = Settings::Instance().VideoEnabled();
            playback.Prepare(nullptr);
            RefreshUi();
            return;
        }

        // SongCore also raises its selection event when difficulty changes.
        // Preserve the prepared decoder for a difficulty-only change. The
        // global switch itself never depends on which level is selected.
        if(levelId == selectedLevelId_)
        {
            RefreshUi();
            return;
        }

        selectedLevelId_ = levelId;
        enabled_ = Settings::Instance().VideoEnabled();
        playback.Prepare(customLevel);
        selectedLevelHasVideo_ = playback.HasPreparedVideo();
        RefreshUi();

        if(selectedLevelHasVideo_ && enabled_ && IsMenuPreviewEnabled())
            playback.Start(PlaybackContext::MenuPreview);
    }

    void SelectionVideoToggle::ApplyGlobalVideoEnabled(bool enabled)
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
            enabled_ = Settings::Instance().VideoEnabled();
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
        Settings::Instance().SetVideoEnabled(enabled);
        enabled_ = enabled;
        PaperLogger.info(
            "Global song video switch changed to {}",
            enabled_ ? "on" : "off");

        // The switch remains useful even when the current song has no video;
        // in that case it simply controls the next video map the user selects.
        if(!selectedLevelHasVideo_)
            return;

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
        ui_->get_gameObject()->SetActive(Settings::Instance().ModEnabled());
    }
}
