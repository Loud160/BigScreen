#pragma once

#include "BigScreen/MapVideoConfig.hpp"

namespace UnityEngine {
    class GameObject;
    class Material;
    class Mesh;
    class Texture2D;
}

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
        void Destroy();
        void SetVisible(bool visible);
        bool Upload(const VideoFrame& frame);

        bool IsCreated() const { return gameObject_ != nullptr; }

    private:
        bool CreateMesh(const MapVideoConfig& config, float aspectRatio);
        bool CreateMaterialAndTexture(int width, int height);

        UnityEngine::GameObject* gameObject_ = nullptr;
        UnityEngine::Mesh* mesh_ = nullptr;
        UnityEngine::Material* material_ = nullptr;
        UnityEngine::Texture2D* texture_ = nullptr;
        int textureWidth_ = 0;
        int textureHeight_ = 0;
        bool visible_ = false;
    };
}
