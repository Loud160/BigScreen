// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/MapVideoConfig.hpp"

namespace BigScreen::UpDownShowcase {
    // The showcase reuses one decoded texture across every surface. Twelve
    // panels therefore add only transforms/material instances, not twelve
    // decoders or twelve frame uploads. Normal Big Screen layouts remain
    // unaffected by this demo-only capacity.
    constexpr std::size_t MaximumPanels = 12;
    constexpr double BeatsPerMinute = 138.0;

    // Geometry changes are intentionally discrete. Transform-only animation is
    // sampled every Unity update, while meshes are rebuilt only when this key
    // changes at a musical cue boundary.
    enum class Geometry {
        Wide,
        UltraWide,
        CurvedIn,
        CurvedOut,
        Tall,
        QuadrantTopLeft,
        QuadrantTopRight,
        QuadrantBottomLeft,
        QuadrantBottomRight
    };

    struct PanelState {
        bool visible = false;
        Float3 position{};
        Float3 rotation{};
        Float3 scale{1.0f, 1.0f, 1.0f};
        float opacity = 1.0f;
        float videoRoll = 0.0f;
        Geometry geometry = Geometry::Wide;
        CoreLogic::SurfaceDeformationSettings deformation{};
        CoreLogic::FractureEffectSettings fracture{};
    };

    struct FrameState {
        bool active = false;
        std::array<PanelState, MaximumPanels> panels{};
    };

    /// Matches only BeatSaver key 11cf8's Lawless Expert+ chart. The directory
    /// key disambiguates the other Quest map with the same song and artist.
    bool MatchesTarget(
        std::string_view songName,
        std::string_view songArtist,
        std::string_view levelDirectoryName,
        std::string_view characteristic,
        int difficulty);

    /// Samples the complete presentation directly from Beat Saber's song time.
    /// The function is deterministic and stateless, so Replay/practice seeks,
    /// pauses, and restarts cannot leave a half-applied transition behind.
    FrameState Sample(double songTimeSeconds);

    /// Returns the authored visibility of the map's central track-ring
    /// structure. This remains a pure song-time decision so pausing, seeking,
    /// practice speed, and Replay cannot desynchronize the environment state.
    /// It covers the floating carousel, flag-wave/corkscrew clearance, and the
    /// later rhythmic damage cue; all exact boundaries restore deterministically.
    bool CenterRingVisible(double songTimeSeconds);

    /// Temporarily removes Big Mirror's NearBuildingLeft/Right structures
    /// during the floating-screen carousel. The gameplay hook restores their
    /// captured state at the exact cue boundary and when the scene exits.
    bool SidePillarsVisible(double songTimeSeconds);

    /// The floating-screen carousel and corkscrew deliberately remove every
    /// captured environment renderer so their depth formations read clearly.
    /// Original renderer states are restored between cues, at each exact end
    /// boundary, and on every gameplay teardown path.
    bool BackgroundEnvironmentVisible(double songTimeSeconds);
}
