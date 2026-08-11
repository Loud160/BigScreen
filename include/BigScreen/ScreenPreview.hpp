#pragma once

#include <optional>

#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ScreenSurface.hpp"

namespace BigScreen {
    /// Owns the optional full-size screen shown in Beat Saber's menu world.
    ///
    /// It deliberately uses ScreenSurface, the same geometry and material path
    /// as decoded song videos, so size, placement, curve, and transparency are
    /// represented at their real world scale instead of inside a UI thumbnail.
    class ScreenPreview final {
    public:
        static ScreenPreview& Instance();

        void ActivateCurrentState();
        void SetEnabled(bool enabled);
        void Refresh();
        void Suspend();

    private:
        ScreenPreview() = default;

        void CaptureBasePlacement();
        bool CreateWorldScreen();

        std::optional<MapVideoConfig> baseConfig_;
        ScreenSurface surface_;
    };
}
