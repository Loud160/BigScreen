#include "BigScreen/ScreenSurface.hpp"

#include <cmath>
#include <cstdint>

#include "BigScreen/FrameDecoder.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/LayerMask.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "System/IntPtr.hpp"

namespace BigScreen {
    namespace {
        constexpr float Pi = 3.14159265358979323846f;
    }

    bool ScreenSurface::Create(
        const MapVideoConfig& config,
        int videoWidth,
        int videoHeight)
    {
        Destroy();
        if(videoWidth <= 0 || videoHeight <= 0)
            return false;

        gameObject_ = UnityEngine::GameObject::New_ctor("Big Screen Video Surface");
        if(!gameObject_)
            return false;

        // Match Beat Saber's environment geometry so the normal VR cameras see
        // the screen. NameToLayer avoids assuming Unity's numeric layer layout.
        const int environmentLayer = UnityEngine::LayerMask::NameToLayer("Environment");
        if(environmentLayer >= 0)
            gameObject_->set_layer(environmentLayer);

        auto transform = gameObject_->get_transform();
        transform->set_position({
            config.screenPosition.x,
            config.screenPosition.y,
            config.screenPosition.z
        });
        transform->set_eulerAngles({
            config.screenRotation.x,
            config.screenRotation.y,
            config.screenRotation.z
        });

        const float aspectRatio = static_cast<float>(videoWidth) / videoHeight;
        if(!CreateMesh(config, aspectRatio) ||
           !CreateMaterialAndTexture(videoWidth, videoHeight, config.transparent))
        {
            Destroy();
            return false;
        }

        auto* filter = gameObject_->AddComponent<UnityEngine::MeshFilter*>();
        auto* renderer = gameObject_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!filter || !renderer)
        {
            Destroy();
            return false;
        }

        filter->set_sharedMesh(mesh_);
        renderer->set_material(material_);

