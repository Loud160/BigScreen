// SPDX-License-Identifier: GPL-3.0-only
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// SPDX-FileCopyrightText: © 2026 Loud160 and the Big Screen contributors
#include "BigScreen/AudioSyncVisualization.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace BigScreen::AudioSync
{
namespace
{
std::vector<float> BucketPeaks(const FeatureIndex& index, double duration, int width)
{
    std::vector<float> peaks(static_cast<std::size_t>(width));
    if(duration <= 0 || width <= 0)
        return peaks;
    for(const auto& frame : index.frames)
    {
        if(frame.time < 0 || frame.time > duration)
            continue;
        const auto x = std::clamp(static_cast<int>(frame.time / duration * width), 0, width - 1);
        peaks[static_cast<std::size_t>(x)] =
            std::max(peaks[static_cast<std::size_t>(x)], std::clamp(frame.peak, 0.0f, 1.0f));
    }
    // Normalize display amplitude only. This does not touch matching data or
    // audio gain; it merely prevents a quiet source from becoming a nearly
    // flat graph that cannot be aligned visually.
    const float maximum = *std::max_element(peaks.begin(), peaks.end());
    if(maximum > 1e-6f)
        for(auto& peak : peaks)
            peak = std::pow(peak / maximum, .7f);
    return peaks;
}

std::vector<std::uint8_t> FilledEnvelope(const std::vector<float>& peaks)
{
    std::vector<std::uint8_t> mask(peaks.size() * OverviewHeight);
    constexpr float center = (OverviewHeight - 1) * .5f;
    constexpr float radius = center - 1.0f;
    for(std::size_t x = 0; x < peaks.size(); ++x)
    {
        const int extent = static_cast<int>(std::lround(peaks[x] * radius));
        const int low = std::max(0, static_cast<int>(std::floor(center)) - extent);
        const int high = std::min(OverviewHeight - 1,
                                  static_cast<int>(std::ceil(center)) + extent);
        for(int y = low; y <= high; ++y)
            mask[static_cast<std::size_t>(y) * peaks.size() + x] = 230;
        // A quiet baseline keeps silent passages and media tails legible.
        if(extent == 0)
        {
            mask[static_cast<std::size_t>(OverviewHeight / 2 - 1) * peaks.size() + x] = 45;
            mask[static_cast<std::size_t>(OverviewHeight / 2) * peaks.size() + x] = 45;
        }
    }
    return mask;
}

void Plot(std::vector<std::uint8_t>& mask, int width, int x, int y, std::uint8_t alpha)
{
    if(x < 0 || x >= width || y < 0 || y >= OverviewHeight)
        return;
    auto& pixel = mask[static_cast<std::size_t>(y) * width + x];
    pixel = std::max(pixel, alpha);
}

std::vector<std::uint8_t> OutlineEnvelope(const std::vector<float>& peaks)
{
    const int width = static_cast<int>(peaks.size());
    std::vector<std::uint8_t> mask(peaks.size() * OverviewHeight);
    constexpr float bottom = OverviewHeight - 2.0f;
    constexpr float range = OverviewHeight - 3.0f;
    auto ordinate = [&](int x)
    {
        return static_cast<int>(std::lround(bottom - peaks[x] * range));
    };
    // The feature index stores one mono peak magnitude, not signed stereo PCM.
    // Drawing that magnitude above and below a centerline made one source look
    // like two audio channels even though the lower trace was only a mirror.
    // Use one full-height amplitude trace instead: silence rests at the bottom
    // and stronger peaks rise toward the top. Map and video still retain their
    // distinct Unity tints, particularly when overlaid for comparison.
    for(int x = 0; x < width; ++x)
    {
        const int y = ordinate(x);
        const int previous = ordinate(std::max(0, x - 1));
        for(int point = std::min(previous, y); point <= std::max(previous, y); ++point)
            Plot(mask, width, x, point, 235);
        Plot(mask, width, x, y - 1, 90);
        Plot(mask, width, x, y + 1, 90);
    }
    return mask;
}
} // namespace

WaveformOverview BuildOverview(const FeatureIndex& index, double duration)
{
    WaveformOverview result;
    result.summary = FilledEnvelope(BucketPeaks(index, duration, OverviewSummaryWidth));
    const auto naturalWidth =
        duration > 0 && index.stepSeconds > 0
            ? static_cast<int>(std::ceil(duration / index.stepSeconds))
            : static_cast<int>(index.frames.size());
    result.detailWidth = std::clamp(naturalWidth, 2, OverviewMaximumDetailWidth);
    result.detail = OutlineEnvelope(BucketPeaks(index, duration, result.detailWidth));
    return result;
}
} // namespace BigScreen::AudioSync
