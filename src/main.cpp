#include "main.hpp"

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/PauseMenuLayoutSelector.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "BigScreen/StorageMaintenanceMenu.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BasicBeatmapEventData.hpp"
#include "GlobalNamespace/BeatmapCallbacksController.hpp"
#include "GlobalNamespace/BeatmapEventData.hpp"
#include "GlobalNamespace/BloomPrePassLight.hpp"
#include "GlobalNamespace/ColorBoostBeatmapEventData.hpp"
#include "GlobalNamespace/DirectionalLight.hpp"
#include "GlobalNamespace/EnvironmentInfoSO.hpp"
#include "GlobalNamespace/EnvironmentEffectsFilterPreset.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/FxBeatmapEventData.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/LightColorBeatmapEventData.hpp"
#include "GlobalNamespace/LightWithIdMonoBehaviour.hpp"
#include "GlobalNamespace/LightRotationBeatmapEventData.hpp"
#include "GlobalNamespace/LightTranslationBeatmapEventData.hpp"
#include "GlobalNamespace/LineLight.hpp"
#include "GlobalNamespace/LightsAnimator.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/PauseMenuManager.hpp"
#include "GlobalNamespace/PointLight.hpp"
#include "GlobalNamespace/Rotate.hpp"
#include "GlobalNamespace/ResultsViewController.hpp"
#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "GlobalNamespace/Spectrogram.hpp"
#include "GlobalNamespace/SpectrogramRow.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "GlobalNamespace/StandardLevelFailedController.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/TrackLaneRingsPositionStepEffectSpawner.hpp"
#include "GlobalNamespace/TrackLaneRing.hpp"
#include "GlobalNamespace/TrackLaneRingsRotationEffect.hpp"
#include "GlobalNamespace/TrackLaneRingsRotationEffectSpawner.hpp"
#include "GlobalNamespace/TransformSpectrogram.hpp"
#include "System/Nullable_1.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/Application.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Transform.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "custom-types/shared/register.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

