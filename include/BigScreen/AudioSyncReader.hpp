// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "BigScreen/AudioSyncAnalysis.hpp"
#include <filesystem>
#include <functional>
#include <span>

namespace BigScreen::AudioSync
{
struct AudioInfo
{
    bool available = false;
    std::string codec;
    std::string error;
    double durationSeconds = 0.0;
    double streamStartSeconds = 0.0;
    int sampleRate = 0;
    int channels = 0;
};

struct ReadRequest
{
    std::filesystem::path path;
    // Coordinates are on the final video's (or effective song's) clock.
    // Zero is NOT automatically subtracted from the first decoded frame.
    double sourceToFinalShiftSeconds = 0.0;
    double startSeconds = 0.0;
    double endSeconds = 86400.0;
    int outputRate = 16000;
    CancelCheck cancelled;
};

struct ReadOutcome
{
    AudioInfo info;
    bool cancelled = false;
    std::uint64_t outputSamples = 0;
    std::string error;
};

/// Worker-only native FFmpeg 4 audio path. No Unity pointers and no video
/// decoder contexts cross this boundary. The chosen video decoder/toggle
/// is irrelevant to audio analysis; codec ABIs must never be mixed.
AudioInfo ProbeAudio(const std::filesystem::path& path, const CancelCheck& cancelled);
/// Used only for a separate YouTube companion. An embedded audio stream
/// already shares video timestamps and must not receive this shift.
std::optional<double> VideoStartTime(const std::filesystem::path& path,
                                     const CancelCheck& cancelled, std::string& error);
/// Worker-only source identity for cache/profile invalidation. This is a
/// path/size/modification-time identity, not a cryptographic authenticity
/// claim. Empty means the source cannot be stat'ed and is not eligible.
std::string SourceFingerprint(const std::filesystem::path& path);

/// Emits short mono floating-point blocks at outputRate. The sink consumes
/// the span synchronously and must copy only what it needs. Returning false
/// requests an early successful stop (for a bounded window); cancellation
/// is separately identified from request.cancelled, not logged as failure.
ReadOutcome ReadAudio(const ReadRequest& request,
                      const std::function<bool(std::span<const float>, double)>& sink);
} // namespace BigScreen::AudioSync
