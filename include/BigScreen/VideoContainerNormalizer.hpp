// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace BigScreen {
    enum class VideoNormalizationState {
        Ready,
        Remuxed,
        SoftwareDecoderRequired,
        Cancelled,
        Failed
    };

    struct VideoNormalizationResult {
        VideoNormalizationState state = VideoNormalizationState::Failed;
        std::string detail;
        std::uint64_t outputBytes = 0;
    };

    /// Converts a video-only H.264 MPEG-TS download into a seek-safe MP4
    /// without decoding or re-encoding its pictures. Direct MP4/WebM inputs
    /// are validated and returned unchanged. All work runs on the caller's
    /// background thread; callbacks must therefore remain Unity-free.
    class VideoContainerNormalizer final {
    public:
        using ProgressCallback =
            std::function<void(std::uint64_t completed, std::uint64_t total)>;
        using CancellationCheck = std::function<bool()>;

        static VideoNormalizationResult PrepareDownloadedVideo(
            const std::filesystem::path& downloadedPath,
            const std::filesystem::path& remuxedPath,
            int requestedHeight,
            const ProgressCallback& progress,
            const CancellationCheck& cancelled);
    };
}
