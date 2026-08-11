#include "BigScreen/ScreenPreview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "BigScreen/Settings.hpp"
#include "HMUI/CurvedTextMeshPro.hpp"
#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "System/IntPtr.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/CameraClearFlags.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/RawImage.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr int PreviewLayer = 30;
        constexpr int RenderWidth = 768;
        constexpr int RenderHeight = 432;
        constexpr int TextureWidth = 512;
        constexpr int TextureHeight = 288;
        constexpr int ScreenSegments = 32;
        constexpr float Pi = 3.14159265358979323846f;

        UnityEngine::Texture2D* CreateTexture(
            const std::vector<std::uint8_t>& rgba)
        {
            auto* texture = UnityEngine::Texture2D::New_ctor(
                TextureWidth,
                TextureHeight,
                UnityEngine::TextureFormat::RGBA32,
                false,
                false);
            if(!texture)
                return nullptr;

            texture->LoadRawTextureData(
                System::IntPtr(const_cast<std::uint8_t*>(rgba.data())),
                static_cast<std::int32_t>(rgba.size()));
            texture->Apply(false, false);
            return texture;
        }

        std::vector<std::uint8_t> MakeReferenceStagePixels()
        {
            std::vector<std::uint8_t> pixels(
                static_cast<std::size_t>(TextureWidth) * TextureHeight * 4);
            for(int y = 0; y < TextureHeight; ++y)
            {
                for(int x = 0; x < TextureWidth; ++x)
                {
                    const bool verticalGrid = x % 48 <= 1;
                    const bool horizontalGrid = y % 36 <= 1;
                    const bool horizon = std::abs(y - TextureHeight / 2) <= 2;
                    const float verticalFade = 1.0f -
                        std::abs(y - TextureHeight * 0.5f) / (TextureHeight * 0.5f);
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * TextureWidth + x) * 4;

                    // A dark blue grid approximates the unobstructed Big Mirror
                    // viewing volume without loading its gameplay scene into the
                    // menu. Bright lines remain useful behind transparent video.
                    pixels[offset + 0] = static_cast<std::uint8_t>(3 + 5 * verticalFade);
                    pixels[offset + 1] = static_cast<std::uint8_t>(8 + 12 * verticalFade);
                    pixels[offset + 2] = static_cast<std::uint8_t>(18 + 28 * verticalFade);
                    if(verticalGrid || horizontalGrid || horizon)
                    {
                        pixels[offset + 0] = horizon ? 28 : 10;
                        pixels[offset + 1] = horizon ? 85 : 38;
                        pixels[offset + 2] = horizon ? 145 : 72;
                    }
                    pixels[offset + 3] = 255;
                }
            }
            return pixels;
        }

        std::vector<std::uint8_t> MakeSampleVideoPixels()
        {
            std::vector<std::uint8_t> pixels(
                static_cast<std::size_t>(TextureWidth) * TextureHeight * 4);
            for(int y = 0; y < TextureHeight; ++y)
            {
                for(int x = 0; x < TextureWidth; ++x)
                {
                    const float u = x / static_cast<float>(TextureWidth - 1);
                    const float v = y / static_cast<float>(TextureHeight - 1);
                    const bool fineLine = x % 64 <= 1 || y % 48 <= 1;
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * TextureWidth + x) * 4;

                    // The colored gradient exposes stretching, transparency,
                    // curvature, and edge orientation more clearly than a flat
                    // placeholder color would.
                    pixels[offset + 0] = static_cast<std::uint8_t>(35 + 185 * u);
                    pixels[offset + 1] = static_cast<std::uint8_t>(30 + 150 * (1.0f - v));
                    pixels[offset + 2] = static_cast<std::uint8_t>(80 + 130 * v);
                    if(fineLine)
                    {
                        pixels[offset + 0] = 235;
                        pixels[offset + 1] = 235;
                        pixels[offset + 2] = 245;
                    }
                    pixels[offset + 3] = 255;
                }
            }
            return pixels;
        }

        UnityEngine::Mesh* CreateQuadMesh(float width, float height)
        {
            ArrayW<UnityEngine::Vector3> vertices(4);
            ArrayW<UnityEngine::Vector2> uvs(4);
            ArrayW<std::int32_t> triangles(6);
            const float halfWidth = width * 0.5f;
            const float halfHeight = height * 0.5f;
            vertices[0] = {-halfWidth, -halfHeight, 0.0f};
            vertices[1] = {-halfWidth, halfHeight, 0.0f};
            vertices[2] = {halfWidth, halfHeight, 0.0f};
            vertices[3] = {halfWidth, -halfHeight, 0.0f};
            uvs[0] = {0.0f, 0.0f};
            uvs[1] = {0.0f, 1.0f};
            uvs[2] = {1.0f, 1.0f};
            uvs[3] = {1.0f, 0.0f};
            triangles[0] = 0;
            triangles[1] = 1;
            triangles[2] = 2;
            triangles[3] = 0;
            triangles[4] = 2;
            triangles[5] = 3;

            auto* mesh = UnityEngine::Mesh::New_ctor();
            mesh->set_vertices(vertices);
            mesh->set_uv(uvs);
            mesh->set_triangles(triangles);
            mesh->RecalculateNormals();
            mesh->RecalculateBounds();
            return mesh;
        }

        void ConfigureTransparency(UnityEngine::Material* material)
        {
            if(!material)
                return;
            material->set_color(UnityEngine::Color{1.0f, 1.0f, 1.0f, 0.75f});
            material->SetInt("_SrcBlend", 5);
            material->SetInt("_DstBlend", 10);
            material->SetInt("_ZWrite", 0);
            material->DisableKeyword("_ALPHATEST_ON");
            material->EnableKeyword("_ALPHABLEND_ON");
            material->set_renderQueue(3000);
        }
    }

    ScreenPreview& ScreenPreview::Instance()
    {
        static ScreenPreview preview;
        return preview;
    }

    void ScreenPreview::Bind(
        HMUI::FlowCoordinator* flowCoordinator,
        HMUI::ViewController* previewViewController)
    {
        if(previewViewController_ != previewViewController)
        {
            // A MenuCore restart creates new view controllers. Never retain UI
            // component pointers owned by the old scene.
            DestroyRenderer();
            previewImage_ = nullptr;
            statusText_ = nullptr;
        }
        flowCoordinator_ = flowCoordinator;
        previewViewController_ = previewViewController;
    }

    void ScreenPreview::ActivateCurrentState()
    {
        if(!Settings::Instance().MenuScreenPreviewEnabled())
            return;
        CreateUi();
        if(CreateRenderer())
            Refresh();
    }

    void ScreenPreview::SetEnabled(bool enabled)
    {
        if(!flowCoordinator_ || !previewViewController_)
            return;

        if(enabled)
        {
            CreateUi();
            CreateRenderer();
            Refresh();
            flowCoordinator_->SetRightScreenViewController(
                previewViewController_,
                HMUI::ViewController::AnimationType::In);
        }
        else
        {
            flowCoordinator_->SetRightScreenViewController(
                nullptr,
                HMUI::ViewController::AnimationType::Out);
            DestroyRenderer();
        }
    }

    void ScreenPreview::Refresh()
    {
        if(!previewRoot_)
            return;
        RebuildScreenMesh();
        RebuildScreenMaterial();
        UpdateStatusText();
    }

    void ScreenPreview::Suspend()
    {
        DestroyRenderer();
        flowCoordinator_ = nullptr;
    }

    void ScreenPreview::CreateUi()
    {
        if(!previewViewController_ || previewImage_)
            return;

        BSML::Lite::CreateText(
            previewViewController_,
            "Big Mirror Reference Preview",
            4.5f,
            {0.0f, 27.0f},
            {68.0f, 7.0f});
        previewImage_ = BSML::Lite::CreateRawImage(
            previewViewController_,
            nullptr,
            {0.0f, 1.0f},
            {68.0f, 38.25f});
        statusText_ = BSML::Lite::CreateText(
            previewViewController_,
            "",
            3.2f,
            {0.0f, -25.0f},
            {70.0f, 8.0f});
    }

    bool ScreenPreview::CreateRenderer()
    {
        if(previewRoot_)
            return true;
        if(!previewImage_)
            return false;

        renderTexture_ = UnityEngine::RenderTexture::New_ctor(
            RenderWidth,
            RenderHeight,
            16);
        if(!renderTexture_ || !renderTexture_->Create())
        {
            PaperLogger.error("Could not create the menu screen-preview render target");
            DestroyRenderer();
            return false;
        }

        previewRoot_ = UnityEngine::GameObject::New_ctor("Big Screen Preview Root");
        cameraObject_ = UnityEngine::GameObject::New_ctor("Big Screen Preview Camera");
        backgroundObject_ = UnityEngine::GameObject::New_ctor("Big Mirror Reference Stage");
        screenObject_ = UnityEngine::GameObject::New_ctor("Big Screen Preview Surface");
        if(!previewRoot_ || !cameraObject_ || !backgroundObject_ || !screenObject_)
        {
            DestroyRenderer();
            return false;
        }

        // The far-away root is a second safety boundary in addition to the
        // private layer: even a menu camera with an unexpectedly broad culling
        // mask cannot see these objects in its normal clipping volume.
        previewRoot_->get_transform()->set_position({10000.0f, 10000.0f, 10000.0f});
        cameraObject_->get_transform()->SetParent(previewRoot_->get_transform(), false);
        backgroundObject_->get_transform()->SetParent(previewRoot_->get_transform(), false);
        screenObject_->get_transform()->SetParent(previewRoot_->get_transform(), false);
        cameraObject_->set_layer(PreviewLayer);
        backgroundObject_->set_layer(PreviewLayer);
        screenObject_->set_layer(PreviewLayer);

        camera_ = cameraObject_->AddComponent<UnityEngine::Camera*>();
        auto* backgroundFilter = backgroundObject_->AddComponent<UnityEngine::MeshFilter*>();
        auto* backgroundRenderer = backgroundObject_->AddComponent<UnityEngine::MeshRenderer*>();
        screenObject_->AddComponent<UnityEngine::MeshFilter*>();
        screenObject_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!camera_ || !backgroundFilter || !backgroundRenderer)
        {
            DestroyRenderer();
            return false;
        }

        cameraObject_->get_transform()->set_localPosition({0.0f, 0.0f, -10.0f});
        camera_->set_clearFlags(UnityEngine::CameraClearFlags::SolidColor);
        camera_->set_backgroundColor(UnityEngine::Color{0.002f, 0.004f, 0.01f, 1.0f});
        camera_->set_cullingMask(1 << PreviewLayer);
        camera_->set_fieldOfView(47.0f);
        camera_->set_targetTexture(renderTexture_);

        backgroundMesh_ = CreateQuadMesh(14.0f, 8.0f);
        backgroundTexture_ = CreateTexture(MakeReferenceStagePixels());
        sampleVideoTexture_ = CreateTexture(MakeSampleVideoPixels());
        auto unlit = UnityEngine::Shader::Find("Unlit/Texture");
        if(!backgroundMesh_ || !backgroundTexture_ || !sampleVideoTexture_ || !unlit)
        {
            DestroyRenderer();
            return false;
        }

        backgroundMaterial_ = UnityEngine::Material::New_ctor(unlit);
        backgroundMaterial_->set_mainTexture(backgroundTexture_);
        backgroundFilter->set_sharedMesh(backgroundMesh_);
        backgroundRenderer->set_material(backgroundMaterial_);
        backgroundObject_->get_transform()->set_localPosition({0.0f, 0.0f, 3.0f});

        previewImage_->set_texture(renderTexture_);
        return true;
    }

    void ScreenPreview::RebuildScreenMesh()
    {
        if(!screenObject_)
            return;

        if(screenMesh_)
            UnityEngine::Object::Destroy(screenMesh_);
        screenMesh_ = nullptr;

        const auto& settings = Settings::Instance();
        const float height = 4.0f * settings.ScreenScale();
        const float width = height * (16.0f / 9.0f);
        const float curvature = settings.CurvedScreenEnabled()
            ? settings.ScreenCurvature()
            : 0.0f;
        const int columns = ScreenSegments + 1;
        ArrayW<UnityEngine::Vector3> vertices(columns * 2);
        ArrayW<UnityEngine::Vector2> uvs(columns * 2);
        ArrayW<std::int32_t> triangles(ScreenSegments * 6);

        for(int column = 0; column < columns; ++column)
        {
            const float u = column / static_cast<float>(ScreenSegments);
            const float x = (u - 0.5f) * width;
            const float centered = u * 2.0f - 1.0f;
            const float edgeShape = 1.0f -
                std::cos(std::abs(centered) * Pi * 0.5f);
            const float z = -curvature * width * 0.12f * edgeShape;
            const int bottom = column * 2;
            vertices[bottom] = {x, -height * 0.5f, z};
            vertices[bottom + 1] = {x, height * 0.5f, z};
            uvs[bottom] = {u, 0.0f};
            uvs[bottom + 1] = {u, 1.0f};
        }

        for(int segment = 0; segment < ScreenSegments; ++segment)
        {
            const int leftBottom = segment * 2;
            const int output = segment * 6;
            triangles[output + 0] = leftBottom;
            triangles[output + 1] = leftBottom + 1;
            triangles[output + 2] = leftBottom + 3;
            triangles[output + 3] = leftBottom;
            triangles[output + 4] = leftBottom + 3;
            triangles[output + 5] = leftBottom + 2;
        }

        screenMesh_ = UnityEngine::Mesh::New_ctor();
        screenMesh_->set_vertices(vertices);
        screenMesh_->set_uv(uvs);
        screenMesh_->set_triangles(triangles);
        screenMesh_->RecalculateNormals();
        screenMesh_->RecalculateBounds();
        auto* filter = screenObject_->GetComponent<UnityEngine::MeshFilter*>();
        if(filter)
            filter->set_sharedMesh(screenMesh_);

        // Compress the large gameplay distance range into the preview camera's
        // useful depth while preserving direction: negative remains closer and
        // positive remains farther away.
        screenObject_->get_transform()->set_localPosition({
            0.0f,
            0.0f,
            settings.ScreenDistanceOffset() * 0.035f
        });
    }

    void ScreenPreview::RebuildScreenMaterial()
    {
        if(!screenObject_ || !sampleVideoTexture_)
            return;
        if(screenMaterial_)
            UnityEngine::Object::Destroy(screenMaterial_);
        screenMaterial_ = nullptr;

        const bool transparent = Settings::Instance().TransparencyEnabled();
        auto shader = UnityEngine::Shader::Find(
            transparent ? "Unlit/Transparent" : "Unlit/Texture");
        if(!shader)
            shader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!shader)
            return;

        screenMaterial_ = UnityEngine::Material::New_ctor(shader);
        screenMaterial_->set_mainTexture(sampleVideoTexture_);
        if(transparent)
            ConfigureTransparency(screenMaterial_);
        auto* renderer = screenObject_->GetComponent<UnityEngine::MeshRenderer*>();
        if(renderer)
            renderer->set_material(screenMaterial_);
    }

    void ScreenPreview::UpdateStatusText()
    {
        if(!statusText_)
            return;
        const auto& settings = Settings::Instance();
        const std::string shape = settings.CurvedScreenEnabled()
            ? "Curved " + std::to_string(settings.ScreenCurvature())
            : "Flat";
        statusText_->set_text(
            shape + "  |  " +
            std::to_string(settings.ScreenScale()) + "x  |  " +
            std::to_string(settings.ResolutionHeight()) + "p");
    }

    void ScreenPreview::DestroyRenderer()
    {
        if(previewImage_)
            previewImage_->set_texture(nullptr);
        if(camera_)
            camera_->set_targetTexture(nullptr);
        if(renderTexture_)
            renderTexture_->Release();
        if(previewRoot_)
            UnityEngine::Object::Destroy(previewRoot_);
        if(screenMesh_)
            UnityEngine::Object::Destroy(screenMesh_);
        if(backgroundMesh_)
            UnityEngine::Object::Destroy(backgroundMesh_);
        if(screenMaterial_)
            UnityEngine::Object::Destroy(screenMaterial_);
        if(backgroundMaterial_)
            UnityEngine::Object::Destroy(backgroundMaterial_);
        if(sampleVideoTexture_)
            UnityEngine::Object::Destroy(sampleVideoTexture_);
        if(backgroundTexture_)
            UnityEngine::Object::Destroy(backgroundTexture_);
        if(renderTexture_)
            UnityEngine::Object::Destroy(renderTexture_);

        previewRoot_ = nullptr;
        cameraObject_ = nullptr;
        backgroundObject_ = nullptr;
        screenObject_ = nullptr;
        camera_ = nullptr;
        renderTexture_ = nullptr;
        backgroundMesh_ = nullptr;
        screenMesh_ = nullptr;
        backgroundMaterial_ = nullptr;
        screenMaterial_ = nullptr;
        backgroundTexture_ = nullptr;
        sampleVideoTexture_ = nullptr;
    }
}
