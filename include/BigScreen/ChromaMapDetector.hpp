#pragma once

#include <filesystem>
#include <string>

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
    };
}
