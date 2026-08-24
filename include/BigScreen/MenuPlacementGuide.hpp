// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <vector>

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/Renderer.hpp"

namespace BigScreen {
    /// Owns the optional open-floor placement reference shown only inside Big
    /// Screen's menu. It never changes a scene object or renderer used during
    /// gameplay, and every renderer it disables is restored on teardown.
    class MenuPlacementGuide final {
    public:
        static MenuPlacementGuide& Instance();

        /// Indexes compatible floor renderers without hiding them. Separating
        /// discovery from Apply avoids a global renderer scan when the player
        /// first opens Big Screen while preserving the setting's exact effect.
        void PrewarmCache();
        /// Applies the current setting while Big Screen's flow is active.
        /// Calling this repeatedly is safe and does not rescan a live guide.
        bool Apply();
        /// Restores the menu floor and destroys all Big Screen guide geometry.
        void Suspend() noexcept;

    private:
        MenuPlacementGuide() = default;

        bool HideCompatibleMenuFloorRenderers();
        bool CreateLaneGuide();
        void DestroyLaneGuide() noexcept;
        void RestoreMenuFloorRenderers() noexcept;

        // Records that the current menu scene has already been scanned, even
        // when no compatible floor was present. This prevents repeated global
        // renderer scans while the independent lane toggle changes.
        bool floorRemovalApplied_ = false;
        bool floorCacheInitialized_ = false;
        std::vector<UnityW<UnityEngine::Renderer>> knownFloorRenderers_;
        std::vector<UnityW<UnityEngine::Renderer>> hiddenFloorRenderers_;
        UnityW<UnityEngine::GameObject> guideRoot_ = nullptr;
        UnityW<UnityEngine::Material> guideMaterial_ = nullptr;
        UnityW<UnityEngine::Mesh> guideMesh_ = nullptr;
    };
}
