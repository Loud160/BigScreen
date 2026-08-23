// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <vector>

#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ScreenSurface.hpp"

namespace UnityEngine { class Texture; }

namespace BigScreen {
    /// Owns mapper-authored Cinema additional screens. Every panel shares the
    /// primary ScreenSurface Texture2D, so one decoder and one Unity upload
    /// feed the complete authored layout.
    class CinemaScreenGroup final {
    public:
        bool Create(
            const MapVideoConfig& primary,
            int videoWidth,
            int videoHeight,
            UnityEngine::Texture* sharedTexture);
        void Destroy();
        void SetVisible(bool visible);
        void ShowLeadIn(bool black);
        bool SetOpacity(float opacity);
        bool IsCreated() const { return !screens_.empty(); }

    private:
        std::vector<std::unique_ptr<ScreenSurface>> screens_;
    };
}
