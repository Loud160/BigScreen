// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace BigScreen {

    /// A small engine-independent vector used while parsing map metadata.
    /// Keeping Unity types out of the parser makes configuration behavior easy
    /// to test on a desktop without loading Beat Saber or IL2CPP.
    struct Float3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /// One Cinema-compatible scene-object change. Optional members preserve
    /// the distinction between "not supplied" and an explicit zero/false.
    struct EnvironmentModification {
        std::string name;
        std::optional<std::string> parentName;
        std::optional<std::string> cloneFrom;
        std::optional<bool> active;
        std::optional<Float3> position;
        std::optional<Float3> rotation;
        std::optional<Float3> scale;
    };

    /// Cinema's optional per-video color transform. Defaults intentionally
    /// describe an unchanged picture so partially specified objects can be
    /// applied without inventing values for fields the mapper omitted.
    struct CinemaColorCorrection {
        float brightness = 1.0f;
        float contrast = 1.0f;
        float saturation = 1.0f;
        float hue = 0.0f;
        float exposure = 1.0f;
        float gamma = 1.0f;
    };

    /// Mapper-authored edge mask used by Cinema's screen shader.
    struct CinemaVignette {
        std::string type = "rectangular";
        float radius = 1.0f;
        float softness = 0.005f;
    };

    /// One additional Cinema screen. Missing members inherit the primary
    /// screen's transform exactly, matching Cinema's clone-based behavior.
    struct CinemaAdditionalScreen {
        std::optional<Float3> position;
        std::optional<Float3> rotation;
        std::optional<Float3> scale;
    };

    /// Normalized Cinema-compatible mapper metadata consumed by Big Screen.
    ///
    /// Field names accepted by LoadFromLevel are interoperability inputs. The
    /// runtime owns this normalized representation, so rendering and timing do
    /// not depend on any particular JSON library or legacy class hierarchy.
    struct MapVideoConfig {
        std::filesystem::path metadataPath;
        std::filesystem::path videoPath;
        std::filesystem::path declaredVideoPath;

        // Download identity is retained even when the mapper intentionally
        // ships metadata without the much larger video file. Big Screen can
        // then offer an explicit download without treating a normal Cinema
        // map as a malformed configuration.
        std::optional<std::string> videoId;
        std::optional<std::string> videoUrl;
        std::optional<std::string> title;
        std::optional<std::string> author;
        bool configByMapper = false;
        // True only when metadata contains presentation fields rather than a
        // video URL/timing alone. Respect Mapper Settings uses the narrower
        // ownership flags below so ordinary videos keep the selected layout.
        bool hasMapperPresentation = false;
        // Screen ownership is narrower than general presentation ownership.
        // Environment, transparency, and timing fields must not disable Big
        // Screen's canvas controls. Only authored position, rotation, size, or
        // curvature qualifies as custom screen geometry.
        bool hasMapperScreenGeometry = false;
        // Environment ownership remains independent from screen ownership so
        // a Chroma map can retain its intended scene without taking over an
        // otherwise ordinary Big Screen video canvas.
        bool hasMapperEnvironmentPresentation = false;
        bool disableDefaultModifications = false;
        bool forceEnvironmentModifications = false;
        std::optional<bool> allowCustomPlatform;
        bool mergePropGroups = false;
        std::optional<bool> colorBlending;

        double offsetSeconds = 0.0;
        double playbackRate = 1.0;
        double declaredDurationSeconds = 0.0;
        bool fitToSong = false;
        bool blackDuringLeadIn = false;
        std::optional<double> stopAtVideoSecond;
        bool loop = false;

        Float3 screenPosition{0.0f, 12.0f, 60.0f};
        Float3 screenRotation{-8.0f, 0.0f, 0.0f};
        float screenHeight = 25.0f;
        std::optional<float> screenWidthOverride;
        float screenCurvature = 0.0f;
        // Cinema expresses mapper curvature as arc degrees. Big Screen's own
        // layout slider uses a signed bow amount, so the two representations
        // stay separate instead of applying a lossy guessed conversion.
        std::optional<float> cinemaCurvatureDegrees;
        bool cinemaCurveYAxis = false;
        bool maintainAspectRatioWhenCurved = false;
        int screenSegments = 32;
        // The screen canvas and decoded picture have independent alpha
        // controls. letterboxTransparent affects only unused canvas exposed
        // by aspect-ratio preservation, rotation, pan, or zoom.
        bool letterboxTransparent = false;
        // Cinema's mapper `transparency` flag controls a full opaque body
        // behind the picture so additive video cannot reveal lights behind
        // the screen. It is independent from Big Screen's letterbox and
        // picture-opacity controls.
        bool opaqueScreenBody = false;
        float videoOpacity = 1.0f;
        // Layout-scoped presentation transforms affect only the image inside
        // the physical screen frame. Screen rotation/placement remains wholly
        // independent so a user can rotate content without moving the frame.
        float videoRotation = 0.0f;
        float videoZoom = 1.0f;
        float videoOffsetX = 0.0f;
        float videoOffsetY = 0.0f;
        float videoTilt = 0.0f;
        bool stretchVideoToFit = false;
        std::optional<bool> mapperTransparency;
        std::optional<CinemaColorCorrection> colorCorrection;
        std::optional<CinemaVignette> vignette;
        std::optional<std::string> requestedEnvironment;
        std::vector<EnvironmentModification> environmentModifications;
        std::vector<CinemaAdditionalScreen> additionalScreens;

        /// Replaces only mapper-authored presentation geometry with Big
        /// Screen's neutral back-wall canvas. Media identity, download URL,
        /// timing, and mapper ownership markers remain intact. This is used
        /// when Respect Mapper Settings is disabled so user offsets are never
        /// added on top of a mapper's custom screen position.
        void ResetPresentationToDefaults();
        void ResetScreenGeometryToDefaults();
        void ResetMapperVisualEffects();

        /// Looks for Big Screen's native file first, followed by compatible map
        /// metadata names already present in existing custom levels.
        static std::optional<MapVideoConfig> LoadFromLevel(
            const std::filesystem::path& levelDirectory,
            std::string& error);

        /// Parses mapper metadata even when its video has not been downloaded.
        /// LoadFromLevel remains the playback-safe API and returns only configs
        /// whose local media file exists.
        static std::optional<MapVideoConfig> LoadDefinitionFromLevel(
            const std::filesystem::path& levelDirectory,
            std::string& error);

        /// Parses one explicitly named metadata file inside a level folder.
        /// The production loader uses this for Big Screen's bundled Quest
        /// compatibility cycle so every ten-second phase exercises the same
        /// Cinema parser as an ordinary map rather than duplicating values in
        /// test-only C++ code.
        static std::optional<MapVideoConfig> LoadDefinitionFromFile(
            const std::filesystem::path& levelDirectory,
            const std::filesystem::path& metadata,
            std::string& error);

        std::optional<std::string> DownloadUrl() const;
        bool HasLocalVideo() const;

        /// Converts Beat Saber's authoritative song position to the media time
        /// the decoder should display. AudioTimeSyncController already includes
        /// practice and gameplay speed, while playbackRate is a mapper-authored
        /// adjustment specific to the video file.
        double MediaTimeForSong(double songTimeSeconds, double decodedDurationSeconds) const;
    };
}
