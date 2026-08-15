// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/ShowcaseSurfaceGroup.hpp"

#include <algorithm>
#include <array>
#include <exception>

#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Vector3.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr std::array<const char*, UpDownShowcase::MaximumPanels>
            PanelNames{
                "Big Screen Showcase Panel 1",
                "Big Screen Showcase Panel 2",
                "Big Screen Showcase Panel 3",
                "Big Screen Showcase Panel 4",
                "Big Screen Showcase Panel 5",
                "Big Screen Showcase Panel 6",
                "Big Screen Showcase Panel 7",
                "Big Screen Showcase Panel 8",
                "Big Screen Showcase Panel 9",
                "Big Screen Showcase Panel 10",
                "Big Screen Showcase Panel 11",
                "Big Screen Showcase Panel 12"};
    }

    MapVideoConfig ShowcaseSurfaceGroup::GeometryConfig(
        const MapVideoConfig& anchor,
        UpDownShowcase::Geometry geometry)
    {
        MapVideoConfig config = anchor;
        // The proof-of-concept presentation is ephemeral. It deliberately
        // ignores the user's saved geometry without mutating or persisting it.
        config.screenPosition = {};
        config.screenRotation = {};
        config.screenHeight = 28.0f;
        config.screenWidthOverride.reset();
        config.screenCurvature = 0.0f;
        config.cinemaCurvatureDegrees.reset();
        config.cinemaCurveYAxis = false;
        config.maintainAspectRatioWhenCurved = false;
        // Showcase panels are the only surfaces that can receive the flag
        // wave. Provision their reusable mesh at the demonstrated 64-column
        // ceiling up front so enabling the effect never allocates mid-song.
        config.screenSegments = 64;
        config.letterboxTransparent = true;
        config.videoOpacity = 0.75f;
        config.videoRotation = 0.0f;
        config.videoZoom = 1.0f;
        config.videoOffsetX = 0.0f;
        config.videoOffsetY = 0.0f;
        config.videoTilt = 0.0f;
        config.stretchVideoToFit = false;

        switch(geometry)
        {
            case UpDownShowcase::Geometry::UltraWide:
                config.screenHeight = 25.0f;
                config.screenWidthOverride = 74.0f;
                break;
            case UpDownShowcase::Geometry::CurvedIn:
                config.screenHeight = 29.0f;
                config.screenCurvature = 5.5f;
                break;
            case UpDownShowcase::Geometry::CurvedOut:
                config.screenHeight = 29.0f;
                config.screenCurvature = -5.5f;
                break;
            case UpDownShowcase::Geometry::Tall:
                config.screenHeight = 34.0f;
                config.screenWidthOverride = 19.0f;
                break;
            case UpDownShowcase::Geometry::QuadrantTopLeft:
            case UpDownShowcase::Geometry::QuadrantTopRight:
            case UpDownShowcase::Geometry::QuadrantBottomLeft:
            case UpDownShowcase::Geometry::QuadrantBottomRight:
            {
                config.screenHeight = 18.0f;
                config.screenWidthOverride = 30.0f;
                config.videoZoom = 2.0f;
                const bool left =
                    geometry == UpDownShowcase::Geometry::QuadrantTopLeft ||
                    geometry == UpDownShowcase::Geometry::QuadrantBottomLeft;
                const bool top =
                    geometry == UpDownShowcase::Geometry::QuadrantTopLeft ||
                    geometry == UpDownShowcase::Geometry::QuadrantTopRight;
                // Moving a 2x image by one half-frame centers the requested
                // source quadrant inside this physical panel.
                config.videoOffsetX = left ? 1.0f : -1.0f;
                config.videoOffsetY = top ? -1.0f : 1.0f;
                break;
            }
            case UpDownShowcase::Geometry::Wide:
            default:
                break;
        }
        return config;
    }

    bool ShowcaseSurfaceGroup::Create(
        const MapVideoConfig& anchorConfig,
        int videoWidth,
        int videoHeight,
        UnityEngine::Texture2D* sharedTexture)
    {
        try
        {
            Destroy();
            if(!sharedTexture || videoWidth <= 0 || videoHeight <= 0)
                return false;

            anchorConfig_ = anchorConfig;
            const auto initial = GeometryConfig(
                anchorConfig_, UpDownShowcase::Geometry::Wide);
            for(std::size_t index = 0; index < panels_.size(); ++index)
            {
                if(!panels_[index].CreateShared(
                       initial,
                       videoWidth,
                       videoHeight,
                       sharedTexture,
                       PanelNames[index]))
                {
                    Destroy();
                    return false;
                }
                if(!panels_[index].SetOpacity(1.0f))
                {
                    Destroy();
                    return false;
                }
                // Several showcase cues rotate curved screens past edge-on.
                // Keep the shared video visible from the rear rather than
                // exposing a blank culled back face during the carousel.
                panels_[index].SetDoubleSided(true);
                panels_[index].SetVisible(false);
                geometry_[index] = UpDownShowcase::Geometry::Wide;
            }
            created_ = true;
            externallyVisible_ = true;
            return true;
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not create the showcase surfaces: {}",
                exception.what());
            try { Destroy(); } catch(...) {}
            return false;
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not create the showcase surfaces because of an unknown native exception");
            try { Destroy(); } catch(...) {}
            return false;
        }
    }

    bool ShowcaseSurfaceGroup::Apply(double songTimeSeconds)
    {
        try
        {
            if(!created_)
                return false;

            const auto frame = UpDownShowcase::Sample(songTimeSeconds);
            const double realTimeSeconds =
                UnityEngine::Time::get_realtimeSinceStartup();
            timelineActive_ = frame.active;
            for(std::size_t index = 0; index < panels_.size(); ++index)
            {
                const auto& state = frame.panels[index];
                if(state.visible && geometry_[index] != state.geometry)
                {
                    const auto config = GeometryConfig(anchorConfig_, state.geometry);
                    if(!panels_[index].UpdateGeometry(config))
                        return false;
                    geometry_[index] = state.geometry;
                }

                if(state.visible)
                {
                    panels_[index].SetWorldTransform(
                        {state.position.x, state.position.y, state.position.z},
                        UnityEngine::Quaternion::Euler(
                            state.rotation.x,
                            state.rotation.y,
                            state.rotation.z));
                    panels_[index].SetWorldScale(
                        {state.scale.x, state.scale.y, state.scale.z});
                    panels_[index].SetVideoLocalRoll(state.videoRoll);
                    const bool shardsOwnGeometry = state.fracture.enabled &&
                        (state.fracture.phase ==
                             CoreLogic::FracturePhase::Shattered ||
                         (state.fracture.phase ==
                              CoreLogic::FracturePhase::Rejoining &&
                          state.fracture.separation > 0.0001f));
                    if(!shardsOwnGeometry &&
                       !panels_[index].SetDeformation(
                           state.deformation,
                           songTimeSeconds,
                           realTimeSeconds))
                    {
                        PaperLogger.error(
                            "Unity rejected showcase deformation for panel {}",
                            index + 1);
                        return false;
                    }
                    if(!panels_[index].SetOpacity(state.opacity))
                    {
                        PaperLogger.error(
                            "Unity rejected showcase opacity for panel {}",
                            index + 1);
                        return false;
                    }
                    if(!panels_[index].SetFractureEffect(
                           state.fracture,
                           state.deformation,
                           songTimeSeconds,
                           realTimeSeconds))
                    {
                        // A fracture is spectacle, never a playback
                        // dependency. Leave the ordinary synchronized screen
                        // visible and report the setup failure once through
                        // the existing showcase circuit breaker.
                        PaperLogger.error(
                            "Unity rejected showcase glass effect for panel {}",
                            index + 1);
                        return false;
                    }
                }
                panels_[index].SetVisible(
                    state.visible && mediaReady_ && externallyVisible_);
            }
            return true;
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not update the showcase surfaces: {}",
                exception.what());
            return false;
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not update the showcase surfaces because of an unknown native exception");
            return false;
        }
    }

    void ShowcaseSurfaceGroup::SetMediaReady(bool ready)
    {
        mediaReady_ = ready;
    }

    void ShowcaseSurfaceGroup::SetVisible(bool visible)
    {
        externallyVisible_ = visible;
        if(!visible)
        {
            for(auto& panel : panels_)
                panel.SetVisible(false);
        }
    }

    void ShowcaseSurfaceGroup::Destroy()
    {
        // Clones must release their materials before the owner destroys the
        // shared texture. ScreenSurface's explicit ownsTexture_ bit guarantees
        // none of these calls can destroy that owner's pixel resource.
        for(auto& panel : panels_)
            panel.Destroy();
        created_ = false;
        mediaReady_ = false;
        externallyVisible_ = true;
        timelineActive_ = false;
    }
}
