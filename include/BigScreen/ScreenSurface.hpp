// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <vector>

#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/MapVideoConfig.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "beatsaber-hook/shared/utils/typedefs-array.hpp"
#include <string>

namespace UnityEngine {
    class GameObject;
    class Material;
    class Mesh;
    class Texture2D;
    struct Quaternion;
    struct Vector3;
}
namespace TMPro { class TextMeshPro; }

namespace BigScreen {
    struct VideoFrame;

    /// Owns the small set of Unity objects used to display decoded video.
    ///
    /// Every method on this class must be called from Unity's main thread.
    /// FFmpeg never sees these objects; its worker hands ordinary RGBA bytes to
    /// PlaybackSession, which then uploads them during Beat Saber's Update.
    class ScreenSurface final {
    public:
        ScreenSurface() = default;
        ~ScreenSurface() = default;

        ScreenSurface(const ScreenSurface&) = delete;
        ScreenSurface& operator=(const ScreenSurface&) = delete;

        bool Create(const MapVideoConfig& config, int videoWidth, int videoHeight);
        /// Creates another independently transformable panel backed by an
        /// existing owner's Texture2D. The clone owns its meshes/materials but
        /// never uploads or destroys the shared texture.
        bool CreateShared(
            const MapVideoConfig& config,
            int videoWidth,
            int videoHeight,
            UnityEngine::Texture2D* sharedTexture,
            const char* rootName);
        /// Rebuilds only geometry and placement while preserving the decoder's
        /// current texture, material, visibility, and most recently presented
        /// frame. This is used by the pause-menu layout selector so changing a
        /// layout cannot restart or desynchronize video playback.
        bool UpdateGeometry(const MapVideoConfig& config);
        void Destroy();
        void SetVisible(bool visible);
        /// Shows an opaque black surface during negative video time, or hides
        /// the surface entirely when transparent lead-in is selected.
        void ShowLeadIn(bool black);
        bool Upload(const VideoFrame& frame);
        /// Updates a small world-space diagnostics label attached to the
        /// screen. Passing an empty string removes it immediately.
        void SetDiagnosticsText(const std::string& text);
        /// Moves the complete surface without rebuilding either mesh. The
        /// undocked editor calls this every frame while a controller is held.
        void SetWorldTransform(
            UnityEngine::Vector3 position,
            UnityEngine::Quaternion rotation);
        void SetWorldScale(UnityEngine::Vector3 scale);
        /// Rotates the decoded picture independently from its physical frame.
        /// Showcase panels use this to counter-roll a tumbling screen without
        /// rebuilding meshes or touching the shared video texture.
        void SetVideoLocalRoll(float degrees);
        /// Returns false if Unity rejected the material-state update. Showcase
        /// callers propagate that failure once instead of retrying a broken
        /// partially destroyed surface on every frame.
        bool SetOpacity(float opacity);
        /// Disables back-face culling for showcase surfaces that deliberately
        /// rotate through 180 degrees. The same synchronized video texture is
        /// then visible from either side of the curved canvas.
        void SetDoubleSided(bool enabled);
        /// Applies showcase-only vertex deformation to the already allocated
        /// video mesh. The normal surface never calls this method, so inactive
        /// maps retain their original mesh and have no per-frame geometry cost.
        bool SetDeformation(
            const CoreLogic::SurfaceDeformationSettings& deformation,
            double songTimeSeconds,
            double realTimeSeconds);
        /// Applies the showcase-only deterministic glass state. Pattern and
        /// mesh setup happen only when the seed/configuration changes; active
        /// frames reuse the preallocated buffers. Non-showcase surfaces never
        /// call this method and allocate no fracture resources.
        bool SetFractureEffect(
            const CoreLogic::FractureEffectSettings& fracture,
            const CoreLogic::SurfaceDeformationSettings& deformation,
            double songTimeSeconds,
            double realTimeSeconds);

        bool IsCreated() const { return gameObject_ != nullptr; }
        UnityEngine::Texture2D* Texture() const { return texture_; }

    private:
        bool CreateInternal(
            const MapVideoConfig& config,
            int videoWidth,
            int videoHeight,
            UnityEngine::Texture2D* sharedTexture,
            const char* rootName,
            bool prepareDeformation);
        bool CreateMesh(const MapVideoConfig& config, float aspectRatio);
        bool CreateVideoMesh(const MapVideoConfig& config, float sourceAspectRatio);
        bool CreateMaterialAndTexture(
            int width,
            int height,
            float videoOpacity,
            UnityEngine::Texture2D* sharedTexture = nullptr);
        bool CreateBackgroundMaterial(bool letterboxTransparent);
        /// Applies picture opacity and letterbox transparency without
        /// replacing the decoded texture or restarting playback.
        bool ApplyPresentation(
            bool letterboxTransparent,
            float videoOpacity);
        bool PrepareFracture(
            const CoreLogic::FractureEffectSettings& fracture);
        void DestroyFractureResources();
        void RestoreWholeVideoMesh();
        UnityEngine::Vector3 MapFracturePoint(
            CoreLogic::FracturePoint point,
            const CoreLogic::SurfaceDeformationSettings& deformation,
            double songTimeSeconds,
            double realTimeSeconds) const;
        bool UpdateCrackOverlay(
            const CoreLogic::FractureEffectSettings& fracture,
            const CoreLogic::SurfaceDeformationSettings& deformation,
            double songTimeSeconds,
            double realTimeSeconds);
        bool CaptureFractureShape(
            const CoreLogic::FractureEffectSettings& fracture,
            const CoreLogic::SurfaceDeformationSettings& deformation,
            double songTimeSeconds,
            double realTimeSeconds);
        bool UpdateFractureVertices(
            const CoreLogic::FractureEffectSettings& fracture);

