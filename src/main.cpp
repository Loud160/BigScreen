// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "main.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <vector>

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/DiagnosticSessionLogger.hpp"
#include "BigScreen/ExperimentalFeatures.hpp"
#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/MenuModal.hpp"
// BLOOM EXPERIMENT DISABLED (2026-08-18): the implementation and hook remain
// behind one named build gate so they can be revisited without reconstructing
// the experiment. Video playback currently ignores mapper-driven bloom.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
#include "BigScreen/CinemaBloomRenderer.hpp"
#endif
#include "BigScreen/MenuPlacementGuide.hpp"
#include "BigScreen/MenuEnvironmentVisibility.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/PerformancePanel.hpp"
#include "BigScreen/PowerBenchmark.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "BigScreen/ShowcaseLauncher.hpp"
#include "BigScreen/ShowcaseMenu.hpp"
#include "BigScreen/StorageMaintenanceMenu.hpp"
#include "BigScreen/LocalVideoBrowserMenu.hpp"
#include "BigScreen/ThumbnailPickerMenu.hpp"
#include "BigScreen/UpDownShowcaseTimeline.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BasicBeatmapEventData.hpp"
#include "GlobalNamespace/BeatmapCallbacksController.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapDataItem.hpp"
#include "GlobalNamespace/BeatmapEventData.hpp"
#include "GlobalNamespace/BeatmapObjectSpawnController.hpp"
#include "GlobalNamespace/BloomPrePassLight.hpp"
#include "GlobalNamespace/BloomPrePass.hpp"
#include "GlobalNamespace/ColorBoostBeatmapEventData.hpp"
#include "GlobalNamespace/DirectionalLight.hpp"
#include "GlobalNamespace/EnvironmentInfoSO.hpp"
#include "GlobalNamespace/EnvironmentEffectsFilterPreset.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/FxBeatmapEventData.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/MissionCompletionResults.hpp"
#include "GlobalNamespace/MissionLevelDetailViewController.hpp"
#include "GlobalNamespace/MissionLevelRestartController.hpp"
#include "GlobalNamespace/MissionSelectionNavigationController.hpp"
#include "GlobalNamespace/MissionLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/IReadonlyBeatmapData.hpp"
#include "GlobalNamespace/LightColorBeatmapEventData.hpp"
#include "GlobalNamespace/LightWithIdMonoBehaviour.hpp"
#include "GlobalNamespace/LightRotationBeatmapEventData.hpp"
#include "GlobalNamespace/LightTranslationBeatmapEventData.hpp"
#include "GlobalNamespace/NoteData.hpp"
#include "GlobalNamespace/LineLight.hpp"
#include "GlobalNamespace/LevelBar.hpp"
#include "GlobalNamespace/LightsAnimator.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/PointLight.hpp"
#include "GlobalNamespace/Rotate.hpp"
#include "GlobalNamespace/ResultsViewController.hpp"
#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "GlobalNamespace/Spectrogram.hpp"
#include "GlobalNamespace/SpectrogramRow.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "GlobalNamespace/StandardLevelRestartController.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/TrackLaneRingsPositionStepEffectSpawner.hpp"
#include "GlobalNamespace/TrackLaneRing.hpp"
#include "GlobalNamespace/TrackLaneRingsRotationEffect.hpp"
#include "GlobalNamespace/TrackLaneRingsRotationEffectSpawner.hpp"
#include "GlobalNamespace/TransformSpectrogram.hpp"
#include "HMUI/ImageView.hpp"
#include "System/Nullable_1.hpp"
#include "System/Collections/Generic/LinkedList_1.hpp"
#include "System/Collections/Generic/LinkedListNode_1.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/Application.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "custom-types/shared/register.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

namespace {
    // SongCore may finish a refresh outside the exact frame on which Big
    // Screen owns Unity UI. The callback publishes only this plain flag; the
    // SongPreviewPlayer update consumes it on Unity's thread before any table
    // or selected-level object is touched.
    std::atomic_bool songCatalogRefreshPending{false};

    void HandleSongsLoaded(
        std::span<SongCore::SongLoader::CustomBeatmapLevel* const>)
    {
        songCatalogRefreshPending.store(true, std::memory_order_release);
    }

    void PrepareGameplayVideoForLevel(
        GlobalNamespace::BeatmapLevel* level,
        GlobalNamespace::BeatmapKey beatmapKey)
    {
        std::string characteristic;
        if(beatmapKey.beatmapCharacteristic)
        {
            const auto serializedName =
                beatmapKey.beatmapCharacteristic->get_serializedName();
            if(serializedName)
                characteristic = std::string(serializedName);
        }

        auto& playback = BigScreen::PlaybackSession::Instance();
        // Prepare benchmark identity even when Video In Map is off so baseline
        // and video runs remain directly comparable.
        BigScreen::PowerBenchmark::Instance().Prepare(
            level && level->levelID ? std::string(level->levelID) : std::string{},
            level && level->songName
                ? std::string(level->songName)
                : "Unknown song",
            level && level->songAuthorName
                ? std::string(level->songAuthorName)
                : "Unknown artist",
            characteristic,
            beatmapKey.difficulty.value__);

        // Gameplay eligibility belongs to the saved global setting, not to
        // StandardLevelDetailView's cached selection UI. Campaign launches do
        // not pass through that screen at all.
        if(BigScreen::Settings::Instance().VideoEnabled())
        {
            playback.Prepare(level);
            playback.ConfigureGameplayBeatmap(
                characteristic,
                beatmapKey.difficulty.value__);
            playback.PrewarmGameplay();
        }
        else
        {
            playback.Prepare(nullptr);
        }
    }

    // Beat Saber 1.40.8 retains both Mission Init overloads. One can delegate
    // to the other depending on which campaign entry point supplied already-
    // loaded beatmap data. Prevent that internal delegation from preparing and
    // prewarming the same video twice while still hooking both public paths.
    bool missionInitPreparationActive = false;

    class MissionInitPreparationScope final {
    public:
        MissionInitPreparationScope()
            : ownsPreparation_(!missionInitPreparationActive)
        {
            if(ownsPreparation_)
                missionInitPreparationActive = true;
        }

        ~MissionInitPreparationScope()
        {
            if(ownsPreparation_)
                missionInitPreparationActive = false;
        }

        bool OwnsPreparation() const { return ownsPreparation_; }

    private:
        bool ownsPreparation_ = false;
    };

    void HandleLevelWasSelected(
        SongCore::API::LevelSelect::LevelWasSelectedEventArgs const& eventArgs)
    {
        if(!BigScreen::Settings::Instance().ModEnabled())
            return;
        BigScreen::ErrorManager::Instance().Guard("handling song selection", [&]() {
            BigScreen::SelectionVideoToggle::Instance().LevelSelected(
                eventArgs.levelID,
                eventArgs.beatmapLevel);
        });
    }
}

bool IsMenuPreviewEnabled()
{
    const auto& settings = BigScreen::Settings::Instance();
    return settings.ModEnabled() && settings.MenuPreviewEnabled();
}

namespace {
    struct ShowcaseRingState {
        UnityW<UnityEngine::GameObject> object = nullptr;
        bool originallyActive = true;
    };

    std::vector<ShowcaseRingState> showcaseRingStates;
    std::optional<bool> appliedShowcaseRingVisibility;
    std::vector<ShowcaseRingState> showcaseSidePillarStates;
    std::optional<bool> appliedShowcaseSidePillarVisibility;
    bool showcaseSidePillarLateCaptureAttempted = false;
    struct ShowcaseRendererState {
        UnityW<UnityEngine::Renderer> renderer = nullptr;
        bool originallyEnabled = true;
    };
    std::vector<ShowcaseRendererState> showcaseBackgroundRenderers;
    std::optional<bool> appliedShowcaseBackgroundVisibility;
    bool showcaseBackgroundLateCaptureAttempted = false;

    void CaptureUniqueShowcaseObject(
        std::vector<ShowcaseRingState>& states,
        UnityEngine::GameObject* gameObject)
    {
        if(!gameObject)
            return;
        const bool duplicate = std::any_of(
            states.begin(), states.end(), [gameObject](const auto& existing)
            {
                return existing.object.unsafePtr() == gameObject;
            });
        if(!duplicate)
            states.push_back({gameObject, gameObject->get_activeSelf()});
    }

    void RestoreShowcaseTrackRings()
    {
        // Restore exactly what was captured rather than blindly enabling every
        // ring. This matters if a mapper intentionally began with an inactive
        // ring object. UnityW makes cleanup harmless after a scene unload.
        for(auto& state : showcaseRingStates)
        {
            if(UnityW<UnityEngine::GameObject>::isAlive(state.object))
                state.object->SetActive(state.originallyActive);
        }
        showcaseRingStates.clear();
        appliedShowcaseRingVisibility.reset();
    }

    void CaptureShowcaseTrackRings()
    {
        RestoreShowcaseTrackRings();
        if(!BigScreen::PlaybackSession::Instance().ShowcaseActive())
            return;

        for(auto* ring : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::TrackLaneRing*>(true))
        {
            if(!ring)
                continue;
            auto gameObject = ring->get_gameObject();
            if(!gameObject)
                continue;

            const bool duplicate = std::any_of(
                showcaseRingStates.begin(), showcaseRingStates.end(),
                [gameObject](const ShowcaseRingState& existing)
                {
                    return existing.object.unsafePtr() == gameObject;
                });
            if(!duplicate)
            {
                showcaseRingStates.push_back(
                    {gameObject, gameObject->get_activeSelf()});
            }
        }
        BigScreen::BigScreenLogger.info(
            "Captured {} track-ring objects for the Up & Down visibility strobe",
            showcaseRingStates.size());
    }

    void RestoreShowcaseSidePillars()
    {
        for(auto& state : showcaseSidePillarStates)
        {
            if(UnityW<UnityEngine::GameObject>::isAlive(state.object))
                state.object->SetActive(state.originallyActive);
        }
        showcaseSidePillarStates.clear();
        appliedShowcaseSidePillarVisibility.reset();
        showcaseSidePillarLateCaptureAttempted = false;
    }

