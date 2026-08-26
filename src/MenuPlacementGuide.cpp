// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/MenuPlacementGuide.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/Settings.hpp"
#include "UnityEngine/Bounds.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr float GuideFloorY = 0.025f;
        constexpr float LaneHalfWidth = 1.2f;
        constexpr float GuideNearZ = -0.75f;
        constexpr float GuideFarZ = 60.0f;
        constexpr float LaneLineWidth = 0.018f;
        constexpr float CenterLineWidth = 0.032f;

        std::string NormalizeName(StringW value)
        {
            std::string normalized;
            for(const unsigned char character : std::string(value))
            {
                if(std::isalnum(character))
                    normalized.push_back(
                        static_cast<char>(std::tolower(character)));
            }
            return normalized;
        }

        bool HasBigScreenAncestor(UnityEngine::Transform* transform)
        {
            // Never consider a renderer owned by this mod as a floor
            // candidate. This is especially important when a setting is
            // toggled off and on without closing the retained flow.
            for(int depth = 0; transform && depth < 12; ++depth)
            {
                if(NormalizeName(transform->get_name()).starts_with("bigscreen"))
                    return true;
                transform = transform->get_parent();
            }
            return false;
        }

        bool HasFloorAncestor(UnityEngine::Transform* transform)
        {
            for(int depth = 0; transform && depth < 8; ++depth)
            {
                const auto name = NormalizeName(transform->get_name());
                // Geometry checks below prevent these broad tokens from
                // matching labels or vertical decor. Supporting FloorMirror,
                // MenuGround, platform, and runway naming makes this resilient
                // across stock Beat Saber menu-environment revisions without
                // maintaining one fragile exact-name list.
                if(name.find("floor") != std::string::npos ||
                   name.find("ground") != std::string::npos ||
                   name.find("platform") != std::string::npos ||
                   name.find("runway") != std::string::npos)
                {
                    return true;
                }
                transform = transform->get_parent();
            }
            return false;
        }

        bool LooksLikeHorizontalFloor(UnityEngine::Renderer* renderer)
        {
            if(!renderer || !HasFloorAncestor(renderer->get_transform()))
                return false;

            auto bounds = renderer->get_bounds();
            const auto size = bounds.get_size();
            const auto center = bounds.get_center();
            const float horizontalMinimum = std::min(size.x, size.z);
            const float horizontalMaximum = std::max(size.x, size.z);

            // A name match alone is not enough: UI or decorative objects can
            // contain the word "floor". The renderer must be a broad, mostly
            // horizontal surface near the gameplay origin. This deliberately
            // rejects the surrounding sky dome and vertical menu structures.
            return std::isfinite(size.x) && std::isfinite(size.y) &&
                std::isfinite(size.z) && horizontalMinimum >= 2.0f &&
                horizontalMaximum >= 4.0f &&
                size.y <= std::max(1.5f, horizontalMaximum * 0.12f) &&
                center.y - size.y * 0.5f <= 1.0f &&
                center.y + size.y * 0.5f >= -2.0f;
        }

        void AppendFloorStrip(
            std::vector<UnityEngine::Vector3>& vertices,
            std::vector<std::int32_t>& triangles,
            UnityEngine::Vector3 start,
            UnityEngine::Vector3 end,
            float width)
        {
            const float deltaX = end.x - start.x;
            const float deltaZ = end.z - start.z;
            const float length = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
            if(length <= 0.0001f)
                return;

            const float halfWidth = width * 0.5f;
            const float perpendicularX = -deltaZ / length * halfWidth;
            const float perpendicularZ = deltaX / length * halfWidth;
            const auto base = static_cast<std::int32_t>(vertices.size());
            vertices.push_back({
                start.x + perpendicularX, start.y,
                start.z + perpendicularZ});
            vertices.push_back({
                end.x + perpendicularX, end.y,
                end.z + perpendicularZ});
            vertices.push_back({
                end.x - perpendicularX, end.y,
                end.z - perpendicularZ});
            vertices.push_back({
                start.x - perpendicularX, start.y,
                start.z - perpendicularZ});

            // Clockwise from above in Unity's coordinate system. The guide
            // is a single lightweight mesh instead of LineRenderer objects:
            // LineRenderer creation is rejected by Beat Saber's stripped
            // Quest player even though its generated API remains callable.
            triangles.insert(triangles.end(), {
                base, base + 1, base + 2,
                base, base + 2, base + 3});
        }
    }

    MenuPlacementGuide& MenuPlacementGuide::Instance()
    {
        static MenuPlacementGuide guide;
        return guide;
    }

    void MenuPlacementGuide::PrewarmCache()
    {
        const bool cachedSceneInvalid = std::any_of(
            knownFloorRenderers_.begin(), knownFloorRenderers_.end(),
            [](UnityW<UnityEngine::Renderer> renderer)
            {
                return !UnityW<UnityEngine::Renderer>::isAlive(
                    renderer.unsafePtr());
            });
        if(floorCacheInitialized_ && !cachedSceneInvalid)
            return;

        knownFloorRenderers_.clear();
        for(auto* renderer : UnityEngine::Object::FindObjectsOfType<
                UnityEngine::Renderer*>(true))
        {
            if(!renderer || !renderer->get_gameObject() ||
               HasBigScreenAncestor(renderer->get_transform()) ||
               !LooksLikeHorizontalFloor(renderer))
                continue;
            knownFloorRenderers_.emplace_back(renderer);
        }
        floorCacheInitialized_ = true;
        BigScreen::BigScreenLogger.info(
            "Prewarmed {} compatible menu-floor renderer(s) for this scene",
            knownFloorRenderers_.size());
    }

    bool MenuPlacementGuide::HideCompatibleMenuFloorRenderers()
    {
        PrewarmCache();
        const std::size_t originalCount = hiddenFloorRenderers_.size();
        for(auto renderer : knownFloorRenderers_)
        {
            if(!UnityW<UnityEngine::Renderer>::isAlive(renderer.unsafePtr()) ||
               !renderer->get_enabled() ||
               !renderer->get_gameObject() ||
               !renderer->get_gameObject()->get_activeInHierarchy() ||
               HasBigScreenAncestor(renderer->get_transform()))
            {
                continue;
            }

            // Disable only the visual component. The menu object, collider,
            // scripts, and scene hierarchy retain their normal lifetime.
            hiddenFloorRenderers_.emplace_back(renderer);
            renderer->set_enabled(false);
            BigScreen::BigScreenLogger.info(
                "Show Menu Environment off: hid menu-floor renderer '{}'",
                std::string(renderer->get_gameObject()->get_name()));
        }
        BigScreen::BigScreenLogger.info(
            "Show Menu Environment off: hid {} newly visible menu-floor renderers ({} total cached)",
            hiddenFloorRenderers_.size() - originalCount,
            hiddenFloorRenderers_.size());
        return hiddenFloorRenderers_.size() > originalCount;
    }

    bool MenuPlacementGuide::CreateLaneGuide()
    {
        auto shader = UnityEngine::Shader::Find("Unlit/Transparent");
        if(!shader)
            shader = UnityEngine::Shader::Find("Unlit/Texture");
        if(!shader)
            return false;

        guideMaterial_ = UnityEngine::Material::New_ctor(shader);
        if(!guideMaterial_)
            return false;
        guideMaterial_->set_name("Big Screen Placement Guide Material");
        guideMaterial_->set_color({0.0f, 0.78f, 1.0f, 0.72f});
        guideMaterial_->SetInt("_SrcBlend", 5);  // SrcAlpha
        guideMaterial_->SetInt("_DstBlend", 10); // OneMinusSrcAlpha
        guideMaterial_->SetInt("_ZWrite", 0);
        guideMaterial_->DisableKeyword("_ALPHATEST_ON");
        guideMaterial_->EnableKeyword("_ALPHABLEND_ON");
        guideMaterial_->DisableKeyword("_ALPHAPREMULTIPLY_ON");
        guideMaterial_->set_renderQueue(3000);

        guideRoot_ = UnityEngine::GameObject::New_ctor(
            "Big Screen Menu Placement Guide");
        if(!guideRoot_)
            return false;

        std::vector<UnityEngine::Vector3> vertices;
        std::vector<std::int32_t> triangles;
        vertices.reserve(13 * 4);
        triangles.reserve(13 * 6);

        // Beat Saber's four note lanes are 0.6 metres apart and centered on
        // X=0. These five rails show both outer boundaries and the center of
        // the actual gameplay corridor all the way to Big Screen's default
        // back-wall position at Z=60.
        constexpr float laneBoundaries[] = {-1.2f, -0.6f, 0.0f, 0.6f, 1.2f};
        for(const float x : laneBoundaries)
        {
            const bool center = std::abs(x) < 0.001f;
            AppendFloorStrip(
                vertices, triangles,
                {x, GuideFloorY, GuideNearZ},
                {x, GuideFloorY, GuideFarZ},
                center ? CenterLineWidth : LaneLineWidth);
        }

        // Crossbars make depth readable without creating a filled floor that
        // could hide the portion of a video placed below gameplay height.
        constexpr float depthMarks[] = {0.0f, 5.0f, 10.0f, 20.0f, 40.0f, 60.0f};
        for(const float z : depthMarks)
        {
            AppendFloorStrip(
                vertices, triangles,
                {-LaneHalfWidth, GuideFloorY, z},
                {LaneHalfWidth, GuideFloorY, z},
                z == 0.0f ? CenterLineWidth : LaneLineWidth);
        }

        // A thicker cross marks the player's origin. It remains outside the
        // normal note path and supplies an immediate scale/orientation cue.
        AppendFloorStrip(
            vertices, triangles,
            {-0.35f, GuideFloorY + 0.005f, 0.0f},
            {0.35f, GuideFloorY + 0.005f, 0.0f},
            CenterLineWidth);
        AppendFloorStrip(
            vertices, triangles,
            {0.0f, GuideFloorY + 0.005f, -0.35f},
            {0.0f, GuideFloorY + 0.005f, 0.35f},
            CenterLineWidth);

        if(vertices.empty() || triangles.empty())
            return false;
        ArrayW<UnityEngine::Vector3> unityVertices(vertices.size());
        ArrayW<std::int32_t> unityTriangles(triangles.size());
        std::copy(vertices.begin(), vertices.end(), unityVertices.begin());
        std::copy(triangles.begin(), triangles.end(), unityTriangles.begin());

        guideMesh_ = UnityEngine::Mesh::New_ctor();
        auto* filter = guideRoot_->AddComponent<UnityEngine::MeshFilter*>();
        auto* renderer = guideRoot_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!guideMesh_ || !filter || !renderer)
            return false;
        guideMesh_->set_name("Big Screen Menu Placement Guide Mesh");
        guideMesh_->set_vertices(unityVertices);
        guideMesh_->set_triangles(unityTriangles);
        guideMesh_->RecalculateNormals();
        guideMesh_->RecalculateBounds();
        filter->set_sharedMesh(guideMesh_);
        renderer->set_sharedMaterial(guideMaterial_);

        return true;
    }

    bool MenuPlacementGuide::Apply()
    {
        const auto& settings = Settings::Instance();
        if(!settings.ModEnabled() || !IsBigScreenMenuActive())
        {
            Suspend();
            return true;
        }

        // The environment switch owns the stock floor as well as scenery and
        // lighting. Lane geometry remains independent so it can still provide
        // a coordinate reference in an otherwise empty placement space.
        if(!settings.ShowMenuFloor())
        {
            const bool firstApplication = !floorRemovalApplied_;
            floorRemovalApplied_ = true;

            // Re-scan even after the preference was already applied. Turning
            // Show Menu Environment back on restores its cached renderers;
            // this catches a newly visible floor in the same reconciliation
            // pass without forgetting renderers that are still hidden.
            if(!HideCompatibleMenuFloorRenderers() && firstApplication)
            {
                BigScreen::BigScreenLogger.warn(
                    "Show Menu Environment is off, but no compatible menu-floor renderer was found");
            }
        }
        else if(floorRemovalApplied_ || !hiddenFloorRenderers_.empty())
        {
            RestoreMenuFloorRenderers();
            floorRemovalApplied_ = false;
        }

        if(!settings.ShowLaneGuidesEnabled())
        {
            DestroyLaneGuide();
            return true;
        }
        if(guideRoot_)
            return true;
        if(!CreateLaneGuide())
        {
            DestroyLaneGuide();
            Settings::Instance().SetShowLaneGuidesEnabled(false);
            ErrorManager::Instance().ReportUserVisible(
                "Lane guides unavailable",
                "Beat Saber could not create the simulated lane guides, so Show Lane Guides was turned back off. Show Menu Environment was left unchanged.");
            return false;
        }
        return true;
    }

    void MenuPlacementGuide::RestoreMenuFloorRenderers() noexcept
    {
        for(auto renderer : hiddenFloorRenderers_)
        {
            try
            {
                if(renderer)
                    renderer->set_enabled(true);
            }
            catch(...)
            {
                BigScreen::BigScreenLogger.error(
                    "Could not restore one Show Menu Environment floor renderer");
                ErrorManager::Instance().RecordError(
                    "Restoring the menu floor",
                    "Beat Saber rejected one cached menu-floor renderer");
            }
        }
        hiddenFloorRenderers_.clear();
    }

    void MenuPlacementGuide::DestroyLaneGuide() noexcept
    {
        try
        {
            if(guideRoot_)
                UnityEngine::Object::Destroy(guideRoot_);
        }
        catch(...)
        {
            BigScreen::BigScreenLogger.error("Could not destroy the menu placement guide");
        }
        guideRoot_ = nullptr;
        try
        {
            if(guideMesh_)
                UnityEngine::Object::Destroy(guideMesh_);
        }
        catch(...)
        {
            BigScreen::BigScreenLogger.error(
                "Could not destroy the menu placement guide mesh");
        }
        guideMesh_ = nullptr;
        try
        {
            if(guideMaterial_)
                UnityEngine::Object::Destroy(guideMaterial_);
        }
        catch(...)
        {
            BigScreen::BigScreenLogger.error(
                "Could not destroy the menu placement guide material");
        }
        guideMaterial_ = nullptr;
    }

    void MenuPlacementGuide::Suspend() noexcept
    {
        DestroyLaneGuide();
        RestoreMenuFloorRenderers();
        floorRemovalApplied_ = false;
    }
}
