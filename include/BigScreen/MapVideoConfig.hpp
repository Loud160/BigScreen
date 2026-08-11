#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace BigScreen {

    /// A small engine-independent vector used while parsing map metadata.
    /// Keeping Unity types out of the parser makes configuration behavior easy
    /// to test on a desktop without loading Beat Saber or IL2CPP.
    struct Float3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /// The subset of map video metadata Big Screen currently needs.
    ///
    /// Field names accepted by LoadFromLevel are interoperability inputs. The
    /// runtime owns this normalized representation, so rendering and timing do
    /// not depend on any particular JSON library or legacy class hierarchy.
    struct MapVideoConfig {
        std::filesystem::path metadataPath;
        std::filesystem::path videoPath;

        double offsetSeconds = 0.0;
        double playbackRate = 1.0;
        double declaredDurationSeconds = 0.0;
        std::optional<double> stopAtVideoSecond;
        bool loop = false;

        Float3 screenPosition{0.0f, 12.0f, 60.0f};
        Float3 screenRotation{-8.0f, 0.0f, 0.0f};
        float screenHeight = 25.0f;
        float screenCurvature = 0.0f;
        int screenSegments = 32;
        bool transparent = false;
        std::optional<std::string> requestedEnvironment;

        /// Looks for Big Screen's native file first, followed by compatible map
        /// metadata names already present in existing custom levels.
        static std::optional<MapVideoConfig> LoadFromLevel(
            const std::filesystem::path& levelDirectory,
            std::string& error);

        /// Converts Beat Saber's authoritative song position to the media time
        /// the decoder should display. AudioTimeSyncController already includes
        /// practice and gameplay speed, while playbackRate is a mapper-authored
        /// adjustment specific to the video file.
        double MediaTimeForSong(double songTimeSeconds, double decodedDurationSeconds) const;
    };
}
