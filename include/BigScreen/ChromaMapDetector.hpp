// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "BigScreen/MapVideoConfig.hpp"

namespace BigScreen {
    /// Detects whether a custom map delegates any of its presentation to
    /// Chroma. This is intentionally map-wide: a user can assign one video to
    /// the song and then launch any characteristic or difficulty, so allowing
    /// Big Screen's environment override on some difficulties would produce
    /// inconsistent behavior and can fight Chroma's scene modifications.
    class ChromaMapDetector final {
    public:
        /// Returns true when Info.dat or any difficulty file declares Chroma,
        /// or when a difficulty contains a Chroma environment-modification
        /// array. `reason` is suitable for diagnostic logging and is empty
        /// when the map does not use Chroma.
        static bool UsesChroma(
            const std::filesystem::path& levelDirectory,
            std::string& reason);

        /// Applies only the initial Chroma environment instructions that target
        /// PC Cinema's canonical `CinemaScreen` object to a copy of the screen
        /// configuration used by Big Screen's menu previews. Gameplay does not
        /// use this path: Quest Chroma owns the real beatmap data, tracks, and
        /// environment pass there. This bridge exists because Chroma is not
        /// running a gameplay beatmap while the user previews a video in the
        /// menus, so a mapper can otherwise leave Cinema's base screen parked
        /// offscreen and rely on Chroma-created duplicates that never appear.
        ///
        /// The characteristic/difficulty pair selects the same difficulty file
        /// as Beat Saber. Passing an empty characteristic or a negative
        /// difficulty performs a deterministic fallback scan, used only until
        /// the Solo detail view reports its selected BeatmapKey.
        static bool ApplyCinemaScreenPreview(
            const std::filesystem::path& levelDirectory,
            std::string_view characteristic,
            int difficulty,
            MapVideoConfig& config,
            std::string& reason);
    };
}
