// SPDX-License-Identifier: GPL-3.0-only
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// SPDX-FileCopyrightText: © 2026 Loud160 and the Big Screen contributors
#pragma once
#include "BigScreen/AudioSyncAnalysis.hpp"
#include <cstdint>
namespace BigScreen::AudioSync
{
// The waveform has two bounded levels of detail. The fixed-width summary is a
// dense filled envelope suited to showing an entire song. The detail mask
// keeps one column per 32 ms analyzer frame (up to the existing feature-index
// limit) and draws one full-height mono amplitude trace, so zooming reveals
// individual peaks without a misleading mirrored second trace. Both are Alpha8 masks:
// Unity supplies the distinct map/video colors without four redundant color
// bytes per pixel, which keeps the Quest 2 memory cost below the previous
// single RGBA overview in ordinary songs despite retaining more information.
constexpr int OverviewSummaryWidth = 4096;
constexpr int OverviewMaximumDetailWidth = 24000;
constexpr int OverviewHeight = 48;
struct WaveformOverview
{
    int detailWidth = 0;
    std::vector<std::uint8_t> summary;
    std::vector<std::uint8_t> detail;
};
WaveformOverview BuildOverview(const FeatureIndex& index, double duration);
} // namespace BigScreen::AudioSync