    void CaptureShowcaseSidePillars()
    {
        RestoreShowcaseSidePillars();
        if(!BigScreen::PlaybackSession::Instance().ShowcaseActive())
            return;

        // These are the same exact Big Mirror roots used by the user's Hide
        // Side Bars setting. Include inactive objects so a globally hidden bar
        // remains hidden when the authored showcase cue later restores the
        // map's visible pillars.
        for(auto* transform : UnityEngine::Object::FindObjectsOfType<
                UnityEngine::Transform*>(true))
        {
            if(!transform)
                continue;
            auto gameObject = transform->get_gameObject();
            if(!gameObject)
                continue;
            const std::string name(gameObject->get_name());
            if(name != "NearBuildingLeft" && name != "NearBuildingRight")
                continue;
            CaptureUniqueShowcaseObject(showcaseSidePillarStates, gameObject);
        }

        // The two remaining side obstructions seen during the floating cue
        // are controlled by the same menu settings as side lasers and
        // spectrogram bars. Capture their actual rendered roots as well as the
        // NearBuilding pair so the showcase invokes the already proven hide
        // behavior without changing the user's saved toggles.
        for(auto* transform : UnityEngine::Object::FindObjectsOfType<
                UnityEngine::Transform*>(true))
        {
            if(!transform)
                continue;
            auto gameObject = transform->get_gameObject();
            if(!gameObject)
                continue;
            const std::string name(gameObject->get_name());
            const bool directionalRail =
                name == "NeonTubeDirectionalL" ||
                name == "NeonTubeDirectionalR" ||
                name == "NeonTubeDirectionalFL" ||
                name == "NeonTubeDirectionalFR";
            if(directionalRail || name.rfind("RotatingLasersPair", 0) == 0 ||
               name.rfind("DoubleColorLaser", 0) == 0)
                CaptureUniqueShowcaseObject(showcaseSidePillarStates, gameObject);
        }
        for(auto* spectrogram : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::Spectrogram*>(true))
        {
            if(!spectrogram)
                continue;
            CaptureUniqueShowcaseObject(
                showcaseSidePillarStates, spectrogram->get_gameObject());
            for(auto renderer : spectrogram->__cordl_internal_get__meshRenderers())
                if(renderer)
                    CaptureUniqueShowcaseObject(
                        showcaseSidePillarStates, renderer->get_gameObject());
        }
        for(auto* row : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::SpectrogramRow*>(true))
        {
            if(!row)
                continue;
            CaptureUniqueShowcaseObject(
                showcaseSidePillarStates, row->get_gameObject());
            for(auto renderer : row->__cordl_internal_get__meshRenderers())
                if(renderer)
                    CaptureUniqueShowcaseObject(
                        showcaseSidePillarStates, renderer->get_gameObject());
        }
        for(auto* spectrogram : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::TransformSpectrogram*>(true))
        {
            if(!spectrogram)
                continue;
            CaptureUniqueShowcaseObject(
                showcaseSidePillarStates, spectrogram->get_gameObject());
            for(auto transform : spectrogram->__cordl_internal_get__transforms())
                if(transform)
                    CaptureUniqueShowcaseObject(
                        showcaseSidePillarStates, transform->get_gameObject());
        }
        BigScreen::BigScreenLogger.info(
            "Captured {} side-obstruction objects for the Up & Down floating section",
            showcaseSidePillarStates.size());
    }

    void RestoreShowcaseBackground()
    {
        for(auto& state : showcaseBackgroundRenderers)
        {
            if(UnityW<UnityEngine::Renderer>::isAlive(state.renderer))
                state.renderer->set_enabled(state.originallyEnabled);
        }
        showcaseBackgroundRenderers.clear();
        appliedShowcaseBackgroundVisibility.reset();
        showcaseBackgroundLateCaptureAttempted = false;
    }

    void CaptureShowcaseBackground()
    {
        RestoreShowcaseBackground();
        if(!BigScreen::PlaybackSession::Instance().ShowcaseActive())
            return;
        auto environment = UnityEngine::GameObject::Find("/Environment");
        if(!environment)
            return;
        auto environmentTransform = environment->get_transform();
        for(auto* renderer : UnityEngine::Object::FindObjectsOfType<
                UnityEngine::Renderer*>(true))
        {
            if(!renderer)
                continue;
            auto current = renderer->get_transform();
            bool belongsToEnvironment = false;
            while(current)
            {
                if(current == environmentTransform)
                {
                    belongsToEnvironment = true;
                    break;
                }
                current = current->get_parent();
            }
            if(belongsToEnvironment)
                showcaseBackgroundRenderers.push_back(
                    {renderer, renderer->get_enabled()});
        }
        BigScreen::BigScreenLogger.info(
            "Captured {} environment renderers for the Up & Down floating and corkscrew sections",
            showcaseBackgroundRenderers.size());
    }

    void UpdateShowcaseTrackRings(double songTimeSeconds)
    {
        if(!BigScreen::PlaybackSession::Instance().ShowcaseActive() ||
           showcaseRingStates.empty())
            return;

        const bool visible =
            BigScreen::UpDownShowcase::CenterRingVisible(songTimeSeconds);
        if(appliedShowcaseRingVisibility == visible)
            return;

        for(auto& state : showcaseRingStates)
        {
            if(UnityW<UnityEngine::GameObject>::isAlive(state.object))
                state.object->SetActive(visible && state.originallyActive);
        }
        appliedShowcaseRingVisibility = visible;
    }

    void UpdateShowcaseSidePillars(double songTimeSeconds)
    {
        if(!BigScreen::PlaybackSession::Instance().ShowcaseActive())
            return;

        const bool visible =
            BigScreen::UpDownShowcase::SidePillarsVisible(songTimeSeconds);
        // Chroma can finish constructing environment roots after StartSong.
        // Re-run the same proven Side Bars, Side Lights, and Spectrogram object
        // discovery exactly once when the authored hide cue begins. This
        // captures late-created geometry without doing scene-wide searches on
        // every gameplay frame or changing any saved environment toggle.
        if(!visible && !showcaseSidePillarLateCaptureAttempted)
        {
            CaptureShowcaseSidePillars();
            showcaseSidePillarLateCaptureAttempted = true;
        }
        if(showcaseSidePillarStates.empty())
            return;
        if(appliedShowcaseSidePillarVisibility == visible)
            return;

        for(auto& state : showcaseSidePillarStates)
        {
            if(UnityW<UnityEngine::GameObject>::isAlive(state.object))
                state.object->SetActive(visible && state.originallyActive);
        }
        appliedShowcaseSidePillarVisibility = visible;
    }

    void UpdateShowcaseBackground(double songTimeSeconds)
    {
        if(!BigScreen::PlaybackSession::Instance().ShowcaseActive())
            return;
        const bool visible =
            BigScreen::UpDownShowcase::BackgroundEnvironmentVisible(
                songTimeSeconds);
        // Chroma may instantiate renderer roots after StartSong. Retry the
        // scene-wide capture once at the first hide cue, never every frame.
        if(!visible && showcaseBackgroundRenderers.empty() &&
           !showcaseBackgroundLateCaptureAttempted)
        {
            CaptureShowcaseBackground();
            showcaseBackgroundLateCaptureAttempted = true;
        }
        if(showcaseBackgroundRenderers.empty())
            return;
        if(appliedShowcaseBackgroundVisibility == visible)
            return;
        for(auto& state : showcaseBackgroundRenderers)
        {
            if(UnityW<UnityEngine::Renderer>::isAlive(state.renderer))
                state.renderer->set_enabled(visible && state.originallyEnabled);
        }
        appliedShowcaseBackgroundVisibility = visible;
    }

    void CenterResultsRect(UnityEngine::RectTransform* rect)
    {
        if(!rect)
            return;
        rect->set_anchorMin({0.5f, 0.5f});
        rect->set_anchorMax({0.5f, 0.5f});
        rect->set_pivot({0.5f, 0.5f});
    }

    HMUI::ImageView* CreateResultsImage(
        UnityEngine::Transform* parent,
        UnityEngine::Vector2 position,
        UnityEngine::Vector2 size,
        UnityEngine::Color color)
    {
        auto* image = BSML::Lite::CreateImage(
            parent,
            BSML::Utilities::ImageResources::GetWhitePixel());
        if(!image)
            return nullptr;
        image->set_color(color);
        image->set_preserveAspect(false);
        image->set_raycastTarget(false);
        auto rect = image->get_transform().cast<UnityEngine::RectTransform>();
        CenterResultsRect(rect);
        rect->set_anchoredPosition(position);
        rect->set_sizeDelta(size);
        return image;
    }

    TMPro::TextMeshProUGUI* CreateResultsText(
        UnityEngine::Transform* parent,
        std::string text,
        UnityEngine::Vector2 position,
        UnityEngine::Vector2 size,
        float fontSize,
        TMPro::TextAlignmentOptions alignment)
    {
        auto* label = BSML::Lite::CreateText(
            parent, text, fontSize, position, size);
        if(!label)
            return nullptr;
        label->set_alignment(alignment);
        label->set_enableWordWrapping(false);
        label->set_raycastTarget(false);
        return label;
    }

