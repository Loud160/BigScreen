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
        void Apply(const MapVideoConfig& config);
    }
}
