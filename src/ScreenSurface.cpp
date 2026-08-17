// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
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
#include "UnityEngine/Graphics.hpp"
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

        // Unity scene teardown leaves ordinary IL2CPP pointers non-null after
        // their managed objects have been destroyed (Unity's "fake null").
        // Every deferred cleanup path must therefore verify liveness before it
        // asks Unity to destroy an object again.
        template<class T>
        void DestroyIfAlive(T*& object)
        {
            if(UnityW<T>::isAlive(object))
                UnityEngine::Object::Destroy(object);
            object = nullptr;
        }

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

        std::uint32_t FractureHash(std::uint32_t seed, std::size_t shard)
        {
            std::uint32_t value = seed ^
                (static_cast<std::uint32_t>(shard) + 1U) * 0x9E3779B9U;
            value ^= value >> 16;
            value *= 0x7FEB352DU;
            value ^= value >> 15;
            value *= 0x846CA68BU;
            value ^= value >> 16;
            return value;
        }

        float HashSigned(std::uint32_t value)
        {
            return static_cast<float>((value >> 8) & 0x00FFFFFFU) /
                static_cast<float>(0x007FFFFFU) - 1.0f;
        }

        UnityEngine::Vector3 RotateFractureVertex(
            UnityEngine::Vector3 value,
            UnityEngine::Vector3 center,
            float xDegrees,
            float yDegrees,
            float zDegrees)
        {
            constexpr float DegreesToRadians = Pi / 180.0f;
            float x = value.x - center.x;
            float y = value.y - center.y;
            float z = value.z - center.z;
            const float cx = std::cos(xDegrees * DegreesToRadians);
            const float sx = std::sin(xDegrees * DegreesToRadians);
            const float cy = std::cos(yDegrees * DegreesToRadians);
            const float sy = std::sin(yDegrees * DegreesToRadians);
            const float cz = std::cos(zDegrees * DegreesToRadians);
            const float sz = std::sin(zDegrees * DegreesToRadians);
            const float firstY = y * cx - z * sx;
            const float firstZ = y * sx + z * cx;
            const float secondX = x * cy + firstZ * sy;
            const float secondZ = -x * sy + firstZ * cy;
            return {
                center.x + secondX * cz - firstY * sz,
                center.y + secondX * sz + firstY * cz,
                center.z + secondZ};
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
        return CreateInternal(
            config, videoWidth, videoHeight, nullptr, "CinemaScreen", false);
    }

    bool ScreenSurface::CreateShared(
        const MapVideoConfig& config,
        int videoWidth,
        int videoHeight,
        UnityEngine::Texture2D* sharedTexture,
        const char* rootName)
    {
        if(!sharedTexture)
            return false;
        return CreateInternal(
            config, videoWidth, videoHeight, sharedTexture, rootName, true);
    }

    bool ScreenSurface::CreateInternal(
        const MapVideoConfig& config,
        int videoWidth,
        int videoHeight,
        UnityEngine::Texture2D* sharedTexture,
        const char* rootName,
        bool prepareDeformation)
    {
        Destroy();
        if(videoWidth <= 0 || videoHeight <= 0 || !rootName)
            return false;
        prepareDeformation_ = prepareDeformation;

        // PC Cinema and Chroma maps conventionally target this exact root name
        // (often with the regex CinemaScreen$). Keeping the compatible name is
        // harmless for ordinary maps and lets later scene integrations find
        // Big Screen's surface without a Quest-specific map variant.
        gameObject_ = UnityEngine::GameObject::New_ctor(rootName);
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
        geometryConfig_ = config;
        geometryAspectRatio_ = aspectRatio;
        const float flatWidth = config.screenWidthOverride.value_or(
            config.screenHeight * aspectRatio);
        screenWidth_ = config.maintainAspectRatioWhenCurved &&
                       std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(config.screenCurvature, config.screenSegments)
            : flatWidth;
        screenHeight_ = config.screenHeight;
        if(!CreateMesh(config, aspectRatio) ||
           !CreateVideoMesh(config, aspectRatio) ||
           !CreateMaterialAndTexture(
               videoWidth, videoHeight, config.videoOpacity, sharedTexture) ||
           !CreateBackgroundMaterial(config.letterboxTransparent))
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
        // Do not rely on a zero-alpha black texture to represent an empty
        // letterbox. Some Unity shader variants still draw it as black on
        // Quest. Removing the background renderer makes the unused part of a
        // non-16:9 frame genuinely transparent.
        renderer->set_enabled(CoreLogic::ScreenBackgroundVisible(
            config.letterboxTransparent, false, videoCoversFrame_));

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

        // Do not render an opaque backing surface when this simple picture
        // already covers every point of the canvas. Besides avoiding needless
        // overdraw, this prevents large and distant menu previews from making
        // the black backing and picture fight for the same depth-buffer value.
        // Rotation, tilt, or pan can expose a corner, so those presentations
        // deliberately retain their configured letterbox background.
        constexpr float CoverageEpsilon = 0.0005f;
        const bool untransformed =
            std::abs(config.videoRotation) <= CoverageEpsilon &&
            std::abs(config.videoTilt) <= CoverageEpsilon &&
            std::abs(panX) <= CoverageEpsilon &&
            std::abs(panY) <= CoverageEpsilon;
        videoCoversFrame_ = untransformed &&
            contentWidth >= frameWidth - CoverageEpsilon &&
            contentHeight >= frameHeight - CoverageEpsilon;

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
        std::vector<DeformationBaseVertex> deformationVertices;
        vertices.reserve(columns * rows * 12);
        uvs.reserve(columns * rows * 12);
        triangles.reserve(columns * rows * 12);
        if(prepareDeformation_)
            deformationVertices.reserve(columns * rows * 12);

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
            const float videoLayerOffset =
                CoreLogic::VideoLayerOffset(frameWidth, frameHeight);
            if(cinemaCurve && !cinemaCurveIsFlat)
            {
                if(config.cinemaCurveYAxis)
                {
                    const float theta = value.y / frameHeight * cinemaArcRadians;
                    return UnityEngine::Vector3{
                        value.x,
                        std::sin(theta) * cinemaRadius,
                        std::cos(theta) * cinemaRadius - cinemaRadius +
                            value.z + videoLayerOffset};
                }
                const float theta = value.x / frameWidth * cinemaArcRadians;
                return UnityEngine::Vector3{
                    std::sin(theta) * cinemaRadius,
                    value.y,
                    std::cos(theta) * cinemaRadius - cinemaRadius +
                        value.z + videoLayerOffset};
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
                curveZ + value.z + videoLayerOffset};
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
                if(prepareDeformation_)
                {
                    deformationVertices.push_back({
                        value.x,
                        value.y,
                        value.z,
                        std::clamp(value.x / frameWidth + 0.5f, 0.0f, 1.0f),
                        std::clamp(value.y / frameHeight + 0.5f, 0.0f, 1.0f)});
                }
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

        // Only showcase clones retain managed vertex/UV arrays plus planar
        // coordinates. They are allocated with the mesh and reused for every
        // song-time or real-time sample; normal map screens pay no extra cost.
        if(prepareDeformation_)
        {
            deformationBaseVertices_ = std::move(deformationVertices);
            undeformedVideoVertices_ = unityVertices;
            dynamicVideoVertices_ = ArrayW<UnityEngine::Vector3>(vertices.size());
            std::copy(
                vertices.begin(), vertices.end(), dynamicVideoVertices_.begin());
            undeformedVideoUvs_ = unityUvs;
            dynamicVideoUvs_ = ArrayW<UnityEngine::Vector2>(uvs.size());
            std::copy(uvs.begin(), uvs.end(), dynamicVideoUvs_.begin());
        }
        deformationWasApplied_ = false;
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
        const bool previousVideoCoversFrame = videoCoversFrame_;
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
            videoCoversFrame_ = previousVideoCoversFrame;
            return false;
        }

        if((config.letterboxTransparent != letterboxTransparent_ ||
            std::abs(config.videoOpacity - opacity_) > 0.0001f) &&
           !ApplyPresentation(
               config.letterboxTransparent,
               config.videoOpacity))
        {
            UnityEngine::Object::Destroy(mesh_);
            UnityEngine::Object::Destroy(videoMesh_);
            mesh_ = previousMesh;
            videoMesh_ = previousVideoMesh;
            videoCoversFrame_ = previousVideoCoversFrame;
            return false;
        }

        // Publish the complete replacement only after mesh creation succeeds.
        // The previous surface and decoded frame remain visible throughout the
        // operation, preventing the gray flash caused by recreating a screen.
        filter->set_sharedMesh(mesh_);
        videoFilter->set_sharedMesh(videoMesh_);
        if(auto* backgroundRenderer =
               gameObject_->GetComponent<UnityEngine::MeshRenderer*>())
        {
            backgroundRenderer->set_enabled(
                CoreLogic::ScreenBackgroundVisible(
                    letterboxTransparent_,
                    leadInActive_ && leadInBlack_,
                    videoCoversFrame_));
        }
        fractureMeshActive_ = false;
        fractureShapeCaptured_ = false;
        fractureSnapshotActive_ = false;
        if(material_)
            material_->set_mainTexture(texture_);
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
        geometryConfig_ = config;
        geometryAspectRatio_ = aspectRatio;

        // Diagnostics is an overlay inside the lower part of the screen. Its
        // RectTransform is centered on X, so placing the object at the old
        // lower-left coordinate shifted half of the text rectangle outside
        // the canvas. Keep the entire rectangle within the visible mesh.
        if(diagnosticsObject_)
        {
            diagnosticsObject_->get_transform()->set_localPosition({
                0.0f,
                -screenHeight_ * 0.36f,
                -0.08f});
            if(diagnosticsText_)
                diagnosticsText_->get_rectTransform()->set_sizeDelta({
                    screenWidth_ * 11.5f,
                    screenHeight_ * 3.0f});
        }
        return true;
    }

    bool ScreenSurface::ApplyPresentation(
        bool letterboxTransparent,
        float videoOpacity)
    {
        if(!gameObject_ || !videoObject_ || !material_ ||
           !backgroundMaterial_ || !texture_)
            return false;

        const float nextOpacity = std::clamp(videoOpacity, 0.0f, 1.0f);
        const bool pictureTransparent = nextOpacity < 0.999f;
        auto videoShader = UnityEngine::Shader::Find(
            pictureTransparent ? "Unlit/Transparent" : "Unlit/Texture");
        auto backgroundShader = UnityEngine::Shader::Find(
            letterboxTransparent ? "Unlit/Transparent" : "Unlit/Texture");
        if(!videoShader && pictureTransparent)
            videoShader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!backgroundShader && letterboxTransparent)
            backgroundShader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!videoShader || !backgroundShader)
            return false;

        material_->set_shader(videoShader);
        backgroundMaterial_->set_shader(backgroundShader);
        material_->set_mainTexture(texture_);
        backgroundMaterial_->set_mainTexture(
            UnityEngine::Texture2D::get_blackTexture());

        if(pictureTransparent)
        {
            material_->set_color(
                UnityEngine::Color{1.0f, 1.0f, 1.0f, nextOpacity});
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

        if(letterboxTransparent)
        {
            backgroundMaterial_->set_color(
                UnityEngine::Color{0.0f, 0.0f, 0.0f, 0.0f});
            backgroundMaterial_->SetInt("_SrcBlend", 5);
            backgroundMaterial_->SetInt("_DstBlend", 10);
            backgroundMaterial_->SetInt("_ZWrite", 0);
            backgroundMaterial_->DisableKeyword("_ALPHATEST_ON");
            backgroundMaterial_->EnableKeyword("_ALPHABLEND_ON");
            backgroundMaterial_->DisableKeyword("_ALPHAPREMULTIPLY_ON");
            backgroundMaterial_->set_renderQueue(2999);
        }
        else
        {
            backgroundMaterial_->set_color(UnityEngine::Color::get_black());
            backgroundMaterial_->SetInt("_SrcBlend", 1);
            backgroundMaterial_->SetInt("_DstBlend", 0);
            backgroundMaterial_->SetInt("_ZWrite", 1);
            backgroundMaterial_->DisableKeyword("_ALPHATEST_ON");
            backgroundMaterial_->DisableKeyword("_ALPHABLEND_ON");
            backgroundMaterial_->DisableKeyword("_ALPHAPREMULTIPLY_ON");
            backgroundMaterial_->set_renderQueue(1999);
        }

        // Changing layouts during a negative offset must not expose the first
        // decoded frame early. Preserve an explicitly requested black lead-in
        // until Upload transitions back to the configured presentation.
        if(leadInActive_ && leadInBlack_)
        {
            material_->set_mainTexture(
                UnityEngine::Texture2D::get_blackTexture());
            material_->set_color(UnityEngine::Color::get_white());
            backgroundMaterial_->set_color(UnityEngine::Color::get_black());
        }

        letterboxTransparent_ = letterboxTransparent;
        opacity_ = nextOpacity;
        if(auto* backgroundRenderer =
               gameObject_->GetComponent<UnityEngine::MeshRenderer*>())
        {
            backgroundRenderer->set_enabled(
                CoreLogic::ScreenBackgroundVisible(
                    letterboxTransparent_,
                    leadInActive_ && leadInBlack_,
                    videoCoversFrame_));
        }
        return true;
    }

    bool ScreenSurface::CreateBackgroundMaterial(bool letterboxTransparent)
    {
        // The background is an independent surface rather than blank pixels in
        // the decoded image. This lets rotation, zoom, and pan expose either a
        // solid black letterbox or a genuinely transparent opening without
        // modifying a frame on the CPU for every presentation.
        auto shader = UnityEngine::Shader::Find(
            letterboxTransparent ? "Unlit/Transparent" : "Unlit/Texture");
        if(!shader && letterboxTransparent)
            shader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!shader)
            return false;

        backgroundMaterial_ = UnityEngine::Material::New_ctor(shader);
        if(!backgroundMaterial_)
            return false;
        backgroundMaterial_->set_mainTexture(
            UnityEngine::Texture2D::get_blackTexture());
        letterboxTransparent_ = letterboxTransparent;
        if(letterboxTransparent)
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
        float videoOpacity,
        UnityEngine::Texture2D* sharedTexture)
    {
        // Unity's transparent unlit shader performs normal alpha blending, so
        // Beat Saber lights and background geometry remain visible through the
        // video without changing the decoded pixels.
        //
        // Configure both modes completely rather than relying on shader
        // defaults. In particular, the opaque mode must write to the depth
        // buffer so scenery and light geometry physically behind the screen
        // cannot be drawn through it.
        const float nextOpacity = std::clamp(videoOpacity, 0.0f, 1.0f);
        const bool pictureTransparent = nextOpacity < 0.999f;
        auto shader = UnityEngine::Shader::Find(
            pictureTransparent ? "Unlit/Transparent" : "Unlit/Texture");
        if(!shader && pictureTransparent)
            shader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!shader)
            return false;

        material_ = UnityEngine::Material::New_ctor(shader);
        texture_ = sharedTexture;
        ownsTexture_ = sharedTexture == nullptr;
        if(ownsTexture_)
        {
            texture_ = UnityEngine::Texture2D::New_ctor(
                width,
                height,
                UnityEngine::TextureFormat::RGBA32,
                false,
                false);
        }
        if(!material_ || !texture_)
            return false;

        textureWidth_ = width;
        textureHeight_ = height;
        opacity_ = nextOpacity;
        material_->set_mainTexture(texture_);
        if(pictureTransparent)
        {
            material_->set_color(
                UnityEngine::Color{1.0f, 1.0f, 1.0f, opacity_});
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
        // Restarting a Beat Saber level destroys GameCore's Unity objects
        // without necessarily reaching Big Screen's normal Finish hook. A raw
        // Texture2D pointer can consequently remain non-null while referring
        // to an already-destroyed object. Never call LoadRawTextureData through
        // such a pointer: on IL2CPP that can jump through stale class metadata
        // and crash UnityMain rather than producing a managed exception.
        if(!UnityW<UnityEngine::GameObject>::isAlive(gameObject_) ||
           !UnityW<UnityEngine::Material>::isAlive(material_) ||
           !UnityW<UnityEngine::Texture2D>::isAlive(texture_) ||
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
            material_->set_color(
                UnityEngine::Color{1.0f, 1.0f, 1.0f, opacity_});
            if(UnityW<UnityEngine::Material>::isAlive(backgroundMaterial_))
                backgroundMaterial_->set_color(letterboxTransparent_
                    ? UnityEngine::Color{0.0f, 0.0f, 0.0f, 0.0f}
                    : UnityEngine::Color::get_black());
            if(auto* backgroundRenderer =
                   gameObject_->GetComponent<UnityEngine::MeshRenderer*>())
            {
                backgroundRenderer->set_enabled(
                    CoreLogic::ScreenBackgroundVisible(
                        letterboxTransparent_, false, videoCoversFrame_));
            }
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
        if(!UnityW<UnityEngine::GameObject>::isAlive(gameObject_) ||
           !UnityW<UnityEngine::Material>::isAlive(material_))
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
        if(UnityW<UnityEngine::Material>::isAlive(backgroundMaterial_))
            backgroundMaterial_->set_color(UnityEngine::Color::get_black());
        if(auto* backgroundRenderer =
               gameObject_->GetComponent<UnityEngine::MeshRenderer*>())
            backgroundRenderer->set_enabled(true);
        leadInActive_ = true;
        leadInBlack_ = true;
        SetVisible(true);
    }

    void ScreenSurface::SetVisible(bool visible)
    {
        if(!UnityW<UnityEngine::GameObject>::isAlive(gameObject_))
        {
            visible_ = false;
            return;
        }
        if(visible_ == visible)
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

    void ScreenSurface::SetWorldScale(UnityEngine::Vector3 scale)
    {
        if(gameObject_)
            gameObject_->get_transform()->set_localScale(scale);
    }

    void ScreenSurface::SetVideoLocalRoll(float degrees)
    {
        if(videoObject_)
            videoObject_->get_transform()->set_localEulerAngles({0.0f, 0.0f, degrees});
    }

    bool ScreenSurface::SetOpacity(float opacity)
    {
        if(!material_)
            return false;
        const float nextOpacity = std::clamp(opacity, 0.0f, 1.0f);
        // Most cues hold opacity steady for many seconds. Avoid repeating the
        // same IL2CPP material write on every Unity update for every panel.
        if(std::abs(nextOpacity - opacity_) < 0.0001f)
            return true;
        // Showcase cues can animate through the opaque boundary. Reconfigure
        // the shader and blend/depth state, not only its color alpha, so the
        // transition remains correct with both Unity unlit shader variants.
        return ApplyPresentation(letterboxTransparent_, nextOpacity);
    }

    void ScreenSurface::SetDoubleSided(bool enabled)
    {
        // Unity's built-in unlit shaders honor the conventional _Cull
        // material property: 0=None and 2=Back. This changes rasterization
        // only; it does not duplicate geometry, textures, or decoder work.
        const int cullMode = enabled ? 0 : 2;
        if(material_)
            material_->SetInt("_Cull", cullMode);
        if(backgroundMaterial_)
            backgroundMaterial_->SetInt("_Cull", cullMode);
    }

    bool ScreenSurface::SetDeformation(
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds)
    {
        if(!videoMesh_ || deformationBaseVertices_.empty() ||
           !undeformedVideoVertices_ || !dynamicVideoVertices_ ||
           !undeformedVideoUvs_ || !dynamicVideoUvs_)
            return false;

        if(!deformation.enabled)
        {
            if(!deformationWasApplied_)
                return true;
            std::copy(
                undeformedVideoVertices_.begin(),
                undeformedVideoVertices_.end(),
                dynamicVideoVertices_.begin());
            std::copy(
                undeformedVideoUvs_.begin(),
                undeformedVideoUvs_.end(),
                dynamicVideoUvs_.begin());
            videoMesh_->set_vertices(dynamicVideoVertices_);
            videoMesh_->set_uv(dynamicVideoUvs_);
            videoMesh_->RecalculateBounds();
            deformationWasApplied_ = false;
            return true;
        }

        const float frameWidth = std::max(screenWidth_, 0.0001f);
        const float frameHeight = std::max(screenHeight_, 0.0001f);
        const bool cinemaCurve = geometryConfig_.cinemaCurvatureDegrees.has_value();
        const float cinemaArcRadians = cinemaCurve
            ? *geometryConfig_.cinemaCurvatureDegrees * Pi / 180.0f : 0.0f;
        const bool cinemaCurveIsFlat =
            !cinemaCurve || std::abs(cinemaArcRadians) < 0.000001f;
        const float cinemaCurveLength = geometryConfig_.cinemaCurveYAxis
            ? frameHeight : frameWidth;
        const float cinemaRadius = cinemaCurveIsFlat
            ? 0.0f : cinemaCurveLength / cinemaArcRadians;
        const double waveClock = deformation.wave.clock ==
                CoreLogic::DeformationClock::RealTime
            ? realTimeSeconds : songTimeSeconds;

        const float videoLayerOffset =
            CoreLogic::VideoLayerOffset(frameWidth, frameHeight);
        for(std::size_t index = 0; index < deformationBaseVertices_.size(); ++index)
        {
            const auto& base = deformationBaseVertices_[index];
            const auto corner = CoreLogic::BilinearCornerOffset(
                deformation.cornerWarp, base.normalizedU, base.normalizedV);
            float x = base.x + corner.x;
            float y = base.y + corner.y;
            float z = base.z + corner.z;

            // Composition order is deliberate: normalized grid, corner warp,
            // the layout's existing curvature, and finally the flag wave.
            if(cinemaCurve && !cinemaCurveIsFlat)
            {
                if(geometryConfig_.cinemaCurveYAxis)
                {
                    const float theta = y / frameHeight * cinemaArcRadians;
                    y = std::sin(theta) * cinemaRadius;
                    z += std::cos(theta) * cinemaRadius - cinemaRadius;
                }
                else
                {
                    const float theta = x / frameWidth * cinemaArcRadians;
                    x = std::sin(theta) * cinemaRadius;
                    z += std::cos(theta) * cinemaRadius - cinemaRadius;
                }
            }
            else if(!cinemaCurve)
            {
                const float normalizedX = std::clamp(
                    x / frameWidth + 0.5f, 0.0f, 1.0f);
                z += -geometryConfig_.screenCurvature * frameWidth * 0.12f *
                    CurveEdgeShape(normalizedX);
            }

            const auto wave = CoreLogic::FlagWaveOffset(
                deformation.wave, base.normalizedU, waveClock);
            dynamicVideoVertices_[index] = {
                x + wave.x,
                y + wave.y,
                z + wave.z + videoLayerOffset};
        }

        const float cover = CoreLogic::DeformationAutoCoverScale(
            frameWidth, frameHeight, deformation);
        for(std::size_t index = 0; index < undeformedVideoUvs_.size(); ++index)
        {
            const auto uv = undeformedVideoUvs_[index];
            dynamicVideoUvs_[index] = {
                std::clamp(0.5f + (uv.x - 0.5f) / cover, 0.0f, 1.0f),
                std::clamp(0.5f + (uv.y - 0.5f) / cover, 0.0f, 1.0f)};
        }
        videoMesh_->set_vertices(dynamicVideoVertices_);
        videoMesh_->set_uv(dynamicVideoUvs_);
        // Normals are irrelevant to the unlit video material. Recalculating
        // only bounds keeps Unity culling correct without avoidable CPU work.
        videoMesh_->RecalculateBounds();
        deformationWasApplied_ = true;
        return true;
    }

    UnityEngine::Vector3 ScreenSurface::MapFracturePoint(
        CoreLogic::FracturePoint point,
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds) const
    {
        const float frameWidth = std::max(screenWidth_, 0.0001f);
        const float frameHeight = std::max(screenHeight_, 0.0001f);
        const auto corner = CoreLogic::BilinearCornerOffset(
            deformation.cornerWarp, point.x, point.y);
        float x = (point.x - 0.5f) * frameWidth + corner.x;
        float y = (point.y - 0.5f) * frameHeight + corner.y;
        float z = corner.z;

        const bool cinemaCurve = geometryConfig_.cinemaCurvatureDegrees.has_value();
        const float cinemaArcRadians = cinemaCurve
            ? *geometryConfig_.cinemaCurvatureDegrees * Pi / 180.0f : 0.0f;
        if(cinemaCurve && std::abs(cinemaArcRadians) >= 0.000001f)
        {
            const float curveLength = geometryConfig_.cinemaCurveYAxis
                ? frameHeight : frameWidth;
            const float radius = curveLength / cinemaArcRadians;
            if(geometryConfig_.cinemaCurveYAxis)
            {
                const float theta = y / frameHeight * cinemaArcRadians;
                y = std::sin(theta) * radius;
                z += std::cos(theta) * radius - radius;
            }
            else
            {
                const float theta = x / frameWidth * cinemaArcRadians;
                x = std::sin(theta) * radius;
                z += std::cos(theta) * radius - radius;
            }
        }
        else if(!cinemaCurve)
        {
            z += -geometryConfig_.screenCurvature * frameWidth * 0.12f *
                CurveEdgeShape(point.x);
        }

        const double waveClock = deformation.wave.clock ==
                CoreLogic::DeformationClock::RealTime
            ? realTimeSeconds : songTimeSeconds;
        const auto wave = CoreLogic::FlagWaveOffset(
            deformation.wave, point.x, waveClock);
        return {
            x + wave.x,
            y + wave.y,
            z + wave.z + CoreLogic::VideoLayerOffset(frameWidth, frameHeight)};
    }

    void ScreenSurface::RestoreWholeVideoMesh()
    {
        // Unity destroys gameplay-scene objects before every Big Screen owner
        // necessarily receives its final Stop()/Destroy() call. Raw IL2CPP
        // pointers remain non-null after that native destruction, so testing
        // only the pointer value is unsafe: calling GetComponent on that
        // "fake-null" object raises a StackTraceException which a BSML event
        // delegate turns into a process abort. Treat an already-destroyed
        // surface as already restored and only touch live Unity objects.
        if(UnityW<UnityEngine::GameObject>::isAlive(videoObject_) &&
           UnityW<UnityEngine::Mesh>::isAlive(videoMesh_))
        {
            if(auto* filter = videoObject_->GetComponent<UnityEngine::MeshFilter*>())
                filter->set_sharedMesh(videoMesh_);
        }
        if(UnityW<UnityEngine::Material>::isAlive(material_) &&
           UnityW<UnityEngine::Texture2D>::isAlive(texture_))
            material_->set_mainTexture(texture_);
        if(UnityW<UnityEngine::GameObject>::isAlive(crackObject_))
            crackObject_->SetActive(false);
        fractureMeshActive_ = false;
        fractureShapeCaptured_ = false;
        fractureSnapshotActive_ = false;
    }

    void ScreenSurface::DestroyFractureResources()
    {
        RestoreWholeVideoMesh();
        DestroyIfAlive(crackObject_);
        DestroyIfAlive(crackMesh_);
        DestroyIfAlive(crackMaterial_);
        DestroyIfAlive(crackTexture_);
        DestroyIfAlive(fractureMesh_);
        DestroyIfAlive(fractureSnapshot_);
        fracturePattern_ = {};
        fractureRevealGroups_.clear();
        fractureVertexMetadata_.clear();
        fractureShardCenters_.clear();
        fractureShardBaseCenters_.clear();
        fractureShardTranslations_.clear();
        fractureShardRotations_.clear();
        fractureShardScales_.clear();
        fractureBaseVertices_ = nullptr;
        dynamicFractureVertices_ = nullptr;
        fractureUvs_ = nullptr;
        dynamicCrackVertices_ = nullptr;
        fracturePrepared_ = false;
        preparedFractureImpactCount_ = 0;
    }

    bool ScreenSurface::PrepareFracture(
        const CoreLogic::FractureEffectSettings& fracture)
    {
        const auto& requested = fracture.pattern;
        // The public programmatic struct intentionally uses a fixed impact
        // array so a future mapper/API wrapper cannot make setup unbounded.
        // Clamp the accompanying count at this final consumer boundary as
        // well. Include both source and cache capacities even though their
        // types currently match, so a future data-model change cannot turn
        // this copy back into an out-of-bounds UnityMain write.
        const std::size_t impactCount = std::min({
            fracture.impactCount,
            fracture.impacts.size(),
            preparedFractureImpacts_.size()});
        bool sameConfiguration = fracturePrepared_ &&
            requested.seed == preparedFractureSettings_.seed &&
            requested.pieceCount == preparedFractureSettings_.pieceCount &&
            requested.spokeCount == preparedFractureSettings_.spokeCount &&
            requested.ringCount == preparedFractureSettings_.ringCount &&
            requested.jitter == preparedFractureSettings_.jitter &&
            requested.impactPoint.x == preparedFractureSettings_.impactPoint.x &&
            requested.impactPoint.y == preparedFractureSettings_.impactPoint.y &&
            impactCount == preparedFractureImpactCount_;
        for(std::size_t index = 0;
            sameConfiguration && index < impactCount; ++index)
        {
            sameConfiguration = CoreLogic::SameFracturePoint(
                fracture.impacts[index], preparedFractureImpacts_[index],
                0.000001f);
        }
        if(sameConfiguration)
            return true;

        DestroyFractureResources();
        if(!prepareDeformation_ || !videoObject_ || !material_ || !texture_)
            return false;

        fracturePattern_ = CoreLogic::GenerateFracturePattern(requested);
        if(fracturePattern_.cells.empty() || fracturePattern_.edges.empty())
            return false;
        std::vector<CoreLogic::FracturePoint> impacts;
        impacts.reserve(impactCount);
        for(std::size_t index = 0; index < impactCount; ++index)
            impacts.push_back(fracture.impacts[index]);
        if(impacts.empty())
            impacts.push_back(requested.impactPoint);
        fractureRevealGroups_ = CoreLogic::PartitionFractureRevealGroups(
            fracturePattern_.edges, impacts);

        std::size_t vertexCount = 0;
        for(const auto& cell : fracturePattern_.cells)
            if(cell.vertices.size() >= 3)
                vertexCount += (cell.vertices.size() - 2) * 3;
        if(vertexCount == 0)
            return false;

        fractureVertexMetadata_.reserve(vertexCount);
        fractureShardCenters_.reserve(fracturePattern_.cells.size());
        fractureShardBaseCenters_.resize(fracturePattern_.cells.size());
        fractureShardTranslations_.resize(fracturePattern_.cells.size());
        fractureShardRotations_.resize(fracturePattern_.cells.size());
        fractureShardScales_.resize(fracturePattern_.cells.size(), 1.0f);
        std::vector<UnityEngine::Vector2> uvs;
        std::vector<std::int32_t> triangles;
        uvs.reserve(vertexCount);
        triangles.reserve(vertexCount);
        for(std::size_t shard = 0; shard < fracturePattern_.cells.size(); ++shard)
        {
            const auto& cell = fracturePattern_.cells[shard];
            fractureShardCenters_.push_back(cell.site);
            const auto fan = CoreLogic::TriangulateFractureCell(cell);
            for(const auto& triangle : fan)
            {
                const std::array<CoreLogic::FracturePoint, 3> points{
                    triangle.a, triangle.b, triangle.c};
                for(const auto point : points)
                {
                    fractureVertexMetadata_.push_back({point, shard});
                    uvs.push_back({point.x, 1.0f - point.y});
                    triangles.push_back(
                        static_cast<std::int32_t>(triangles.size()));
                }
            }
        }
        fractureBaseVertices_ = ArrayW<UnityEngine::Vector3>(vertexCount);
        dynamicFractureVertices_ = ArrayW<UnityEngine::Vector3>(vertexCount);
        fractureUvs_ = ArrayW<UnityEngine::Vector2>(vertexCount);
        ArrayW<std::int32_t> unityTriangles(triangles.size());
        std::copy(uvs.begin(), uvs.end(), fractureUvs_.begin());
        std::copy(triangles.begin(), triangles.end(), unityTriangles.begin());
        fractureMesh_ = UnityEngine::Mesh::New_ctor();
        if(!fractureMesh_)
            return false;
        fractureMesh_->set_vertices(fractureBaseVertices_);
        fractureMesh_->set_uv(fractureUvs_);
        fractureMesh_->set_triangles(unityTriangles);

        const std::size_t edgeCount = fracturePattern_.edges.size();
        dynamicCrackVertices_ = ArrayW<UnityEngine::Vector3>(edgeCount * 4);
        ArrayW<UnityEngine::Vector2> crackUvs(edgeCount * 4);
        ArrayW<std::int32_t> crackTriangles(edgeCount * 6);
        for(std::size_t edge = 0; edge < edgeCount; ++edge)
        {
            const std::size_t vertex = edge * 4;
            crackUvs[vertex] = {0.0f, 0.0f};
            crackUvs[vertex + 1] = {1.0f, 0.0f};
            crackUvs[vertex + 2] = {1.0f, 1.0f};
            crackUvs[vertex + 3] = {0.0f, 1.0f};
            const std::size_t triangle = edge * 6;
            crackTriangles[triangle] = static_cast<std::int32_t>(vertex);
            crackTriangles[triangle + 1] = static_cast<std::int32_t>(vertex + 1);
            crackTriangles[triangle + 2] = static_cast<std::int32_t>(vertex + 2);
            crackTriangles[triangle + 3] = static_cast<std::int32_t>(vertex);
            crackTriangles[triangle + 4] = static_cast<std::int32_t>(vertex + 2);
            crackTriangles[triangle + 5] = static_cast<std::int32_t>(vertex + 3);
        }
        crackMesh_ = UnityEngine::Mesh::New_ctor();
        auto shader = UnityEngine::Shader::Find("Unlit/Transparent");
        if(!shader)
            shader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!crackMesh_ || !shader)
            return false;
        crackMesh_->set_vertices(dynamicCrackVertices_);
        crackMesh_->set_uv(crackUvs);
        crackMesh_->set_triangles(crackTriangles);

        crackTexture_ = UnityEngine::Texture2D::New_ctor(
            4, 1, UnityEngine::TextureFormat::RGBA32, false, false);
        crackMaterial_ = UnityEngine::Material::New_ctor(shader);
        crackObject_ = UnityEngine::GameObject::New_ctor("Big Screen Glass Cracks");
        if(!crackTexture_ || !crackMaterial_ || !crackObject_)
            return false;
        ArrayW<UnityEngine::Color> crackColors(4);
        crackColors[0] = {0.82f, 0.90f, 1.0f, 0.92f};
        crackColors[1] = {0.015f, 0.02f, 0.03f, 0.86f};
        crackColors[2] = {0.015f, 0.02f, 0.03f, 0.86f};
        crackColors[3] = {0.82f, 0.90f, 1.0f, 0.92f};
        crackTexture_->SetPixels(crackColors);
        crackTexture_->Apply(false, false);
        crackMaterial_->set_mainTexture(crackTexture_);
        crackMaterial_->set_color(UnityEngine::Color::get_white());
        crackMaterial_->SetInt("_SrcBlend", 5);
        crackMaterial_->SetInt("_DstBlend", 10);
        crackMaterial_->SetInt("_ZWrite", 0);
        crackMaterial_->SetInt("_Cull", 0);
        crackMaterial_->EnableKeyword("_ALPHABLEND_ON");
        crackMaterial_->set_renderQueue(3100);
        crackObject_->set_layer(videoObject_->get_layer());
        crackObject_->get_transform()->SetParent(videoObject_->get_transform(), false);
        auto* crackFilter = crackObject_->AddComponent<UnityEngine::MeshFilter*>();
        auto* crackRenderer = crackObject_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!crackFilter || !crackRenderer)
            return false;
        crackFilter->set_sharedMesh(crackMesh_);
        crackRenderer->set_sharedMaterial(crackMaterial_);
        crackObject_->SetActive(false);

        fractureSnapshot_ = UnityEngine::Texture2D::New_ctor(
            textureWidth_, textureHeight_, UnityEngine::TextureFormat::RGBA32,
            false, false);
        if(!fractureSnapshot_)
            return false;

        preparedFractureSettings_ = requested;
        preparedFractureImpactCount_ = impactCount;
        for(std::size_t index = 0; index < preparedFractureImpactCount_; ++index)
            preparedFractureImpacts_[index] = fracture.impacts[index];
        fracturePrepared_ = true;
        fractureShapeCaptured_ = false;
        return true;
    }

    bool ScreenSurface::UpdateCrackOverlay(
        const CoreLogic::FractureEffectSettings& fracture,
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds)
    {
        if(!crackMesh_ || !crackObject_ || !dynamicCrackVertices_)
            return false;
        constexpr float CrackHalfWidth = 0.045f;
        // Every Voronoi seam is an independent quad. Butt-ended quads leave
        // tiny visible gaps where several short seams meet, especially near
        // the outer screen edges, and the resulting crack looks like a dotted
        // line in the headset. Extend both ends by slightly more than half the
        // line width so neighboring segments overlap without making the main
        // fracture branches materially thicker.
        constexpr float CrackEndOverlap = CrackHalfWidth * 0.65f;
        for(std::size_t edge = 0; edge < fracturePattern_.edges.size(); ++edge)
        {
            const auto& source = fracturePattern_.edges[edge];
            const bool revealed = edge < fractureRevealGroups_.size() &&
                fractureRevealGroups_[edge] < fracture.revealedGroupCount;
            const auto from = MapFracturePoint(
                source.from, deformation, songTimeSeconds, realTimeSeconds);
            const auto to = MapFracturePoint(
                source.to, deformation, songTimeSeconds, realTimeSeconds);
            const std::size_t vertex = edge * 4;
            if(!revealed)
            {
                dynamicCrackVertices_[vertex] = from;
                dynamicCrackVertices_[vertex + 1] = from;
                dynamicCrackVertices_[vertex + 2] = from;
                dynamicCrackVertices_[vertex + 3] = from;
                continue;
            }
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float inverseLength = 1.0f /
                std::max(0.0001f, std::sqrt(dx * dx + dy * dy));
            const float tangentX = dx * inverseLength;
            const float tangentY = dy * inverseLength;
            const float offsetX = -dy * inverseLength * CrackHalfWidth;
            const float offsetY = dx * inverseLength * CrackHalfWidth;
            const float fromX = from.x - tangentX * CrackEndOverlap;
            const float fromY = from.y - tangentY * CrackEndOverlap;
            const float toX = to.x + tangentX * CrackEndOverlap;
            const float toY = to.y + tangentY * CrackEndOverlap;
            dynamicCrackVertices_[vertex] =
                {fromX - offsetX, fromY - offsetY, from.z - 0.04f};
            dynamicCrackVertices_[vertex + 1] =
                {fromX + offsetX, fromY + offsetY, from.z - 0.04f};
            dynamicCrackVertices_[vertex + 2] =
                {toX + offsetX, toY + offsetY, to.z - 0.04f};
            dynamicCrackVertices_[vertex + 3] =
                {toX - offsetX, toY - offsetY, to.z - 0.04f};
        }
        crackMesh_->set_vertices(dynamicCrackVertices_);
        crackMesh_->RecalculateBounds();
        if(crackMaterial_)
            crackMaterial_->set_color({1.0f, 1.0f, 1.0f,
                std::clamp(fracture.crackOpacity, 0.0f, 1.0f)});
        crackObject_->SetActive(true);
        return true;
    }

    bool ScreenSurface::CaptureFractureShape(
        const CoreLogic::FractureEffectSettings& fracture,
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds)
    {
        if(!fractureMesh_ || !fractureBaseVertices_ || !fractureUvs_)
            return false;
        const float cover = CoreLogic::DeformationAutoCoverScale(
            screenWidth_, screenHeight_, deformation);
        for(std::size_t index = 0; index < fractureVertexMetadata_.size(); ++index)
        {
            const auto point = fractureVertexMetadata_[index].point;
            fractureBaseVertices_[index] = MapFracturePoint(
                point, deformation, songTimeSeconds, realTimeSeconds);
            dynamicFractureVertices_[index] = fractureBaseVertices_[index];
            fractureUvs_[index] = {
                std::clamp(0.5f + (point.x - 0.5f) / cover, 0.0f, 1.0f),
                std::clamp(0.5f + ((1.0f - point.y) - 0.5f) / cover,
                           0.0f, 1.0f)};
        }
        for(std::size_t shard = 0; shard < fractureShardCenters_.size(); ++shard)
            fractureShardBaseCenters_[shard] = MapFracturePoint(
                fractureShardCenters_[shard], deformation,
                songTimeSeconds, realTimeSeconds);
        fractureMesh_->set_vertices(fractureBaseVertices_);
        fractureMesh_->set_uv(fractureUvs_);
        fractureMesh_->RecalculateBounds();
        fractureShapeCaptured_ = true;
        if(fracture.freezeOnShatter)
        {
            UnityEngine::Graphics::CopyTexture(texture_, fractureSnapshot_);
            material_->set_mainTexture(fractureSnapshot_);
            fractureSnapshotActive_ = true;
        }
        else
        {
            material_->set_mainTexture(texture_);
            fractureSnapshotActive_ = false;
        }
        return true;
    }

    bool ScreenSurface::UpdateFractureVertices(
        const CoreLogic::FractureEffectSettings& fracture)
    {
        if(!fractureMesh_ || !fractureBaseVertices_ ||
           !dynamicFractureVertices_)
            return false;
        const float separation = std::clamp(fracture.separation, 0.0f, 1.0f);
        const auto impact = fracture.pattern.impactPoint;
        const std::size_t overrideCount = std::min(
            fracture.shardTransformCount, fracture.shardTransforms.size());
        for(std::size_t shard = 0; shard < fractureShardCenters_.size(); ++shard)
        {
            const auto centerPoint = fractureShardCenters_[shard];
            float directionX =
                (centerPoint.x - impact.x) * std::max(screenWidth_, 0.001f);
            float directionY =
                (centerPoint.y - impact.y) * std::max(screenHeight_, 0.001f);
            const float radius = std::sqrt(
                directionX * directionX + directionY * directionY);
            if(radius > 0.0001f)
            {
                directionX /= radius;
                directionY /= radius;
            }
            const float normalizedRadius = std::clamp(
                std::sqrt(
                    (centerPoint.x - impact.x) * (centerPoint.x - impact.x) +
                    (centerPoint.y - impact.y) * (centerPoint.y - impact.y)) *
                    1.6f,
                0.0f, 1.0f);
            const float delay = (1.0f - normalizedRadius) *
                std::clamp(fracture.stagger, 0.0f, 0.8f);
            const float local = std::clamp(
                (separation - delay) / std::max(0.001f, 1.0f - delay),
                0.0f, 1.0f);
            const float eased = local * local * (3.0f - 2.0f * local);
            const std::uint32_t hash = FractureHash(fracture.pattern.seed, shard);
            const float randomX = HashSigned(hash);
            const float randomY = HashSigned(hash ^ 0xA511E9B3U);
            const float randomZ = HashSigned(hash ^ 0x63D83595U);
            CoreLogic::FractureShardTransform authored{};
            authored.shardIndex = shard;
            for(std::size_t index = 0; index < overrideCount; ++index)
            {
                if(fracture.shardTransforms[index].shardIndex == shard)
                {
                    authored = fracture.shardTransforms[index];
                    break;
                }
            }
            const float rotation = fracture.tumbleDegrees * eased;
            fractureShardRotations_[shard] = {
                rotation * randomX + authored.rotationDegrees.x * eased,
                rotation * randomY + authored.rotationDegrees.y * eased,
                rotation * randomZ + authored.rotationDegrees.z * eased};
            UnityEngine::Vector3 value{};
            const float kick = fracture.outwardDistance * eased *
                (0.72f + std::abs(randomX) * 0.36f);
            value.x = directionX * kick + randomX * kick * 0.18f +
                authored.translation.x * eased;
            value.y = directionY * kick + randomY * kick * 0.14f -
                fracture.gravityDistance * local * local +
                authored.translation.y * eased;
            const float randomizedForwardAmount =
                0.25f + (randomZ * 0.5f + 0.5f) * 0.75f;
            const float forwardTravel = fracture.forwardScatterDistance > 0.0f
                ? fracture.forwardScatterDistance * eased *
                    randomizedForwardAmount
                : kick * (0.35f + std::abs(randomZ) * 0.35f);
            // Negative local Z is toward the player for the back-wall screen.
            // A separately authored depth range lets shards fall forward as a
            // three-dimensional cloud instead of remaining a thin glass row.
            value.z = -forwardTravel + authored.translation.z * eased;
            fractureShardTranslations_[shard] = value;
            fractureShardScales_[shard] = std::max(
                0.01f, 1.0f + (authored.scale - 1.0f) * eased);
        }
        for(std::size_t index = 0; index < fractureVertexMetadata_.size(); ++index)
        {
            const std::size_t shard = fractureVertexMetadata_[index].shard;
            const auto center = fractureShardBaseCenters_[shard];
            const auto rotation = fractureShardRotations_[shard];
            auto value = RotateFractureVertex(
                fractureBaseVertices_[index], center,
                rotation.x, rotation.y, rotation.z);
            const float scale = fractureShardScales_[shard];
            value.x = center.x + (value.x - center.x) * scale;
            value.y = center.y + (value.y - center.y) * scale;
            value.z = center.z + (value.z - center.z) * scale;
            const auto translation = fractureShardTranslations_[shard];
            value.x += translation.x;
            value.y += translation.y;
            value.z += translation.z;
            dynamicFractureVertices_[index] = value;
        }
        fractureMesh_->set_vertices(dynamicFractureVertices_);
        fractureMesh_->RecalculateBounds();
        return true;
    }

    bool ScreenSurface::SetFractureEffect(
        const CoreLogic::FractureEffectSettings& fracture,
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds)
    {
        if(!fracture.enabled ||
           fracture.phase == CoreLogic::FracturePhase::Inactive)
        {
            if(fractureMeshActive_ || fractureSnapshotActive_ ||
               (crackObject_ && crackObject_->get_activeSelf()))
                RestoreWholeVideoMesh();
            return true;
        }
        if(!PrepareFracture(fracture))
            return false;

        if(fracture.phase == CoreLogic::FracturePhase::Prepared)
        {
            if(fractureMeshActive_ || fractureSnapshotActive_ ||
               (crackObject_ && crackObject_->get_activeSelf()))
                RestoreWholeVideoMesh();
            return true;
        }
        if(fracture.phase == CoreLogic::FracturePhase::CrackOnly)
        {
            if(fractureMeshActive_ || fractureSnapshotActive_)
                RestoreWholeVideoMesh();
            return UpdateCrackOverlay(
                fracture, deformation, songTimeSeconds, realTimeSeconds);
        }

        if(!fractureShapeCaptured_ &&
           !CaptureFractureShape(
               fracture, deformation, songTimeSeconds, realTimeSeconds))
            return false;
        if(fracture.freezeOnShatter)
        {
            if(!fractureSnapshotActive_)
                UnityEngine::Graphics::CopyTexture(texture_, fractureSnapshot_);
            material_->set_mainTexture(fractureSnapshot_);
            fractureSnapshotActive_ = true;
        }
        else if(!fracture.freezeOnShatter && fractureSnapshotActive_)
        {
            material_->set_mainTexture(texture_);
            fractureSnapshotActive_ = false;
        }
        if(crackObject_)
            crackObject_->SetActive(false);
        if(!fractureMeshActive_)
        {
            auto* filter = videoObject_->GetComponent<UnityEngine::MeshFilter*>();
            if(!filter)
                return false;
            filter->set_sharedMesh(fractureMesh_);
            fractureMeshActive_ = true;
        }
        if(!UpdateFractureVertices(fracture))
            return false;

        if(fracture.phase == CoreLogic::FracturePhase::Rejoining &&
           fracture.separation <= 0.0001f)
        {
            RestoreWholeVideoMesh();
            if(fracture.retainCracksAfterRejoin)
                return UpdateCrackOverlay(
                    fracture, deformation, songTimeSeconds, realTimeSeconds);
        }
        return true;
    }

    void ScreenSurface::Destroy()
    {
        DestroyFractureResources();
        DestroyIfAlive(diagnosticsObject_);
        diagnosticsText_ = nullptr;
        // Destroying the GameObject also destroys its MeshFilter and Renderer.
        // Mesh, material, and texture were created as standalone Unity objects,
        // so they are released explicitly when gameplay ends or a level changes.
        DestroyIfAlive(videoObject_);
        DestroyIfAlive(gameObject_);
        if(ownsTexture_)
            DestroyIfAlive(texture_);
        else
            texture_ = nullptr;
        DestroyIfAlive(material_);
        DestroyIfAlive(mesh_);
        DestroyIfAlive(videoMesh_);
        DestroyIfAlive(backgroundMaterial_);
        textureWidth_ = 0;
        textureHeight_ = 0;
        ownsTexture_ = false;
        screenWidth_ = 0.0f;
        screenHeight_ = 0.0f;
        letterboxTransparent_ = false;
        videoCoversFrame_ = false;
        opacity_ = 1.0f;
        visible_ = false;
        leadInActive_ = false;
        leadInBlack_ = false;
        geometryConfig_ = {};
        geometryAspectRatio_ = 1.0f;
        deformationBaseVertices_.clear();
        undeformedVideoVertices_ = nullptr;
        dynamicVideoVertices_ = nullptr;
        undeformedVideoUvs_ = nullptr;
        dynamicVideoUvs_ = nullptr;
        prepareDeformation_ = false;
        deformationWasApplied_ = false;
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
            // The text rectangle spans almost the full canvas width and is
            // centered at this transform. Center X and keep its lower edge
            // just inside the video frame so flat, curved, enlarged, and
            // undocked layouts all show the same readable overlay.
            transform->set_localPosition({
                0.0f,
                -screenHeight_ * 0.36f,
                -0.08f});
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