    void CreateResultsPerformancePanel(
        UnityEngine::Transform* parent,
        float centerY,
        const BigScreen::PlaybackResultsData& data)
    {
        // A compact card layout mirrors the visual hierarchy used by counter
        // mods: one title, two clearly separated metric groups, restrained
        // secondary labels, and a cyan accent. All elements are decorative and
        // cannot intercept results-screen pointer input.
        constexpr float panelWidth = 98.0f;
        constexpr float panelHeight = 20.0f;
        constexpr float borderThickness = 0.4f;
        constexpr float halfPanelWidth = panelWidth * 0.5f;
        constexpr float halfPanelHeight = panelHeight * 0.5f;
        const UnityEngine::Color borderColor{0.0f, 0.80f, 1.0f, 1.0f};

        CreateResultsImage(
            parent, {0.0f, centerY}, {panelWidth, panelHeight},
            {0.018f, 0.043f, 0.075f, 0.98f});

        // Draw one restrained outline around the complete card. The previous
        // results summary created only a thick top accent, so the dark body
        // blended into Beat Saber's results scene and looked like an
        // unfinished panel. These four equal strips mirror the framed-card
        // hierarchy used by Qounters-style displays without moving or
        // resizing any statistics content.
        CreateResultsImage(
            parent,
            {0.0f, centerY + halfPanelHeight - borderThickness * 0.5f},
            {panelWidth, borderThickness}, borderColor);
        CreateResultsImage(
            parent,
            {0.0f, centerY - halfPanelHeight + borderThickness * 0.5f},
            {panelWidth, borderThickness}, borderColor);
        CreateResultsImage(
            parent,
            {-halfPanelWidth + borderThickness * 0.5f, centerY},
            {borderThickness, panelHeight}, borderColor);
        CreateResultsImage(
            parent,
            {halfPanelWidth - borderThickness * 0.5f, centerY},
            {borderThickness, panelHeight}, borderColor);
        CreateResultsImage(
            parent, {-24.0f, centerY - 1.3f}, {46.0f, 14.5f},
            {0.050f, 0.095f, 0.150f, 1.0f});
        CreateResultsImage(
            parent, {24.0f, centerY - 1.3f}, {46.0f, 14.5f},
            {0.050f, 0.095f, 0.150f, 1.0f});

        CreateResultsText(
            parent,
            "<color=#75DFFF><b>BIG SCREEN PERFORMANCE</b></color>",
            // The statistics cards end at +5.95 and the panel ends at +10.
            // Center the title in that dedicated header band so it does not
            // crowd either column's blue section heading.
            {0.0f, centerY + 8.0f}, {92.0f, 4.0f}, 3.0f,
            TMPro::TextAlignmentOptions::Center);

        const auto gameplayAverage = data.sampledGameplayFrames > 0
            ? std::format("{:.1f}", data.averageGameplayFps)
            : std::string("--");
        const auto gameplayMinimum = data.sampledGameplayFrames > 0
            ? std::format("{:.1f}", data.minimumGameplayFps)
            : std::string("--");
        const auto gameplayMaximum = data.sampledGameplayFrames > 0
            ? std::format("{:.1f}", data.maximumGameplayFps)
            : std::string("--");
        CreateResultsText(
            parent,
            std::format(
                "<color=#75DFFF><b>GAMEPLAY</b></color>\n"
                "<size=130%><b>{}</b></size>  <color=#AEBAC8>Average FPS</color>\n"
                "<color=#AEBAC8>Minimum</color>  <b>{}</b>     "
                "<color=#AEBAC8>Maximum</color>  <b>{}</b>",
                gameplayAverage, gameplayMinimum, gameplayMaximum),
            {-24.0f, centerY - 1.3f}, {42.0f, 12.5f}, 2.65f,
            TMPro::TextAlignmentOptions::Center);

        CreateResultsText(
            parent,
            std::format(
                "<color=#75DFFF><b>VIDEO · {} · {} · FFMPEG {}</b></color>\n"
                "<color=#AEBAC8>Video</color> <b>{}x{} @ {:.1f}</b>   "
                "<color=#AEBAC8>FPS Limit</color> <b>{}</b>\n"
                "<color=#AEBAC8>Frames Skipped</color> <b>{}</b>   "
                "<color=#AEBAC8>Frame Rate Loss</color> <b>{:.2f}%</b>\n"
                "<color=#AEBAC8>Video FPS Average</color> <b>{:.1f}</b>\n"
                "<color=#AEBAC8>Prep CPU</color> <b>{:.2f} avg / {:.2f} peak ms</b>",
                data.video.decodeMethod == "hardware" ? "HARDWARE" : "SOFTWARE",
                data.video.codec.empty() ? "UNKNOWN" : data.video.codec,
                data.video.decoderRuntime,
                data.video.videoWidth,
                data.video.videoHeight,
                data.video.sourceFps,
                data.video.outputFpsLimit,
                data.missedVideoFrames,
                data.missedVideoFramePercent,
                data.averageVideoFps,
                data.video.averageDecodeMilliseconds,
                data.video.peakDecodeMilliseconds),
            {24.0f, centerY - 1.3f}, {42.0f, 12.5f}, 2.3f,
            TMPro::TextAlignmentOptions::MidlineLeft);
    }

    template<typename T>
    int DisableLoadedComponents()
    {
        int disabled = 0;
        for(auto* component : UnityEngine::Object::FindObjectsOfType<T*>(false))
        {
            if(component && component->get_enabled())
            {
                component->set_enabled(false);
                ++disabled;
            }
        }
        return disabled;
    }

    void DisableEnvironmentMotion()
    {
        // These are scenery-only components used by Beat Saber's tunnel rings
        // and continuously rotating background props. Gameplay spawn rotation,
        // note movement, sabers, and cameras use different component types and
        // are deliberately left untouched.
        int disabled = 0;
        disabled += DisableLoadedComponents<GlobalNamespace::TrackLaneRingsRotationEffect>();
        disabled += DisableLoadedComponents<GlobalNamespace::Rotate>();
        BigScreen::BigScreenLogger.info("Disabled {} rotating or moving environment components", disabled);
    }

    void HideTrackLaneRings()
    {
        int hidden = 0;
        for(auto* ring : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::TrackLaneRing*>(false))
        {
            if(!ring)
                continue;
            auto gameObject = ring->get_gameObject();
            if(gameObject && gameObject->get_activeSelf())
            {
                // The ring's MonoBehaviour only drives position and rotation;
                // disabling it would leave its mesh visible. Deactivating the
                // ring root hides the mesh and its child lights while keeping
                // notes, obstacles, the platform, and unrelated side scenery.
                gameObject->SetActive(false);
                ++hidden;
            }
        }
        BigScreen::BigScreenLogger.info("Hidden {} track-lane ring objects for video gameplay", hidden);
    }

    void HideSideBars()
    {
        int hidden = 0;
        for(auto* transform : UnityEngine::Object::FindObjectsOfType<
                UnityEngine::Transform*>(false))
        {
            if(!transform)
                continue;
            auto gameObject = transform->get_gameObject();
            if(!gameObject || !gameObject->get_activeSelf())
            {
                continue;
            }

            // The visible rigid bars in Big Mirror belong to the two near-
            // building roots. They are not the RotatingLasersPair effects the
            // first implementation targeted; live logging proved those effects
            // were deactivated while the bars remained visible. Match only the
            // two exact scene roots so map lighting and unrelated scenery stay.
            const std::string name(gameObject->get_name());
            if(name != "NearBuildingLeft" && name != "NearBuildingRight")
                continue;

            gameObject->SetActive(false);
            ++hidden;
        }
        BigScreen::BigScreenLogger.info("Hidden {} Big Mirror side-bar structures for video gameplay", hidden);
    }

    void HideSpectrogramBars()
    {
        int hidden = 0;
        for(auto* spectrogram : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::Spectrogram*>(false))
        {
            if(!spectrogram)
                continue;
            auto gameObject = spectrogram->get_gameObject();
            if(!gameObject || !gameObject->get_activeSelf())
                continue;

            // The Spectrogram is only a driver. Big Mirror's visible left and
            // right waveform meshes are referenced in _meshRenderers rather
            // than parented below this GameObject, so disabling the driver by
            // itself merely freezes both still-visible meshes.
            for(auto renderer : spectrogram->__cordl_internal_get__meshRenderers())
            {
                if(!renderer)
                    continue;
                auto rendererObject = renderer->get_gameObject();
                if(!rendererObject || !rendererObject->get_activeSelf())
                    continue;
                rendererObject->SetActive(false);
                ++hidden;
            }

            // Hide the separate update driver after its referenced meshes are
            // gone so it cannot continue doing audio-reactive material work.
            gameObject->SetActive(false);
            ++hidden;
        }

        // SpectrogramRow is a separate lighting-effect implementation used
        // for the thin, jagged rows visible near Big Mirror's floor. Each row
        // owns its own renderer array and is not referenced by Spectrogram or
        // TransformSpectrogram, so it must be handled independently.
        for(auto* spectrogramRow : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::SpectrogramRow*>(false))
        {
            if(!spectrogramRow)
                continue;

            for(auto renderer : spectrogramRow->__cordl_internal_get__meshRenderers())
            {
                if(!renderer)
                    continue;
                auto rendererObject = renderer->get_gameObject();
                if(!rendererObject || !rendererObject->get_activeSelf())
                    continue;
                rendererObject->SetActive(false);
                ++hidden;
            }

            auto rowObject = spectrogramRow->get_gameObject();
            if(rowObject && rowObject->get_activeSelf())
            {
                rowObject->SetActive(false);
                ++hidden;
            }
        }

        // Big Mirror also builds the two large symmetric waveform/light
        // structures from TransformSpectrogram components. They do not derive
        // from Spectrogram, so the original pass hid the central driver while
        // leaving this left/right pair visible below the video screen.
        for(auto* transformSpectrogram : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::TransformSpectrogram*>(false))
        {
            if(!transformSpectrogram)
                continue;
            auto gameObject = transformSpectrogram->get_gameObject();
            if(!gameObject || !gameObject->get_activeSelf())
                continue;

            // TransformSpectrogram animates an array of independent scene
            // transforms. Those objects are not required to be children of
            // the component's GameObject, so disabling only this driver stops
            // their motion but leaves their last waveform shape rendered.
            // Deactivate every referenced object first to remove the actual
            // left/right spectrogram geometry from the environment.
            for(auto referencedTransform :
                transformSpectrogram->__cordl_internal_get__transforms())
            {
                if(!referencedTransform)
                    continue;
                auto referencedObject = referencedTransform->get_gameObject();
                if(!referencedObject || !referencedObject->get_activeSelf())
                    continue;
                referencedObject->SetActive(false);
                ++hidden;
            }

            gameObject->SetActive(false);
            ++hidden;
        }
        BigScreen::BigScreenLogger.info("Hidden {} spectrogram-bar objects for video gameplay", hidden);
    }

    void HideSideLaserGeometry()
    {
        int hidden = 0;
        for(auto* transform : UnityEngine::Object::FindObjectsOfType<
                UnityEngine::Transform*>(false))
        {
            if(!transform)
                continue;
            auto gameObject = transform->get_gameObject();
            if(!gameObject || !gameObject->get_activeSelf())
                continue;

            const std::string name(gameObject->get_name());
            const bool directionalRail =
                name == "NeonTubeDirectionalL" ||
                name == "NeonTubeDirectionalR" ||
                name == "NeonTubeDirectionalFL" ||
                name == "NeonTubeDirectionalFR";
            const bool rotatingLaserPair =
                name.rfind("RotatingLasersPair", 0) == 0;
            const bool doubleColorLaser =
                name.rfind("DoubleColorLaser", 0) == 0;
            if(!directionalRail && !rotatingLaserPair && !doubleColorLaser)
                continue;

            // The channel filter disables the managed LightWithId components,
            // but Big Mirror's side-light roots also contain ordinary
            // BoxLight and BakedBloom renderers. Those children keep their
            // initialized white glow after the controller is unregistered.
            // Deactivate the complete roots assigned to the left/right laser
            // channels so their live lights and baked geometry disappear.
            gameObject->SetActive(false);
            ++hidden;
        }
        BigScreen::BigScreenLogger.info(
            "Hidden {} Big Mirror side-laser geometry roots for video gameplay",
            hidden);
    }

