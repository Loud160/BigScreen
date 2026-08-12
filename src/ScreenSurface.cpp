#include "BigScreen/ScreenSurface.hpp"

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>

#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshPro.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/LayerMask.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Quaternion.hpp"
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

        struct ContentVertex {
            float x;
            float y;
            float z;
            float u;
            float v;
        };

        ContentVertex Interpolate(
            const ContentVertex& left,
            const ContentVertex& right,
            float amount)
        {
            return {
                left.x + (right.x - left.x) * amount,
                left.y + (right.y - left.y) * amount,
                left.z + (right.z - left.z) * amount,
                left.u + (right.u - left.u) * amount,
                left.v + (right.v - left.v) * amount};
        }

        template<typename Inside, typename Intersection>
        std::vector<ContentVertex> ClipEdge(
            const std::vector<ContentVertex>& input,
            Inside inside,
            Intersection intersection)
        {
            std::vector<ContentVertex> output;
            if(input.empty())
                return output;
            ContentVertex previous = input.back();
            bool previousInside = inside(previous);
            for(const auto& current : input)
            {
                const bool currentInside = inside(current);
                if(currentInside != previousInside)
                    output.push_back(intersection(previous, current));
                if(currentInside)
                    output.push_back(current);
                previous = current;
                previousInside = currentInside;
            }
            return output;
        }

        std::vector<ContentVertex> ClipToFrame(
            std::vector<ContentVertex> polygon,
            float halfWidth,
            float halfHeight)
        {
            const auto verticalIntersection = [](float boundary)
            {
                return [boundary](const ContentVertex& a, const ContentVertex& b)
                {
                    const float denominator = b.x - a.x;
                    const float amount = std::abs(denominator) < 0.000001f
                        ? 0.0f : (boundary - a.x) / denominator;
                    auto result = Interpolate(a, b, amount);
                    result.x = boundary;
                    return result;
                };
            };
            const auto horizontalIntersection = [](float boundary)
            {
                return [boundary](const ContentVertex& a, const ContentVertex& b)
                {
                    const float denominator = b.y - a.y;
                    const float amount = std::abs(denominator) < 0.000001f
                        ? 0.0f : (boundary - a.y) / denominator;
                    auto result = Interpolate(a, b, amount);
                    result.y = boundary;
                    return result;
                };
            };
            polygon = ClipEdge(polygon,
                [halfWidth](const ContentVertex& v) { return v.x >= -halfWidth; },
                verticalIntersection(-halfWidth));
            polygon = ClipEdge(polygon,
                [halfWidth](const ContentVertex& v) { return v.x <= halfWidth; },
                verticalIntersection(halfWidth));
            polygon = ClipEdge(polygon,
                [halfHeight](const ContentVertex& v) { return v.y >= -halfHeight; },
                horizontalIntersection(-halfHeight));
            return ClipEdge(polygon,
                [halfHeight](const ContentVertex& v) { return v.y <= halfHeight; },
                horizontalIntersection(halfHeight));
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

        // PC Cinema and Chroma maps conventionally target this exact root name
        // (often with the regex CinemaScreen$). Keeping the compatible name is
        // harmless for ordinary maps and lets later scene integrations find
        // Big Screen's surface without a Quest-specific map variant.
        gameObject_ = UnityEngine::GameObject::New_ctor("CinemaScreen");
        if(!gameObject_)
            return false;

        // Quest Chroma intentionally indexes only objects belonging to an
        // environment scene. A newly constructed object otherwise lands in
        // GameCore and is invisible to Chroma's CinemaScreen$ lookup. Move the
        // root into the loaded environment scene before Chroma's delayed scan;
        // no parenting is needed, so the mapper's world coordinates stay exact.
        if(auto environmentRoot = UnityEngine::GameObject::Find("/Environment"))
        {
            UnityEngine::SceneManagement::SceneManager::MoveGameObjectToScene(
                gameObject_, environmentRoot->get_scene());
        }

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
        const float flatWidth = config.screenWidthOverride.value_or(
            config.screenHeight * aspectRatio);
        screenWidth_ = config.maintainAspectRatioWhenCurved &&
                       std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(config.screenCurvature, config.screenSegments)
            : flatWidth;
        screenHeight_ = config.screenHeight;
        if(!CreateMesh(config, aspectRatio) ||
           !CreateVideoMesh(config, aspectRatio) ||
           !CreateMaterialAndTexture(videoWidth, videoHeight, config.transparent) ||
           !CreateBackgroundMaterial(config.transparent))
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
        renderer->set_material(backgroundMaterial_);

        videoObject_ = UnityEngine::GameObject::New_ctor("Big Screen Video Content");
        if(!videoObject_)
        {
            Destroy();
            return false;
        }
        videoObject_->set_layer(gameObject_->get_layer());
        videoObject_->get_transform()->SetParent(gameObject_->get_transform(), false);
        auto* videoFilter = videoObject_->AddComponent<UnityEngine::MeshFilter*>();
        auto* videoRenderer = videoObject_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!videoFilter || !videoRenderer)
        {
            Destroy();
            return false;
        }
        videoFilter->set_sharedMesh(videoMesh_);
        videoRenderer->set_material(material_);

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

        const float flatWidth = config.screenWidthOverride.value_or(
            config.screenHeight * aspectRatio);
        const float width = config.maintainAspectRatioWhenCurved &&
                            std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(
                config.screenCurvature,
                config.screenSegments)
            : flatWidth;
        const float halfWidth = width * 0.5f;
        const float halfHeight = config.screenHeight * 0.5f;

        const bool cinemaCurve = config.cinemaCurvatureDegrees.has_value();
        const float cinemaArcRadians = cinemaCurve
            ? *config.cinemaCurvatureDegrees * Pi / 180.0f : 0.0f;
        const bool cinemaCurveIsFlat =
            !cinemaCurve || std::abs(cinemaArcRadians) < 0.000001f;
        const float cinemaCurveLength = config.cinemaCurveYAxis
            ? config.screenHeight : width;
        const float cinemaRadius = cinemaCurveIsFlat
            ? 0.0f : cinemaCurveLength / cinemaArcRadians;

        for(int column = 0; column < columns; ++column)
        {
            const float u = static_cast<float>(column) / config.screenSegments;
            const int bottom = column * 2;
            const int top = bottom + 1;

            if(cinemaCurve && !cinemaCurveIsFlat)
            {
                // Cinema defines curvature as an arc angle, not a bow-depth
                // multiplier. Reproduce its circular surface mathematically so
                // mapper values have the same physical size on PC and Quest.
                const float theta = (u - 0.5f) * cinemaArcRadians;
                const float curvedAxis = std::sin(theta) * cinemaRadius;
                const float z = std::cos(theta) * cinemaRadius - cinemaRadius;
                if(config.cinemaCurveYAxis)
                {
                    // Reverse left/right storage to retain the same clockwise
                    // front-face winding used by the horizontal mesh.
                    vertices[bottom] = {halfWidth, curvedAxis, z};
                    vertices[top] = {-halfWidth, curvedAxis, z};
                    uvs[bottom] = {1.0f, 1.0f - u};
                    uvs[top] = {0.0f, 1.0f - u};
                }
                else
                {
                    vertices[bottom] = {curvedAxis, -halfHeight, z};
                    vertices[top] = {curvedAxis, halfHeight, z};
                    uvs[bottom] = {u, 1.0f};
                    uvs[top] = {u, 0.0f};
                }
            }
            else
            {
                const float x = (u - 0.5f) * width;
                // Big Screen layouts retain their signed bow model. An
                // explicit Cinema curvature of zero reaches this path with a
                // flat surface and is never replaced by the user's curve.
                const float edgeShape = CurveEdgeShape(u);
                const float z = cinemaCurve
                    ? 0.0f
                    : -config.screenCurvature * width * 0.12f * edgeShape;
                vertices[bottom] = {x, -halfHeight, z};
                vertices[top] = {x, halfHeight, z};
                // swscale writes rows from the top of the decoded image while
                // Unity's texture UV origin is at the bottom, so V is inverted.
                uvs[bottom] = {u, 1.0f};
                uvs[top] = {u, 0.0f};
            }
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

    bool ScreenSurface::CreateVideoMesh(
        const MapVideoConfig& config,
        float sourceAspectRatio)
    {
        const float flatFrameWidth = config.screenWidthOverride.value_or(
            config.screenHeight * sourceAspectRatio);
        const float frameWidth = config.maintainAspectRatioWhenCurved &&
                                 std::abs(config.screenCurvature) > 0.0001f
            ? flatFrameWidth * CurvedWidthScale(
                config.screenCurvature, config.screenSegments)
            : flatFrameWidth;
        const float frameHeight = config.screenHeight;
        const auto content = CoreLogic::FitVideoContent(
            frameWidth, frameHeight, sourceAspectRatio,
            config.stretchVideoToFit, config.videoZoom);
        const float contentWidth = content.width;
        const float contentHeight = content.height;

        const float rotation = config.videoRotation * Pi / 180.0f;
        const float tilt = config.videoTilt * Pi / 180.0f;
        const float rotationCos = std::cos(rotation);
        const float rotationSin = std::sin(rotation);
        const float tiltCos = std::cos(tilt);
        const float tiltSin = std::sin(tilt);
        const float panX = config.videoOffsetX * frameWidth * 0.5f;
        const float panY = config.videoOffsetY * frameHeight * 0.5f;

        const int columns = std::max(8, config.screenSegments);
        const int rows = 8;
        const auto transformed = [&](float u, float v)
        {
            const float originalX = (u - 0.5f) * contentWidth;
            const float originalY = (v - 0.5f) * contentHeight;
            const float tiltedY = originalY * tiltCos;
            // Pivot around the farther horizontal edge instead of the center.
            // Both edges therefore remain on or in front of the opaque frame
            // background. A center-pivoted plane placed half its vertices
            // behind that depth-writing layer, which made one half of a
            // strongly tilted video disappear even though the mesh was valid.
            const float normalizedY = std::clamp(
                originalY / std::max(contentHeight, 0.0001f) + 0.5f,
                0.0f, 1.0f);
            const float tiltDepth = std::abs(tiltSin) * contentHeight;
            const float tiltedZ = tiltSin >= 0.0f
                ? -normalizedY * tiltDepth
                : -(1.0f - normalizedY) * tiltDepth;
            return ContentVertex{
                originalX * rotationCos - tiltedY * rotationSin + panX,
                originalX * rotationSin + tiltedY * rotationCos + panY,
                tiltedZ,
                u,
                1.0f - v};
        };

        std::vector<UnityEngine::Vector3> vertices;
        std::vector<UnityEngine::Vector2> uvs;
        std::vector<std::int32_t> triangles;
        vertices.reserve(columns * rows * 12);
        uvs.reserve(columns * rows * 12);
        triangles.reserve(columns * rows * 12);

        const bool cinemaCurve = config.cinemaCurvatureDegrees.has_value();
        const float cinemaArcRadians = cinemaCurve
            ? *config.cinemaCurvatureDegrees * Pi / 180.0f : 0.0f;
        const bool cinemaCurveIsFlat =
            !cinemaCurve || std::abs(cinemaArcRadians) < 0.000001f;
        const float cinemaCurveLength = config.cinemaCurveYAxis
            ? frameHeight : frameWidth;
        const float cinemaRadius = cinemaCurveIsFlat
            ? 0.0f : cinemaCurveLength / cinemaArcRadians;

        const auto mapToSurface = [&](const ContentVertex& value)
        {
            constexpr float VideoLayerOffset = -0.015f;
            if(cinemaCurve && !cinemaCurveIsFlat)
            {
                if(config.cinemaCurveYAxis)
                {
                    const float theta = value.y / frameHeight * cinemaArcRadians;
                    return UnityEngine::Vector3{
                        value.x,
                        std::sin(theta) * cinemaRadius,
                        std::cos(theta) * cinemaRadius - cinemaRadius +
                            value.z + VideoLayerOffset};
                }
                const float theta = value.x / frameWidth * cinemaArcRadians;
                return UnityEngine::Vector3{
                    std::sin(theta) * cinemaRadius,
                    value.y,
                    std::cos(theta) * cinemaRadius - cinemaRadius +
                        value.z + VideoLayerOffset};
            }

            const float normalizedX = std::clamp(
                value.x / frameWidth + 0.5f, 0.0f, 1.0f);
            const float curveZ = cinemaCurve
                ? 0.0f
                : -config.screenCurvature * frameWidth * 0.12f *
                    CurveEdgeShape(normalizedX);
            return UnityEngine::Vector3{
                value.x,
                value.y,
                curveZ + value.z + VideoLayerOffset};
        };

        const auto appendTriangle = [&](const ContentVertex& a,
                                        const ContentVertex& b,
                                        const ContentVertex& c)
        {
            auto polygon = ClipToFrame(
                {a, b, c}, frameWidth * 0.5f, frameHeight * 0.5f);
            if(polygon.size() < 3)
                return;
            const auto base = static_cast<std::int32_t>(vertices.size());
            for(const auto& value : polygon)
            {
                vertices.push_back(mapToSurface(value));
                uvs.push_back({value.u, value.v});
            }
            for(std::size_t index = 1; index + 1 < polygon.size(); ++index)
            {
                triangles.push_back(base);
                triangles.push_back(base + static_cast<std::int32_t>(index));
                triangles.push_back(base + static_cast<std::int32_t>(index + 1));
            }
        };

        for(int row = 0; row < rows; ++row)
        {
            const float v0 = static_cast<float>(row) / rows;
            const float v1 = static_cast<float>(row + 1) / rows;
            for(int column = 0; column < columns; ++column)
            {
                const float u0 = static_cast<float>(column) / columns;
                const float u1 = static_cast<float>(column + 1) / columns;
                const auto bottomLeft = transformed(u0, v0);
                const auto topLeft = transformed(u0, v1);
                const auto topRight = transformed(u1, v1);
                const auto bottomRight = transformed(u1, v0);
                appendTriangle(bottomLeft, topLeft, topRight);
                appendTriangle(bottomLeft, topRight, bottomRight);
            }
        }

        if(vertices.empty())
            return false;
        ArrayW<UnityEngine::Vector3> unityVertices(vertices.size());
        ArrayW<UnityEngine::Vector2> unityUvs(uvs.size());
        ArrayW<std::int32_t> unityTriangles(triangles.size());
        std::copy(vertices.begin(), vertices.end(), unityVertices.begin());
        std::copy(uvs.begin(), uvs.end(), unityUvs.begin());
        std::copy(triangles.begin(), triangles.end(), unityTriangles.begin());

        videoMesh_ = UnityEngine::Mesh::New_ctor();
        if(!videoMesh_)
            return false;
        videoMesh_->set_vertices(unityVertices);
        videoMesh_->set_uv(unityUvs);
        videoMesh_->set_triangles(unityTriangles);
        videoMesh_->RecalculateNormals();
        videoMesh_->RecalculateBounds();
        return true;
    }

    bool ScreenSurface::UpdateGeometry(const MapVideoConfig& config)
    {
        if(!gameObject_ || !videoObject_ ||
           textureWidth_ <= 0 || textureHeight_ <= 0)
            return false;

        auto* filter = gameObject_->GetComponent<UnityEngine::MeshFilter*>();
        auto* videoFilter = videoObject_->GetComponent<UnityEngine::MeshFilter*>();
        if(!filter || !videoFilter)
            return false;

        const float aspectRatio =
            static_cast<float>(textureWidth_) / textureHeight_;
        auto* previousMesh = mesh_;
        auto* previousVideoMesh = videoMesh_;
        mesh_ = nullptr;
        videoMesh_ = nullptr;
        if(!CreateMesh(config, aspectRatio) || !mesh_ ||
           !CreateVideoMesh(config, aspectRatio) || !videoMesh_)
        {
            if(mesh_)
                UnityEngine::Object::Destroy(mesh_);
            if(videoMesh_)
                UnityEngine::Object::Destroy(videoMesh_);
            mesh_ = previousMesh;
            videoMesh_ = previousVideoMesh;
            return false;
        }

        // Publish the complete replacement only after mesh creation succeeds.
        // The previous surface and decoded frame remain visible throughout the
        // operation, preventing the gray flash caused by recreating a screen.
        filter->set_sharedMesh(mesh_);
        videoFilter->set_sharedMesh(videoMesh_);
        if(previousMesh)
            UnityEngine::Object::Destroy(previousMesh);
        if(previousVideoMesh)
            UnityEngine::Object::Destroy(previousVideoMesh);

        auto transform = gameObject_->get_transform();
        transform->set_position({
            config.screenPosition.x,
            config.screenPosition.y,
            config.screenPosition.z});
        transform->set_eulerAngles({
            config.screenRotation.x,
            config.screenRotation.y,
            config.screenRotation.z});

        const float flatWidth = config.screenWidthOverride.value_or(
            config.screenHeight * aspectRatio);
        screenWidth_ = config.maintainAspectRatioWhenCurved &&
                       std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(
                config.screenCurvature,
                config.screenSegments)
            : flatWidth;
        screenHeight_ = config.screenHeight;

        // Diagnostics is parented to the screen. Reposition its lower-left
        // anchor after a live size change without changing the text itself.
        if(diagnosticsObject_)
        {
            diagnosticsObject_->get_transform()->set_localPosition({
                -screenWidth_ * 0.48f,
                -screenHeight_ * 0.62f,
                -0.05f});
            if(diagnosticsText_)
                diagnosticsText_->get_rectTransform()->set_sizeDelta({
                    screenWidth_ * 11.5f,
                    screenHeight_ * 3.0f});
        }
        return true;
    }

    bool ScreenSurface::CreateBackgroundMaterial(bool transparent)
    {
        // The background is an independent surface rather than blank pixels in
        // the decoded image. This lets rotation, zoom, and pan expose either a
        // solid black letterbox or a genuinely transparent opening without
        // modifying a frame on the CPU for every presentation.
        auto shader = UnityEngine::Shader::Find(
            transparent ? "Unlit/Transparent" : "Unlit/Texture");
        if(!shader && transparent)
            shader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!shader)
            return false;

        backgroundMaterial_ = UnityEngine::Material::New_ctor(shader);
        if(!backgroundMaterial_)
            return false;
        backgroundMaterial_->set_mainTexture(
            UnityEngine::Texture2D::get_blackTexture());
        if(transparent)
        {
            backgroundMaterial_->set_color(
                UnityEngine::Color{0.0f, 0.0f, 0.0f, 0.0f});
            backgroundMaterial_->SetInt("_SrcBlend", 5);  // SrcAlpha
            backgroundMaterial_->SetInt("_DstBlend", 10); // OneMinusSrcAlpha
            backgroundMaterial_->SetInt("_ZWrite", 0);
            backgroundMaterial_->EnableKeyword("_ALPHABLEND_ON");
            backgroundMaterial_->set_renderQueue(2999);
        }
        else
        {
            backgroundMaterial_->set_color(UnityEngine::Color::get_black());
            backgroundMaterial_->SetInt("_SrcBlend", 1); // One
            backgroundMaterial_->SetInt("_DstBlend", 0); // Zero
            backgroundMaterial_->SetInt("_ZWrite", 1);
            backgroundMaterial_->DisableKeyword("_ALPHABLEND_ON");
            backgroundMaterial_->set_renderQueue(1999);
        }
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

        // Only a lead-in changes these invariant material properties. Avoid
        // sending identical texture/color updates through IL2CPP every frame.
        if(leadInActive_)
        {
            material_->set_mainTexture(texture_);
            material_->set_color(transparent_
                ? UnityEngine::Color{1.0f, 1.0f, 1.0f, 0.75f}
                : UnityEngine::Color::get_white());
            if(backgroundMaterial_)
                backgroundMaterial_->set_color(transparent_
                    ? UnityEngine::Color{0.0f, 0.0f, 0.0f, 0.0f}
                    : UnityEngine::Color::get_black());
            leadInActive_ = false;
            leadInBlack_ = false;
        }

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
            leadInActive_ = true;
            leadInBlack_ = false;
            SetVisible(false);
            return;
        }
        if(!material_)
            return;

        if(leadInActive_ && leadInBlack_)
        {
            SetVisible(true);
            return;
        }

        // Unity owns this shared 2x2 texture, so it costs no per-video upload
        // or allocation and must not be destroyed with the screen surface.
        material_->set_mainTexture(UnityEngine::Texture2D::get_blackTexture());
        material_->set_color(UnityEngine::Color::get_white());
        // Lead-In Background describes the complete frame, not just the
        // transformed/cropped video polygon. Make the independent letterbox
        // layer opaque until Upload restores its configured transparency.
        if(backgroundMaterial_)
            backgroundMaterial_->set_color(UnityEngine::Color::get_black());
        leadInActive_ = true;
        leadInBlack_ = true;
        SetVisible(true);
    }

    void ScreenSurface::SetVisible(bool visible)
    {
        if(!gameObject_ || visible_ == visible)
            return;
        gameObject_->SetActive(visible);
        visible_ = visible;
    }

    void ScreenSurface::SetWorldTransform(
        UnityEngine::Vector3 position,
        UnityEngine::Quaternion rotation)
    {
        if(gameObject_)
            gameObject_->get_transform()->SetPositionAndRotation(position, rotation);
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
        if(videoObject_)
            UnityEngine::Object::Destroy(videoObject_);
        if(gameObject_)
            UnityEngine::Object::Destroy(gameObject_);
        if(texture_)
            UnityEngine::Object::Destroy(texture_);
        if(material_)
            UnityEngine::Object::Destroy(material_);
        if(mesh_)
            UnityEngine::Object::Destroy(mesh_);
        if(videoMesh_)
            UnityEngine::Object::Destroy(videoMesh_);
        if(backgroundMaterial_)
            UnityEngine::Object::Destroy(backgroundMaterial_);

        gameObject_ = nullptr;
        videoObject_ = nullptr;
        mesh_ = nullptr;
        videoMesh_ = nullptr;
        material_ = nullptr;
        backgroundMaterial_ = nullptr;
        texture_ = nullptr;
        textureWidth_ = 0;
        textureHeight_ = 0;
        screenWidth_ = 0.0f;
        screenHeight_ = 0.0f;
        transparent_ = false;
        visible_ = false;
        leadInActive_ = false;
        leadInBlack_ = false;
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
