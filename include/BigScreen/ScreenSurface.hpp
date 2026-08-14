#pragma once

#include "BigScreen/MapVideoConfig.hpp"
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
        void SetOpacity(float opacity);

        bool IsCreated() const { return gameObject_ != nullptr; }
        UnityEngine::Texture2D* Texture() const { return texture_; }

    private:
        bool CreateInternal(
            const MapVideoConfig& config,
            int videoWidth,
            int videoHeight,
            UnityEngine::Texture2D* sharedTexture,
            const char* rootName);
        bool CreateMesh(const MapVideoConfig& config, float aspectRatio);
        bool CreateVideoMesh(const MapVideoConfig& config, float sourceAspectRatio);
        bool CreateMaterialAndTexture(
            int width,
            int height,
            bool transparent,
            UnityEngine::Texture2D* sharedTexture = nullptr);
        bool CreateBackgroundMaterial(bool transparent);
        /// Applies a per-layout transparency change without replacing the
        /// decoded texture or restarting playback.
        bool ApplyTransparency(bool transparent);

        UnityEngine::GameObject* gameObject_ = nullptr;
        UnityEngine::GameObject* videoObject_ = nullptr;
        UnityEngine::Mesh* mesh_ = nullptr;
        UnityEngine::Mesh* videoMesh_ = nullptr;
        UnityEngine::Material* material_ = nullptr;
        UnityEngine::Material* backgroundMaterial_ = nullptr;
        UnityEngine::Texture2D* texture_ = nullptr;
        UnityEngine::GameObject* diagnosticsObject_ = nullptr;
        TMPro::TextMeshPro* diagnosticsText_ = nullptr;
        float screenWidth_ = 0.0f;
        float screenHeight_ = 0.0f;
        int textureWidth_ = 0;
        int textureHeight_ = 0;
        bool ownsTexture_ = false;
        bool transparent_ = false;
        float opacity_ = 1.0f;
        bool visible_ = false;
        bool leadInActive_ = false;
        bool leadInBlack_ = false;
    };
}
