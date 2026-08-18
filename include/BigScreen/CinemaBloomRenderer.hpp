// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <chrono>
#include <vector>

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"

namespace UnityEngine { class Camera; }
namespace GlobalNamespace { class KawaseBlurRendererSO; }

namespace BigScreen {
    /// Ports PC Cinema's CustomBloomPrePass: after Beat Saber renders its own
    /// camera-owned bloom target, the registered video screens are drawn with
    /// the exact material the player sees into a linear HDR temporary, blurred
    /// with the game's own Kawase renderer, and additively blitted into the
    /// bloom pre-pass texture. This is the ONLY way the video screen can glow:
    /// the screen surface itself deliberately clears the framebuffer's
    /// bloom-emission alpha where video covers it (see ScreenSurface), because
    /// letting the game's own composite read the screen turns the picture
    /// solid white instead of producing a frame glow.
    ///
    /// Every method must be called from Unity's main thread. OnCameraPreRender
    /// is invoked from the Camera.FireOnPreRender hook for every camera; with
    /// no registered sources it returns after one empty-vector check.
    class CinemaBloomRenderer final {
    public:
        static CinemaBloomRenderer& Instance();

        CinemaBloomRenderer(const CinemaBloomRenderer&) = delete;
        CinemaBloomRenderer& operator=(const CinemaBloomRenderer&) = delete;

        /// Registers (or refreshes) a video screen object as a bloom source.
        /// The object's own MeshRenderer material and MeshFilter mesh are
        /// drawn, so the glow always matches what the player currently sees.
        void RegisterSource(
            UnityEngine::GameObject* videoObject,
            float intensity,
            float screenWidth,
            float screenHeight,
            bool pcStyle);
        /// Updates an existing registration after geometry or settings
        /// changes without restarting playback.
        void UpdateSource(
            UnityEngine::GameObject* videoObject,
            float intensity,
            float screenWidth,
            float screenHeight,
            bool pcStyle);
        void UnregisterSource(UnityEngine::GameObject* videoObject);

        /// Renders every registered source's bloom contribution for this
        /// camera. Never throws: failures are rate-limited into the error
        /// history and playback continues without the glow pass.
        void OnCameraPreRender(UnityEngine::Camera* camera) noexcept;

    private:
        CinemaBloomRenderer() = default;

        struct Source {
            UnityW<UnityEngine::GameObject> videoObject;
            float intensity = 1.0f;
            float width = 1.0f;
            float height = 1.0f;
            bool pcStyle = true;
        };

        GlobalNamespace::KawaseBlurRendererSO* ResolveBlurRenderer();
        bool EnsureAdditiveMaterial(
            GlobalNamespace::KawaseBlurRendererSO* renderer);
        void RenderSource(UnityEngine::Camera* camera, Source& source);
        void ReportFailure(const char* detail) noexcept;

        std::vector<Source> sources_{};
        UnityW<GlobalNamespace::KawaseBlurRendererSO> blurRenderer_ = nullptr;
        UnityW<UnityEngine::Material> additiveMaterial_ = nullptr;
        std::chrono::steady_clock::time_point lastFailure_{};
    };
}
