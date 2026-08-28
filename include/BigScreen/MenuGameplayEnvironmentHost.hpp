// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
#include "cordl_internals/unity-utils.hpp"

namespace GlobalNamespace {
    class AudioTimeSyncController;
    class BeatmapCallbacksController;
    class BeatmapCallbacksUpdater;
    class BeatmapLevel;
    class GameScenesManager;
    class MenuEnvironmentManager;
    class PlayerSpecificSettings;
    class StandardLevelScenesTransitionSetupDataSO;
    class UIKeyboardManager;
}

namespace UnityEngine { class GameObject; }
namespace VRUIControls { class VRInputModule; }

namespace BigScreen {
    /// Owns the complete, persistent gameplay-scene host used by the optional
    /// map-environment menu modes. Unlike the removed partial-scene experiment,
    /// this always enters through Beat Saber's standard level transition so
    /// environment installers receive their complete gameplay dependency graph.
    /// Gameplay cameras, players, object spawning, automatic timing, and audio
    /// are then disabled while MenuCore and Big Screen's flow remain active.
    class MenuGameplayEnvironmentHost final {
    public:
        static MenuGameplayEnvironmentHost& Instance();

        /// Marks Big Screen's flow active and reconciles the persisted mode.
        void ActivateMenu();
        /// Starts fail-safe scene removal and prevents any completion callback
        /// from reapplying menu state after the flow has closed.
        void DeactivateMenu() noexcept;
        /// Reconciles a live dropdown/master-toggle change.
        void ApplyMode();
        /// Removes the hosted scene while the application is backgrounded,
        /// retaining the selected map so focus restoration can rebuild it.
        void SuspendForFocusLoss() noexcept;
        void ResumeAfterFocusGain();

        /// Selects the representative map/difficulty used for environment
        /// preview. Returns true when video-screen creation must wait for an
        /// asynchronous scene load to complete.
        bool SelectLevel(GlobalNamespace::BeatmapLevel* level);
        /// Advances the selected map's callbacks only in Map + Lightshow mode.
        void SetPreviewSongTime(double songTimeSeconds);
        /// Reapplies the Environment-tab visibility, motion, and lighting
        /// choices to the already loaded preview scene. This is deliberately
        /// reversible and never submits a GameScenesManager transition.
        void ApplyEnvironmentControls();

        /// Existing gameplay hooks use these guards to distinguish Big Screen's
        /// private preview transition from an actual level launch.
        bool OwnsTransitionSetup(
            GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* setup) const;
        bool OwnsAudioController(
            GlobalNamespace::AudioTimeSyncController* controller) const;
        bool TransitionInProgress() const;
        /// True while Beat Saber may temporarily deactivate Big Screen's flow
        /// as part of Push/Pop even though the user did not close it.
        bool RetainsMenuDuringSceneTransition() const;
        void CaptureAudioController(
            GlobalNamespace::AudioTimeSyncController* controller) noexcept;
        /// Moves a newly created Big Screen surface into the exact gameplay
        /// environment scene owned by this menu host. This avoids Unity's
        /// ambiguous global "Environment" lookup while MenuCore and the
        /// gameplay environment are loaded together.
        bool MoveObjectIntoHostedEnvironment(
            UnityEngine::GameObject* object) noexcept;

    private:
        enum class State {
            Idle,
            Loading,
            Ready,
            Unloading,
            Failed
        };

        MenuGameplayEnvironmentHost() = default;

        bool MapModeRequested() const;
        bool LightshowRequested() const;
        bool BeginLoad(GlobalNamespace::BeatmapLevel* level);
        bool ReloadLevel(GlobalNamespace::BeatmapLevel* level);
        void BeginUnload(bool restoreMenuEnvironment) noexcept;
        void OnScenesReady(std::uint64_t generation) noexcept;
        void OnScenesUnloaded(std::uint64_t generation) noexcept;
        void ConfigureLoadedScene();
        void ReconcileEnvironmentPresentation() noexcept;
        void RestoreTemporaryPlayerSettings() noexcept;
        void RestoreMenuScene() noexcept;
        void FailCurrentTransition(
            std::string context,
            std::string detail) noexcept;
        void ContinuePendingSelection();

        State state_ = State::Idle;
        bool menuActive_ = false;
        bool suspendedForFocusLoss_ = false;
        bool restoreMenuEnvironmentAfterUnload_ = false;
        bool unloadRequested_ = false;
        std::uint64_t generation_ = 0;
        std::chrono::steady_clock::time_point transitionStartedAt_{};
        double previewSongTime_ = 0.0;
        double appliedSongTime_ = -1.0;

        std::string selectedLevelId_;
        std::string loadingLevelId_;
        std::string loadedLevelId_;
        std::string loadingEnvironmentName_;
        std::string loadedEnvironmentName_;
        std::string loadingEnvironmentSceneName_;
        std::string loadedEnvironmentSceneName_;
        bool loadingEnvironmentUsesOverride_ = false;
        // Runtime BeatmapLevel is a managed data object, not a UnityEngine
        // Object, so UnityW's native-object liveness contract does not apply.
        // Beat Saber/SongCore retain these catalog objects for the menu scene.
        GlobalNamespace::BeatmapLevel* selectedLevel_ = nullptr;
        GlobalNamespace::BeatmapLevel* pendingLevel_ = nullptr;
        UnityW<GlobalNamespace::GameScenesManager> gameScenesManager_ = nullptr;
        UnityW<GlobalNamespace::StandardLevelScenesTransitionSetupDataSO>
            transitionSetup_ = nullptr;
        UnityW<GlobalNamespace::MenuEnvironmentManager> menuEnvironment_ = nullptr;
        UnityW<GlobalNamespace::UIKeyboardManager> keyboardManager_ = nullptr;
        UnityW<VRUIControls::VRInputModule> menuInput_ = nullptr;
        UnityW<VRUIControls::VRInputModule> gameplayInput_ = nullptr;
        UnityW<UnityEngine::GameObject> menuEnvironmentCore_ = nullptr;
        // Some environment scenes have several top-level roots and no child
        // literally named Environment. This object is only a live scene
        // anchor; renderer/component state owns visual presentation.
        UnityW<UnityEngine::GameObject> hostedEnvironmentRoot_ = nullptr;
        UnityW<UnityEngine::GameObject> localPlayer_ = nullptr;
        UnityW<GlobalNamespace::BeatmapCallbacksUpdater> callbacksUpdater_ = nullptr;
        GlobalNamespace::BeatmapCallbacksController* callbacksController_ = nullptr;
        UnityW<GlobalNamespace::AudioTimeSyncController> audioController_ = nullptr;
        // Standard scene setup reads the live player-settings object while it
        // constructs gameplay HUDs. Keep the user's original value only for
        // the duration of that asynchronous transition, then restore it.
        GlobalNamespace::PlayerSpecificSettings* temporaryPlayerSettings_ = nullptr;
        bool savedNoTextsAndHuds_ = false;
        bool loadedShowGameplayHud_ = false;
    };
}