namespace {
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
        PaperLogger.info("Disabled {} rotating or moving environment components", disabled);
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
        PaperLogger.info("Hidden {} track-lane ring objects for video gameplay", hidden);
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
        PaperLogger.info("Hidden {} Big Mirror side-bar structures for video gameplay", hidden);
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
        PaperLogger.info("Hidden {} spectrogram-bar objects for video gameplay", hidden);
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
        PaperLogger.info(
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

        PaperLogger.info(
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

        PaperLogger.info(
            "Blackened and disabled {} lights in selected legacy channel groups",
            blackenedAndDisabled);
    }

    bool ShouldSuppressEnvironmentMotion()
    {
        return BigScreen::Settings::Instance().ModEnabled() &&
               BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
               !BigScreen::PlaybackSession::Instance().MapperPresentationActive() &&
               BigScreen::Settings::Instance().DisableEnvironmentMotion();
    }

    bool ShouldSuppressMapLighting()
    {
        return BigScreen::Settings::Instance().ModEnabled() &&
               BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
               !BigScreen::PlaybackSession::Instance().MapperPresentationActive() &&
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
        PauseMenuManager_Start,
        &GlobalNamespace::PauseMenuManager::Start,
        void,
        GlobalNamespace::PauseMenuManager* self)
    {
        PauseMenuManager_Start(self);
        BigScreen::ErrorManager::Instance().Guard(
            "creating the pause-menu screen layout selector",
            [&]() {
                BigScreen::PauseMenuLayoutSelector::Instance().CreateUi(self);
            });
    }

    MAKE_HOOK_MATCH(
        PauseMenuManager_ShowMenu,
        &GlobalNamespace::PauseMenuManager::ShowMenu,
        void,
        GlobalNamespace::PauseMenuManager* self)
    {
        PauseMenuManager_ShowMenu(self);
        BigScreen::ErrorManager::Instance().Guard(
            "showing the pause-menu screen layout selector",
            []() {
                BigScreen::PauseMenuLayoutSelector::Instance().MenuShown();
            });
    }

    MAKE_HOOK_MATCH(
        PauseMenuManager_OnDestroy,
        &GlobalNamespace::PauseMenuManager::OnDestroy,
        void,
        GlobalNamespace::PauseMenuManager* self)
    {
        BigScreen::PauseMenuLayoutSelector::Instance().ForgetUi();
        PauseMenuManager_OnDestroy(self);
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
            BigScreen::SelectionVideoToggle::Instance().SongSelectionShown();
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
        if(BigScreen::SelectionVideoToggle::Instance().IsEnabledForSelectedLevel())
        {
            playback.Prepare(self->get_beatmapLevel());
            playback.PrewarmGameplay();
        }
        else
            playback.Prepare(nullptr);
        StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo(
            self,
            overrideEnvironmentSettings,
            environmentsListModel);

        const auto& settings = BigScreen::Settings::Instance();
        if(playback.MapperPresentationActive())
        {
            // A mapper-requested environment is part of the Cinema scene
            // contract. If none is supplied, retain the map's normal Chroma-
            // aware environment instead of forcing Big Mirror.
            const auto& mapperEnvironment = playback.RequestedEnvironment();
            if(!mapperEnvironment || !environmentsListModel)
            {
                PaperLogger.info(
                    "Allow Chroma Override retained the map's intended environment");
                return;
            }
            auto environment = environmentsListModel->GetEnvironmentInfoBySerializedNameSafe(
                StringW(*mapperEnvironment));
            if(environment &&
               std::string(environment->get_serializedName()) == *mapperEnvironment)
            {
                self->set_environmentInfo(environment);
                self->set_usingOverrideEnvironment(true);
                PaperLogger.info(
                    "Allow Chroma Override loaded mapper environment '{}'",
                    *mapperEnvironment);
            }
            else
            {
                PaperLogger.warn(
                    "Mapper environment '{}' is unavailable; keeping the map environment",
                    *mapperEnvironment);
            }
            return;
        }

        if(!settings.GlassDesertOverrideEnabled() &&
           !settings.EnvironmentOverrideEnabled())
        {
            if(playback.HasPreparedVideo())
                PaperLogger.info("Environment overrides disabled; using the map's intended environment");
            return;
        }

        if(!playback.HasPreparedVideo() || !environmentsListModel)
            return;

        // Glass Desert is an explicit experiment and takes precedence while
        // enabled. Turning it back off restores the independent Big Mirror
        // preference without losing that user's normal override choice.
        constexpr auto bigMirrorName = "BigMirrorEnvironment";
        constexpr auto glassDesertName = "GlassDesertEnvironment";
        const auto* requestedName = settings.GlassDesertOverrideEnabled()
            ? glassDesertName
            : bigMirrorName;
        auto environment = environmentsListModel->GetEnvironmentInfoBySerializedNameSafe(
            StringW(requestedName));
        if(environment && std::string(environment->get_serializedName()) == requestedName)
        {
            self->set_environmentInfo(environment);
            self->set_usingOverrideEnvironment(true);
            PaperLogger.info(
                "Forced {} environment for video gameplay",
                settings.GlassDesertOverrideEnabled() ? "Glass Desert" : "Big Mirror");
        }
        else
        {
            PaperLogger.error(
                "Requested {} environment is unavailable; keeping the map environment",
                requestedName);
        }
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
        if(BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
           !BigScreen::PlaybackSession::Instance().MapperPresentationActive() &&
           !BigScreen::Settings::Instance().MapLightShowEnabled() &&
           playerSpecificSettings)
        {
            // CopyWith preserves every player preference while replacing only
            // the two difficulty-dependent environment filters. Passing a copy
            // avoids mutating Beat Saber's saved setting or affecting non-video
            // songs after this scene transition.
            using OptionalEffects =
                System::Nullable_1<GlobalNamespace::EnvironmentEffectsFilterPreset>;
            const OptionalEffects noEffects{
                true,
                GlobalNamespace::EnvironmentEffectsFilterPreset::NoEffects
            };
            effectiveSettings = playerSpecificSettings->CopyWith(
                {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
                {}, {}, {}, {}, {}, {}, {}, noEffects, noEffects, {});
            PaperLogger.info("Map light show disabled for this video level");
        }

        StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes(
            self,
            effectiveSettings,
            backButtonText,
            startPaused);
    }

    MAKE_HOOK_MATCH(
        AudioTimeSyncController_StartSong,
        &GlobalNamespace::AudioTimeSyncController::StartSong,
        void,
        GlobalNamespace::AudioTimeSyncController* self,
        float startTimeOffset)
    {
        BigScreen::Settings::Instance().Flush();
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
            const bool mapperControlsPresentation =
                playback.MapperPresentationActive();
            if(playback.HasPreparedVideo() && !mapperControlsPresentation &&
               settings.DisableEnvironmentMotion())
                DisableEnvironmentMotion();
            if(playback.HasPreparedVideo() && !mapperControlsPresentation &&
               !settings.MapLightShowEnabled())
                DisableEnvironmentLighting();
            else if(playback.HasPreparedVideo() && !mapperControlsPresentation)
                DisableSelectedLightingChannels();
            if(playback.HasPreparedVideo() && !mapperControlsPresentation &&
               settings.HideTrackRings())
                HideTrackLaneRings();
            if(playback.HasPreparedVideo() && !mapperControlsPresentation &&
               settings.HideSideBars())
                HideSideBars();
            if(playback.HasPreparedVideo() && !mapperControlsPresentation &&
               settings.HideSpectrogramBars())
                HideSpectrogramBars();
            if(playback.HasPreparedVideo() && !mapperControlsPresentation &&
               settings.HideSideLaserLights())
                HideSideLaserGeometry();
            BigScreen::PlaybackSession::Instance().Start(BigScreen::PlaybackContext::Gameplay);
        });
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
            BigScreen::ErrorManager::Instance().Guard(
                "cancelling screen positioning after focus loss", []()
                {
                    BigScreen::ScreenPreview::Instance()
                        .CancelUndockedEditing();
                });
            BigScreen::Settings::Instance().Flush();
        }
    }

    MAKE_HOOK_MATCH(
        SongPreviewPlayer_Update,
        &GlobalNamespace::SongPreviewPlayer::Update,
        void,
        GlobalNamespace::SongPreviewPlayer* self)
    {
        SongPreviewPlayer_Update(self);

        BigScreen::Settings::Instance().TickPersistence();
        // Error dialogs remain available after the circuit breaker disables
        // the mod; all other Big Screen menu work stays behind the master flag.
        BigScreen::ErrorManager::Instance().TickMainThread();
        if(!BigScreen::Settings::Instance().ModEnabled())
            return;

        BigScreen::ErrorManager::Instance().Guard("updating menu video UI", [&]() {
            BigScreen::SelectionVideoToggle::Instance().TickDownloadUi();
            BigScreen::VideoLibraryMenu::Instance().Tick(self);
            BigScreen::StorageMaintenanceMenu::Instance().Tick();
            BigScreen::ScreenPreview::Instance().TickUndockedEditor();
        });
        static int downloaderUiFrame = 0;
        if(++downloaderUiFrame >= 30)
        {
            downloaderUiFrame = 0;
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
        if(!controllers || activeChannel < 0 || activeChannel >= controllers.size())
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
            BigScreen::PlaybackSession::Instance().Tick(self->get_songTime());
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
            BigScreen::PlaybackSession::Instance().Stop();
        });
        BigScreen::ErrorManager::Instance().SetGameplayActive(false);
        StandardLevelScenesTransitionSetupDataSO_Finish(self, levelCompletionResults);
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
        const auto& summary =
            BigScreen::PlaybackSession::Instance().LastDiagnosticsSummary();
        if(summary.empty())
            return;
        auto* text = BSML::Lite::CreateText(
            self,
            "<b>Big Screen Performance</b>\n" + summary,
            2.7f,
            UnityEngine::Vector2{0.0f, -36.0f},
            UnityEngine::Vector2{92.0f, 12.0f});
        if(text)
            text->set_alignment(TMPro::TextAlignmentOptions::Center);
    }

    MAKE_HOOK_MATCH(
        StandardLevelFailedController_HandleLevelFailed,
        &GlobalNamespace::StandardLevelFailedController::HandleLevelFailed,
        void,
        GlobalNamespace::StandardLevelFailedController* self)
    {
        StandardLevelFailedController_HandleLevelFailed(self);
        BigScreen::ErrorManager::Instance().Guard("showing failed-map performance information", []() {
            BigScreen::PlaybackSession::Instance().FinalizeDiagnosticsDisplay();
        });
    }
}

MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept
{
    *info = modInfo.to_c();
    Paper::Logger::RegisterFileContextId(PaperLogger.tag);
    BigScreen::ErrorManager::Instance().Guard("loading settings", []() {
        BigScreen::Settings::Instance().Load();
    });
    PaperLogger.info("Big Screen {} initialized", VERSION);
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
            PaperLogger.error("Downloader unavailable: {}", downloaderError);
        else
            BigScreen::DownloadManager::Instance().StartScheduledUpdaterCheck(
                BigScreen::Settings::Instance().NightlyDownloaderUpdates());
    });

    // Hooks stay on public Beat Saber lifecycle and clock APIs: selection view
    // visibility owns menu preview lifetime, scene transition owns gameplay
    // setup, and Beat Saber's audio clocks remain authoritative for sync.
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo);
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes);
    INSTALL_HOOK(PaperLogger, StandardLevelDetailView_Awake);
    INSTALL_HOOK(PaperLogger, StandardLevelDetailView_OnEnable);
    INSTALL_HOOK(PaperLogger, StandardLevelDetailView_OnDisable);
    INSTALL_HOOK(PaperLogger, StandardLevelDetailView_OnDestroy);
    INSTALL_HOOK(PaperLogger, PauseMenuManager_Start);
    INSTALL_HOOK(PaperLogger, PauseMenuManager_ShowMenu);
    INSTALL_HOOK(PaperLogger, PauseMenuManager_OnDestroy);
    INSTALL_HOOK(PaperLogger, BeatmapCallbacksController_TriggerBeatmapEvent);
    INSTALL_HOOK(PaperLogger, TrackLaneRingsRotationEffectSpawner_HandleBeatmapEvent);
    INSTALL_HOOK(PaperLogger, TrackLaneRingsPositionStepEffectSpawner_HandleBeatmapEvent);
    INSTALL_HOOK(PaperLogger, AudioTimeSyncController_StartSong);
    INSTALL_HOOK(PaperLogger, AudioTimeSyncController_Update);
    INSTALL_HOOK(PaperLogger, SongPreviewPlayer_Update);
    INSTALL_HOOK(PaperLogger, Application_InvokeFocusChanged);
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_Finish);
    INSTALL_HOOK(PaperLogger, ResultsViewController_DidActivate);
    INSTALL_HOOK(PaperLogger, StandardLevelFailedController_HandleLevelFailed);

    // SongCore publishes selections after its custom-level details are ready,
    // including WIP songs. A plain native callback keeps this path independent
    // of Beat Saber's private view-controller field layout.
    SongCore::API::LevelSelect::GetLevelWasSelectedEvent().addCallback(HandleLevelWasSelected);
    BigScreen::SettingsMenu::Instance().Register();
    PaperLogger.info("Big Screen hooks installed");
}
