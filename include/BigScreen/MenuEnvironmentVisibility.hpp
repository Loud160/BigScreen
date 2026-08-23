// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <vector>

#include "UnityEngine/Behaviour.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Transform.hpp"

namespace BigScreen {
    /// Reconciles the positive Show Menu Environment preference while Big
    /// Screen's menu is active. It disables only environment-owned rendering
    /// and lighting components, never the environment root or input hierarchy.
    class MenuEnvironmentVisibility final {
    public:
        static MenuEnvironmentVisibility& Instance();

        /// Applies the current preference. Repeated calls while hidden also
        /// capture components that another menu system enabled afterward.
        void Apply();
        /// Restores every component that Big Screen observed as enabled.
        void Restore() noexcept;

    private:
        MenuEnvironmentVisibility() = default;

        void HideVisualComponents();

        bool hidden_ = false;
        // Global FindObjectsOfType calls are expensive in heavily modded menu
        // scenes. Cache the environment-owned components through ordinary menu
        // visits and rebuild this weak-handle index after Unity replaces them.
        UnityW<UnityEngine::Transform> environmentRoot_ = nullptr;
        std::vector<UnityW<UnityEngine::Renderer>> knownRenderers_;
        std::vector<UnityW<UnityEngine::Behaviour>> knownLights_;
        std::vector<UnityW<UnityEngine::Renderer>> hiddenRenderers_;
        std::vector<UnityW<UnityEngine::Behaviour>> hiddenLights_;
    };
}