    void DisableEnvironmentLighting()
    {
        int blackenedAndDisabled = 0;
        for(auto* light : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::LightWithIdMonoBehaviour*>(false))
        {
            if(!light || !light->get_enabled())
                continue;

            // A registered light retains its last assigned color even after
            // map events stop. Push black through its concrete implementation
            // before disabling it, then OnDisable unregisters it from Beat
            // Saber's light manager so Chroma or another manager cannot relight
            // it later in the level.
            light->ColorWasSet(UnityEngine::Color::get_black());
            light->set_enabled(false);
            ++blackenedAndDisabled;
        }

        // The newer environment renderer also owns bloom, line, point, and
        // directional lights that are not all represented by a LightWithId
        // component. Disabling them unregisters the actual rendered geometry
        // and clears any already-lit/default state left after scene creation.
        int rendererLightsDisabled = 0;
        rendererLightsDisabled += DisableLoadedComponents<GlobalNamespace::BloomPrePassLight>();
        rendererLightsDisabled += DisableLoadedComponents<GlobalNamespace::LineLight>();
        rendererLightsDisabled += DisableLoadedComponents<GlobalNamespace::PointLight>();
        rendererLightsDisabled += DisableLoadedComponents<GlobalNamespace::DirectionalLight>();

        int animatedLightingDisabled = 0;
        for(auto* animator : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::LightsAnimator*>(false))
        {
            if(animator && animator->get_enabled())
            {
                animator->SetLightsColor(UnityEngine::Color::get_black());
                animator->set_enabled(false);
                ++animatedLightingDisabled;
            }
        }
        // Spectrograms are driven directly by song audio rather than map-event
        // callbacks, but visually form part of the environment light show.
        animatedLightingDisabled += DisableLoadedComponents<GlobalNamespace::Spectrogram>();
        animatedLightingDisabled += DisableLoadedComponents<GlobalNamespace::TransformSpectrogram>();

        BigScreen::BigScreenLogger.info(
            "Map lighting scene shutdown blackened {} managed lights, disabled {} renderer lights, and stopped {} animated lights",
            blackenedAndDisabled,
            rendererLightsDisabled,
            animatedLightingDisabled);
    }

    bool ShouldDisableSelectedLightId(int lightId)
    {
        const auto& settings = BigScreen::Settings::Instance();

        // Big Mirror and Glass Desert share the standard legacy mapping:
        // events 0..4 drive light IDs 1..5. Keep the user-facing controls
        // grouped by what the mapper sees rather than exposing raw IDs.
        return (settings.HideBackWallLights() && (lightId == 1 || lightId == 5)) ||
               (settings.HideRingLights() && lightId == 2) ||
               (settings.HideSideLaserLights() && (lightId == 3 || lightId == 4));
    }

    void DisableSelectedLightingChannels()
    {
        int blackenedAndDisabled = 0;
        for(auto* light : UnityEngine::Object::FindObjectsOfType<
                GlobalNamespace::LightWithIdMonoBehaviour*>(false))
        {
            if(!light || !light->get_enabled() ||
               !ShouldDisableSelectedLightId(light->get_lightId()))
            {
                continue;
            }

            // Blacken the color assigned while the environment initializes,
            // then disable the component so OnDisable unregisters it from the
            // manager. The unregistered light no longer receives map or Chroma
            // updates, so selective event interception is unnecessary.
            light->ColorWasSet(UnityEngine::Color::get_black());
            light->set_enabled(false);
            ++blackenedAndDisabled;
        }

        BigScreen::BigScreenLogger.info(
            "Blackened and disabled {} lights in selected legacy channel groups",
            blackenedAndDisabled);
    }

    bool ShouldSuppressEnvironmentMotion()
    {
        return BigScreen::Settings::Instance().ModEnabled() &&
               BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
               !BigScreen::PlaybackSession::Instance().MapperEnvironmentPresentationActive() &&
               BigScreen::Settings::Instance().DisableEnvironmentMotion();
    }

    bool ShouldSuppressMapLighting()
    {
        return BigScreen::Settings::Instance().ModEnabled() &&
               BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
               !BigScreen::PlaybackSession::Instance().MapperEnvironmentPresentationActive() &&
               !BigScreen::Settings::Instance().MapLightShowEnabled();
    }

    bool IsLightingEvent(GlobalNamespace::BeatmapEventData* eventData)
    {
        if(!eventData)
            return false;

        // Legacy maps encode lights, color boost, ring movement, and other
        // environment effects as BasicBeatmapEventData. BPM changes can also
        // use that class, so preserve type 100 because it affects song timing
        // rather than the environment.
        if(const auto basic =
               il2cpp_utils::try_cast<GlobalNamespace::BasicBeatmapEventData>(eventData))
        {
            return (*basic)->basicBeatmapEventType.value__ != 100;
        }

        // Event-box maps use dedicated event classes. Suppress only the visual
        // environment classes so spawn rotation, BPM changes, notes, obstacles,
        // and every other gameplay callback continue through the normal path.
        return il2cpp_utils::try_cast<GlobalNamespace::LightColorBeatmapEventData>(eventData).has_value() ||
               il2cpp_utils::try_cast<GlobalNamespace::LightRotationBeatmapEventData>(eventData).has_value() ||
               il2cpp_utils::try_cast<GlobalNamespace::LightTranslationBeatmapEventData>(eventData).has_value() ||
               il2cpp_utils::try_cast<GlobalNamespace::ColorBoostBeatmapEventData>(eventData).has_value() ||
               il2cpp_utils::try_cast<GlobalNamespace::FxBeatmapEventData>(eventData).has_value();
    }

    // BLOOM EXPERIMENT DISABLED (2026-08-18): retain the complete hook but do
    // not intercept Beat Saber's bloom pre-pass in a release build.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
    MAKE_HOOK_MATCH(
        BloomPrePass_OnPreRender,
        &GlobalNamespace::BloomPrePass::OnPreRender,
        void,
        GlobalNamespace::BloomPrePass* self)
    {
        // Inject only after this BloomPrePass has finished rendering and
        // publishing its camera-owned destination. Camera.FireOnPreRender is
        // too broad on Quest: the component can subsequently rebuild the
        // target and discard Big Screen's otherwise valid blurred pixels.
        BloomPrePass_OnPreRender(self);
        if(self)
        {
            static std::vector<GlobalNamespace::BloomPrePass*> loggedInstances;
            if(std::find(loggedInstances.begin(), loggedInstances.end(), self) ==
               loggedInstances.end())
            {
                loggedInstances.push_back(self);
                BigScreen::BigScreenLogger.info(
                    "Observed BloomPrePass instance {} with mode {}",
                    static_cast<void*>(self),
                    self->__cordl_internal_get__mode().value__);
            }
            auto* camera = self->GetComponent<UnityEngine::Camera*>();
            BigScreen::CinemaBloomRenderer::Instance().OnCameraPreRender(
                camera, self);
        }
    }
#endif

    MAKE_HOOK_MATCH(
        BeatmapCallbacksController_TriggerBeatmapEvent,
        &GlobalNamespace::BeatmapCallbacksController::TriggerBeatmapEvent,
        void,
        GlobalNamespace::BeatmapCallbacksController* self,
        GlobalNamespace::BeatmapEventData* eventData)
    {
        // Filtering at Beat Saber's central dispatcher avoids updating every
        // individual light, laser, particle, and movement component. The check
        // runs per map event, not per rendered frame, and disabling the effects
        // therefore reduces rather than increases the level's rendering work.
        if(ShouldSuppressMapLighting() && IsLightingEvent(eventData))
            return;
        BeatmapCallbacksController_TriggerBeatmapEvent(self, eventData);
    }

    MAKE_HOOK_MATCH(
        TrackLaneRingsRotationEffectSpawner_HandleBeatmapEvent,
        &GlobalNamespace::TrackLaneRingsRotationEffectSpawner::HandleBeatmapEvent,
        void,
        GlobalNamespace::TrackLaneRingsRotationEffectSpawner* self,
        GlobalNamespace::BasicBeatmapEventData* eventData)
    {
        // Beat map callbacks are ordinary delegates and can invoke a disabled
        // MonoBehaviour. Suppress the callback itself so no new ring rotation
        // effects accumulate while the visual effect component is frozen.
        if(ShouldSuppressEnvironmentMotion())
            return;
        TrackLaneRingsRotationEffectSpawner_HandleBeatmapEvent(self, eventData);
    }

    MAKE_HOOK_MATCH(
        TrackLaneRingsPositionStepEffectSpawner_HandleBeatmapEvent,
        &GlobalNamespace::TrackLaneRingsPositionStepEffectSpawner::HandleBeatmapEvent,
        void,
        GlobalNamespace::TrackLaneRingsPositionStepEffectSpawner* self,
        GlobalNamespace::BasicBeatmapEventData* eventData)
    {
        // Position-step callbacks move the same tunnel scenery directly, so
        // component.enabled alone would not stop them once they subscribed.
        if(ShouldSuppressEnvironmentMotion())
            return;
        TrackLaneRingsPositionStepEffectSpawner_HandleBeatmapEvent(self, eventData);
    }

    MAKE_HOOK_MATCH(
        StandardLevelDetailView_Awake,
        &GlobalNamespace::StandardLevelDetailView::Awake,
        void,
        GlobalNamespace::StandardLevelDetailView* self)
    {
        StandardLevelDetailView_Awake(self);
        BigScreen::ErrorManager::Instance().Guard("creating song-screen controls", [&]() {
            BigScreen::SelectionVideoToggle::Instance().CreateUi(self);
        });
    }

    MAKE_HOOK_MATCH(
        StandardLevelDetailView_OnEnable,
        &GlobalNamespace::StandardLevelDetailView::OnEnable,
        void,
        GlobalNamespace::StandardLevelDetailView* self)
    {
        // Let Beat Saber restore its audio/selection subscriptions first, then
        // resume the retained preview only if the user's global settings allow
        // it. Initial activation is harmless because no map is prepared yet.
        StandardLevelDetailView_OnEnable(self);
        BigScreen::ErrorManager::Instance().Guard("showing song-screen video controls", [&]() {
            auto& controls = BigScreen::SelectionVideoToggle::Instance();
            // StandardLevelDetailView is commonly retained rather than Awakened
            // again after visiting Campaign. Re-anchor the shared canvas here
            // before showing it so it always returns to Solo's ScreenSystem.
            controls.CreateUi(self);
            controls.SongSelectionShown();
        });
    }

