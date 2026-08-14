#pragma once

#include <array>

#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ScreenSurface.hpp"
#include "BigScreen/UpDownShowcaseTimeline.hpp"

namespace UnityEngine { class Texture2D; }

namespace BigScreen {
    /// A fixed, preallocated pool of showcase panels. Every panel has its own
    /// geometry and transform but references the primary ScreenSurface texture.
    /// No decoder, pixel buffer, or texture upload is duplicated.
    class ShowcaseSurfaceGroup final {
    public:
        bool Create(
            const MapVideoConfig& anchorConfig,
            int videoWidth,
            int videoHeight,
            UnityEngine::Texture2D* sharedTexture);
        bool Apply(double songTimeSeconds);
        void SetMediaReady(bool ready);
        void SetVisible(bool visible);
        void Destroy();

        bool IsCreated() const { return created_; }
        bool TimelineActive() const { return timelineActive_; }

    private:
        static MapVideoConfig GeometryConfig(
            const MapVideoConfig& anchor,
            UpDownShowcase::Geometry geometry);

        std::array<ScreenSurface, UpDownShowcase::MaximumPanels> panels_{};
        std::array<UpDownShowcase::Geometry, UpDownShowcase::MaximumPanels>
            geometry_{};
        MapVideoConfig anchorConfig_{};
        bool created_ = false;
        bool mediaReady_ = false;
        bool externallyVisible_ = true;
        bool timelineActive_ = false;
    };
}
