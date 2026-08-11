#pragma once

namespace HMUI {
    class CurvedTextMeshPro;
    class FlowCoordinator;
    class ViewController;
}

namespace UnityEngine {
    class Camera;
    class GameObject;
    class Material;
    class Mesh;
    class RenderTexture;
    class Texture2D;
}

namespace UnityEngine::UI {
    class RawImage;
}

namespace BigScreen {
    /// Owns the optional right-panel render preview shown by the settings flow.
    ///
    /// The preview uses a private camera and render texture rather than placing
    /// geometry in the user's menu view. This isolates it from Beat Saber's VR
    /// cameras and from other mods that also add menu panels.
    class ScreenPreview final {
    public:
        static ScreenPreview& Instance();

        void Bind(
            HMUI::FlowCoordinator* flowCoordinator,
            HMUI::ViewController* previewViewController);
        void ActivateCurrentState();
        void SetEnabled(bool enabled);
        void Refresh();
        void Suspend();

    private:
        ScreenPreview() = default;

        void CreateUi();
        bool CreateRenderer();
        void DestroyRenderer();
        void RebuildScreenMesh();
        void RebuildScreenMaterial();
        void UpdateStatusText();

        HMUI::FlowCoordinator* flowCoordinator_ = nullptr;
        HMUI::ViewController* previewViewController_ = nullptr;
        UnityEngine::UI::RawImage* previewImage_ = nullptr;
        HMUI::CurvedTextMeshPro* statusText_ = nullptr;

        UnityEngine::GameObject* previewRoot_ = nullptr;
        UnityEngine::GameObject* cameraObject_ = nullptr;
        UnityEngine::GameObject* backgroundObject_ = nullptr;
        UnityEngine::GameObject* screenObject_ = nullptr;
        UnityEngine::Camera* camera_ = nullptr;
        UnityEngine::RenderTexture* renderTexture_ = nullptr;
        UnityEngine::Mesh* backgroundMesh_ = nullptr;
        UnityEngine::Mesh* screenMesh_ = nullptr;
        UnityEngine::Material* backgroundMaterial_ = nullptr;
        UnityEngine::Material* screenMaterial_ = nullptr;
        UnityEngine::Texture2D* backgroundTexture_ = nullptr;
        UnityEngine::Texture2D* sampleVideoTexture_ = nullptr;
    };
}