    MAKE_HOOK_MATCH(
        StandardLevelDetailView_OnDisable,
        &GlobalNamespace::StandardLevelDetailView::OnDisable,
        void,
        GlobalNamespace::StandardLevelDetailView* self)
    {
        // OnDisable is the reliable boundary for leaving song selection. The
        // view is commonly kept alive while Beat Saber shows its home screen
        // or a mod flow, so waiting for OnDestroy can leave the world-space
        // video surface and decoder running outside the song browser.
        BigScreen::ErrorManager::Instance().Guard("hiding song-screen video controls", []() {
            BigScreen::SelectionVideoToggle::Instance().SongSelectionHidden();
        });
        StandardLevelDetailView_OnDisable(self);
    }

    MAKE_HOOK_MATCH(
        StandardLevelDetailView_OnDestroy,
        &GlobalNamespace::StandardLevelDetailView::OnDestroy,
        void,
        GlobalNamespace::StandardLevelDetailView* self)
    {
        // OnDisable normally runs first, but repeat the context-guarded cleanup
        // here as a defensive fallback for scene teardown ordering changes.
        BigScreen::ErrorManager::Instance().Guard("destroying song-screen video controls", []() {
            BigScreen::SelectionVideoToggle::Instance().SongSelectionHidden();
            BigScreen::SelectionVideoToggle::Instance().ForgetUi();
        });
        StandardLevelDetailView_OnDestroy(self);
    }

    MAKE_HOOK_MATCH(
        MissionLevelDetailViewController_DidActivate,
        &GlobalNamespace::MissionLevelDetailViewController::DidActivate,
        void,
        GlobalNamespace::MissionLevelDetailViewController* self,
        bool firstActivation,
        bool addedToHierarchy,
        bool screenSystemEnabling)
    {
        // Campaign owns a separate mission-detail controller and never creates
        // StandardLevelDetailView, so Solo's lifecycle hooks cannot create the
        // global video controls here. Hook the detail view itself rather than
        // its parent NavigationController: the parent's DidActivate runs before
        // this view is reliably attached to ScreenSystem and previously placed
        // the row hundreds of world units offscreen on the first Campaign visit.
        MissionLevelDetailViewController_DidActivate(
            self, firstActivation, addedToHierarchy, screenSystemEnabling);
        BigScreen::ErrorManager::Instance().Guard(
            "showing campaign video controls", [&]() {
                auto& controls = BigScreen::SelectionVideoToggle::Instance();
                controls.CreateCampaignUi(self);
                controls.CampaignSelectionShown();
            });
    }

    MAKE_HOOK_MATCH(
        MissionSelectionNavigationController_DidDeactivate,
        &GlobalNamespace::MissionSelectionNavigationController::DidDeactivate,
        void,
        GlobalNamespace::MissionSelectionNavigationController* self,
        bool removedFromHierarchy,
        bool screenSystemDisabling)
    {
        // The canvas is a scene-root object rather than a child of Campaign's
        // navigation controller. Hide it explicitly before the controller is
        // hidden so it cannot remain over gameplay, home, or another flow.
        // Keep the shared controls alive because Beat Saber also retains Solo's
        // detail view and download row between visits.
        BigScreen::ErrorManager::Instance().Guard(
            "hiding campaign video controls", []() {
                auto& controls = BigScreen::SelectionVideoToggle::Instance();
                controls.CampaignSelectionHidden();
            });
        MissionSelectionNavigationController_DidDeactivate(
            self, removedFromHierarchy, screenSystemDisabling);
    }

    MAKE_HOOK_MATCH(
        StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo,
        &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::InitEnvironmentInfo,
        void,
        GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
        GlobalNamespace::OverrideEnvironmentSettings* overrideEnvironmentSettings,
        GlobalNamespace::EnvironmentsListModel* environmentsListModel)
    {
        if(!BigScreen::Settings::Instance().ModEnabled())
        {
            StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo(
                self,
                overrideEnvironmentSettings,
                environmentsListModel);
            return;
        }

        // Init has already stored the chosen BeatmapLevel by the time it calls
        // this helper. Preparing here is early enough to influence environment
        // selection but late enough to avoid hooking both overloaded Init APIs.
        auto& playback = BigScreen::PlaybackSession::Instance();
        BigScreen::ErrorManager::Instance().Guard(
            "preparing video before environment selection", [&]() {
                PrepareGameplayVideoForLevel(
                    self->get_beatmapLevel(),
                    self->get_beatmapKey());
            });
        StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo(
            self,
            overrideEnvironmentSettings,
            environmentsListModel);

        BigScreen::ErrorManager::Instance().Guard(
            "applying the video environment override", [&]() {
                const auto& settings = BigScreen::Settings::Instance();
                if(playback.MapperEnvironmentPresentationActive())
                {
                    // A mapper-requested environment is part of the Cinema
                    // contract. If none is supplied, retain the map's normal
                    // Chroma-aware environment rather than forcing Big Mirror.
                    const auto& mapperEnvironment =
                        playback.RequestedEnvironment();
                    if(!mapperEnvironment)
                    {
                        BigScreen::BigScreenLogger.info(
                            "Allow Chroma Override retained the map's intended environment");
                        return;
                    }
                    if(!environmentsListModel)
                    {
                        BigScreen::BigScreenLogger.warn(
                            "Beat Saber's environment list was unavailable; retaining the map environment");
                        BigScreen::ErrorManager::Instance().RecordError(
                            "Applying the mapper environment",
                            "Beat Saber's environment list was unavailable; the map environment was retained");
                        return;
                    }
                    auto environment = environmentsListModel
                        ->GetEnvironmentInfoBySerializedNameSafe(
                            StringW(*mapperEnvironment));
                    if(environment && std::string(
                           environment->get_serializedName()) ==
                           *mapperEnvironment)
                    {
                        // Beat Saber 1.40 separates the map's original
                        // environment from the target environment selected for
                        // the gameplay transition. Override only the target so
                        // the original remains available for restoration and
                        // Chroma-aware fallback behavior.
                        self->set_targetEnvironmentInfo(environment);
                        self->set_usingOverrideEnvironment(true);
                        BigScreen::BigScreenLogger.info(
                            "Allow Chroma Override loaded mapper environment '{}'",
                            *mapperEnvironment);
                    }
                    else
                    {
                        BigScreen::BigScreenLogger.warn(
                            "Mapper environment '{}' is unavailable; keeping the map environment",
                            *mapperEnvironment);
                    }
                    return;
                }

                if(!settings.GlassDesertOverrideEnabled() &&
                   !settings.EnvironmentOverrideEnabled())
                {
                    if(playback.HasPreparedVideo())
                    {
                        BigScreen::BigScreenLogger.info(
                            "Environment overrides disabled; using the map's intended environment");
                    }
                    return;
                }

                if(!playback.HasPreparedVideo())
                    return;
                if(!environmentsListModel)
                {
                    BigScreen::BigScreenLogger.warn(
                        "Beat Saber's environment list was unavailable; retaining the map environment");
                    BigScreen::ErrorManager::Instance().RecordError(
                        "Applying the video environment",
                        "Beat Saber's environment list was unavailable; the map environment was retained");
                    return;
                }

                // Glass Desert is an explicit experiment and takes precedence
                // while enabled. Turning it off restores the independent Big
                // Mirror preference without losing the normal override choice.
                constexpr auto bigMirrorName = "BigMirrorEnvironment";
                constexpr auto glassDesertName = "GlassDesertEnvironment";
                const auto* requestedName = settings.GlassDesertOverrideEnabled()
                    ? glassDesertName
                    : bigMirrorName;
                auto environment = environmentsListModel
                    ->GetEnvironmentInfoBySerializedNameSafe(
                        StringW(requestedName));
                if(environment && std::string(
                       environment->get_serializedName()) == requestedName)
                {
                    // See the mapper-environment path above: 1.40 expects an
                    // override to replace the transition target, not the
                    // preserved original environment.
                    self->set_targetEnvironmentInfo(environment);
                    self->set_usingOverrideEnvironment(true);
                    BigScreen::BigScreenLogger.info(
                        "Forced {} environment for video gameplay",
                        settings.GlassDesertOverrideEnabled()
                            ? "Glass Desert"
                            : "Big Mirror");
                }
                else
                {
                    BigScreen::BigScreenLogger.error(
                        "Requested {} environment is unavailable; keeping the map environment",
                        requestedName);
                    BigScreen::ErrorManager::Instance().RecordError(
                        "Applying the requested environment",
                        std::string("Environment '") + requestedName +
                            "' was unavailable; the map environment was retained");
                }
            });
    }

