// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "BigScreen/AudioSyncOperation.hpp"
#include "BigScreen/AudioSyncPcm.hpp"
#include "BigScreen/AudioSyncReader.hpp"
#include "BigScreen/AudioSyncVisualization.hpp"

namespace BigScreen::AudioSync
{
struct Source
{
    std::filesystem::path path;
    std::string key;
    double timelineShift = 0.0;
    std::shared_ptr<const PcmLease> captured;
    Source() = default;
    Source(std::filesystem::path file, std::string identity, double shift,
           std::shared_ptr<const PcmLease> lease = {})
        : path(std::move(file)), key(std::move(identity)), timelineShift(shift),
          captured(std::move(lease))
    {
    }
};
struct PreparedSource
{
    Source source;
    AudioInfo audio;
    FeatureIndex features;
    std::shared_ptr<const PcmLease> audition;
    WaveformOverview overview;
};
struct PreparedPair
{
    PreparedSource song;
    PreparedSource video;
    // Media/marker bounds may extend past the last audible sample.
    Bounds bounds;
};
using Preparation = Operation<PreparedPair>;
using Analysis = Operation<MatchResult>;

/// Call only from the operation worker; returns owned immutable native
/// snapshots. Failure detail is delivered through Context for the owner
/// to report using Big Screen's normal diagnostics/dialog conventions.
std::optional<PreparedPair> Prepare(const Source& song, const Source& video,
                                    const Preparation::Context& context,
                                    const std::filesystem::path& cacheRoot = {},
                                    std::size_t maximumFeatures = 24000);
std::optional<MatchResult> Analyze(const PreparedPair& pair, int desiredAnchors,
                                   double windowSeconds, const Analysis::Context& context);
} // namespace BigScreen::AudioSync
