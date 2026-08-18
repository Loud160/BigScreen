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
namespace GlobalNamespace {
    class BloomPrePass;
    class KawaseBlurRendererSO;
}

namespace BigScreen {
    /// Ports PC Cinema's CustomBloomPrePass: after Beat Saber renders its own
    /// camera-owned bloom target, each registered screen's live texture and
    /// tint are drawn through a dedicated mono-safe capture material into a
    /// linear HDR temporary, blurred with the game's own Kawase renderer, and
    /// additively blitted into the bloom pre-pass texture. The capture material
    /// is deliberately independent from the visible stereo material. The
    /// screen surface separately clears the framebuffer's bloom-emission alpha
    /// where video covers it (see ScreenSurface), preventing the game's normal
    /// bloom composite from washing the picture out.
    ///
    /// Every method must be called from Unity's main thread. OnCameraPreRender
    /// is invoked immediately after each BloomPrePass.OnPreRender owns and
    /// finalizes its camera target; with no registered sources it returns
    /// after one empty-vector check.
    class CinemaBloomRenderer final {
    public:
        static CinemaBloomRenderer& Instance();

        CinemaBloomRenderer(const CinemaBloomRenderer&) = delete;
        CinemaBloomRenderer& operator=(const CinemaBloomRenderer&) = delete;

        /// Registers (or refreshes) a video screen object as a bloom source.
        /// The object's live MeshRenderer texture/tint and MeshFilter mesh are
        /// sampled through the capture material so the glow follows the current
        /// frame without reusing a stereo-only pass in the mono bloom target.
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
        void OnCameraPreRender(
            UnityEngine::Camera* camera,
            GlobalNamespace::BloomPrePass* prePass) noexcept;

    private:
        CinemaBloomRenderer() = default;

        struct Source {
            UnityW<UnityEngine::GameObject> videoObject;
            float intensity = 1.0f;
            float width = 1.0f;
            float height = 1.0f;
            bool pcStyle = true;
            bool successfulCaptureLogged = false;
            bool pixelSignalLogged = false;
        };

        GlobalNamespace::KawaseBlurRendererSO* ResolveBlurRenderer();
        bool EnsureAdditiveMaterial(
            GlobalNamespace::KawaseBlurRendererSO* renderer);
        bool EnsureCaptureMaterial();
        void RenderSource(
            UnityEngine::Camera* camera,
            GlobalNamespace::BloomPrePass* prePass,
            Source& source);
        void ReportFailure(const char* detail) noexcept;

        std::vector<Source> sources_{};
        UnityW<GlobalNamespace::KawaseBlurRendererSO> blurRenderer_ = nullptr;
        UnityW<UnityEngine::Material> additiveMaterial_ = nullptr;
        UnityW<UnityEngine::Material> captureMaterial_ = nullptr;
        std::chrono::steady_clock::time_point lastFailure_{};
    };
}
