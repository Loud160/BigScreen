// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

namespace BigScreen {
    struct MapVideoConfig;

    /// Applies the small, data-driven environment transformation subset used
    /// by Cinema maps. Work occurs once as gameplay starts, never per frame.
    namespace CinemaEnvironment {
        /// Creates mapper-requested clones early and temporarily offsets them
        /// before Chroma assigns environment prop groups. This mirrors PC
        /// Cinema's mergePropGroups behavior and prevents an unmerged clone
        /// from silently joining its source light group.
        void Prepare(const MapVideoConfig& config);
        void Apply(const MapVideoConfig& config);
        void Cleanup();
    }
}
