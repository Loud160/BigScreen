// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/MenuGameplayEnvironmentHost.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/MenuEnvironmentVisibility.hpp"
#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/MenuPlacementGuide.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BeatmapBasicData.hpp"
#include "GlobalNamespace/BeatmapCallbacksController.hpp"
#include "GlobalNamespace/BeatmapCallbacksUpdater.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BloomPrePassLight.hpp"
#include "GlobalNamespace/CoreGameHUDController.hpp"
#include "GlobalNamespace/DirectionalLight.hpp"
#include "GlobalNamespace/EnvironmentInfoSO.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/FadeInOutController.hpp"
#include "GlobalNamespace/GameScenesManager.hpp"
#include "GlobalNamespace/LightWithIdMonoBehaviour.hpp"
#include "GlobalNamespace/LightsAnimator.hpp"
#include "GlobalNamespace/LineLight.hpp"
#include "GlobalNamespace/MenuEnvironmentManager.hpp"
#include "GlobalNamespace/MenuTransitionsHelper.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/PointLight.hpp"
#include "GlobalNamespace/RecordingToolManager.hpp"
#include "GlobalNamespace/Rotate.hpp"
#include "GlobalNamespace/SceneInfo.hpp"
#include "GlobalNamespace/SceneType.hpp"
#include "GlobalNamespace/SimpleLevelStarter.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/Spectrogram.hpp"
#include "GlobalNamespace/SpectrogramRow.hpp"
#include "GlobalNamespace/TrackLaneRing.hpp"
#include "GlobalNamespace/TrackLaneRingsRotationEffect.hpp"
#include "GlobalNamespace/TransformSpectrogram.hpp"
#include "GlobalNamespace/UIKeyboardManager.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "GlobalNamespace/VRRenderingParamsSetup.hpp"
#include "System/Action_1.hpp"
#include "System/Collections/Generic/HashSet_1.hpp"
#include "System/Nullable_1.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Behaviour.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/EventSystems/EventSystem.hpp"
#include "VRUIControls/VRInputModule.hpp"
#include "VRUIControls/VRPointer.hpp"
#include "Zenject/DiContainer.hpp"
#include "custom-types/shared/delegate.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        template<class T>
        bool Alive(T* object)
        {
            return UnityW<T>::isAlive(object);
        }

        template<class T>
        bool Alive(UnityW<T> object)
        {
            return object.isAlive();
        }

        GlobalNamespace::SimpleLevelStarter* FindLevelStarter()
        {
            auto starters = UnityEngine::Resources::FindObjectsOfTypeAll<
                GlobalNamespace::SimpleLevelStarter*>();
            for(int index = static_cast<int>(starters.size()) - 1;
                index >= 0;
                --index)
                if(Alive(starters[index]))
                    return starters[index];
            return nullptr;
        }

        UnityEngine::Transform* FindRecursive(
            UnityEngine::Transform* parent,
            std::string_view name)
        {
            if(!Alive(parent))
                return nullptr;
            if(std::string(parent->get_name()) == name)
                return parent;
            for(int index = 0; index < parent->get_childCount(); ++index)
                if(auto* found = FindRecursive(parent->GetChild(index), name))
                    return found;
            return nullptr;
        }

        void DisableAllButDirectChildren(
            UnityEngine::Transform* parent,
            const std::unordered_set<std::string>& allowed)
        {
            if(!Alive(parent))
                return;
            for(int index = 0; index < parent->get_childCount(); ++index)
            {
                auto child = parent->GetChild(index);
                if(!Alive(child))
                    continue;
                if(!allowed.contains(std::string(child->get_name())))
                    child->get_gameObject()->SetActive(false);
            }
        }

        UnityEngine::GameObject* ResolveEnvironmentAnchorInScene(
            std::string_view sceneName)
        {
            if(sceneName.empty())
                return nullptr;

            auto scene = UnityEngine::SceneManagement::SceneManager::
                GetSceneByName(StringW(sceneName));
            if(!scene.IsValid() || !scene.get_isLoaded())
                return nullptr;

            auto roots = scene.GetRootGameObjects();
            for(auto root : roots)
            {
                if(!Alive(root))
                    continue;
                if(auto* environment = FindRecursive(
                       root->get_transform(), "Environment"))
                    return environment->get_gameObject();
            }

            // Environment scenes are not required to expose one GameObject
            // named "Environment". Big Mirror, for example, can publish
            // several top-level roots. The scene itself is authoritative, so
            // retain any live root only as a scene/lifetime anchor. Visual
            // presentation is controlled component-by-component below; never
            // deactivate an arbitrary top-level root that may also own a
            // SceneContext or a third-party gameplay callback.
            for(auto root : roots)
            {
                if(!Alive(root))
                    continue;
                BigScreenLogger.warn(
                    "Hosted environment scene '{}' has no Environment child; using '{}' as an anchor across {} scene roots",
                    sceneName,
                    std::string(root->get_name()),
                    roots.size());
                return root.ptr();
            }
            return nullptr;
        }

        std::optional<GlobalNamespace::BeatmapKey> RepresentativeKey(
            GlobalNamespace::BeatmapLevel* level)
        {
            if(!level)
                return std::nullopt;

            // GetBeatmapKeys populates BeatmapLevel's stable array cache. Use
            // the hardest Standard chart when one exists because authored
            // environment/lightshow content is most commonly complete there;
            // otherwise use the last available characteristic/difficulty.
            level->GetBeatmapKeys();
            const auto keys = level->__cordl_internal_get__beatmapKeysCache();
            if(keys.size() == 0)
                return std::nullopt;

            std::optional<GlobalNamespace::BeatmapKey> fallback;
            std::optional<GlobalNamespace::BeatmapKey> standard;
            for(auto key : keys)
            {
                if(!key.IsValid())
                    continue;
                fallback = key;
                if(key.beatmapCharacteristic &&
                   std::string(key.beatmapCharacteristic->get_serializedName()) ==
                       "Standard")
                    standard = key;
            }
            return standard ? standard : fallback;
        }

        std::string EnvironmentForKey(
            GlobalNamespace::BeatmapLevel* level,
            const GlobalNamespace::BeatmapKey& key)
        {
            auto* data = level && key.beatmapCharacteristic
                ? level->GetDifficultyBeatmapData(
                    key.beatmapCharacteristic, key.difficulty)
                : nullptr;
            return data
                ? std::string(data->__cordl_internal_get_environmentName()._environmentName)
                : std::string{};
        }

        struct HostedEnvironmentChoice {
            std::string serializedName;
            bool useOverride = false;
        };

        HostedEnvironmentChoice ResolveHostedEnvironment(
            GlobalNamespace::BeatmapLevel* level,
            const GlobalNamespace::BeatmapKey& key)
        {
            HostedEnvironmentChoice choice{EnvironmentForKey(level, key)};
            const auto& settings = Settings::Instance();

            // Respect Mapper Settings and Allow Chroma Override are screen
            // presentation choices. They must never take ownership of the
            // Environment-tab controls or the menu host's environment choice.
            // Big Mirror/Glass Desert are independent environment settings.
            if(settings.EnvironmentOverrideEnabled() ||
               settings.GlassDesertOverrideEnabled())
            {
                choice.serializedName = settings.GlassDesertOverrideEnabled()
                    ? "GlassDesertEnvironment"
                    : "BigMirrorEnvironment";
                choice.useOverride = true;
            }
            return choice;
        }

        struct BehaviourState {
            UnityW<UnityEngine::Behaviour> behaviour = nullptr;
            bool enabled = false;
        };

        struct GameObjectState {
            UnityW<UnityEngine::GameObject> object = nullptr;
            bool active = false;
        };

        struct RendererState {
            UnityW<UnityEngine::Renderer> renderer = nullptr;
            bool enabled = false;
        };

        struct PreviewSceneControls {
            int environmentSceneHandle = 0;
            std::vector<RendererState> renderers;
            std::vector<BehaviourState> behaviours;
            std::vector<UnityW<UnityEngine::Behaviour>> motionBehaviours;
            std::vector<UnityW<UnityEngine::Behaviour>> lightingBehaviours;
            std::vector<UnityW<GlobalNamespace::LightWithIdMonoBehaviour>> lights;
            std::vector<UnityW<GlobalNamespace::LightsAnimator>> lightAnimators;
            std::vector<GameObjectState> objects;
            std::vector<UnityW<UnityEngine::GameObject>> trackRings;
            std::vector<UnityW<UnityEngine::GameObject>> sideBars;
            std::vector<UnityW<UnityEngine::GameObject>> sideLasers;
            std::vector<UnityW<UnityEngine::GameObject>> spectrogramObjects;
            std::vector<UnityW<GlobalNamespace::CoreGameHUDController>> hudControllers;
            std::vector<GameObjectState> detachedHudObjects;
        };

        PreviewSceneControls PreviewControls;

        bool IsInHostedEnvironment(UnityEngine::GameObject* object)
        {
            return Alive(object) && PreviewControls.environmentSceneHandle != 0 &&
                object->get_scene().get_handle() ==
                    PreviewControls.environmentSceneHandle;
        }

        bool IsInHostedEnvironment(UnityEngine::Transform* transform)
        {
            return Alive(transform) &&
                IsInHostedEnvironment(transform->get_gameObject());
        }

        void RememberBehaviour(
            UnityEngine::Behaviour* behaviour,
            std::vector<UnityW<UnityEngine::Behaviour>>& category)
        {
            if(!Alive(behaviour) || !behaviour->get_enabled() ||
               !IsInHostedEnvironment(behaviour->get_transform()))
                return;
            const auto duplicate = std::ranges::any_of(
                PreviewControls.behaviours,
                [behaviour](const BehaviourState& state)
                {
                    return state.behaviour.ptr() == behaviour;
                });
            if(!duplicate)
                PreviewControls.behaviours.push_back({behaviour, true});
            category.emplace_back(behaviour);
        }

        void RememberObject(
            UnityEngine::GameObject* object,
            std::vector<UnityW<UnityEngine::GameObject>>& category)
        {
            if(!Alive(object) || !object->get_activeSelf() ||
               !IsInHostedEnvironment(object))
                return;
            const auto duplicate = std::ranges::any_of(
                PreviewControls.objects,
                [object](const GameObjectState& state)
                {
                    return state.object.ptr() == object;
                });
            if(!duplicate)
                PreviewControls.objects.push_back({object, true});
            if(std::ranges::none_of(
                   category,
                   [object](const auto& remembered)
                   {
                       return remembered.ptr() == object;
                   }))
                category.emplace_back(object);
        }

        template<class T>
        void RememberActiveBehaviours(
            std::vector<UnityW<UnityEngine::Behaviour>>& category)
        {
            for(auto* component : UnityEngine::Object::FindObjectsOfType<T*>(false))
                RememberBehaviour(component, category);
        }

        void SetObjectsActive(
            const std::vector<UnityW<UnityEngine::GameObject>>& objects,
            bool active)
        {
            for(auto object : objects)
                if(Alive(object))
                    object->SetActive(active);
        }

        void ClearPreviewSceneControls()
        {
            PreviewControls = {};
        }

        void CapturePreviewSceneControls(
            UnityEngine::GameObject* environment)
        {
            ClearPreviewSceneControls();
            if(!Alive(environment))
            {
                BigScreenLogger.warn(
                    "Hosted map environment did not expose an Environment root; live environment controls are unavailable");
                return;
            }
            PreviewControls.environmentSceneHandle =
                environment->get_scene().get_handle();

            // Renderer visibility is the safe presentation boundary for a
            // retained gameplay environment. Disabling a scene root also
            // disables Zenject and mod-owned MonoBehaviours; re-enabling that
            // hierarchy after a fallback previously left Replay callbacks
            // running against retired gameplay services. Keep the complete
            // host alive and hide only the renderers when another menu mode is
            // selected.
            for(auto* renderer : UnityEngine::Object::FindObjectsOfType<
                    UnityEngine::Renderer*>(false))
            {
                if(!Alive(renderer) || !renderer->get_enabled() ||
                   !IsInHostedEnvironment(renderer->get_transform()))
                    continue;
                PreviewControls.renderers.push_back({renderer, true});
            }

            RememberActiveBehaviours<GlobalNamespace::TrackLaneRingsRotationEffect>(
                PreviewControls.motionBehaviours);
            RememberActiveBehaviours<GlobalNamespace::Rotate>(
                PreviewControls.motionBehaviours);

            for(auto* light : UnityEngine::Object::FindObjectsOfType<
                    GlobalNamespace::LightWithIdMonoBehaviour*>(false))
            {
                RememberBehaviour(light, PreviewControls.lightingBehaviours);
                if(Alive(light) && light->get_enabled() &&
                   IsInHostedEnvironment(light->get_transform()))
                    PreviewControls.lights.emplace_back(light);
            }
            RememberActiveBehaviours<GlobalNamespace::BloomPrePassLight>(
                PreviewControls.lightingBehaviours);
            RememberActiveBehaviours<GlobalNamespace::LineLight>(
                PreviewControls.lightingBehaviours);
            RememberActiveBehaviours<GlobalNamespace::PointLight>(
                PreviewControls.lightingBehaviours);
            RememberActiveBehaviours<GlobalNamespace::DirectionalLight>(
                PreviewControls.lightingBehaviours);
            RememberActiveBehaviours<GlobalNamespace::Spectrogram>(
                PreviewControls.lightingBehaviours);
            RememberActiveBehaviours<GlobalNamespace::TransformSpectrogram>(
                PreviewControls.lightingBehaviours);
            for(auto* animator : UnityEngine::Object::FindObjectsOfType<
                    GlobalNamespace::LightsAnimator*>(false))
            {
                RememberBehaviour(animator, PreviewControls.lightingBehaviours);
                if(Alive(animator) && animator->get_enabled() &&
                   IsInHostedEnvironment(animator->get_transform()))
                    PreviewControls.lightAnimators.emplace_back(animator);
            }

            for(auto* ring : UnityEngine::Object::FindObjectsOfType<
                    GlobalNamespace::TrackLaneRing*>(false))
                if(Alive(ring))
                    RememberObject(
                        ring->get_gameObject(), PreviewControls.trackRings);

            for(auto* transform : UnityEngine::Object::FindObjectsOfType<
                    UnityEngine::Transform*>(false))
            {
                if(!Alive(transform))
                    continue;
                auto object = transform->get_gameObject();
                if(!Alive(object) || !object->get_activeSelf())
                    continue;
                const std::string name(object->get_name());
                if(name == "NearBuildingLeft" || name == "NearBuildingRight")
                    RememberObject(object.ptr(), PreviewControls.sideBars);
                const bool directionalRail =
                    name == "NeonTubeDirectionalL" ||
                    name == "NeonTubeDirectionalR" ||
                    name == "NeonTubeDirectionalFL" ||
                    name == "NeonTubeDirectionalFR";
                if(directionalRail ||
                   name.rfind("RotatingLasersPair", 0) == 0 ||
                   name.rfind("DoubleColorLaser", 0) == 0)
                    RememberObject(object.ptr(), PreviewControls.sideLasers);
            }

            for(auto* spectrogram : UnityEngine::Object::FindObjectsOfType<
                    GlobalNamespace::Spectrogram*>(false))
            {
                if(!Alive(spectrogram) ||
                   !IsInHostedEnvironment(spectrogram->get_transform()))
                    continue;
                RememberObject(
                    spectrogram->get_gameObject(),
                    PreviewControls.spectrogramObjects);
                for(auto renderer : spectrogram->__cordl_internal_get__meshRenderers())
                    if(Alive(renderer))
                        RememberObject(
                            renderer->get_gameObject(),
                            PreviewControls.spectrogramObjects);
            }
            for(auto* row : UnityEngine::Object::FindObjectsOfType<
                    GlobalNamespace::SpectrogramRow*>(false))
            {
                if(!Alive(row) || !IsInHostedEnvironment(row->get_transform()))
                    continue;
                RememberObject(
                    row->get_gameObject(), PreviewControls.spectrogramObjects);
                for(auto renderer : row->__cordl_internal_get__meshRenderers())
                    if(Alive(renderer))
                        RememberObject(
                            renderer->get_gameObject(),
                            PreviewControls.spectrogramObjects);
            }
            for(auto* spectrogram : UnityEngine::Object::FindObjectsOfType<
                    GlobalNamespace::TransformSpectrogram*>(false))
            {
                if(!Alive(spectrogram) ||
                   !IsInHostedEnvironment(spectrogram->get_transform()))
                    continue;
                RememberObject(
                    spectrogram->get_gameObject(),
                    PreviewControls.spectrogramObjects);
                for(auto transform : spectrogram->__cordl_internal_get__transforms())
                    if(Alive(transform))
                        RememberObject(
                            transform->get_gameObject(),
                            PreviewControls.spectrogramObjects);
            }

            for(auto* controller : UnityEngine::Resources::FindObjectsOfTypeAll<
                    GlobalNamespace::CoreGameHUDController*>())
                if(Alive(controller))
                    PreviewControls.hudControllers.emplace_back(controller);
            for(auto* transform : UnityEngine::Resources::FindObjectsOfTypeAll<
                    UnityEngine::Transform*>())
            {
                if(!Alive(transform))
                    continue;
                auto object = transform->get_gameObject();
                if(!Alive(object))
                    continue;
                const std::string name(object->get_name());
                if(name == "QounterGroup" || name == "QountersRotationalAnchor")
                    PreviewControls.detachedHudObjects.push_back(
                        {object, object->get_activeSelf()});
            }

            BigScreenLogger.info(
                "Captured hosted environment scene {} controls: {} behaviours, {} objects, {} HUD controllers",
                PreviewControls.environmentSceneHandle,
                PreviewControls.behaviours.size(),
                PreviewControls.objects.size(),
                PreviewControls.hudControllers.size());
        }

        void RestorePreviewSceneControls()
        {
            for(auto& state : PreviewControls.renderers)
                if(Alive(state.renderer))
                    state.renderer->set_enabled(state.enabled);
            for(auto& state : PreviewControls.objects)
                if(Alive(state.object))
                    state.object->SetActive(state.active);
            for(auto& state : PreviewControls.behaviours)
                if(Alive(state.behaviour))
                    state.behaviour->set_enabled(state.enabled);
        }

        VRUIControls::VRInputModule* CurrentInputModule()
        {
            auto eventSystem = UnityEngine::EventSystems::EventSystem::get_current();
            if(!Alive(eventSystem))
                return nullptr;
            auto input = eventSystem->get_currentInputModule();
            if(!Alive(input))
                return nullptr;
            return input.try_cast<VRUIControls::VRInputModule>()
                .value_or(nullptr);
        }

        VRUIControls::VRInputModule* FindGameplayInputModule(
            VRUIControls::VRInputModule* menuInput)
        {
            // The current EventSystem is authoritative after PushScenes. Other
            // mods can retain additional inactive VRInputModules. Never fall
            // back to an arbitrary module: that can disable the real menu
            // pointer and activate an unrelated mod's input hierarchy.
            if(auto* current = CurrentInputModule();
               Alive(current) && current != menuInput)
                return current;
            return nullptr;
        }

        void SetInputObjectsActive(
            VRUIControls::VRInputModule* input,
            bool active)
        {
            if(!Alive(input))
                return;
            input->get_gameObject()->SetActive(active);
            if(auto eventSystem = input->get_eventSystem(); Alive(eventSystem))
                eventSystem->get_gameObject()->SetActive(active);
            auto pointer = input->__cordl_internal_get__vrPointer();
            if(!Alive(pointer))
                return;
            for(auto controller : {
                    pointer->__cordl_internal_get__leftVRController(),
                    pointer->__cordl_internal_get__rightVRController()})
                if(Alive(controller))
                    controller->get_gameObject()->SetActive(active);
        }
    }

    MenuGameplayEnvironmentHost& MenuGameplayEnvironmentHost::Instance()
    {
        static MenuGameplayEnvironmentHost host;
        return host;
    }

    bool MenuGameplayEnvironmentHost::MapModeRequested() const
    {
        if(!menuActive_ || suspendedForFocusLoss_ ||
           !Settings::Instance().ModEnabled())
            return false;
        const auto mode = Settings::Instance().MenuEnvironment();
        return mode == MenuEnvironmentMode::MapEnvironment ||
            mode == MenuEnvironmentMode::MapEnvironmentAndLightshow;
    }

    bool MenuGameplayEnvironmentHost::LightshowRequested() const
    {
        return MapModeRequested() && Settings::Instance().MenuEnvironment() ==
            MenuEnvironmentMode::MapEnvironmentAndLightshow &&
            Settings::Instance().MapLightShowEnabled() &&
            // A forced environment may remain resident while the player
            // browses many maps. Never drive its callbacks with the song clock
            // after selection has moved away from the beatmap data that built
            // this controller; doing so would show stale lighting and can call
            // map-owned handlers with the wrong data lifetime.
            !loadedLevelId_.empty() && loadedLevelId_ == selectedLevelId_;
    }

    void MenuGameplayEnvironmentHost::ActivateMenu()
    {
        menuActive_ = true;
        suspendedForFocusLoss_ = false;
        if(state_ == State::Failed)
            state_ = State::Idle;
        ApplyMode();
    }

    void MenuGameplayEnvironmentHost::DeactivateMenu() noexcept
    {
        menuActive_ = false;
        suspendedForFocusLoss_ = false;
        pendingLevel_ = nullptr;
        selectedLevel_ = nullptr;
        selectedLevelId_.clear();
        if(state_ == State::Ready || state_ == State::Loading)
            BeginUnload(false);
        else
            RestoreMenuScene();
    }

    void MenuGameplayEnvironmentHost::SuspendForFocusLoss() noexcept
    {
        if(!menuActive_ || suspendedForFocusLoss_)
            return;
        suspendedForFocusLoss_ = true;
        pendingLevel_ = nullptr;
        if(state_ == State::Ready || state_ == State::Loading)
            BeginUnload(false);
        else
            RestoreMenuScene();
    }

    void MenuGameplayEnvironmentHost::ResumeAfterFocusGain()
    {
        if(!menuActive_ || !suspendedForFocusLoss_)
            return;
        suspendedForFocusLoss_ = false;
        if(state_ == State::Loading)
            unloadRequested_ = false;
        if(state_ == State::Unloading)
            pendingLevel_ = selectedLevel_;
        else
            ApplyMode();
    }

    void MenuGameplayEnvironmentHost::ApplyMode()
    {
        if(!menuActive_ || !Settings::Instance().ModEnabled())
        {
            BeginUnload(true);
            MenuEnvironmentVisibility::Instance().Restore();
            MenuPlacementGuide::Instance().Apply();
            return;
        }

        if(!MapModeRequested())
        {
            // Keep a completed gameplay host resident for this Big Screen menu
            // session and change only which environment is visible. Repeated
            // Pop/Push cycles retire and recreate AudioTimeSyncController and
            // GameCore objects; Replay and other gameplay mods can still own
            // callbacks to the retiring set and have aborted during the next
            // Push. The one host is fully removed when Big Screen closes,
            // loses focus, is disabled, or genuinely changes environment.
            if(state_ == State::Loading)
                unloadRequested_ = false;
            ReconcileEnvironmentPresentation();
            MenuPlacementGuide::Instance().Apply();
            MenuEnvironmentVisibility::Instance().Apply();
            return;
        }

        // Map modes intentionally hide the stock scenery while the complete
        // host is being prepared. The retained selected environment replaces
        // it once the asynchronous standard transition finishes.
        MenuPlacementGuide::Instance().Apply();
        MenuEnvironmentVisibility::Instance().Apply();
        if(menuEnvironment_)
            menuEnvironment_->ShowEnvironmentType(
                GlobalNamespace::MenuEnvironmentManager::MenuEnvironmentType::None);

        // A mode change or short focus loss may have requested teardown while
        // PushScenes was still completing. Returning to a map
        // mode before that callback arrives safely keeps the incoming host.
        if(state_ == State::Loading)
            unloadRequested_ = false;

        // ActivateMenu runs before HMUI has attached Big Screen's panels. Do
        // not discover or select a bootstrap level from this reconciliation
        // path: beginning PushScenes reentrantly from DidActivate can remove
        // the controller/pointer hierarchy that is still being constructed.
        // MenuFlowCoordinator explicitly selects the safe bootstrap level once
        // ProvideInitialViewControllers has completed.
        auto* targetLevel = selectedLevel_;
        if(state_ == State::Idle && targetLevel)
        {
            // A forced Big Mirror/Glass Desert is a persistent menu backdrop.
            // It must not wait for the player to open a song editor or press
            // Play, so use a safe installed level only to satisfy Beat Saber's
            // standard scene-construction contract.
            selectedLevel_ = targetLevel;
            selectedLevelId_ = targetLevel->levelID
                ? std::string(targetLevel->levelID) : std::string{};
            BeginLoad(targetLevel);
        }
        else if(state_ == State::Ready)
        {
            if(targetLevel)
            {
                const auto key = RepresentativeKey(targetLevel);
                if(key)
                {
                    const auto desired = ResolveHostedEnvironment(
                        targetLevel, *key);
                    const bool environmentChanged =
                        desired.serializedName.empty() ||
                        desired.serializedName != loadedEnvironmentName_;
                    const bool mapLightshowNeedsItsOwnData =
                        !desired.useOverride &&
                        Settings::Instance().MenuEnvironment() ==
                            MenuEnvironmentMode::MapEnvironmentAndLightshow &&
                        Settings::Instance().MapLightShowEnabled() &&
                        loadedLevelId_ != selectedLevelId_;
                    if(environmentChanged || mapLightshowNeedsItsOwnData)
                    {
                        ReloadLevel(targetLevel);
                        return;
                    }
                }
            }
            // HUD and Environment-tab changes are live mutations of the
            // retained preview. Never replace GameCore for these controls:
            // Replay and similar mods can still have callbacks against a
            // gameplay clock while a scene transition is underway.
            ReconcileEnvironmentPresentation();
            ApplyEnvironmentControls();
        }
    }

    bool MenuGameplayEnvironmentHost::SelectLevel(
        GlobalNamespace::BeatmapLevel* level)
    {
        if(!level || !level->levelID)
            return false;

        selectedLevel_ = level;
        selectedLevelId_ = std::string(level->levelID);
        previewSongTime_ = 0.0;
        appliedSongTime_ = -1.0;

        if(!MapModeRequested())
            return false;

        if(state_ == State::Loading || state_ == State::Unloading)
        {
            pendingLevel_ = level;
            return true;
        }

        const auto key = RepresentativeKey(level);
        if(!key)
        {
            FailCurrentTransition(
                "Selecting a map environment",
                "The selected map did not expose a playable difficulty.");
            return false;
        }
        const auto environmentChoice = ResolveHostedEnvironment(
            level, *key);
        const auto& desiredEnvironment = environmentChoice.serializedName;

        const bool lightshowNeedsSelectedMap =
            Settings::Instance().MenuEnvironment() ==
                MenuEnvironmentMode::MapEnvironmentAndLightshow &&
            Settings::Instance().MapLightShowEnabled() &&
            !environmentChoice.useOverride &&
            loadedLevelId_ != selectedLevelId_;
        const bool environmentChanged = state_ != State::Ready ||
            desiredEnvironment.empty() ||
            desiredEnvironment != loadedEnvironmentName_;
        if(state_ == State::Ready && !environmentChanged &&
           !lightshowNeedsSelectedMap)
        {
            BigScreenLogger.info(
                "Reusing persistent menu map environment '{}' for '{}'",
                loadedEnvironmentName_, selectedLevelId_);
            return false;
        }

        return state_ == State::Ready
            ? ReloadLevel(level)
            : BeginLoad(level);
    }

    bool MenuGameplayEnvironmentHost::ReloadLevel(
        GlobalNamespace::BeatmapLevel* level)
    {
        if(!level || !level->levelID || !MapModeRequested())
            return false;
        if(state_ != State::Ready)
        {
            pendingLevel_ = level;
            return true;
        }

        // Never overlap two complete GameCore scene sets. ReplaceScenes left
        // third-party AudioTimeSyncController hooks (notably Replay) running
        // against the retiring controller and caused a native abort. Suspend
        // the preview, finish PopScenes, and only then PushScenes for the most
        // recently selected map. Rapid selections collapse into pendingLevel_.
        pendingLevel_ = level;
        VideoLibraryMenu::Instance().EnvironmentHostTransitionStarting();
        BigScreenLogger.info(
            "Serializing menu environment switch: unload '{}' before loading '{}'",
            loadedLevelId_, std::string(level->levelID));
        BeginUnload(false);
        return true;
    }

    bool MenuGameplayEnvironmentHost::BeginLoad(
        GlobalNamespace::BeatmapLevel* level)
    {
        if(!level || !level->levelID || !MapModeRequested())
            return false;

        try
        {
            if(auto existing = UnityEngine::GameObject::Find("StandardGameplay");
               existing && state_ == State::Idle)
            {
                throw std::runtime_error(
                    "another mod already owns a gameplay environment in the menu");
            }

            auto* starter = FindLevelStarter();
            auto* helper = starter
                ? starter->__cordl_internal_get__menuTransitionsHelper().ptr()
                : nullptr;
            auto* manager = starter
                ? starter->__cordl_internal_get__gameScenesManager().ptr()
                : nullptr;
            auto* playerDataModel = starter
                ? starter->__cordl_internal_get__playerDataModel().ptr()
                : nullptr;
            auto* playerData = playerDataModel
                ? playerDataModel->get_playerData()
                : nullptr;
            auto key = RepresentativeKey(level);
            if(!starter || !helper || !manager || !playerData || !key)
                throw std::runtime_error(
                    "Beat Saber's standard level transition services were unavailable");

            auto* setup = helper->__cordl_internal_get__standardLevelScenesTransitionSetupData().ptr();
            if(!setup)
                throw std::runtime_error(
                    "Beat Saber's standard level setup object was unavailable");

            if(!menuEnvironment_)
                menuEnvironment_ = UnityEngine::Object::FindObjectOfType<
                    GlobalNamespace::MenuEnvironmentManager*>(true);
            if(!keyboardManager_)
                keyboardManager_ = UnityEngine::Object::FindObjectOfType<
                    GlobalNamespace::UIKeyboardManager*>(true);
            if(!Alive(menuInput_))
            {
                menuInput_ = CurrentInputModule();
                if(!Alive(menuInput_))
                    menuInput_ = UnityEngine::Object::FindObjectOfType<
                        VRUIControls::VRInputModule*>(true);
            }
            if(!menuEnvironmentCore_)
                menuEnvironmentCore_ =
                    UnityEngine::GameObject::Find("MenuEnvironmentCore");

            gameScenesManager_ = manager;
            transitionSetup_ = setup;
            loadingLevelId_ = std::string(level->levelID);
            const auto environmentChoice = ResolveHostedEnvironment(
                level, *key);
            loadingEnvironmentName_ = environmentChoice.serializedName;
            loadingEnvironmentUsesOverride_ =
                environmentChoice.useOverride;

            manager->__cordl_internal_get__neverUnloadScenes()->Add("MenuCore");
            if(menuEnvironment_)
                menuEnvironment_->ShowEnvironmentType(
                    GlobalNamespace::MenuEnvironmentManager::MenuEnvironmentType::None);

            auto* playerSpecificSettings =
                playerData->get_playerSpecificSettings();
            if(!playerSpecificSettings)
                throw std::runtime_error(
                    "Beat Saber's player-specific settings were unavailable");

            // Always let gameplay HUD installers construct their hierarchy in
            // the private host. The menu setting changes visibility in place
            // after load; rebuilding GameCore just to toggle a canvas caused
            // Replay to abort against the retiring AudioTimeSyncController.
            // The user's actual no-HUD gameplay preference is restored as soon
            // as this asynchronous transition completes.
            RestoreTemporaryPlayerSettings();
            if(playerSpecificSettings->get_noTextsAndHuds())
            {
                temporaryPlayerSettings_ = playerSpecificSettings;
                savedNoTextsAndHuds_ =
                    playerSpecificSettings->get_noTextsAndHuds();
                playerSpecificSettings
                    ->__cordl_internal_set__noTextsAndHuds(false);
            }

            // The existing Big Screen gameplay hooks recognize this exact
            // setup pointer and bypass normal video/gameplay side effects.
            state_ = State::Loading;
            transitionStartedAt_ = std::chrono::steady_clock::now();
            const auto generation = ++generation_;
            setup->Init(
                "BigScreenMenuEnvironment",
                byref(*key),
                level,
                nullptr,
                nullptr,
                false,
                nullptr,
                starter->__cordl_internal_get__gameplayModifiers(),
                playerSpecificSettings,
                nullptr,
                starter->__cordl_internal_get__environmentsListModel(),
                helper->__cordl_internal_get__audioClipAsyncLoader(),
                helper->__cordl_internal_get__beatmapDataLoader(),
                helper->__cordl_internal_get__settingsManager(),
                "",
                helper->__cordl_internal_get__beatmapLevelsModel(),
                helper->__cordl_internal_get__beatmapLevelsEntitlementModel(),
                false,
                false,
                System::Nullable_1<
                    GlobalNamespace::RecordingToolManager_SetupData>());

            if(loadingEnvironmentUsesOverride_)
            {
                auto* environments =
                    starter->__cordl_internal_get__environmentsListModel();
                auto requested = environments
                    ? environments->GetEnvironmentInfoBySerializedNameSafe(
                        StringW(loadingEnvironmentName_))
                    : nullptr;
                if(requested && std::string(requested->get_serializedName()) ==
                       loadingEnvironmentName_)
                {
                    setup->set_targetEnvironmentInfo(requested);
                    setup->set_usingOverrideEnvironment(true);
                }
                else
                {
                    BigScreenLogger.warn(
                        "Requested menu preview environment '{}' was unavailable; retaining the map environment",
                        loadingEnvironmentName_);
                    loadingEnvironmentName_ =
                        EnvironmentForKey(level, *key);
                    loadingEnvironmentUsesOverride_ = false;
                }
            }

            if(auto target = setup->get_targetEnvironmentInfo(); Alive(target))
            {
                auto sceneInfo = target->get_sceneInfo();
                loadingEnvironmentSceneName_ = Alive(sceneInfo)
                    ? std::string(sceneInfo->get_sceneName())
                    : std::string{};
            }
            else
            {
                loadingEnvironmentSceneName_.clear();
            }
            if(loadingEnvironmentSceneName_.empty())
                throw std::runtime_error(
                    "the selected environment did not identify its Unity scene");

            auto* finish = custom_types::MakeDelegate<
                System::Action_1<Zenject::DiContainer*>*>(
                    std::function<void(Zenject::DiContainer*)>{
                        [generation](Zenject::DiContainer*)
                        {
                            Instance().OnScenesReady(generation);
                        }});
            manager->PushScenes(setup, 0.05f, nullptr, finish);

            BigScreenLogger.info(
                "Loading persistent menu map environment '{}' for '{}'",
                loadingEnvironmentName_.empty()
                    ? "map default" : loadingEnvironmentName_,
                loadingLevelId_);
            return true;
        }
        catch(const std::exception& error)
        {
            RestoreTemporaryPlayerSettings();
            FailCurrentTransition("Loading a map environment", error.what());
        }
        catch(...)
        {
            RestoreTemporaryPlayerSettings();
            FailCurrentTransition(
                "Loading a map environment", "Unknown native exception");
        }
        return false;
    }

    void MenuGameplayEnvironmentHost::OnScenesReady(
        std::uint64_t generation) noexcept
    {
        if(generation != generation_)
            return;
        RestoreTemporaryPlayerSettings();
        try
        {
            state_ = State::Ready;
            const auto elapsedMs = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - transitionStartedAt_)
                                       .count();
            loadedLevelId_ = loadingLevelId_;
            loadedEnvironmentName_ = loadingEnvironmentName_;
            loadedEnvironmentSceneName_ = loadingEnvironmentSceneName_;

            // GameScenesManager transitions are serialized. If the menu was
            // closed, lost focus, or changed to a non-map mode while this
            // Push/Replace was running, wait for this completion boundary and
            // then pop it instead of issuing two transitions concurrently.
            if(unloadRequested_ || !menuActive_)
            {
                const bool restore = restoreMenuEnvironmentAfterUnload_;
                unloadRequested_ = false;
                BeginUnload(restore);
                return;
            }
            ConfigureLoadedScene();
            if(!RestoreBigScreenMenuAfterEnvironmentTransition())
                throw std::runtime_error(
                    "Beat Saber could not reattach Big Screen's retained menu flow");

            if(pendingLevel_ && MapModeRequested())
            {
                // SelectLevel already recorded the latest level. Reconcile it
                // against the now-complete host so a forced, unchanged Big
                // Mirror is reused, while a real environment change is first
                // serialized through PopScenes.
                pendingLevel_ = nullptr;
                ApplyMode();
                if(state_ != State::Ready)
                    return;
            }
            else if(pendingLevel_)
                pendingLevel_ = nullptr;

            VideoLibraryMenu::Instance().EnvironmentHostReady(loadedLevelId_);
            BigScreenLogger.info(
                "Persistent menu map environment ready after {} ms: '{}' ({})",
                elapsedMs,
                loadedEnvironmentName_.empty()
                    ? "map default" : loadedEnvironmentName_,
                loadedLevelId_);
        }
        catch(const std::exception& error)
        {
            FailCurrentTransition(
                "Activating a map environment", error.what());
        }
        catch(...)
        {
            FailCurrentTransition(
                "Activating a map environment", "Unknown native exception");
        }
    }

    void MenuGameplayEnvironmentHost::ConfigureLoadedScene()
    {
        if(menuEnvironment_ && Alive(menuEnvironment_->get_transform()))
            menuEnvironment_->get_transform()->get_root()
                ->get_gameObject()->SetActive(true);
        if(menuEnvironmentCore_)
            menuEnvironmentCore_->SetActive(false);

        if(auto gameplay = UnityEngine::GameObject::Find("StandardGameplay"))
        {
            static const std::unordered_set<std::string> allowed{
                "GameplayData",
                "BaseGameEffects",
                "InteropSabersManager",
                "GameplaySabersManager",
                "GameplayDriversManager",
                "LocalPlayerGameCore"
            };
            DisableAllButDirectChildren(gameplay->get_transform(), allowed);
            if(auto local = gameplay->get_transform()->Find(
                   "LocalPlayerGameCore"))
            {
                localPlayer_ = local->get_gameObject();
                localPlayer_->SetActive(false);
                if(auto* camera = FindRecursive(local, "MainCamera"))
                    camera->get_gameObject()->SetActive(false);
            }
        }

        callbacksUpdater_ = UnityEngine::Object::FindObjectOfType<
            GlobalNamespace::BeatmapCallbacksUpdater*>(true);
        callbacksController_ = callbacksUpdater_
            ? callbacksUpdater_->__cordl_internal_get__beatmapCallbacksController()
            : nullptr;
        if(callbacksUpdater_)
            callbacksUpdater_->set_enabled(false);
        if(callbacksController_)
        {
            // Beatmap objects at or after this time are filtered while events
            // remain available. This gives map lighting its normal callbacks
            // without ever letting preview notes or obstacles spawn.
            callbacksController_->__cordl_internal_set__startFilterTime(
                1.0e9f);
            callbacksController_->ManualUpdate(0.0f);
        }

        if(audioController_)
            audioController_->Pause();

        // Gameplay scenes configure XR rendering for a gameplay camera. Big
        // Screen deliberately keeps MenuCore's camera and UI, so reapply the
        // game's menu rendering profile after the hosted environment arrives.
        // Without this step the transition succeeds but the environment can
        // remain absent behind the retained menu.
        if(auto* rendering = UnityEngine::Object::FindObjectOfType<
               GlobalNamespace::VRRenderingParamsSetup*>(true))
        {
            rendering->__cordl_internal_set__sceneType(
                GlobalNamespace::SceneType::Menu);
            rendering->OnEnable();
        }
        else
        {
            BigScreenLogger.warn(
                "Map environment loaded without VRRenderingParamsSetup; retaining current rendering parameters");
        }

        gameplayInput_ = FindGameplayInputModule(menuInput_);
        auto* gameplayInput = Alive(gameplayInput_)
            ? gameplayInput_.ptr() : nullptr;
        auto* menuInput = Alive(menuInput_) ? menuInput_.ptr() : nullptr;
        if(gameplayInput && gameplayInput != menuInput)
        {
            if(keyboardManager_)
            {
                keyboardManager_->__cordl_internal_set__vrInputModule(
                    gameplayInput->i___GlobalNamespace__IVRInputModule());
                keyboardManager_->Start();
            }
            SetInputObjectsActive(menuInput, false);
            SetInputObjectsActive(gameplayInput, true);
        }
        else
        {
            // Keeping the known-good menu input is safer than disabling it in
            // favor of an arbitrary retained module. The menu remains usable
            // even if this game scene does not publish a new active module.
            gameplayInput_ = nullptr;
            SetInputObjectsActive(menuInput, true);
            BigScreenLogger.warn(
                "Map environment did not publish a distinct active VR input module; retaining menu input");
        }

        hostedEnvironmentRoot_ = ResolveEnvironmentAnchorInScene(
            loadedEnvironmentSceneName_);
        if(!Alive(hostedEnvironmentRoot_))
            throw std::runtime_error(
                "the loaded environment scene did not expose a usable root");
        CapturePreviewSceneControls(hostedEnvironmentRoot_.ptr());
        appliedSongTime_ = -1.0;
        ReconcileEnvironmentPresentation();
        ApplyEnvironmentControls();

        BigScreenLogger.info(
            "Resolved hosted environment anchor '{}' in scene '{}'",
            std::string(hostedEnvironmentRoot_->get_name()),
            loadedEnvironmentSceneName_);
    }

    void MenuGameplayEnvironmentHost::ReconcileEnvironmentPresentation() noexcept
    {
        try
        {
            const bool showHosted = state_ == State::Ready &&
                menuActive_ && MapModeRequested() &&
                Alive(hostedEnvironmentRoot_);
            const bool showStock = menuActive_ &&
                Settings::Instance().ModEnabled() &&
                Settings::Instance().MenuEnvironment() ==
                    MenuEnvironmentMode::MenuEnvironment;

            // Keep the hosted scene graph active. A gameplay environment can
            // have several roots, and one of them can own Zenject or mod
            // update components. Renderer/light/HUD visibility provides the
            // same visual result without invalidating those lifetimes.
            for(auto& state : PreviewControls.renderers)
                if(Alive(state.renderer))
                    state.renderer->set_enabled(showHosted && state.enabled);
            for(auto behaviour : PreviewControls.lightingBehaviours)
                if(Alive(behaviour))
                    behaviour->set_enabled(showHosted);
            if(!showHosted)
            {
                for(auto controller : PreviewControls.hudControllers)
                    if(Alive(controller))
                        controller->set_alpha(0.0f);
                for(auto& state : PreviewControls.detachedHudObjects)
                    if(Alive(state.object))
                        state.object->SetActive(false);
            }
            if(Alive(menuEnvironmentCore_))
                menuEnvironmentCore_->SetActive(!showHosted);
            if(menuEnvironment_)
                menuEnvironment_->ShowEnvironmentType(
                    showStock
                        ? GlobalNamespace::MenuEnvironmentManager::
                            MenuEnvironmentType::Default
                        : GlobalNamespace::MenuEnvironmentManager::
                            MenuEnvironmentType::None);

            // Reapply the stock visibility cache after MenuEnvironmentCore or
            // MenuEnvironmentManager changed state. This leaves exactly one
            // presentation authoritative: hosted gameplay, stock menu, or no
            // scenery, while Big Screen's UI and pointer remain in MenuCore.
            MenuEnvironmentVisibility::Instance().Apply();
        }
        catch(const std::exception& error)
        {
            BigScreenLogger.error(
                "Could not reconcile menu environment presentation: {}",
                error.what());
        }
        catch(...)
        {
            BigScreenLogger.error(
                "Could not reconcile menu environment presentation");
        }
    }

    void MenuGameplayEnvironmentHost::ApplyEnvironmentControls()
    {
        if(state_ != State::Ready)
            return;

        try
        {
            RestorePreviewSceneControls();
            const auto& settings = Settings::Instance();

            // Environment-tab controls are independent of screen ownership.
            // Respect Mapper Settings and Allow Chroma Override affect only
            // screen presentation and can never bypass these environment
            // visibility, motion, or lighting choices.
            if(settings.DisableEnvironmentMotion())
                for(auto behaviour : PreviewControls.motionBehaviours)
                    if(Alive(behaviour))
                        behaviour->set_enabled(false);
            if(settings.HideTrackRings())
                SetObjectsActive(PreviewControls.trackRings, false);
            if(settings.HideSideBars())
                SetObjectsActive(PreviewControls.sideBars, false);
            if(settings.HideSpectrogramBars())
                SetObjectsActive(
                    PreviewControls.spectrogramObjects, false);
            if(settings.HideSideLaserLights())
                SetObjectsActive(PreviewControls.sideLasers, false);

            if(!settings.MapLightShowEnabled())
            {
                for(auto light : PreviewControls.lights)
                    if(Alive(light))
                        light->ColorWasSet(
                            UnityEngine::Color::get_black());
                for(auto animator : PreviewControls.lightAnimators)
                    if(Alive(animator))
                        animator->SetLightsColor(
                            UnityEngine::Color::get_black());
                for(auto behaviour : PreviewControls.lightingBehaviours)
                    if(Alive(behaviour))
                        behaviour->set_enabled(false);
            }

            // Restoring components does not restore their last event-driven
            // color or transform. Rewind and replay the selected map's
            // callbacks at the current preview time before applying
            // per-channel exclusions.
            if(callbacksController_ && settings.MapLightShowEnabled())
            {
                callbacksController_->ManualUpdate(0.0f);
                appliedSongTime_ = 0.0;
                if(LightshowRequested() && previewSongTime_ > 0.0005)
                {
                    callbacksController_->ManualUpdate(
                        static_cast<float>(previewSongTime_));
                    appliedSongTime_ = previewSongTime_;
                }
            }
            else if(callbacksController_ && appliedSongTime_ > 0.0005)
            {
                callbacksController_->ManualUpdate(0.0f);
                appliedSongTime_ = 0.0;
            }

            if(settings.MapLightShowEnabled())
            {
                for(auto light : PreviewControls.lights)
                {
                    if(!Alive(light))
                        continue;
                    const int lightId = light->get_lightId();
                    const bool hidden =
                        (settings.HideBackWallLights() &&
                         (lightId == 1 || lightId == 5)) ||
                        (settings.HideRingLights() && lightId == 2) ||
                        (settings.HideSideLaserLights() &&
                         (lightId == 3 || lightId == 4));
                    if(hidden)
                    {
                        light->ColorWasSet(
                            UnityEngine::Color::get_black());
                        light->set_enabled(false);
                    }
                }
            }

            const bool showHud = settings.ShowMenuGameplayHud();
            for(auto controller : PreviewControls.hudControllers)
                if(Alive(controller))
                    controller->set_alpha(showHud ? 1.0f : 0.0f);
            for(auto& state : PreviewControls.detachedHudObjects)
                if(Alive(state.object))
                    state.object->SetActive(showHud && state.active);
            loadedShowGameplayHud_ = showHud;
        }
        catch(const std::exception& error)
        {
            BigScreenLogger.error(
                "Could not update the hosted environment controls: {}",
                error.what());
            ErrorManager::Instance().RecordError(
                "Updating the map environment preview", error.what());
            ErrorManager::Instance().ReportUserVisible(
                "Map preview control unavailable",
                "Big Screen could not apply one of the environment controls to this map preview. The menu remains usable, and gameplay settings were not changed.\n\n" +
                    std::string(error.what()));
        }
        catch(...)
        {
            BigScreenLogger.error(
                "Could not update the hosted environment controls: unknown native exception");
            ErrorManager::Instance().RecordError(
                "Updating the map environment preview",
                "Unknown native exception");
        }
    }

    void MenuGameplayEnvironmentHost::SetPreviewSongTime(
        double songTimeSeconds)
    {
        previewSongTime_ = std::max(0.0, songTimeSeconds);
        if(state_ != State::Ready || !LightshowRequested() ||
           !callbacksController_ ||
           std::abs(previewSongTime_ - appliedSongTime_) < 0.0005)
            return;
        try
        {
            callbacksController_->ManualUpdate(
                static_cast<float>(previewSongTime_));
            appliedSongTime_ = previewSongTime_;
        }
        catch(const std::exception& error)
        {
            BigScreenLogger.error(
                "Map lightshow preview stopped after a callback error: {}",
                error.what());
            callbacksController_ = nullptr;
            ErrorManager::Instance().ReportUserVisible(
                "Map lightshow preview stopped",
                "This map's environment is still available, but Beat Saber could not advance its lighting preview. Video playback and the rest of Big Screen remain usable.");
        }
    }

    void MenuGameplayEnvironmentHost::BeginUnload(
        bool restoreMenuEnvironment) noexcept
    {
        restoreMenuEnvironmentAfterUnload_ = restoreMenuEnvironment;
        if(state_ == State::Idle || state_ == State::Failed)
        {
            RestoreMenuScene();
            return;
        }
        if(state_ == State::Loading)
        {
            // Push must reach its own completion callback before a
            // Pop can be submitted to GameScenesManager. OnScenesReady will
            // immediately honor this request without activating the host.
            unloadRequested_ = true;
            return;
        }
        if(state_ == State::Unloading)
            return;

        const auto generation = ++generation_;
        state_ = State::Unloading;
        transitionStartedAt_ = std::chrono::steady_clock::now();
        unloadRequested_ = false;
        callbacksController_ = nullptr;
        callbacksUpdater_ = nullptr;
        if(Alive(audioController_))
        {
            audioController_->Pause();
            audioController_->set_enabled(false);
        }
        audioController_ = nullptr;
        ClearPreviewSceneControls();

        try
        {
            auto* manager = Alive(gameScenesManager_)
                ? gameScenesManager_.ptr() : nullptr;
            if(!manager)
            {
                OnScenesUnloaded(generation);
                return;
            }
            auto* finish = custom_types::MakeDelegate<
                System::Action_1<Zenject::DiContainer*>*>(
                    std::function<void(Zenject::DiContainer*)>{
                        [generation](Zenject::DiContainer*)
                        {
                            Instance().OnScenesUnloaded(generation);
                        }});
            manager->PopScenes(0.05f, nullptr, finish);
        }
        catch(const std::exception& error)
        {
            BigScreenLogger.error(
                "Could not unload the menu map environment normally: {}",
                error.what());
            OnScenesUnloaded(generation);
        }
        catch(...)
        {
            BigScreenLogger.error(
                "Could not unload the menu map environment normally");
            OnScenesUnloaded(generation);
        }
    }

    void MenuGameplayEnvironmentHost::OnScenesUnloaded(
        std::uint64_t generation) noexcept
    {
        if(generation != generation_)
            return;
        const auto elapsedMs = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - transitionStartedAt_)
                                   .count();
        BigScreenLogger.info(
            "Persistent menu map environment unloaded after {} ms",
            elapsedMs);
        RestoreMenuScene();
        state_ = State::Idle;
        transitionSetup_ = nullptr;
        gameScenesManager_ = nullptr;
        loadedLevelId_.clear();
        loadedEnvironmentName_.clear();
        loadedEnvironmentSceneName_.clear();
        loadingLevelId_.clear();
        loadingEnvironmentName_.clear();
        loadingEnvironmentSceneName_.clear();
        loadingEnvironmentUsesOverride_ = false;
        loadedShowGameplayHud_ = false;
        unloadRequested_ = false;

        if(menuActive_ && MapModeRequested() && pendingLevel_)
        {
            ContinuePendingSelection();
            return;
        }
        if(menuActive_ && !suspendedForFocusLoss_)
        {
            RestoreBigScreenMenuAfterEnvironmentTransition();
            MenuPlacementGuide::Instance().Apply();
            MenuEnvironmentVisibility::Instance().Apply();
            if(MapModeRequested() && selectedLevel_ &&
               !selectedLevelId_.empty())
            {
                // A failed host falls back to the normal menu, but a preview
                // deliberately suspended before that failure still needs a
                // valid screen and transport state in the fallback scene.
                VideoLibraryMenu::Instance().EnvironmentHostReady(
                    selectedLevelId_);
            }
        }
    }

    void MenuGameplayEnvironmentHost::RestoreMenuScene() noexcept
    {
        RestoreTemporaryPlayerSettings();
        try
        {
            if(transitionSetup_)
                transitionSetup_->__cordl_internal_set_didFinishEvent(nullptr);
            if(gameScenesManager_)
                gameScenesManager_->__cordl_internal_get__neverUnloadScenes()
                    ->Remove("MenuCore");

            auto* gameplayInput = Alive(gameplayInput_)
                ? gameplayInput_.ptr() : nullptr;
            auto* menuInput = Alive(menuInput_) ? menuInput_.ptr() : nullptr;
            if(gameplayInput && gameplayInput != menuInput)
                SetInputObjectsActive(gameplayInput, false);
            SetInputObjectsActive(menuInput, true);
            if(keyboardManager_ && menuInput)
            {
                keyboardManager_->__cordl_internal_set__vrInputModule(
                    menuInput->i___GlobalNamespace__IVRInputModule());
                keyboardManager_->Start();
            }
            gameplayInput_ = nullptr;

            if(menuEnvironmentCore_)
                menuEnvironmentCore_->SetActive(true);
            if(menuEnvironment_ &&
               (restoreMenuEnvironmentAfterUnload_ || !menuActive_))
                menuEnvironment_->ShowEnvironmentType(
                    GlobalNamespace::MenuEnvironmentManager::MenuEnvironmentType::Default);
            if(auto* fade = UnityEngine::Object::FindObjectOfType<
                   GlobalNamespace::FadeInOutController*>(true))
                fade->FadeIn();
        }
        catch(const std::exception& error)
        {
            BigScreenLogger.error(
                "Menu scene restoration after environment preview failed: {}",
                error.what());
        }

        localPlayer_ = nullptr;
        hostedEnvironmentRoot_ = nullptr;
        callbacksUpdater_ = nullptr;
        callbacksController_ = nullptr;
        audioController_ = nullptr;
        ClearPreviewSceneControls();
        restoreMenuEnvironmentAfterUnload_ = false;
    }

    void MenuGameplayEnvironmentHost::RestoreTemporaryPlayerSettings() noexcept
    {
        if(!temporaryPlayerSettings_)
            return;
        try
        {
            temporaryPlayerSettings_
                ->__cordl_internal_set__noTextsAndHuds(
                    savedNoTextsAndHuds_);
        }
        catch(...)
        {
            BigScreenLogger.error(
                "Could not restore the temporary menu gameplay-HUD override");
        }
        temporaryPlayerSettings_ = nullptr;
        savedNoTextsAndHuds_ = false;
    }

    void MenuGameplayEnvironmentHost::FailCurrentTransition(
        std::string context,
        std::string detail) noexcept
    {
        RestoreTemporaryPlayerSettings();
        BigScreenLogger.error("{}: {}", context, detail);
        ErrorManager::Instance().RecordError(context, detail);
        ErrorManager::Instance().ReportUserVisible(
            "Map environment preview unavailable",
            "Big Screen could not load this map's gameplay environment. The normal menu remains usable. Try another map or select Menu Environment in General.\n\n" +
                detail);
        if(state_ == State::Ready)
        {
            // Configuration can fail after GameScenesManager has successfully
            // installed the scene set. Pop that complete host before returning
            // to the normal menu; merely restoring renderers would leave an
            // invisible gameplay scene and input module resident.
            pendingLevel_ = nullptr;
            BeginUnload(true);
            return;
        }
        state_ = State::Failed;
        callbacksController_ = nullptr;
        callbacksUpdater_ = nullptr;
        // A failed map environment must never leave the player in an empty
        // scene. Restore Beat Saber's stock menu environment for this menu
        // session while retaining the saved map-mode choice for another map.
        restoreMenuEnvironmentAfterUnload_ = true;
        RestoreMenuScene();
        MenuEnvironmentVisibility::Instance().Restore();
        if(menuActive_ && !suspendedForFocusLoss_ && selectedLevel_ &&
           !selectedLevelId_.empty())
            VideoLibraryMenu::Instance().EnvironmentHostReady(
                selectedLevelId_);
    }

    void MenuGameplayEnvironmentHost::ContinuePendingSelection()
    {
        auto pending = pendingLevel_;
        pendingLevel_ = nullptr;
        if(!pending)
            return;
        selectedLevel_ = pending;
        selectedLevelId_ = pending->levelID
            ? std::string(pending->levelID) : std::string{};
        BeginLoad(pending);
    }

    bool MenuGameplayEnvironmentHost::OwnsTransitionSetup(
        GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* setup) const
    {
        // Scene-transition hooks can run while a previously captured Unity
        // object is being destroyed. UnityW::ptr() throws for that state, and
        // an exception escaping a beatsaber-hook wrapper aborts the process.
        // Prove liveness before reading the pointer so an unrelated transition
        // simply falls through to Big Screen's normal gameplay path.
        return setup && Alive(transitionSetup_) &&
            transitionSetup_.ptr() == setup &&
            state_ != State::Idle && state_ != State::Failed;
    }

    bool MenuGameplayEnvironmentHost::OwnsAudioController(
        GlobalNamespace::AudioTimeSyncController* controller) const
    {
        if(!controller)
            return false;

        // Push/Pop temporarily expose outgoing, incoming, and captured
        // AudioTimeSyncController instances. During those private transitions
        // none of them may advance: the outgoing controller can be retired
        // before StartSong captures the incoming one, and calling its original
        // Update through another mod's hook chain aborts Unity. Suppress every
        // gameplay audio update across that narrow transition boundary. Once
        // the hosted scene is stable, resume exact identity matching so an
        // unrelated controller can never be claimed by Big Screen.
        if(state_ == State::Loading || state_ == State::Unloading)
            return true;

        return state_ == State::Ready && Alive(audioController_) &&
            audioController_.ptr() == controller;
    }

    bool MenuGameplayEnvironmentHost::TransitionInProgress() const
    {
        return state_ == State::Loading;
    }

    bool MenuGameplayEnvironmentHost::RetainsMenuDuringSceneTransition() const
    {
        return menuActive_ && !suspendedForFocusLoss_ &&
            (state_ == State::Loading || state_ == State::Unloading);
    }

    void MenuGameplayEnvironmentHost::CaptureAudioController(
        GlobalNamespace::AudioTimeSyncController* controller) noexcept
    {
        if(!controller || state_ != State::Loading)
            return;
        audioController_ = controller;
    }

    bool MenuGameplayEnvironmentHost::MoveObjectIntoHostedEnvironment(
        UnityEngine::GameObject* object) noexcept
    {
        if(!Alive(object) || state_ != State::Ready ||
           !Alive(hostedEnvironmentRoot_))
            return false;
        try
        {
            UnityEngine::SceneManagement::SceneManager::MoveGameObjectToScene(
                object, hostedEnvironmentRoot_->get_scene());
            return true;
        }
        catch(...)
        {
            BigScreenLogger.error(
                "Could not move a video surface into the hosted environment scene");
            return false;
        }
    }
}