    MAKE_HOOK_MATCH(
        StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes,
        &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::InitAndSetupScenes,
        void,
        GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
        GlobalNamespace::PlayerSpecificSettings* playerSpecificSettings,
        StringW backButtonText,
        bool startPaused)
    {
        if(!BigScreen::Settings::Instance().ModEnabled())
        {
            StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes(
                self,
                playerSpecificSettings,
                backButtonText,
                startPaused);
            return;
        }

        auto* effectiveSettings = playerSpecificSettings;
        BigScreen::ErrorManager::Instance().Guard(
            "preparing video-specific player settings", [&]() {
                if(BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
                   !BigScreen::PlaybackSession::Instance()
                        .MapperEnvironmentPresentationActive() &&
                   !BigScreen::Settings::Instance().MapLightShowEnabled() &&
                   playerSpecificSettings)
                {
                    // CopyWith preserves every player preference while
                    // replacing only the two difficulty-dependent environment
                    // filters. A copy cannot alter saved settings or later maps.
                    using OptionalEffects = System::Nullable_1<
                        GlobalNamespace::EnvironmentEffectsFilterPreset>;
                    const OptionalEffects noEffects{
                        true,
                        GlobalNamespace::EnvironmentEffectsFilterPreset::NoEffects
                    };
                    effectiveSettings = playerSpecificSettings->CopyWith(
                        {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
                        {}, {}, {}, {}, {}, {}, {}, noEffects, noEffects, {});
                    BigScreen::BigScreenLogger.info(
                        "Map light show disabled for this video level");
                }
            });

        StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes(
            self,
            effectiveSettings,
            backButtonText,
            startPaused);
    }

    using MissionObjectiveArray = ArrayW<
        GlobalNamespace::MissionObjective*,
        Array<GlobalNamespace::MissionObjective*>*>;
    using MissionInitWithLoadedData = void (
        GlobalNamespace::MissionLevelScenesTransitionSetupDataSO::*)(
            StringW,
            GlobalNamespace::IBeatmapLevelData*,
            ByRef<GlobalNamespace::BeatmapKey>,
            GlobalNamespace::BeatmapLevel*,
            MissionObjectiveArray,
            GlobalNamespace::ColorScheme*,
            GlobalNamespace::GameplayModifiers*,
            GlobalNamespace::PlayerSpecificSettings*,
            GlobalNamespace::EnvironmentsListModel*,
            GlobalNamespace::AudioClipAsyncLoader*,
            GlobalNamespace::SettingsManager*,
            GlobalNamespace::BeatmapDataLoader*,
            StringW);
    using MissionInitWithLevelsModel = void (
        GlobalNamespace::MissionLevelScenesTransitionSetupDataSO::*)(
            StringW,
            ByRef<GlobalNamespace::BeatmapKey>,
            GlobalNamespace::BeatmapLevel*,
            MissionObjectiveArray,
            GlobalNamespace::ColorScheme*,
            GlobalNamespace::GameplayModifiers*,
            GlobalNamespace::PlayerSpecificSettings*,
            GlobalNamespace::EnvironmentsListModel*,
            GlobalNamespace::BeatmapLevelsModel*,
            GlobalNamespace::AudioClipAsyncLoader*,
            GlobalNamespace::SettingsManager*,
            GlobalNamespace::BeatmapDataLoader*,
            StringW);

    MAKE_HOOK_MATCH(
        MissionLevelScenesTransitionSetupDataSO_InitWithLoadedData,
        static_cast<MissionInitWithLoadedData>(
            &GlobalNamespace::MissionLevelScenesTransitionSetupDataSO::Init),
        void,
        GlobalNamespace::MissionLevelScenesTransitionSetupDataSO* self,
        StringW missionId,
        GlobalNamespace::IBeatmapLevelData* beatmapLevelData,
        ByRef<GlobalNamespace::BeatmapKey> beatmapKey,
        GlobalNamespace::BeatmapLevel* beatmapLevel,
        MissionObjectiveArray missionObjectives,
        GlobalNamespace::ColorScheme* overrideColorScheme,
        GlobalNamespace::GameplayModifiers* gameplayModifiers,
        GlobalNamespace::PlayerSpecificSettings* playerSpecificSettings,
        GlobalNamespace::EnvironmentsListModel* environmentsListModel,
        GlobalNamespace::AudioClipAsyncLoader* audioClipAsyncLoader,
        GlobalNamespace::SettingsManager* settingsManager,
        GlobalNamespace::BeatmapDataLoader* beatmapDataLoader,
        StringW backButtonText)
    {
        MissionInitPreparationScope preparationScope;
        if(BigScreen::Settings::Instance().ModEnabled() &&
           preparationScope.OwnsPreparation())
        {
            BigScreen::ErrorManager::Instance().Guard(
                "preparing campaign video", [&]() {
                    PrepareGameplayVideoForLevel(beatmapLevel, *beatmapKey);
                });
        }
        MissionLevelScenesTransitionSetupDataSO_InitWithLoadedData(
            self,
            missionId,
            beatmapLevelData,
            beatmapKey,
            beatmapLevel,
            missionObjectives,
            overrideColorScheme,
            gameplayModifiers,
            playerSpecificSettings,
            environmentsListModel,
            audioClipAsyncLoader,
            settingsManager,
            beatmapDataLoader,
            backButtonText);
    }

    MAKE_HOOK_MATCH(
        MissionLevelScenesTransitionSetupDataSO_InitWithLevelsModel,
        static_cast<MissionInitWithLevelsModel>(
            &GlobalNamespace::MissionLevelScenesTransitionSetupDataSO::Init),
        void,
        GlobalNamespace::MissionLevelScenesTransitionSetupDataSO* self,
        StringW missionId,
        ByRef<GlobalNamespace::BeatmapKey> beatmapKey,
        GlobalNamespace::BeatmapLevel* beatmapLevel,
        MissionObjectiveArray missionObjectives,
        GlobalNamespace::ColorScheme* overrideColorScheme,
        GlobalNamespace::GameplayModifiers* gameplayModifiers,
        GlobalNamespace::PlayerSpecificSettings* playerSpecificSettings,
        GlobalNamespace::EnvironmentsListModel* environmentsListModel,
        GlobalNamespace::BeatmapLevelsModel* beatmapLevelsModel,
        GlobalNamespace::AudioClipAsyncLoader* audioClipAsyncLoader,
        GlobalNamespace::SettingsManager* settingsManager,
        GlobalNamespace::BeatmapDataLoader* beatmapDataLoader,
        StringW backButtonText)
    {
        MissionInitPreparationScope preparationScope;
        if(BigScreen::Settings::Instance().ModEnabled() &&
           preparationScope.OwnsPreparation())
        {
            BigScreen::ErrorManager::Instance().Guard(
                "preparing campaign video", [&]() {
                    PrepareGameplayVideoForLevel(beatmapLevel, *beatmapKey);
                });
        }
        MissionLevelScenesTransitionSetupDataSO_InitWithLevelsModel(
            self,
            missionId,
            beatmapKey,
            beatmapLevel,
            missionObjectives,
            overrideColorScheme,
            gameplayModifiers,
            playerSpecificSettings,
            environmentsListModel,
            beatmapLevelsModel,
            audioClipAsyncLoader,
            settingsManager,
            beatmapDataLoader,
            backButtonText);
    }

    MAKE_HOOK_MATCH(
        StandardLevelRestartController_RestartLevel,
        &GlobalNamespace::StandardLevelRestartController::RestartLevel,
        void,
        GlobalNamespace::StandardLevelRestartController* self)
    {
        // Restart destroys GameCore before StartSong runs for the replacement
        // scene. Stop at the button/controller boundary so no intervening
        // AudioTimeSyncController update can upload into a destroyed texture.
        BigScreen::ErrorManager::Instance().Guard(
            "stopping video before level restart", []() {
                RestoreShowcaseTrackRings();
                RestoreShowcaseSidePillars();
                RestoreShowcaseBackground();
                BigScreen::PlaybackSession::Instance().Stop();
            });
        StandardLevelRestartController_RestartLevel(self);
    }

    MAKE_HOOK_MATCH(
        MissionLevelRestartController_RestartLevel,
        &GlobalNamespace::MissionLevelRestartController::RestartLevel,
        void,
        GlobalNamespace::MissionLevelRestartController* self)
    {
        BigScreen::ErrorManager::Instance().Guard(
            "stopping campaign video before level restart", []() {
                RestoreShowcaseTrackRings();
                RestoreShowcaseSidePillars();
                RestoreShowcaseBackground();
                BigScreen::PlaybackSession::Instance().Stop();
            });
        MissionLevelRestartController_RestartLevel(self);
    }

    MAKE_HOOK_MATCH(
        BeatmapObjectSpawnController_Start,
        &GlobalNamespace::BeatmapObjectSpawnController::Start,
        void,
        GlobalNamespace::BeatmapObjectSpawnController* self)
    {
        BeatmapObjectSpawnController_Start(self);

        if(!BigScreen::Settings::Instance().ModEnabled() ||
           !BigScreen::Settings::Instance().PerformanceDiagnosticsEnabled() ||
           !BigScreen::PlaybackSession::Instance().HasPreparedVideo() || !self)
            return;

        // Beatmap data is already sorted by song time. Walking backward avoids
        // scanning every note and lighting event; the first NoteData found is
        // the last playable note. This one-time map-start lookup prevents the
        // results transition from contaminating headset-FPS minimum/maximum
        // values without doing work in the real-time playback loop.
        BigScreen::ErrorManager::Instance().Guard(
            "finding the final gameplay note", [self]()
            {
                auto* callbacks =
                    self->__cordl_internal_get__beatmapCallbacksController();
                auto* beatmapData = callbacks
                    ? callbacks->__cordl_internal_get__beatmapData()
                    : nullptr;
                auto* items = beatmapData
                    ? beatmapData->get_allBeatmapDataItems()
                    : nullptr;
                for(auto* node = items ? items->get_Last() : nullptr;
                    node;
                    node = node->get_Previous())
                {
                    auto* item = node->get_Value();
                    if(const auto note =
                           il2cpp_utils::try_cast<GlobalNamespace::NoteData>(item))
                    {
                        BigScreen::PlaybackSession::Instance()
                            .SetGameplayLastNoteTime((*note)->get_time());
                        break;
                    }
                }
            });
    }

    MAKE_HOOK_MATCH(
        AudioTimeSyncController_StartSong,
        &GlobalNamespace::AudioTimeSyncController::StartSong,
        void,
        GlobalNamespace::AudioTimeSyncController* self,
        float startTimeOffset)
    {
        BigScreen::ErrorManager::Instance().Guard("saving settings before gameplay", []() {
            BigScreen::Settings::Instance().Flush();
        });
        AudioTimeSyncController_StartSong(self, startTimeOffset);

        if(!BigScreen::Settings::Instance().ModEnabled())
            return;

        BigScreen::ErrorManager::Instance().SetGameplayActive(true);
        // StartSong runs after the gameplay scene and environment have loaded.
        // Treat environment cleanup and screen creation as one protected mod
        // operation: any failure is logged/queued, never allowed to interrupt
        // Beat Saber's already-started song.
        BigScreen::ErrorManager::Instance().Guard("starting gameplay video", [&]() {
            const auto& settings = BigScreen::Settings::Instance();
            auto& playback = BigScreen::PlaybackSession::Instance();
            if(playback.IsGameplayActive())
            {
                // Beat Saber's in-level Restart replaces GameCore without
                // calling StandardLevelScenesTransitionSetupDataSO::Finish.
                // The old Unity screen has therefore been scene-destroyed even
                // though PlaybackSession still says it is active. Tear down
                // the retained decoder/session now; Stop deliberately keeps
                // the prepared map configuration, allowing Start below to
                // create a fresh screen synchronized to the restarted song.
                BigScreen::BigScreenLogger.info(
                    "Detected gameplay restart; rebuilding the video session");
                playback.Stop();
            }
            const bool mapperControlsEnvironment =
                playback.MapperEnvironmentPresentationActive();
            if(playback.HasPreparedVideo() && !mapperControlsEnvironment &&
               settings.DisableEnvironmentMotion())
                DisableEnvironmentMotion();
            if(playback.HasPreparedVideo() && !mapperControlsEnvironment &&
               !settings.MapLightShowEnabled())
                DisableEnvironmentLighting();
            else if(playback.HasPreparedVideo() && !mapperControlsEnvironment)
                DisableSelectedLightingChannels();
            if(playback.HasPreparedVideo() && !mapperControlsEnvironment &&
               settings.HideTrackRings())
                HideTrackLaneRings();
            if(playback.HasPreparedVideo() && !mapperControlsEnvironment &&
               settings.HideSideBars())
                HideSideBars();
            if(playback.HasPreparedVideo() && !mapperControlsEnvironment &&
               settings.HideSpectrogramBars())
                HideSpectrogramBars();
            if(playback.HasPreparedVideo() && !mapperControlsEnvironment &&
               settings.HideSideLaserLights())
                HideSideLaserGeometry();
            playback.Start(BigScreen::PlaybackContext::Gameplay);
            // The showcase keeps the map's authored environment intact, then
            // temporarily strobes only the central track-ring meshes from a
            // cached set and hides the two side pillars during the floating
            // section. Capture once; never search the Unity scene per frame.
            CaptureShowcaseTrackRings();
            CaptureShowcaseSidePillars();
            CaptureShowcaseBackground();
        });
        auto& playback = BigScreen::PlaybackSession::Instance();
        BigScreen::PowerBenchmark::Instance().Start(
            playback.IsGameplayActive(),
            playback.IsGameplayActive() && playback.ShowcaseActive(),
            playback.Diagnostics());
    }

    MAKE_HOOK_MATCH(
        Application_InvokeFocusChanged,
        &UnityEngine::Application::InvokeFocusChanged,
        void,
        bool hasFocus)
    {
        Application_InvokeFocusChanged(hasFocus);
        if(!hasFocus)
        {
            BigScreen::MenuEnvironmentVisibility::Instance().Restore();
            BigScreen::MenuPlacementGuide::Instance().Suspend();
            BigScreen::ErrorManager::Instance().Guard(
                "cancelling screen positioning after focus loss", []()
                {
                    BigScreen::ScreenPreview::Instance()
                        .CancelUndockedEditing();
                });
            BigScreen::ErrorManager::Instance().Guard("saving settings after focus loss", []() {
                BigScreen::Settings::Instance().Flush();
            });
        }
        else if(BigScreen::IsBigScreenMenuActive())
        {
            // Focus loss always restores the stock renderer state. Recreate
            // the optional guide only after Beat Saber owns the foreground
            // again and Big Screen's retained flow is still active.
            BigScreen::ErrorManager::Instance().Guard(
                "restoring menu placement visuals after focus return", []()
                {
                    BigScreen::MenuPlacementGuide::Instance().Apply();
                    BigScreen::MenuEnvironmentVisibility::Instance().Apply();
                });
        }
    }

    MAKE_HOOK_MATCH(
        SongPreviewPlayer_Update,
        &GlobalNamespace::SongPreviewPlayer::Update,
        void,
        GlobalNamespace::SongPreviewPlayer* self)
    {
        SongPreviewPlayer_Update(self);

        BigScreen::ErrorManager::Instance().Guard("saving deferred settings", []() {
            BigScreen::Settings::Instance().TickPersistence();
            BigScreen::DiagnosticSessionLogger::Instance().Tick();
        });
        // The benchmark toggle may already be enabled from the prior app run.
        // Probe from the first stable main-menu update so battery telemetry is
        // verified before the user spends time on an A/B gameplay pair.
        BigScreen::PowerBenchmark::Instance().ProbeBatteryAccessOnce();
        // This must run even when Big Screen has disabled itself: it services
        // safe-frame error dismissal and the interrupted-lifecycle fail-safe.
        // Normal close/re-entry is released directly by HMUI DidDeactivate.
        BigScreen::TickMenuReentryGuard();
        // A retained Configure Video launch selects its map only after HMUI has
        // finished presenting Big Screen. This avoids losing the editor panel
        // to the enclosing side-controller restoration during DidActivate.
        BigScreen::TickPendingMenuNavigation();
        // Error dialogs remain available after the circuit breaker disables
        // the mod; all other Big Screen menu work stays behind the master flag.
        BigScreen::ErrorManager::Instance().TickMainThread();
        // Unity and BSML objects must be created on this thread. Build only
        // one retained menu page per stable startup frame instead of moving
        // unsafe Unity work to a worker or charging the complete hierarchy to
        // the player's first click. TickMenuPrewarm enforces the master-switch
        // and circuit-breaker gates internally before creating anything.
        BigScreen::ErrorManager::Instance().Guard(
            "prewarming the Big Screen menu", []()
            {
                BigScreen::TickMenuPrewarm();
            });
        if(songCatalogRefreshPending.exchange(
               false, std::memory_order_acq_rel))
        {
            // UI construction is retained, but its map model is not frozen at
            // startup. A map downloaded by any supported installer becomes
            // eligible for Video Available/Download and Configure Video as
            // soon as SongCore publishes its completed refresh.
            BigScreen::VideoLibraryMenu::Instance().RequestCatalogRefresh();
            BigScreen::BigScreenLogger.info(
                "Invalidated Big Screen's retained catalog after SongCore loaded new map data");
        }
        if(BigScreen::IsBigScreenMenuActive())
            BigScreen::TickFrontmostMenuModal();
        if(!BigScreen::Settings::Instance().ModEnabled() ||
           BigScreen::ErrorManager::Instance().MenuRecoveryActive())
            return;

        // Showcase preparation spans downloads, SongCore refresh, menu
        // dismissal, and Solo presentation. It must continue after Big
        // Screen's own flow is no longer active, but remains on Unity's main
        // thread so no IL2CPP menu object is touched by a worker.
        BigScreen::ErrorManager::Instance().Guard(
            "preparing the Big Screen showcase", []()
            {
                BigScreen::ShowcaseLauncher::Instance().Tick();
            });

        BigScreen::ErrorManager::Instance().Guard("updating menu video UI", [&]() {
            // One-shot on the first menu tick: publish the active video
            // shader tier to the log so every deployment can verify it from
            // logcat without opening a menu or starting a video.
            BigScreen::ScreenSurface::LogVideoShaderTierOnce();
            BigScreen::SelectionVideoToggle::Instance().TickDownloadUi();
            // The remaining objects belong exclusively to Big Screen's own
            // retained flow. A MenuCore soft restart can destroy that scene
            // without first nulling native singleton fields; never touch them
            // unless the UnityW-backed coordinator is still alive and active.
            if(BigScreen::IsBigScreenMenuActive())
            {
                BigScreen::VideoLibraryMenu::Instance().Tick(self);
                BigScreen::StorageMaintenanceMenu::Instance().Tick();
                BigScreen::ShowcaseMenu::Instance().Tick();
                BigScreen::LocalVideoBrowserMenu::Instance().Tick();
                BigScreen::ThumbnailPickerMenu::Instance().Tick();
                BigScreen::ScreenPreview::Instance().TickUndockedEditor();
                BigScreen::PerformancePanel::Instance().TickInteraction();
                // Some retained panels refresh or add child objects during
                // their Tick calls. Reassert the active modal after all of
                // that work as the final menu-layer operation for this frame.
                BigScreen::TickFrontmostMenuModal();
            }
        });
        static int downloaderUiFrame = 0;
        if(++downloaderUiFrame >= 30)
        {
            downloaderUiFrame = 0;
            if(BigScreen::IsBigScreenMenuActive())
                BigScreen::ErrorManager::Instance().Guard("refreshing downloader status", []() {
                    BigScreen::SettingsMenu::Instance().RefreshDownloaderStatus();
                });
        }

        // SongPreviewPlayer crossfades between a small bank of AudioSources.
        // Reading its active source after the original Update gives Big Screen
        // the exact clip position heard by the user, including preview start,
        // pause, resume, and crossfade channel changes.
        const int activeChannel = self->__cordl_internal_get__activeChannel();
        auto controllers = self->__cordl_internal_get__audioSourceControllers();
        if(!controllers || activeChannel < 0 ||
           static_cast<std::size_t>(activeChannel) >= controllers.size())
            return;

        auto* controller = controllers[activeChannel];
        if(!controller)
            return;
        auto audioSource = controller->__cordl_internal_get_audioSource();
        if(audioSource)
        {
            // An AudioSource can remain alive for Beat Saber's default menu
            // track or while its controller is completely faded out. Neither
            // represents an audible selected-song preview, so neither should
            // restart or advance Big Screen's menu video.
            const auto activeClip = self->get_activeAudioClip();
            const auto defaultClip = self->__cordl_internal_get__defaultAudioClip();
            const bool selectedSongAudioIsAudible =
                audioSource->get_isPlaying() &&
                controller->get_volume() > 0.001f &&
                activeClip &&
                activeClip != defaultClip;
            BigScreen::ErrorManager::Instance().Guard("synchronizing song-menu preview", [&]() {
                BigScreen::SelectionVideoToggle::Instance().TickSongPreview(
                    self,
                    audioSource->get_time(),
                    selectedSongAudioIsAudible);
            });
        }
    }

    MAKE_HOOK_MATCH(
        AudioTimeSyncController_Update,
        &GlobalNamespace::AudioTimeSyncController::Update,
        void,
        GlobalNamespace::AudioTimeSyncController* self)
    {
        AudioTimeSyncController_Update(self);

        if(!BigScreen::Settings::Instance().ModEnabled())
            return;

        // Beat Saber's song time is the sole playback clock. It stops during a
        // pause, jumps on restart/scrub, incorporates practice speed, and is the
        // timeline Replay advances during playback and frame-by-frame capture.
        BigScreen::ErrorManager::Instance().Guard("updating gameplay video", [&]() {
            const double songTimeSeconds = self->get_songTime();
            auto& playback = BigScreen::PlaybackSession::Instance();
            playback.Tick(songTimeSeconds);
            BigScreen::PowerBenchmark::Instance().Tick(
                songTimeSeconds,
                playback.Diagnostics());
            UpdateShowcaseTrackRings(songTimeSeconds);
            UpdateShowcaseSidePillars(songTimeSeconds);
            UpdateShowcaseBackground(songTimeSeconds);
            BigScreen::PerformancePanel::Instance().TickInteraction();
        });
    }

    MAKE_HOOK_MATCH(
        StandardLevelScenesTransitionSetupDataSO_Finish,
        &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::Finish,
        void,
        GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
        GlobalNamespace::LevelCompletionResults* levelCompletionResults)
    {
        // Release native decoder resources and Unity objects before the normal
        // transition tears down GameCore. This also guarantees that the next
        // selected map cannot inherit a stale frame or decoder worker.
        // Stop unconditionally because the circuit breaker may have changed
        // ModEnabled during the song while a screen/decoder still exists.
        BigScreen::ErrorManager::Instance().Guard("stopping gameplay video", [&]() {
            RestoreShowcaseTrackRings();
            RestoreShowcaseSidePillars();
            RestoreShowcaseBackground();
            BigScreen::PlaybackSession::Instance().Stop();
        });
        BigScreen::PowerBenchmark::Instance().Finish(
            BigScreen::PlaybackSession::Instance().LastResultsData());
        BigScreen::ShowcaseLauncher::Instance().OnGameplayFinished();
        BigScreen::ErrorManager::Instance().SetGameplayActive(false);
        StandardLevelScenesTransitionSetupDataSO_Finish(self, levelCompletionResults);
    }

    MAKE_HOOK_MATCH(
        MissionLevelScenesTransitionSetupDataSO_Finish,
        &GlobalNamespace::MissionLevelScenesTransitionSetupDataSO::Finish,
        void,
        GlobalNamespace::MissionLevelScenesTransitionSetupDataSO* self,
        GlobalNamespace::MissionCompletionResults* levelCompletionResults)
    {
        // Campaign uses a separate transition setup type; StandardLevel's
        // Finish hook is never called. Release the campaign screen and decoder
        // while their GameCore objects are still valid, before Mission Finish
        // unloads the scene.
        BigScreen::ErrorManager::Instance().Guard(
            "stopping campaign gameplay video", [&]() {
                RestoreShowcaseTrackRings();
                RestoreShowcaseSidePillars();
                RestoreShowcaseBackground();
                BigScreen::PlaybackSession::Instance().Stop();
            });
        BigScreen::PowerBenchmark::Instance().Finish(
            BigScreen::PlaybackSession::Instance().LastResultsData());
        BigScreen::ShowcaseLauncher::Instance().OnGameplayFinished();
        BigScreen::ErrorManager::Instance().SetGameplayActive(false);
        MissionLevelScenesTransitionSetupDataSO_Finish(
            self,
            levelCompletionResults);
    }

    MAKE_HOOK_MATCH(
        ResultsViewController_DidActivate,
        &GlobalNamespace::ResultsViewController::DidActivate,
        void,
        GlobalNamespace::ResultsViewController* self,
        bool firstActivation,
        bool addedToHierarchy,
        bool screenSystemEnabling)
    {
        ResultsViewController_DidActivate(
            self, firstActivation, addedToHierarchy, screenSystemEnabling);
        if(!firstActivation ||
           !BigScreen::Settings::Instance().PerformanceDiagnosticsEnabled())
            return;
        BigScreen::ErrorManager::Instance().Guard(
            "showing results performance information", [&]() {
                const auto& results =
                    BigScreen::PlaybackSession::Instance().LastResultsData();
                if(!results)
                    return;
                UnityEngine::Transform* summaryParent = self->get_transform();
                float summaryCenterY = 40.0f;
                if(self->____levelBar)
                {
                    summaryParent = self->____levelBar->get_transform();
                    summaryCenterY = 21.0f;
                }
                CreateResultsPerformancePanel(summaryParent, summaryCenterY, *results);
            });
    }

}

MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept
{
    *info = modInfo.to_c();
    BigScreen::BigScreenLogger.Initialize(VERSION);

    // CustomTypes 0.18.4 installs optional liveness-diagnostic hooks that read
    // Unity's liveness-state filter class from the wrong structure offset on
    // the Unity version used by Beat Saber 1.40.8. When campaign MissionDataSO
    // assets are collected after gameplay, the diagnostic mistakes a valid
    // object for corruption and then dereferences the bogus filter-class
    // pointer in HasParentUnsafe(), crashing Unity's AssetGarbageCol thread.
    //
    // CT_DISABLE_LIVENESS_CHECKS is CustomTypes' supported runtime escape
    // hatch for these diagnostic hooks. It does not disable custom type
    // registration, delegates, or normal GC; it only passes the three debug
    // traversal hooks straight through to Unity. Set it before AutoRegister()
    // and before Big Screen creates any custom UI type. Remove this workaround
    // after the corrected CustomTypes build is available for this game/toolchain
    // generation and has passed campaign-exit regression testing.
    if(::setenv("CT_DISABLE_LIVENESS_CHECKS", "1", 1) != 0)
        BigScreen::BigScreenLogger.warn(
            "Could not disable CustomTypes' incompatible liveness diagnostics; "
            "campaign asset cleanup may remain unstable");
    else
        BigScreen::BigScreenLogger.info(
            "Disabled CustomTypes 0.18.4 liveness diagnostics for Beat Saber 1.40.8");

    BigScreen::ErrorManager::Instance().InitializePersistentLog();
    BigScreen::ErrorManager::Instance().Guard("loading settings", []() {
        BigScreen::Settings::Instance().Load();
    });
    BigScreen::BigScreenLogger.info("Big Screen {} initialized", VERSION);
}

MOD_EXTERN_FUNC void late_load() noexcept
{
    il2cpp_functions::Init();
    custom_types::Register::AutoRegister();
    BigScreen::ErrorManager::Instance().Guard("initializing video library", []() {
        BigScreen::VideoLibrary::Instance().Initialize();
    });
    BigScreen::ErrorManager::Instance().Guard("initializing downloader", []() {
        std::string downloaderError;
        if(!BigScreen::DownloadManager::Instance().Initialize(downloaderError))
        {
            BigScreen::BigScreenLogger.error("Downloader unavailable: {}", downloaderError);
            BigScreen::ErrorManager::Instance().RecordError(
                "Initializing downloader",
                downloaderError);
        }
        else
        {
            // The package that passed activation and the embedded smoke test
            // is authoritative. Older builds stored a separate update-channel
            // preference, allowing a nightly runtime to appear with an Off
            // switch. Reconcile that legacy field before any menu is created.
            const bool nightly =
                BigScreen::DownloadManager::Instance()
                    .CurrentYtDlpChannel() == "nightly";
            if(BigScreen::Settings::Instance().NightlyDownloaderUpdates() !=
               nightly)
            {
                BigScreen::Settings::Instance()
                    .SetNightlyDownloaderUpdates(nightly);
            }
        }
        // Release checks begin only after the player enters Big Screen. The
        // settings UI schedules them on dedicated background workers once per
        // game session, so startup and menu activation never wait on GitHub.
    });

    // Hooks stay on public Beat Saber lifecycle and clock APIs: selection view
    // visibility owns menu preview lifetime, scene transition owns gameplay
    // setup, and Beat Saber's audio clocks remain authoritative for sync.
    INSTALL_HOOK(BigScreen::BigScreenLogger, StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo);
    INSTALL_HOOK(BigScreen::BigScreenLogger, StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes);
    INSTALL_HOOK(BigScreen::BigScreenLogger, MissionLevelScenesTransitionSetupDataSO_InitWithLoadedData);
    INSTALL_HOOK(BigScreen::BigScreenLogger, MissionLevelScenesTransitionSetupDataSO_InitWithLevelsModel);
    INSTALL_HOOK(BigScreen::BigScreenLogger, StandardLevelRestartController_RestartLevel);
    INSTALL_HOOK(BigScreen::BigScreenLogger, MissionLevelRestartController_RestartLevel);
    INSTALL_HOOK(BigScreen::BigScreenLogger, StandardLevelDetailView_Awake);
    INSTALL_HOOK(BigScreen::BigScreenLogger, StandardLevelDetailView_OnEnable);
    INSTALL_HOOK(BigScreen::BigScreenLogger, StandardLevelDetailView_OnDisable);
    INSTALL_HOOK(BigScreen::BigScreenLogger, StandardLevelDetailView_OnDestroy);
    INSTALL_HOOK(BigScreen::BigScreenLogger, MissionLevelDetailViewController_DidActivate);
    INSTALL_HOOK(BigScreen::BigScreenLogger, MissionSelectionNavigationController_DidDeactivate);
    // Do not install pause-menu UI hooks on Beat Saber 1.40.8. The attempted
    // BSML IncrementSetting/ToggleSetting controls never rendered in the pause
    // menu, and destroying their hidden hierarchy left an invalid
    // UnityEngine.UI::AnimationTriggers reference for Custom Types 0.18.4's
    // AssetGarbageCol liveness traversal. The result was a reproducible native
    // SIGSEGV whenever a video map exited. Reintroduce this feature only with
    // controls proven visible and a lifecycle that does not use that failed
    // custom-setting hierarchy.
    // BLOOM EXPERIMENT DISABLED (2026-08-18): see the preserved hook above.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
    INSTALL_HOOK(BigScreen::BigScreenLogger, BloomPrePass_OnPreRender);
#endif
    INSTALL_HOOK(BigScreen::BigScreenLogger, BeatmapCallbacksController_TriggerBeatmapEvent);
    INSTALL_HOOK(BigScreen::BigScreenLogger, TrackLaneRingsRotationEffectSpawner_HandleBeatmapEvent);
    INSTALL_HOOK(BigScreen::BigScreenLogger, TrackLaneRingsPositionStepEffectSpawner_HandleBeatmapEvent);
    INSTALL_HOOK(BigScreen::BigScreenLogger, BeatmapObjectSpawnController_Start);
    INSTALL_HOOK(BigScreen::BigScreenLogger, AudioTimeSyncController_StartSong);
    INSTALL_HOOK(BigScreen::BigScreenLogger, AudioTimeSyncController_Update);
    INSTALL_HOOK(BigScreen::BigScreenLogger, SongPreviewPlayer_Update);
    INSTALL_HOOK(BigScreen::BigScreenLogger, Application_InvokeFocusChanged);
    INSTALL_HOOK(BigScreen::BigScreenLogger, StandardLevelScenesTransitionSetupDataSO_Finish);
    INSTALL_HOOK(BigScreen::BigScreenLogger, MissionLevelScenesTransitionSetupDataSO_Finish);
    INSTALL_HOOK(BigScreen::BigScreenLogger, ResultsViewController_DidActivate);
    // SongCore publishes selections after its custom-level details are ready,
    // including WIP songs. A plain native callback keeps this path independent
    // of Beat Saber's private view-controller field layout.
    SongCore::API::LevelSelect::GetLevelWasSelectedEvent().addCallback(HandleLevelWasSelected);
    SongCore::API::Loading::GetSongsLoadedEvent().addCallback(HandleSongsLoaded);
    BigScreen::SettingsMenu::Instance().Register();
    BigScreen::BigScreenLogger.info("Big Screen hooks installed");
}
