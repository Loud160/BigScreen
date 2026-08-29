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
#include <memory>
#include <string>

namespace BigScreen {
    enum class VideoTranscodeEncoder : std::uint8_t {
        None,
        AndroidMediaCodec,
        X264Software
    };

    struct VideoTranscodeResult {
        bool completed = false;
        bool cancelled = false;
        VideoTranscodeEncoder encoder = VideoTranscodeEncoder::None;
        std::uint64_t outputBytes = 0;
        std::string hardwareFailure;
        std::string detail;
    };

    /// ABI-neutral boundary around the FFmpeg 9 transcoder. No FFmpeg public
    /// structures cross into libbigscreen.so, preserving the same runtime
    /// isolation used by the switchable 4.4/9 decoder backends.
    class VideoTranscoderBackend {
    public:
        using ProgressCallback =
            std::function<void(std::uint64_t completed, std::uint64_t total)>;
        using CancellationCheck = std::function<bool()>;

        virtual ~VideoTranscoderBackend() = default;
        virtual VideoTranscodeResult TranscodeToH264Mp4(
            const std::filesystem::path& inputPath,
            const std::filesystem::path& outputPath,
            void* javaVm,
            const ProgressCallback& progress,
            const CancellationCheck& cancelled) = 0;
    };

#if defined(_WIN32)
#define BIGSCREEN_TRANSCODER_BACKEND_EXPORT __declspec(dllexport)
#else
#define BIGSCREEN_TRANSCODER_BACKEND_EXPORT \
    __attribute__((visibility("default")))
#endif

    BIGSCREEN_TRANSCODER_BACKEND_EXPORT
    std::unique_ptr<VideoTranscoderBackend> CreateVideoTranscoder9Backend();
}
