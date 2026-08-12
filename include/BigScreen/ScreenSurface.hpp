#pragma once

#include "BigScreen/MapVideoConfig.hpp"
#include <string>

namespace UnityEngine {
    class GameObject;
    class Material;
    class Mesh;
    class Texture2D;
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

        bool IsCreated() const { return gameObject_ != nullptr; }

    private:
        bool CreateMesh(const MapVideoConfig& config, float aspectRatio);
        bool CreateMaterialAndTexture(int width, int height, bool transparent);

        UnityEngine::GameObject* gameObject_ = nullptr;
        UnityEngine::Mesh* mesh_ = nullptr;
        UnityEngine::Material* material_ = nullptr;
        UnityEngine::Texture2D* texture_ = nullptr;
        UnityEngine::GameObject* diagnosticsObject_ = nullptr;
        TMPro::TextMeshPro* diagnosticsText_ = nullptr;
        float screenWidth_ = 0.0f;
        float screenHeight_ = 0.0f;
        int textureWidth_ = 0;
        int textureHeight_ = 0;
        bool transparent_ = false;
        bool visible_ = false;
    };
}
