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
    namespace {
        void PlaceRightSideToggle(
            BSML::ToggleSetting* setting,
            std::string_view objectName,
            float verticalPosition)
        {
            if(!setting)
                return;

            setting->get_gameObject()->set_name(objectName);
            if(auto* layout = setting->GetComponent<UnityEngine::UI::LayoutElement*>())
                layout->set_preferredWidth(50.0f);

            auto rect = setting->get_transform().cast<UnityEngine::RectTransform>();
            if(rect)
            {
                // The BSML toggle template normally stretches across the full
                // song-detail view. Two such roots at the same position overlap
                // and the upper one intercepts pointer events for both visible
                // switches. Give each control its own compact right-side hit
                // area and a separate vertical row below Solo Play instead.
                rect->set_anchorMin({0.5f, 0.5f});
                rect->set_anchorMax({0.5f, 0.5f});
                rect->set_pivot({0.5f, 0.5f});
                rect->set_anchoredPosition({20.0f, verticalPosition});
                rect->set_sizeDelta({50.0f, 8.0f});
            }

            if(setting->text)
            {
                setting->text->set_fontSize(3.25f);
                auto labelRect =
                    setting->text->get_transform().cast<UnityEngine::RectTransform>();
                if(labelRect)
                {
                    // Each label occupies the left portion of its compact row,
                    // with the switch immediately to its right.
                    labelRect->set_anchorMin({0.5f, 0.5f});
                    labelRect->set_anchorMax({0.5f, 0.5f});
                    labelRect->set_pivot({0.5f, 0.5f});
                    labelRect->set_anchoredPosition({0.0f, 0.0f});
                    labelRect->set_sizeDelta({24.0f, 8.0f});
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
                    switchRect->set_anchoredPosition({19.0f, 0.0f});
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
    }

    SelectionVideoToggle& SelectionVideoToggle::Instance()
    {
        static SelectionVideoToggle control;
        return control;
    }

    void SelectionVideoToggle::CreateUi(
        GlobalNamespace::StandardLevelDetailView* detailView)
    {
        if(!detailView || previewUi_ || inMapUi_)
            return;

        const auto& settings = Settings::Instance();
        inMapEnabled_ = settings.VideoEnabled();

        // The controls are global preferences rather than properties of the
        // selected song. Create both unconditionally and keep them in separate
        // right-side rows so scrolling between any built-in or custom song
        // never changes their visibility or makes their hit areas overlap.
        previewUi_ = BSML::Lite::CreateToggle(
            detailView,
            "Preview Video",
            settings.MenuPreviewEnabled(),
            UnityEngine::Vector2{20.0f, 19.0f},
            [this](bool value)
            {
                PreviewToggleChanged(value);
            });
        inMapUi_ = BSML::Lite::CreateToggle(
            detailView,
            "Video In Map",
            inMapEnabled_,
            UnityEngine::Vector2{20.0f, 10.0f},
            [this](bool value)
            {
                InMapToggleChanged(value);
            });

        if(!previewUi_ || !inMapUi_)
        {
            PaperLogger.error("Could not create both song-selection video toggles");
            return;
        }

        PlaceRightSideToggle(previewUi_, "Big Screen Preview Video Toggle", 19.0f);
        PlaceRightSideToggle(inMapUi_, "Big Screen Video In Map Toggle", 10.0f);
        BSML::Lite::AddHoverHint(
            previewUi_,
            "Plays the selected song's video on the song-selection screen.");
        BSML::Lite::AddHoverHint(
            inMapUi_,
            "Shows the selected song's video while playing the map.");
        RefreshUi();
        PaperLogger.info("Created stacked Preview Video and Video In Map controls");
    }

    void SelectionVideoToggle::ForgetUi()
    {
        // The menu scene owns the actual object and destroys it normally. Drop
        // our native pointer during StandardLevelDetailView.OnDestroy so a
        // later menu scene can construct a fresh control safely.
        previewUi_ = nullptr;
        inMapUi_ = nullptr;
    }

    void SelectionVideoToggle::SongSelectionShown()
    {
        // Stop() deliberately retains the parsed map configuration. If Beat
        // Saber returns to the same still-selected song, this common settings
        // path can rebuild the decoder and screen without rereading the map or
        // waiting for SongCore to raise another selection event.
        MenuPreviewPreferenceChanged();
    }

    void SelectionVideoToggle::SongSelectionHidden()
    {
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
        bool isCustom,
        const std::string& levelId,
        SongCore::SongLoader::CustomBeatmapLevel* customLevel)
    {
        auto& playback = PlaybackSession::Instance();

        if(!isCustom || !customLevel)
        {
            selectedLevelId_ = levelId;
            selectedLevelHasVideo_ = false;
            inMapEnabled_ = Settings::Instance().VideoEnabled();
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
        inMapEnabled_ = Settings::Instance().VideoEnabled();
        playback.Prepare(customLevel);
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
            // Clear the selected-map identity as well as stopping playback.
            // SongCore selections are intentionally ignored while disabled,
            // so retaining this data could resurrect the wrong song if the
            // user changes selection before re-enabling Big Screen.
            selectedLevelId_.clear();
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

    bool SelectionVideoToggle::IsEnabledForSelectedLevel() const
    {
        return selectedLevelHasVideo_ && inMapEnabled_;
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
        if(!previewUi_ && !inMapUi_)
            return;

        const auto& settings = Settings::Instance();
        inMapEnabled_ = settings.VideoEnabled();

        // SetIsOnWithoutNotify prevents selection refreshes from masquerading
        // as a user click and reopening a decoder that is already running.
        SetToggleWithoutNotification(inMapUi_, inMapEnabled_);
        SetToggleWithoutNotification(previewUi_, settings.MenuPreviewEnabled());
        if(previewUi_)
            previewUi_->set_interactable(inMapEnabled_);

        // Visibility deliberately does not inspect selectedLevelHasVideo_: both
        // switches control all songs the user subsequently browses. Only the
        // master mod switch removes them from Beat Saber's song screen.
        if(inMapUi_)
            inMapUi_->get_gameObject()->SetActive(settings.ModEnabled());
        if(previewUi_)
            previewUi_->get_gameObject()->SetActive(settings.ModEnabled());
    }
}