        // A screen starts hidden so a black rectangle is not shown during a
        // negative mapper offset or while FFmpeg seeks to the opening frame.
        SetVisible(false);
        return true;
    }

    bool ScreenSurface::CreateMesh(const MapVideoConfig& config, float aspectRatio)
    {
        const int columns = config.screenSegments + 1;
        const int vertexCount = columns * 2;
        const int indexCount = config.screenSegments * 6;

        ArrayW<UnityEngine::Vector3> vertices(vertexCount);
        ArrayW<UnityEngine::Vector2> uvs(vertexCount);
        ArrayW<std::int32_t> triangles(indexCount);

        const float width = config.screenHeight * aspectRatio;
        const float halfWidth = width * 0.5f;
        const float halfHeight = config.screenHeight * 0.5f;

        for(int column = 0; column < columns; ++column)
        {
            const float u = static_cast<float>(column) / config.screenSegments;
            const float x = (u - 0.5f) * width;

            // Curvature is deliberately expressed as a normalized bow amount:
            // zero is a plane and +/-1 moves the outer edges by 12% of width.
            // The center remains at the configured map position, making flat
            // and curved screens share predictable placement controls.
            const float centered = u * 2.0f - 1.0f;
            const float edgeShape = 1.0f - std::cos(std::abs(centered) * Pi * 0.5f);
            const float z = -config.screenCurvature * width * 0.12f * edgeShape;

            const int bottom = column * 2;
            const int top = bottom + 1;
            vertices[bottom] = {x, -halfHeight, z};
            vertices[top] = {x, halfHeight, z};

            // swscale writes rows from the top of the decoded image while
            // Unity's texture UV origin is at the bottom, so V is inverted.
            uvs[bottom] = {u, 1.0f};
            uvs[top] = {u, 0.0f};
        }

        for(int segment = 0; segment < config.screenSegments; ++segment)
        {
            const int leftBottom = segment * 2;
            const int leftTop = leftBottom + 1;
            const int rightBottom = leftBottom + 2;
            const int rightTop = leftBottom + 3;
            const int output = segment * 6;

            // Clockwise winding faces the player when the screen is positioned
            // in front of the gameplay origin and uses its default rotation.
            triangles[output + 0] = leftBottom;
            triangles[output + 1] = leftTop;
            triangles[output + 2] = rightTop;
            triangles[output + 3] = leftBottom;
            triangles[output + 4] = rightTop;
            triangles[output + 5] = rightBottom;
        }

        mesh_ = UnityEngine::Mesh::New_ctor();
        if(!mesh_)
            return false;
        mesh_->set_vertices(vertices);
        mesh_->set_uv(uvs);
        mesh_->set_triangles(triangles);
        mesh_->RecalculateNormals();
        mesh_->RecalculateBounds();
        return true;
    }

    bool ScreenSurface::CreateMaterialAndTexture(
        int width,
        int height,
        bool transparent)
    {
        // Unity's transparent unlit shader performs normal alpha blending, so
        // Beat Saber lights and background geometry remain visible through the
        // video without changing the decoded pixels. A fixed 75% opacity keeps
        // lyrics and motion readable while still revealing the light show.
        auto shader = UnityEngine::Shader::Find(
            transparent ? "Unlit/Transparent" : "Unlit/Texture");
        if(!shader && transparent)
            shader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!shader)
            return false;

        material_ = UnityEngine::Material::New_ctor(shader);
        texture_ = UnityEngine::Texture2D::New_ctor(
            width,
            height,
            UnityEngine::TextureFormat::RGBA32,
            false,
            false);
        if(!material_ || !texture_)
            return false;

        textureWidth_ = width;
        textureHeight_ = height;
        material_->set_mainTexture(texture_);
        if(transparent)
        {
            material_->set_color(UnityEngine::Color{1.0f, 1.0f, 1.0f, 0.75f});
            // These standard Unity blend properties also make the fallback
            // Unlit/Texture material transparent on builds where the named
            // Unlit/Transparent shader was stripped from the player.
            material_->SetInt("_SrcBlend", 5);  // SrcAlpha
            material_->SetInt("_DstBlend", 10); // OneMinusSrcAlpha
            material_->SetInt("_ZWrite", 0);
            material_->DisableKeyword("_ALPHATEST_ON");
            material_->EnableKeyword("_ALPHABLEND_ON");
            material_->set_renderQueue(3000);
        }
        return true;
    }

    bool ScreenSurface::Upload(const VideoFrame& frame)
    {
        if(!texture_ ||
           frame.width != textureWidth_ ||
           frame.height != textureHeight_ ||
           frame.rgba.empty())
        {
            return false;
        }

        // LoadRawTextureData copies into Unity's CPU-side texture buffer. Apply
        // performs the GPU upload on the main thread, as required by Unity.
        texture_->LoadRawTextureData(
            System::IntPtr(const_cast<std::uint8_t*>(frame.rgba.data())),
            static_cast<std::int32_t>(frame.rgba.size()));
        texture_->Apply(false, false);
        return true;
    }

    void ScreenSurface::SetVisible(bool visible)
    {
        if(!gameObject_ || visible_ == visible)
            return;
        gameObject_->SetActive(visible);
        visible_ = visible;
    }

    void ScreenSurface::Destroy()
    {
        // Destroying the GameObject also destroys its MeshFilter and Renderer.
        // Mesh, material, and texture were created as standalone Unity objects,
        // so they are released explicitly when gameplay ends or a level changes.
        if(gameObject_)
            UnityEngine::Object::Destroy(gameObject_);
        if(texture_)
            UnityEngine::Object::Destroy(texture_);
        if(material_)
            UnityEngine::Object::Destroy(material_);
        if(mesh_)
            UnityEngine::Object::Destroy(mesh_);

        gameObject_ = nullptr;
        mesh_ = nullptr;
        material_ = nullptr;
        texture_ = nullptr;
        textureWidth_ = 0;
        textureHeight_ = 0;
        visible_ = false;
    }
}
