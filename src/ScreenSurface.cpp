#include "BigScreen/ScreenSurface.hpp"

#include <cmath>
#include <cstdint>

#include "BigScreen/FrameDecoder.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshPro.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "bsml/shared/Helpers/getters.hpp"
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

        float CurveEdgeShape(float u)
        {
            const float centered = u * 2.0f - 1.0f;
            return 1.0f - std::cos(std::abs(centered) * Pi * 0.5f);
        }

        float CurvedWidthScale(float curvature, int segments)
        {
            // The screen curve is scale-invariant: for a unit-wide surface,
            // both X and Z scale linearly with the final width. Measure that
            // unit curve using the same segments as the actual mesh. Dividing
            // projected width by this path-length multiplier makes the curved
            // surface's total image width equal the original flat width.
            float pathLength = 0.0f;
            float previousX = -0.5f;
            float previousZ = -curvature * 0.12f * CurveEdgeShape(0.0f);
            for(int segment = 1; segment <= segments; ++segment)
            {
                const float u = static_cast<float>(segment) / segments;
                const float x = u - 0.5f;
                const float z = -curvature * 0.12f * CurveEdgeShape(u);
                const float dx = x - previousX;
                const float dz = z - previousZ;
                pathLength += std::sqrt(dx * dx + dz * dz);
                previousX = x;
                previousZ = z;
            }
            return pathLength > 0.0001f ? 1.0f / pathLength : 1.0f;
        }
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
        const float flatWidth = config.screenHeight * aspectRatio;
        screenWidth_ = config.maintainAspectRatioWhenCurved &&
                       std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(config.screenCurvature, config.screenSegments)
            : flatWidth;
        screenHeight_ = config.screenHeight;
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

        // A screen starts hidden so an uninitialized gray/black texture is not
        // exposed during a negative mapper offset or while FFmpeg seeks to the
        // opening frame. Do this directly: visible_ already starts false, so
        // routing the initial transition through SetVisible(false) would be
        // treated as a no-op even though a new GameObject is active by default.
        gameObject_->SetActive(false);
        visible_ = false;
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

        const float flatWidth = config.screenHeight * aspectRatio;
        const float width = config.maintainAspectRatioWhenCurved &&
                            std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(
                config.screenCurvature,
                config.screenSegments)
            : flatWidth;
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
            const float edgeShape = CurveEdgeShape(u);
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
        //
        // Configure both modes completely rather than relying on shader
        // defaults. In particular, the opaque mode must write to the depth
        // buffer so scenery and light geometry physically behind the screen
        // cannot be drawn through it.
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
        transparent_ = transparent;
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
            material_->DisableKeyword("_ALPHAPREMULTIPLY_ON");
            material_->set_renderQueue(3000);
        }
        else
        {
            material_->set_color(UnityEngine::Color::get_white());
            material_->SetInt("_SrcBlend", 1); // One
            material_->SetInt("_DstBlend", 0); // Zero
            material_->SetInt("_ZWrite", 1);
            material_->DisableKeyword("_ALPHATEST_ON");
            material_->DisableKeyword("_ALPHABLEND_ON");
            material_->DisableKeyword("_ALPHAPREMULTIPLY_ON");
            material_->set_renderQueue(2000);
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

        // A black lead-in temporarily replaces the material's texture and
        // opacity. Restore the actual video appearance before publishing the
        // first non-negative frame.
        material_->set_mainTexture(texture_);
        material_->set_color(transparent_
            ? UnityEngine::Color{1.0f, 1.0f, 1.0f, 0.75f}
            : UnityEngine::Color::get_white());

        // LoadRawTextureData copies into Unity's CPU-side texture buffer. Apply
        // performs the GPU upload on the main thread, as required by Unity.
        texture_->LoadRawTextureData(
            System::IntPtr(const_cast<std::uint8_t*>(frame.rgba.data())),
            static_cast<std::int32_t>(frame.rgba.size()));
        texture_->Apply(false, false);
        return true;
    }

    void ScreenSurface::ShowLeadIn(bool black)
    {
        if(!black)
        {
            SetVisible(false);
            return;
        }
        if(!material_)
            return;

        // Unity owns this shared 2x2 texture, so it costs no per-video upload
        // or allocation and must not be destroyed with the screen surface.
        material_->set_mainTexture(UnityEngine::Texture2D::get_blackTexture());
        material_->set_color(UnityEngine::Color::get_white());
        SetVisible(true);
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
        if(diagnosticsObject_)
            UnityEngine::Object::Destroy(diagnosticsObject_);
        diagnosticsObject_ = nullptr;
        diagnosticsText_ = nullptr;
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
        screenWidth_ = 0.0f;
        screenHeight_ = 0.0f;
        transparent_ = false;
        visible_ = false;
    }

    void ScreenSurface::SetDiagnosticsText(const std::string& text)
    {
        if(text.empty())
        {
            if(diagnosticsObject_)
                UnityEngine::Object::Destroy(diagnosticsObject_);
            diagnosticsObject_ = nullptr;
            diagnosticsText_ = nullptr;
            return;
        }
        if(!gameObject_)
            return;
        if(!diagnosticsObject_)
        {
            diagnosticsObject_ = UnityEngine::GameObject::New_ctor(
                "Big Screen Performance Information");
            diagnosticsObject_->set_layer(gameObject_->get_layer());
            auto transform = diagnosticsObject_->get_transform();
            transform->SetParent(gameObject_->get_transform(), false);
            transform->set_localPosition({
                -screenWidth_ * 0.48f,
                -screenHeight_ * 0.62f,
                -0.05f});
            transform->set_localEulerAngles({0.0f, 0.0f, 0.0f});
            transform->set_localScale({0.08f, 0.08f, 0.08f});
            diagnosticsText_ = diagnosticsObject_->AddComponent<TMPro::TextMeshPro*>();
            if(!diagnosticsText_)
            {
                UnityEngine::Object::Destroy(diagnosticsObject_);
                diagnosticsObject_ = nullptr;
                return;
            }
            diagnosticsText_->set_font(BSML::Helpers::GetMainTextFont());
            diagnosticsText_->set_fontSize(4.0f);
            diagnosticsText_->set_alignment(TMPro::TextAlignmentOptions::BottomLeft);
            diagnosticsText_->set_color(UnityEngine::Color::get_white());
            diagnosticsText_->get_rectTransform()->set_sizeDelta({
                screenWidth_ * 11.5f,
                screenHeight_ * 3.0f});
        }
        diagnosticsText_->set_text(text);
    }
}