        struct DeformationBaseVertex {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float normalizedU = 0.0f;
            float normalizedV = 0.0f;
        };

        struct FractureVertex {
            CoreLogic::FracturePoint point{};
            std::size_t shard = 0;
        };

        UnityEngine::GameObject* gameObject_ = nullptr;
        UnityEngine::GameObject* videoObject_ = nullptr;
        UnityEngine::Mesh* mesh_ = nullptr;
        UnityEngine::Mesh* videoMesh_ = nullptr;
        UnityEngine::Material* material_ = nullptr;
        UnityEngine::Material* backgroundMaterial_ = nullptr;
        UnityEngine::Texture2D* texture_ = nullptr;
        UnityEngine::GameObject* crackObject_ = nullptr;
        UnityEngine::Mesh* crackMesh_ = nullptr;
        UnityEngine::Material* crackMaterial_ = nullptr;
        UnityEngine::Texture2D* crackTexture_ = nullptr;
        UnityEngine::Mesh* fractureMesh_ = nullptr;
        UnityEngine::Texture2D* fractureSnapshot_ = nullptr;
        UnityEngine::GameObject* diagnosticsObject_ = nullptr;
        TMPro::TextMeshPro* diagnosticsText_ = nullptr;
        float screenWidth_ = 0.0f;
        float screenHeight_ = 0.0f;
        int textureWidth_ = 0;
        int textureHeight_ = 0;
        bool ownsTexture_ = false;
        bool letterboxTransparent_ = false;
        float opacity_ = 1.0f;
        bool visible_ = false;
        bool leadInActive_ = false;
        bool leadInBlack_ = false;
        MapVideoConfig geometryConfig_{};
        float geometryAspectRatio_ = 1.0f;
        std::vector<DeformationBaseVertex> deformationBaseVertices_{};
        ArrayW<UnityEngine::Vector3> undeformedVideoVertices_{};
        ArrayW<UnityEngine::Vector3> dynamicVideoVertices_{};
        ArrayW<UnityEngine::Vector2> undeformedVideoUvs_{};
        ArrayW<UnityEngine::Vector2> dynamicVideoUvs_{};
        CoreLogic::FracturePattern fracturePattern_{};
        CoreLogic::FracturePatternSettings preparedFractureSettings_{};
        // Reuse the public settings array type instead of duplicating its
        // capacity here. The showcase currently authors eighteen impacts; a
        // smaller independent cache previously let PrepareFracture copy past
        // this member and corrupt the ScreenSurface object on UnityMain.
        decltype(CoreLogic::FractureEffectSettings::impacts)
            preparedFractureImpacts_{};
        std::size_t preparedFractureImpactCount_ = 0;
        std::vector<std::size_t> fractureRevealGroups_{};
        std::vector<FractureVertex> fractureVertexMetadata_{};
        std::vector<CoreLogic::FracturePoint> fractureShardCenters_{};
        std::vector<UnityEngine::Vector3> fractureShardBaseCenters_{};
        // Bulk and optional per-shard transforms are evaluated once per shard,
        // then reused by every triangle vertex owned by that shard. This avoids
        // repeating hashes, square roots, and trigonometry for duplicated fan
        // vertices on every showcase frame.
        std::vector<UnityEngine::Vector3> fractureShardTranslations_{};
        std::vector<UnityEngine::Vector3> fractureShardRotations_{};
        std::vector<float> fractureShardScales_{};
        ArrayW<UnityEngine::Vector3> fractureBaseVertices_{};
        ArrayW<UnityEngine::Vector3> dynamicFractureVertices_{};
        ArrayW<UnityEngine::Vector2> fractureUvs_{};
        ArrayW<UnityEngine::Vector3> dynamicCrackVertices_{};
        bool fracturePrepared_ = false;
        bool fractureMeshActive_ = false;
        bool fractureShapeCaptured_ = false;
        bool fractureSnapshotActive_ = false;
        // Only showcase clones allocate the high-density deformation state.
        // The ordinary single-screen path therefore retains its original
        // allocation and per-frame cost when the showcase is inactive.
        bool prepareDeformation_ = false;
        bool deformationWasApplied_ = false;
    };
}
