#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "BigScreen/MapVideoConfig.hpp"

namespace BigScreen::UpDownShowcase {
    constexpr std::size_t MaximumPanels = 8;
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
    /// practice speed, and Replay cannot desynchronize the environment strobe.
    /// Outside the 2:01-2:29 showcase cue the structure is always visible.
    bool CenterRingVisible(double songTimeSeconds);
}
