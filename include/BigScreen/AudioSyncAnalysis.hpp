// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "BigScreen/AudioSyncModel.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BigScreen::AudioSync
{
struct Feature
{
    double time = 0.0;
    float rms = 0.0f;
    float peak = 0.0f;
    float onset = 0.0f;
    std::array<float, 16> bands{};
};

struct FeatureIndex
{
    std::vector<Feature> frames;
    double stepSeconds = 0.032;
    std::optional<double> activeStart;
    std::optional<double> activeEnd;
};

/// Streaming 16 kHz mono input -> compact spectral/waveform index. No
/// complete PCM track is retained. A 512-sample FFT (32 ms) has enough
/// coarse frequency detail to disambiguate regions; precise offsets must
/// subsequently be refined from local PCM, not claimed from these bins.
class FeatureBuilder final
{
  public:
    explicit FeatureBuilder(std::size_t maximumFrames = 32000);
    bool Append(std::span<const float> mono, double firstSampleTime, std::string& error);
    FeatureIndex Finish();

  private:
    void Emit(std::size_t count);
    FeatureIndex index_;
    std::array<float, 512> samples_{};
    std::array<float, 16> previousBands_{};
    std::size_t used_ = 0;
    std::size_t maximumFrames_;
    double frameStart_ = 0.0;
    std::optional<double> nextTime_;
};

struct Anchor
{
    double songTime = 0.0;
    double videoTime = 0.0;
    double strength = 0.0;
    double ambiguity = 0.0; // difference between selected and strongest alternative
    bool heldOut = false;
};

struct MatchResult
{
    std::optional<Timing> timing;
    Confidence confidence = Confidence::Low;
    std::string explanation;
    std::vector<Anchor> anchors;
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::size_t validationCount = 0;
    double fitRmsSeconds = 0.0;
    double validationMaxSeconds = 0.0;
    bool incompatible = false;
};

/// Robust deterministic two-point consensus followed by centered weighted
/// regression. Held-out points NEVER participate in hypothesis scoring or
/// the regression; their residuals are independent validation evidence.
MatchResult FitAnchors(std::span<const Anchor> anchors, double toleranceSeconds);

using CancelCheck = std::function<bool()>;
using AnalysisProgress = std::function<void(std::size_t, std::size_t)>;

/// Bounded coarse candidates for one region; multiple candidates are kept
/// so a repeated chorus cannot win solely from one local correlation peak.
struct Candidate
{
    double videoTime = 0.0;
    double score = 0.0;
    double playbackRate = 1.0; // meaningful for PCM refinement only
};
std::vector<Candidate> FindCandidates(const FeatureIndex& song, const FeatureIndex& video,
                                      double songTime, double windowSeconds,
                                      const CancelCheck& cancelled);

/// Local normalized PCM correlation at the proposed RATE. This compensates
/// drift within the refinement window rather than aligning unequal-rate
/// arrays as if both clocks ran at 1x. Caller supplies bounded, predecoded
/// windows, with their real origins, resampled to the same sample rate.
std::optional<Candidate> Refine(std::span<const float> song, std::span<const float> video,
                                int sampleRate, double songStart, double videoStart,
                                Timing predicted, double searchRadiusSeconds,
                                const CancelCheck& cancelled);
} // namespace BigScreen::AudioSync
