#include "BigScreen/SelectionVideoToggle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerSensitivityFlag.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "HMUI/CurvedCanvasSettings.hpp"
#include "HMUI/ImageView.hpp"
#include "HMUI/Screen.hpp"
#include "HMUI/ScreenSystem.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Rect.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/Toggle.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        // The three global controls live on their own slim floating canvas.
        // Children of Beat Saber's main canvas outside its rect still render
        // but never receive VR pointer raycasts (the original top-strip
        // placement proved this), while a FloatingScreen carries its own
        // canvas and raycaster and is interactive at any world position.
        constexpr float ControlsScreenWidth = 150.0f;
        constexpr float ControlsScreenHeight = 12.0f;
        // The row hangs just above Beat Saber's title bar / back button,
        // which live on the ScreenSystem's dedicated top screen: the row's
        // bottom edge clears that screen's top edge by this gap. A detail-
        // view-relative height is only the fallback if the top screen cannot
        // be resolved. The small forward offset keeps this canvas clear of
        // any coplanar stock graphics so its raycaster always wins.
        constexpr float ControlsRowGapAboveTopScreen = 2.0f;
        constexpr float ControlsRowDetailLocalY = 42.0f;
        constexpr float ControlsRowDetailLocalZ = -1.0f;
        // The row is vertically centered on its own floating strip.
        constexpr float SongHeaderControlY = 0.0f;
        constexpr float SongHeaderLabelControlGap = 2.5f;
        constexpr float SongHeaderGroupGap = 3.0f;
        // These are one visual group centered over the middle song panel.
        // Root widths: selector 50, each toggle 44, with one group gap
        // between neighbors: total span = 50 + 3 + 44 + 3 + 44 = 144,
        // so the group runs -72..72 around the detail view's center line.
        constexpr float LayoutSelectorWidth = 50.0f;
        constexpr float ToggleRootWidth = 44.0f;
        constexpr float SongHeaderGroupSpan =
            LayoutSelectorWidth + ToggleRootWidth * 2.0f +
            SongHeaderGroupGap * 2.0f;
        constexpr float LayoutSelectorX =
            -SongHeaderGroupSpan * 0.5f + LayoutSelectorWidth * 0.5f;
        constexpr float PreviewToggleX = LayoutSelectorX +
            LayoutSelectorWidth * 0.5f + SongHeaderGroupGap +
            ToggleRootWidth * 0.5f;
        constexpr float InMapToggleX = PreviewToggleX +
            ToggleRootWidth + SongHeaderGroupGap;

        void PlaceTopBarToggle(
            BSML::ToggleSetting* setting,
            std::string_view objectName,
            float horizontalPosition)
        {
            if(!setting)
                return;

            setting->get_gameObject()->set_name(objectName);
            if(auto* layout = setting->GetComponent<UnityEngine::UI::LayoutElement*>())
                layout->set_preferredWidth(ToggleRootWidth);

            auto rect = setting->get_transform().cast<UnityEngine::RectTransform>();
            if(rect)
            {
                // Each toggle receives a separate compact root on the shared
                // header row, so their invisible hit areas cannot overlap and
                // steal clicks from one another.
                rect->set_anchorMin({0.5f, 0.5f});
                rect->set_anchorMax({0.5f, 0.5f});
                rect->set_pivot({0.5f, 0.5f});
                rect->set_anchoredPosition(
                    {horizontalPosition, SongHeaderControlY});
                rect->set_sizeDelta({ToggleRootWidth, 8.0f});
            }

            if(setting->text)
            {
                setting->text->set_fontSize(3.1f);
                setting->text->set_enableWordWrapping(false);
                setting->text->set_overflowMode(
                    TMPro::TextOverflowModes::Overflow);
                setting->text->set_alignment(
                    TMPro::TextAlignmentOptions::MidlineRight);
                auto labelRect =
                    setting->text->get_transform().cast<UnityEngine::RectTransform>();
                if(labelRect)
                {
                    // Anchor the label's visible right edge immediately before
                    // the switch. Right-aligning the text is important: the
                    // stock prefab left-aligns it inside a wide settings-row
                    // rectangle, which made the visual gap much larger than
                    // the actual distance between these RectTransforms.
                    labelRect->set_anchorMin({0.5f, 0.5f});
                    labelRect->set_anchorMax({0.5f, 0.5f});
                    labelRect->set_pivot({1.0f, 0.5f});
                    labelRect->set_anchoredPosition(
                        {-SongHeaderLabelControlGap * 0.5f, 0.0f});
                    labelRect->set_sizeDelta({22.0f, 8.0f});
                }
            }

            if(auto switchTransform = setting->get_transform()->Find("SwitchView"))
            {
                auto switchRect = switchTransform.cast<UnityEngine::RectTransform>();
                if(switchRect)
                {
                    switchRect->set_anchorMin({0.5f, 0.5f});
                    switchRect->set_anchorMax({0.5f, 0.5f});
                    switchRect->set_pivot({0.0f, 0.5f});
                    switchRect->set_anchoredPosition(
                        {SongHeaderLabelControlGap * 0.5f, 0.0f});
                }
            }
        }

        void PlaceTopBarLayoutSelector(BSML::IncrementSetting* setting)
        {
            if(!setting)
                return;

            setting->get_gameObject()->set_name(
                "Big Screen Song Screen Layout Selector");
            if(auto* layout =
                   setting->GetComponent<UnityEngine::UI::LayoutElement*>())
            {
                layout->set_preferredWidth(LayoutSelectorWidth);
            }

            auto root =
                setting->get_transform().cast<UnityEngine::RectTransform>();
            if(!root)
                return;

            root->set_anchorMin({0.5f, 0.5f});
            root->set_anchorMax({0.5f, 0.5f});
            root->set_pivot({0.5f, 0.5f});
            root->set_anchoredPosition({LayoutSelectorX, SongHeaderControlY});
            root->set_sizeDelta({LayoutSelectorWidth, 8.0f});

            // BSML's increment-setting template is designed around a
            // 90-unit settings row. Merely shrinking its root to fit the song
            // header squeezes NameText until TMPro wraps one character per
            // line and leaves the native arrow picker outside its useful hit
            // area. Re-layout the two existing template sections instead of
            // replacing the proven native arrow buttons.
            if(auto labelTransform = root->Find("NameText"))
            {
                auto labelRect =
                    labelTransform.cast<UnityEngine::RectTransform>();
                if(labelRect)
                {
                    labelRect->set_anchorMin({0.5f, 0.5f});
                    labelRect->set_anchorMax({0.5f, 0.5f});
                    labelRect->set_pivot({0.0f, 0.5f});
                    labelRect->set_anchoredPosition(
                        {-LayoutSelectorWidth * 0.5f, 0.0f});
                    labelRect->set_sizeDelta({21.5f, 8.0f});
                }
                if(auto* label =
                       labelTransform->GetComponent<TMPro::TextMeshProUGUI*>())
                {
                    label->set_fontSize(2.8f);
                    label->set_enableWordWrapping(false);
                    label->set_overflowMode(TMPro::TextOverflowModes::Overflow);
                    label->set_alignment(
                        TMPro::TextAlignmentOptions::MidlineRight);
                }
            }

            // IncDecSettingTag creates its native value and arrow group as the
            // second child. Anchor that complete group to the right side; this
            // preserves BSML's button listeners, hover visuals, and raycast
            // targets while giving the horizontal label its own fixed space.
            if(root->get_childCount() > 1)
            {
                auto pickerRect = root->GetChild(1)
                    .cast<UnityEngine::RectTransform>();
                if(pickerRect)
                {
                    // 21.5 label + 2.5 gap + 26 picker = the 50-unit selector
                    // width. The picker only ever shows a single digit between
                    // its two arrows, so 26 keeps full-size arrow hit areas
                    // without the previous excess width. The 2.5 gap matches
                    // SongHeaderLabelControlGap so all three label-to-control
                    // spacings on this row are identical.
                    pickerRect->set_anchorMin({0.5f, 0.5f});
                    pickerRect->set_anchorMax({0.5f, 0.5f});
                    pickerRect->set_pivot({0.0f, 0.5f});
                    pickerRect->set_anchoredPosition(
                        {-LayoutSelectorWidth * 0.5f + 21.5f +
                         SongHeaderLabelControlGap, 0.0f});
                    pickerRect->set_sizeDelta({26.0f, 8.0f});
                }
            }

            if(setting->text)
            {
                setting->text->set_fontSize(3.0f);
                setting->text->set_enableWordWrapping(false);
                setting->text->set_overflowMode(
                    TMPro::TextOverflowModes::Overflow);
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

        bool ExplicitContentAllowed()
        {
            auto* container = BSML::Helpers::GetDiContainer();
            auto* model = container
                ? container->Resolve<GlobalNamespace::PlayerDataModel*>()
                : nullptr;
            auto* data = model ? model->get_playerData() : nullptr;
            return data && data->get_desiredSensitivityFlag().value__ >=
                GlobalNamespace::PlayerSensitivityFlag::Explicit.value__;
        }

        std::string Megabytes(std::uint64_t bytes)
        {
            std::ostringstream text;
            text << std::fixed << std::setprecision(1)
                 << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MB";
            return text.str();
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
        if(!detailView || previewUi_ || inMapUi_ || layoutUi_)
            return;

        const auto& settings = Settings::Instance();
        inMapEnabled_ = settings.VideoEnabled();

        // The canvas is created at a harmless placeholder position, then
        // moved by PositionControlsRow() — immediately below, and again on
        // every SongSelectionShown once Beat Saber has finished laying the
        // menu out. Computing the position lazily avoids trusting rects that
        // are not final during the detail view's construction.
        // hasBackground gives BSML's standard semi-transparent dark backdrop
        // so the switches and their labels stay readable over any scene.
        controlsScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
            {ControlsScreenWidth, ControlsScreenHeight},
            false,
            {0.0f, 1.0f, 2.4f},
            UnityEngine::Quaternion::get_identity(),
            0.0f,
            true);
        if(!controlsScreen_)
        {
            PaperLogger.error("Could not create the song-selection controls canvas");
            ErrorManager::Instance().RecordError(
                "Creating song-selection video controls",
                "BSML could not create the floating controls canvas");
            return;
        }
        controlsScreen_->get_gameObject()->set_name(
            "Big Screen Song Selection Controls");
        // The shared hover-hint panel is reparented into this canvas when a
        // tooltip opens, inheriting its curvature. Force an effectively flat
        // radius so tooltip text renders straight instead of curling off the
        // hint panel's background.
        if(auto* curved = controlsScreen_->get_gameObject()
               ->GetComponentInChildren<HMUI::CurvedCanvasSettings*>())
            curved->SetRadius(10000.0f);
        detailView_ = detailView;
        PositionControlsRow();
        auto controlsParent = controlsScreen_->get_transform();

        // The controls are global preferences rather than properties of the
        // selected song. Create both unconditionally and place their compact
        // roots side by side on the floating row below the song panel.
        previewUi_ = BSML::Lite::CreateToggle(
            controlsParent,
            "Preview Video",
            settings.MenuPreviewEnabled(),
            UnityEngine::Vector2{PreviewToggleX, SongHeaderControlY},
            [this](bool value)
            {
                PreviewToggleChanged(value);
            });
        inMapUi_ = BSML::Lite::CreateToggle(
            controlsParent,
            "Video In Map",
            inMapEnabled_,
            UnityEngine::Vector2{InMapToggleX, SongHeaderControlY},
            [this](bool value)
            {
                InMapToggleChanged(value);
            });
        layoutUi_ = BSML::Lite::CreateIncrementSetting(
            controlsParent,
            "Screen Layout",
            0,
            1.0f,
            static_cast<float>(settings.ActiveScreenLayout() + 1),
            1.0f,
            5.0f,
            UnityEngine::Vector2{LayoutSelectorX, SongHeaderControlY},
            [this](float value)
            {
                LayoutSelectorChanged(value);
            });
        PlaceTopBarLayoutSelector(layoutUi_);
        // ---- Cinema-parity download row --------------------------------
        // Cinema (PC) presents this workflow as one centered row in the empty
        // strip between the difficulty selector and the Play/Practice
        // buttons: italic status text on the left, the Download/Cancel/Retry
        // button on the right, with the difficulty row's own background
        // cloned behind them so it reads as native UI. Reproduce Cinema's
        // exact hierarchy lookups and offset math so Quest users get the
        // workflow they already know. Like Cinema, there is no separate
        // progress bar: the percentage is written into the label text.
        auto levelDetail = detailView->get_transform();
        auto difficulty = levelDetail->Find("BeatmapDifficulty");
        auto characteristic = levelDetail->Find("BeatmapCharacteristic");
        auto actionButtons = levelDetail->Find("ActionButtons");
        UnityW<UnityEngine::Transform> difficultyBackground = nullptr;
        if(difficulty)
            difficultyBackground = difficulty->Find("BG");

        auto* rowObject =
            UnityEngine::GameObject::New_ctor("Big Screen Video Download Row");
        downloadRow_ = rowObject;
        auto* rowRect = rowObject->AddComponent<UnityEngine::RectTransform*>();
        rowRect->SetParent(levelDetail, false);
        if(difficulty && characteristic && actionButtons && difficultyBackground)
        {
            auto difficultyRect =
                difficulty.cast<UnityEngine::RectTransform>();
            auto characteristicRect =
                characteristic.cast<UnityEngine::RectTransform>();
            auto actionRect = actionButtons.cast<UnityEngine::RectTransform>();
            rowRect->set_anchorMin(difficultyRect->get_anchorMin());
            rowRect->set_anchorMax(difficultyRect->get_anchorMax());
            rowRect->set_pivot(difficultyRect->get_pivot());
            const auto difficultyMin = difficultyRect->get_offsetMin();
            const auto difficultyMax = difficultyRect->get_offsetMax();
            const auto characteristicMin = characteristicRect->get_offsetMin();
            const auto characteristicMax = characteristicRect->get_offsetMax();
            // Cinema's placement formula: one difficulty-row-height below the
            // difficulty row, clamped so the row can never overlap the
            // Play/Practice buttons, at the difficulty row's exact width.
            const float belowDifficulty =
                difficultyMin.y + (difficultyMin.y - characteristicMin.y);
            const float aboveActionButtons =
                actionRect->get_offsetMin().y + actionRect->get_sizeDelta().y;
            rowRect->set_offsetMin(
                {difficultyMin.x,
                 std::max(belowDifficulty, aboveActionButtons)});
            rowRect->set_offsetMax(
                {difficultyMax.x,
                 difficultyMax.y + (difficultyMax.y - characteristicMax.y)});
            UnityEngine::Object::Instantiate(
                difficultyBackground->get_gameObject(),
                rowObject->get_transform());
        }
        else
        {
            // A game update renamed the stock hierarchy. Keep the workflow
            // usable with a fixed placement and record the mismatch so it
            // appears in logs and reports.
            PaperLogger.warn(
                "Stock level-detail rows were not found; using fallback download row placement");
            ErrorManager::Instance().RecordError(
                "Placing the download row",
                "Stock level-detail hierarchy changed; fallback placement used");
            rowRect->set_anchorMin({0.5f, 0.5f});
            rowRect->set_anchorMax({0.5f, 0.5f});
            rowRect->set_pivot({0.5f, 0.5f});
            rowRect->set_anchoredPosition({0.0f, -6.5f});
            rowRect->set_sizeDelta({70.0f, 10.0f});
        }

        UnityEngine::Transform* rowParent = rowObject->get_transform();
        auto* rowLayout = BSML::Lite::CreateHorizontalLayoutGroup(rowParent);
        if(rowLayout)
        {
            // Cinema's row is a centered PreferredSize horizontal with
            // spacing 6; the pair recenters itself as the label text and
            // button visibility change.
            rowLayout->set_spacing(6.0f);
            rowLayout->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
            rowLayout->set_childControlWidth(true);
            rowLayout->set_childControlHeight(true);
            rowLayout->set_childForceExpandWidth(false);
            rowLayout->set_childForceExpandHeight(false);
            auto layoutRect = rowLayout->get_transform()
                .cast<UnityEngine::RectTransform>();
            layoutRect->set_anchorMin({0.0f, 0.0f});
            layoutRect->set_anchorMax({1.0f, 1.0f});
            layoutRect->set_offsetMin({0.0f, 0.0f});
            layoutRect->set_offsetMax({0.0f, 0.0f});
            if(auto* fitter = rowLayout->get_gameObject()
                   ->GetComponent<UnityEngine::UI::ContentSizeFitter*>())
            {
                fitter->set_horizontalFit(
                    UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
                fitter->set_verticalFit(
                    UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
            }
            rowParent = rowLayout->get_transform();
        }

        downloadStatus_ = BSML::Lite::CreateText(
            rowParent,
            "",
            3.0f,
            UnityEngine::Vector2{0.0f, 0.0f},
            UnityEngine::Vector2{33.0f, 7.0f});
        if(downloadStatus_)
        {
            // Cinema styles this label italic, font size 3, left-aligned.
            downloadStatus_->set_fontStyle(TMPro::FontStyles::Italic);
            downloadStatus_->set_alignment(
                TMPro::TextAlignmentOptions::MidlineLeft);
            downloadStatus_->set_enableWordWrapping(false);
            downloadStatus_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        }
        downloadButton_ = BSML::Lite::CreateUIButton(
            rowParent,
            "Download Video",
            UnityEngine::Vector2{0.0f, 0.0f},
            UnityEngine::Vector2{30.0f, 8.0f},
            [this]() { DownloadButtonPressed(); });
        if(downloadButton_)
        {
            if(auto* buttonText = downloadButton_
                   ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            {
                buttonText->set_fontSize(3.0f);
                buttonText->set_color(UnityEngine::Color::get_white());
            }
        }
        rowObject->SetActive(false);

        // Song selection keeps Cinema's compact one-button row. Pressing that
        // button opens this modal so all source tiers can be offered without
        // widening or restructuring the stock difficulty/action area.
        resolutionModal_ = BSML::Lite::CreateModal(
            detailView, {92.0f, 48.0f}, nullptr, true);
        resolutionModalText_ = BSML::Lite::CreateText(
            resolutionModal_,
            "Checking available resolutions...",
            TMPro::FontStyles::Normal,
            3.0f,
            {0.0f, 11.0f},
            {84.0f, 17.0f});
        resolutionModalText_->set_enableWordWrapping(true);
        resolutionModalText_->set_enableAutoSizing(true);
        resolutionModalText_->set_fontSizeMin(2.4f);
        resolutionModalText_->set_fontSizeMax(3.0f);
        resolutionModalText_->set_overflowMode(
            TMPro::TextOverflowModes::Ellipsis);
        resolutionModalText_->set_alignment(
            TMPro::TextAlignmentOptions::Center);
        resolutionButtons_.clear();
        displayedResolutionHeights_.clear();
        constexpr std::array<float, 4> ResolutionButtonX{
            11.5f, 34.5f, 57.5f, 80.5f};
        for(std::size_t index = 0; index < ResolutionButtonX.size(); ++index)
        {
            auto* button = BSML::Lite::CreateUIButton(
                resolutionModal_->get_transform(),
                "DOWNLOAD",
                {ResolutionButtonX[index], -6.0f},
                {21.5f, 8.0f},
                [this, index]() { ResolutionButtonPressed(index); });
            BSML::Lite::SetButtonTextSize(button, 2.45f);
            if(auto* buttonText = button->get_gameObject()
                   ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
                buttonText->set_color(UnityEngine::Color::get_white());
            button->get_gameObject()->SetActive(false);
            resolutionButtons_.push_back(button);
        }
        BSML::Lite::CreateUIButton(
            resolutionModal_->get_transform(),
            "Cancel",
            {25.0f, -36.0f},
            {27.0f, 8.0f},
            [this]()
            {
                const auto snapshot = DownloadManager::Instance().Snapshot();
                if(snapshot.metadataOnly && snapshot.Active() &&
                   snapshot.levelId == selectedLevelId_)
                    DownloadManager::Instance().Cancel();
                pendingDownloadHeight_ = 0;
                resolutionModalOpen_ = false;
                if(resolutionModal_)
                    resolutionModal_->Hide();
            });
        confirmResolutionButton_ = BSML::Lite::CreateUIButton(
            resolutionModal_->get_transform(),
            "Download",
            {63.0f, -36.0f},
            {34.0f, 8.0f},
            [this]() { ConfirmPendingResolutionDownload(); });
        confirmResolutionButton_->get_gameObject()->SetActive(false);

        if(!previewUi_ || !inMapUi_)
        {
            PaperLogger.error("Could not create both song-selection video toggles");
            ErrorManager::Instance().RecordError(
                "Creating song-selection video controls",
                "Beat Saber did not create both toggle controls");
            // Do not leave a half-populated floating canvas or an empty
            // download row in the scene.
            if(controlsScreen_)
                UnityEngine::Object::Destroy(controlsScreen_->get_gameObject());
            controlsScreen_ = nullptr;
            detailView_ = nullptr;
            if(downloadRow_)
                UnityEngine::Object::Destroy(downloadRow_);
            downloadRow_ = nullptr;
            previewUi_ = nullptr;
            inMapUi_ = nullptr;
            layoutUi_ = nullptr;
            downloadButton_ = nullptr;
            downloadStatus_ = nullptr;
            return;
        }

        PlaceTopBarToggle(
            previewUi_, "Big Screen Preview Video Toggle", PreviewToggleX);
        PlaceTopBarToggle(
            inMapUi_, "Big Screen Video In Map Toggle", InMapToggleX);
        BSML::Lite::AddHoverHint(
            previewUi_,
            "Turns video previews on or off while browsing songs. This is a global setting and requires Video In Map to be enabled.");
        BSML::Lite::AddHoverHint(
            inMapUi_,
            "Master switch for all song videos. Turning it off disables both in-map playback and song-selection previews.");
        BSML::Lite::AddHoverHint(
            layoutUi_,
            "Selects Layout 1 through 5 for video previews and gameplay. The choice applies to every song unless a map is allowed to use its own Cinema or Chroma placement.");
        RefreshUi();
        BringHeaderControlsToFront();
        PaperLogger.info("Created top-row Preview Video and Video In Map controls");
    }

    void SelectionVideoToggle::ForgetUi()
    {
        // The menu scene owns the detail-view children and destroys them
        // normally, but the floating controls canvas is scene-root and ours:
        // destroy it here so a retained-then-recreated detail view cannot
        // orphan one row and stack a duplicate beneath it. During a scene
        // teardown Unity may have destroyed it already, so this is guarded.
        try
        {
            if(controlsScreen_)
                UnityEngine::Object::Destroy(controlsScreen_->get_gameObject());
        }
        catch(...)
        {
            // Destruction is best-effort during scene teardown; clearing the
            // pointers below is what guarantees safe recreation.
        }
        controlsScreen_ = nullptr;
        detailView_ = nullptr;
        previewUi_ = nullptr;
        inMapUi_ = nullptr;
        layoutUi_ = nullptr;
        downloadRow_ = nullptr;
        downloadButton_ = nullptr;
        downloadStatus_ = nullptr;
        resolutionModal_ = nullptr;
        resolutionModalText_ = nullptr;
        resolutionButtons_.clear();
        displayedResolutionHeights_.clear();
        confirmResolutionButton_ = nullptr;
        selectedLevel_ = nullptr;
        selectedLevelId_.clear();
        selectedDescriptor_ = {};
        selectedLevelHasVideo_ = false;
        ownedDownloadLevelId_.clear();
        probedDownloadUrl_.clear();
        pendingDownloadHeight_ = 0;
        resolutionModalOpen_ = false;
    }

    void SelectionVideoToggle::PositionControlsRow()
    {
        if(!controlsScreen_ || !detailView_)
            return;

        // Preferred anchor: the ScreenSystem's top screen, which hosts the
        // menu title bar and back button. The row's bottom edge sits a small
        // gap above that screen's real top edge, so the placement follows
        // the stock UI instead of a guessed coordinate. Falls back to a
        // detail-view-relative position if the top screen cannot be found.
        UnityEngine::Vector3 rowCenter;
        UnityEngine::Quaternion rowRotation;
        const char* source = "top screen";
        bool positioned = false;
        if(auto* screenSystem =
               detailView_->GetComponentInParent<HMUI::ScreenSystem*>())
        {
            if(auto topScreen = screenSystem->get_topScreen())
            {
                auto topRect = topScreen->get_transform()
                    .cast<UnityEngine::RectTransform>();
                if(topRect)
                {
                    // Unity's generated Rect accessors are non-const member
                    // functions, so this local must stay mutable.
                    auto rect = topRect->get_rect();
                    if(rect.get_height() >= 1.0f && rect.get_height() <= 200.0f)
                    {
                        rowCenter = topRect->TransformPoint(
                            {rect.get_center().x,
                             rect.get_yMax() + ControlsRowGapAboveTopScreen +
                                 ControlsScreenHeight * 0.5f,
                             ControlsRowDetailLocalZ});
                        rowRotation = topRect->get_rotation();
                        positioned = true;
                    }
                }
            }
        }
        if(!positioned)
        {
            auto detailTransform = detailView_->get_transform();
            if(!detailTransform)
            {
                PaperLogger.warn(
                    "Song controls row could not be positioned: no usable anchor");
                return;
            }
            rowCenter = detailTransform->TransformPoint(
                {0.0f, ControlsRowDetailLocalY, ControlsRowDetailLocalZ});
            rowRotation = detailTransform->get_rotation();
            source = "detail view fallback";
        }
        auto screenTransform = controlsScreen_->get_transform();
        screenTransform->set_position(rowCenter);
        screenTransform->set_rotation(rowRotation);
        PaperLogger.info(
            "Positioned song controls row at ({:.2f}, {:.2f}, {:.2f}) from {}",
            rowCenter.x,
            rowCenter.y,
            rowCenter.z,
            source);
    }

    void SelectionVideoToggle::SongSelectionShown()
    {
        // The floating row is not a child of the detail view, so Beat Saber's
        // own panel visibility no longer hides it implicitly. Mirror the
        // panel's visibility here and in SongSelectionHidden, and re-derive
        // the row's position now that the menu layout is guaranteed final.
        if(controlsScreen_)
        {
            controlsScreen_->get_gameObject()->SetActive(true);
            PositionControlsRow();
        }
        if(selectedLevel_)
        {
            selectedDescriptor_ = VideoLibrary::Instance().Describe(selectedLevel_);
            if(selectedDescriptor_.CanPlay() && !selectedLevelHasVideo_)
            {
                PlaybackSession::Instance().Prepare(selectedLevel_);
                selectedLevelHasVideo_ = PlaybackSession::Instance().HasPreparedVideo();
            }
        }
        RefreshUi();
        // Beat Saber reconstructs and reorders portions of the song-detail UI
        // during OnEnable. These controls deliberately sit on the upper header,
        // outside the detail panel's usual content row, so a later stock
        // sibling could otherwise render above their native raycast targets and
        // consume every pointer click. Restore their priority only after the
        // stock OnEnable has completed; no custom hit targets are introduced.
        BringHeaderControlsToFront();

        // Returning from gameplay does not necessarily restart Beat Saber's
        // audio preview for the still-selected song. Defer the video restart
        // until SongPreviewPlayer confirms that the non-default song clip is
        // actually audible; otherwise Big Screen can visibly replay a silent
        // video while Beat Saber remains on its normal menu soundtrack.
        auto& playback = PlaybackSession::Instance();
        resumeWhenSongAudioStarts_ =
            selectedLevelHasVideo_ &&
            inMapEnabled_ &&
            IsMenuPreviewEnabled() &&
            playback.HasPreparedVideo();
        resumeWaitReported_ = false;
    }

    void SelectionVideoToggle::SongSelectionHidden()
    {
        if(controlsScreen_)
            controlsScreen_->get_gameObject()->SetActive(false);
        resumeWhenSongAudioStarts_ = false;
        resumeWaitReported_ = false;
        resolutionModalOpen_ = false;
        pendingDownloadHeight_ = 0;
        if(resolutionModal_)
            resolutionModal_->Hide();
        // yt-dlp retains its .part file. Returning to this song offers Resume
        // instead of wasting storage or network data.
        const auto download = DownloadManager::Instance().Snapshot();
        if(!ownedDownloadLevelId_.empty() && download.Active() &&
           download.levelId == ownedDownloadLevelId_)
            DownloadManager::Instance().Cancel();
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
        const std::string& levelId,
        GlobalNamespace::BeatmapLevel* level)
    {
        auto& playback = PlaybackSession::Instance();

        // SongCore also raises its selection event when difficulty changes.
        // Preserve the prepared decoder for a difficulty-only change. The
        // global switch itself never depends on which level is selected.
        if(levelId == selectedLevelId_)
        {
            RefreshUi();
            return;
        }

        resumeWhenSongAudioStarts_ = false;
        resumeWaitReported_ = false;
        resolutionModalOpen_ = false;
        pendingDownloadHeight_ = 0;
        probedDownloadUrl_.clear();
        if(resolutionModal_)
            resolutionModal_->Hide();
        selectedLevelId_ = levelId;
        selectedLevel_ = level;
        selectedDescriptor_ = level
            ? VideoLibrary::Instance().Describe(level)
            : VideoDescriptor{};
        inMapEnabled_ = Settings::Instance().VideoEnabled();
        playback.Prepare(level);
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
            resumeWhenSongAudioStarts_ = false;
            resumeWaitReported_ = false;
            // Clear the selected-map identity as well as stopping playback.
            // SongCore selections are intentionally ignored while disabled,
            // so retaining this data could resurrect the wrong song if the
            // user changes selection before re-enabling Big Screen.
            selectedLevelId_.clear();
            selectedLevel_ = nullptr;
            selectedDescriptor_ = {};
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
        resumeWhenSongAudioStarts_ = false;
        resumeWaitReported_ = false;
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

    void SelectionVideoToggle::ScreenLayoutPreferenceChanged()
    {
        RefreshUi();
    }

    void SelectionVideoToggle::TickSongPreview(
        double songTimeSeconds,
        bool selectedSongAudioIsAudible)
    {
        auto& playback = PlaybackSession::Instance();

        // The Video Library menu owns the playback session while its own
        // preview is running. This ticker still fires every frame from the
        // SongPreviewPlayer hook, so without this guard its resume logic
        // could start a menu preview over the library's active session.
        if(playback.IsLibraryPreviewActive()) return;

        if(resumeWhenSongAudioStarts_)
        {
            if(!selectedLevelHasVideo_ ||
               !inMapEnabled_ ||
               !IsMenuPreviewEnabled() ||
               !playback.HasPreparedVideo())
            {
                resumeWhenSongAudioStarts_ = false;
                resumeWaitReported_ = false;
                return;
            }

            if(!selectedSongAudioIsAudible)
            {
                if(!resumeWaitReported_)
                {
                    PaperLogger.info(
                        "Waiting for Beat Saber song-preview audio before resuming the menu video");
                    resumeWaitReported_ = true;
                }
                return;
            }

            playback.Start(PlaybackContext::MenuPreview);
            resumeWhenSongAudioStarts_ = false;
            resumeWaitReported_ = false;
            PaperLogger.info("Resumed menu video with Beat Saber's song-preview audio");
        }

        // Drive the video only while the selected song clip is genuinely
        // playing. This prevents a muted/faded/default menu channel from
        // advancing the video independently after a gameplay transition.
        if(playback.IsMenuPreviewActive() && selectedSongAudioIsAudible)
            playback.Tick(songTimeSeconds);
    }

    bool SelectionVideoToggle::IsEnabledForSelectedLevel() const
    {
        return selectedLevelHasVideo_ && inMapEnabled_;
    }

    void SelectionVideoToggle::DownloadButtonPressed()
    {
        auto& downloader = DownloadManager::Instance();
        const auto snapshot = downloader.Snapshot();
        if(snapshot.Active())
        {
            if((!ownedDownloadLevelId_.empty() &&
                snapshot.levelId == ownedDownloadLevelId_) ||
               (snapshot.metadataOnly &&
                snapshot.levelId == selectedLevelId_))
                downloader.Cancel();
            return;
        }
        if(!selectedLevel_) return;

        OpenResolutionDialog();
    }

    void SelectionVideoToggle::OpenResolutionDialog()
    {
        if(!selectedLevel_ || !selectedDescriptor_.downloadUrl ||
           !resolutionModal_)
            return;

        pendingDownloadHeight_ = 0;
        resolutionModalOpen_ = true;
        displayedResolutionHeights_.clear();
        for(auto* button : resolutionButtons_)
            if(button) button->get_gameObject()->SetActive(false);
        if(confirmResolutionButton_)
            confirmResolutionButton_->get_gameObject()->SetActive(false);
        if(resolutionModalText_)
            resolutionModalText_->set_text(
                "Checking available resolutions...");
        resolutionModal_->Show();

        const auto snapshot = DownloadManager::Instance().Snapshot();
        const auto& url = *selectedDescriptor_.downloadUrl;
        const bool cached = snapshot.levelId == selectedDescriptor_.levelId &&
            snapshot.metadataOnly &&
            snapshot.state == DownloadState::ProbeCompleted &&
            probedDownloadUrl_ == url;
        if(!cached)
        {
            std::string error;
            if(!DownloadManager::Instance().StartProbe(
                   selectedDescriptor_.levelId, url, error))
            {
                if(resolutionModalText_)
                    resolutionModalText_->set_text(error.empty()
                        ? "Available resolutions could not be checked."
                        : error);
                return;
            }
            probedDownloadUrl_ = url;
        }
        RefreshResolutionDialog();
    }

    void SelectionVideoToggle::RefreshResolutionDialog()
    {
        if(!resolutionModalOpen_ || !resolutionModalText_)
            return;
        const auto snapshot = DownloadManager::Instance().Snapshot();
        if(snapshot.levelId != selectedLevelId_)
        {
            resolutionModalText_->set_text(
                snapshot.Active()
                    ? "Another downloader task is running. Cancel it or wait for it to finish."
                    : "Checking available resolutions...");
            return;
        }
        if(snapshot.state == DownloadState::Probing)
        {
            resolutionModalText_->set_text(
                "Checking available resolutions...");
            return;
        }
        if(snapshot.metadataOnly && snapshot.state == DownloadState::Failed)
        {
            resolutionModalText_->set_text(snapshot.message.empty()
                ? "Big Screen could not check the available resolutions. Close this window and try again."
                : snapshot.message);
            return;
        }
        if(!snapshot.metadataOnly ||
           snapshot.state != DownloadState::ProbeCompleted)
            return;

        displayedResolutionHeights_ = snapshot.availableHeights;
        // A direct URL or an older downloader runtime may complete a probe
        // without enumerating tiers. Preserve the legacy workflow by offering
        // its established 1080p request rather than trapping the user here.
        if(displayedResolutionHeights_.empty())
            displayedResolutionHeights_.push_back(1080);
        if(displayedResolutionHeights_.size() > resolutionButtons_.size())
            displayedResolutionHeights_.resize(resolutionButtons_.size());

        resolutionModalText_->set_text(
            "Choose the source resolution to download. The playback-resolution setting can downscale this file later, but it never changes these choices.");
        for(std::size_t index = 0; index < resolutionButtons_.size(); ++index)
        {
            auto* button = resolutionButtons_[index];
            if(!button)
                continue;
            const bool visible = index < displayedResolutionHeights_.size();
            button->get_gameObject()->SetActive(visible);
            if(visible)
            {
                BSML::Lite::SetButtonText(
                    button,
                    "DOWNLOAD " +
                        std::to_string(displayedResolutionHeights_[index]) +
                        "p");
                if(auto* buttonText = button->get_gameObject()
                       ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
                    buttonText->set_color(UnityEngine::Color::get_white());
            }
        }
    }

    void SelectionVideoToggle::ResolutionButtonPressed(std::size_t buttonIndex)
    {
        if(buttonIndex >= displayedResolutionHeights_.size())
            return;
        RequestResolutionDownload(displayedResolutionHeights_[buttonIndex]);
    }

    void SelectionVideoToggle::RequestResolutionDownload(int height)
    {
        if(height < 1 || height > 1440)
            return;
        const bool replacing = selectedDescriptor_.CanPlay();
        if(height != 1440 && !replacing)
        {
            resolutionModalOpen_ = false;
            if(resolutionModal_)
                resolutionModal_->Hide();
            StartResolutionDownload(height);
            return;
        }

        pendingDownloadHeight_ = height;
        for(auto* button : resolutionButtons_)
            if(button) button->get_gameObject()->SetActive(false);
        if(confirmResolutionButton_)
        {
            confirmResolutionButton_->get_gameObject()->SetActive(true);
            BSML::Lite::SetButtonText(
                confirmResolutionButton_,
                "Download " + std::to_string(height) + "p");
        }
        std::ostringstream message;
        message << "<b>Download " << height << "p video";
        if(replacing)
            message << " and replace the current assignment";
        message << "?</b>\n\n";
        if(height == 1440)
            message << "1440p requires Hardware Video Decoding. Software decoding is not supported. If hardware decoding fails, Big Screen stops the video while the map continues.";
        if(replacing)
        {
            if(height == 1440)
                message << "\n\n";
            message << "The current video remains available until the new download succeeds. Local files are never deleted by replacement.";
        }
        if(resolutionModalText_)
            resolutionModalText_->set_text(message.str());
    }

    void SelectionVideoToggle::ConfirmPendingResolutionDownload()
    {
        const int height = pendingDownloadHeight_;
        pendingDownloadHeight_ = 0;
        resolutionModalOpen_ = false;
        if(resolutionModal_)
            resolutionModal_->Hide();
        if(height > 0)
            StartResolutionDownload(height);
    }

    void SelectionVideoToggle::StartResolutionDownload(int height)
    {
        if(!selectedLevel_ || !selectedDescriptor_.downloadUrl)
            return;

        auto& downloader = DownloadManager::Instance();
        const auto& descriptor = selectedDescriptor_;
        DownloadRequest request;
        request.levelId = descriptor.levelId;
        request.songName = descriptor.songName;
        request.songAuthor = descriptor.songAuthor;
        request.sourceUrl = *descriptor.downloadUrl;
        request.origin = descriptor.downloadOrigin;
        request.explicitContentAllowed = ExplicitContentAllowed();
        request.requestedHeight = height;
        request.maximumSourceFps = Settings::Instance().PlaybackFpsLimit();
        if(descriptor.mapperDefinition)
        {
            request.offsetSeconds = descriptor.mapperDefinition->offsetSeconds;
            request.playbackRate = descriptor.mapperDefinition->playbackRate;
            request.fitToSong = descriptor.mapperDefinition->fitToSong;
            request.blackDuringLeadIn = descriptor.mapperDefinition->blackDuringLeadIn;
        }
        std::string error;
        if(!downloader.Start(std::move(request), error))
        {
            if(downloadStatus_)
            {
                downloadStatus_->set_text("Download unavailable");
                downloadStatus_->set_color({1.0f, 0.28f, 0.25f, 1.0f});
            }
            ReportDownloadFailure(error);
        }
        else
        {
            // A retry deserves a fresh result dialog even if YouTube returns
            // the same reason as the previous attempt.
            reportedDownloadFailure_.clear();
            ownedDownloadLevelId_ = descriptor.levelId;
        }
        TickDownloadUi();
    }

    void SelectionVideoToggle::ReportDownloadFailure(const std::string& detail)
    {
        const std::string reason = detail.empty()
            ? "YouTube did not provide a usable video at the selected resolution."
            : detail;
        ErrorManager::Instance().ReportUserVisible(
            "Video download failed",
            reason +
            "\n\nOpen Big Screen from the Mods menu and select this song. "
            "You can search YouTube for another video, paste a different link, "
            "or assign a compatible local MP4 or WebM file.");
    }

    void SelectionVideoToggle::TickDownloadUi()
    {
        if(!downloadButton_ || !downloadStatus_) return;
        const float now = UnityEngine::Time::get_unscaledTime();
        if(now < nextDownloadUiRefreshTime_)
            return;
        nextDownloadUiRefreshTime_ = now + 0.1f;
        const auto snapshot = DownloadManager::Instance().Snapshot();
        RefreshResolutionDialog();
        if(!snapshot.Active() && snapshot.levelId == ownedDownloadLevelId_)
            ownedDownloadLevelId_.clear();
        const bool forSelection = !snapshot.levelId.empty() &&
                                  snapshot.levelId == selectedLevelId_;
        // A completed task is the one event that changes the selected map's
        // descriptor without a new SongCore selection callback. Refresh once,
        // then keep using the cached result for all later UI ticks.
        if(forSelection && snapshot.state == DownloadState::Completed &&
           selectedLevel_ && !selectedDescriptor_.CanPlay())
            selectedDescriptor_ = VideoLibrary::Instance().Describe(selectedLevel_);
        const auto& descriptor = selectedDescriptor_;
        const bool show = Settings::Instance().ModEnabled() &&
                          (descriptor.CanPlay() ||
                           descriptor.CanDownload() ||
                           forSelection);
        // Like Cinema, the whole row (cloned native background included)
        // appears only when the selected song has video context, and download
        // progress is presented inside the label text itself.
        if(downloadRow_)
            downloadRow_->SetActive(show);
        downloadButton_->get_gameObject()->SetActive(show && !descriptor.CanPlay());
        downloadStatus_->get_gameObject()->SetActive(show);
        if(!show) return;

        if(forSelection && snapshot.Active())
        {
            BSML::Lite::SetButtonText(downloadButton_, "Cancel Download");
            downloadStatus_->set_color({1.0f, 0.86f, 0.25f, 1.0f});
            if(snapshot.totalBytes)
            {
                const float progress = CoreLogic::DownloadProgressFraction(
                    snapshot.downloadedBytes,
                    snapshot.totalBytes);
                std::ostringstream status;
                status << "Downloading "
                       << static_cast<int>(std::round(progress * 100.0f))
                       << "%  |  " << Megabytes(snapshot.downloadedBytes)
                       << " / " << Megabytes(snapshot.totalBytes);
                downloadStatus_->set_text(status.str());
            }
            else
            {
                downloadStatus_->set_text(snapshot.message.empty()
                    ? "Preparing download..."
                    : snapshot.message);
            }
        }
        else if(forSelection && snapshot.state == DownloadState::Failed)
        {
            BSML::Lite::SetButtonText(downloadButton_, "Retry Download");
            downloadStatus_->set_text("Download failed — select Retry");
            downloadStatus_->set_color({1.0f, 0.28f, 0.25f, 1.0f});
            const auto failureKey = snapshot.levelId + "\n" + snapshot.message;
            if(reportedDownloadFailure_ != failureKey)
            {
                reportedDownloadFailure_ = failureKey;
                ReportDownloadFailure(snapshot.message);
            }
        }
        else if(forSelection && snapshot.state == DownloadState::Cancelled)
        {
            BSML::Lite::SetButtonText(downloadButton_, "Resume Download");
            downloadStatus_->set_text("Download paused");
            downloadStatus_->set_color({1.0f, 0.68f, 0.20f, 1.0f});
        }
        else if(descriptor.CanPlay())
        {
            downloadStatus_->set_text("Video ready!");
            downloadStatus_->set_color({0.20f, 0.90f, 0.42f, 1.0f});
            if(!selectedLevelHasVideo_)
            {
                PlaybackSession::Instance().Prepare(selectedLevel_);
                selectedLevelHasVideo_ = PlaybackSession::Instance().HasPreparedVideo();
                if(selectedLevelHasVideo_ && inMapEnabled_ && IsMenuPreviewEnabled())
                    PlaybackSession::Instance().Start(PlaybackContext::MenuPreview);
            }
        }
        else
        {
            BSML::Lite::SetButtonText(downloadButton_, "Download Video");
            downloadStatus_->set_text("Video available");
            downloadStatus_->set_color(UnityEngine::Color::get_white());
        }
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
        SettingsMenu::Instance().RefreshControls();
    }

    void SelectionVideoToggle::InMapToggleChanged(bool enabled)
    {
        Settings::Instance().SetVideoEnabled(enabled);
        // Read back the authoritative saved state because disabling video also
        // disables its dependent preview preference in Settings.
        inMapEnabled_ = Settings::Instance().VideoEnabled();
        RefreshUi();
        SettingsMenu::Instance().RefreshControls();
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

    void SelectionVideoToggle::LayoutSelectorChanged(float value)
    {
        auto& settings = Settings::Instance();
        settings.SetActiveScreenLayout(
            std::clamp(static_cast<int>(value) - 1, 0, 4));

        // Keep the full settings page and this compact song-screen selector on
        // the same persisted layout immediately. Refreshing with notification-
        // free setters prevents this synchronization from recursively firing
        // either control's callback.
        RefreshUi();
        SettingsMenu::Instance().RefreshControls();

        auto& playback = PlaybackSession::Instance();
        const bool restartPreview = playback.IsMenuPreviewActive();
        playback.RefreshDisplaySettings();
        if(restartPreview)
            playback.Start(PlaybackContext::MenuPreview);
    }

    void SelectionVideoToggle::BringHeaderControlsToFront()
    {
        // On the dedicated floating canvas no stock graphics compete for the
        // pointer anymore; keeping the three roots as the last siblings is
        // retained only so any future additions to that canvas (status text,
        // separators) cannot cover their raycast targets.
        if(layoutUi_)
            layoutUi_->get_transform()->SetAsLastSibling();
        if(previewUi_)
            previewUi_->get_transform()->SetAsLastSibling();
        if(inMapUi_)
            inMapUi_->get_transform()->SetAsLastSibling();
    }

    void SelectionVideoToggle::RefreshUi()
    {
        // UI events need an immediate refresh; the normal Update hook is
        // deliberately limited to 10 Hz to avoid reparsing custom-map metadata
        // and rewriting unchanged Unity controls every rendered frame.
        nextDownloadUiRefreshTime_ = 0.0f;
        if(!previewUi_ && !inMapUi_ && !layoutUi_)
            return;

        const auto& settings = Settings::Instance();
        inMapEnabled_ = settings.VideoEnabled();

        // SetIsOnWithoutNotify prevents selection refreshes from masquerading
        // as a user click and reopening a decoder that is already running.
        SetToggleWithoutNotification(inMapUi_, inMapEnabled_);
        SetToggleWithoutNotification(previewUi_, settings.MenuPreviewEnabled());
        if(previewUi_)
            previewUi_->set_interactable(inMapEnabled_);
        if(layoutUi_)
        {
            layoutUi_->set_Value(
                static_cast<float>(settings.ActiveScreenLayout() + 1));
            layoutUi_->set_interactable(settings.ModEnabled());
            // set_Value calls UpdateState, which applies the min/max state to
            // BSML's native arrow buttons as well as refreshing the value text.
            // Calling it explicitly after interactability changes covers the
            // case where IncDecSetting's same-value guard skipped button state.
            layoutUi_->UpdateState();
        }

        // Visibility deliberately does not inspect selectedLevelHasVideo_: both
        // switches control all songs the user subsequently browses. Only the
        // master mod switch removes them from Beat Saber's song screen.
        if(inMapUi_)
            inMapUi_->get_gameObject()->SetActive(settings.ModEnabled());
        if(previewUi_)
            previewUi_->get_gameObject()->SetActive(settings.ModEnabled());
        if(layoutUi_)
            layoutUi_->get_gameObject()->SetActive(settings.ModEnabled());
        TickDownloadUi();
    }
}
