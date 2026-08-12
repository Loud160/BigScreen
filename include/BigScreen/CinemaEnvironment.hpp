#pragma once

namespace BigScreen {
    struct MapVideoConfig;

    /// Applies the small, data-driven environment transformation subset used
    /// by Cinema maps. Work occurs once as gameplay starts, never per frame.
    namespace CinemaEnvironment {
        void Apply(const MapVideoConfig& config);
    }
}
