// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncService.hpp"
#include "BigScreen/AudioSyncVisualization.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

template <class T> bool Wait(BigScreen::AudioSync::Operation<T>& operation)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while(operation.Busy() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(operation.Busy())
    {
        operation.Cancel();
        return false;
    }
    const auto state = operation.Poll();
    if(state->status != BigScreen::AudioSync::OperationStatus::Succeeded)
    {
        std::cerr << state->error << '\n';
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    if(argc < 2 || argc > 3)
        return 2;
    using namespace BigScreen::AudioSync;
    const std::filesystem::path root(argv[1]);
    const std::string scenario = argc == 3 ? argv[2] : "matching-video.wav";
    const bool identity = scenario == "aligned-mastered.wav";
    const bool positive = identity || scenario == "matching-video.wav" ||
                          scenario == "matching-video.m4a";
    const Source song{root / (scenario == "repeated.wav" ? "repeated.wav" : "matching-map.wav"),
                      "map-fixture", 0};
    const Source video{root / scenario, "video-fixture", 0};
    Preparation preparation;
    preparation.Start(1, "fixture", [=](const auto& context)
                      { return Prepare(song, video, context, root / "sync-cache"); });
    if(!Wait(preparation))
        return 1;
    const auto pair = preparation.TakeResult(1, "fixture");
    if(!pair || !pair->song.audition || !pair->video.audition)
        return 1;
    const auto validWaveformMask = [](const auto& pixels, std::size_t expected)
    {
        if(pixels.size() != expected)
            return false;
        bool transparent = false, visible = false;
        for(const auto alpha : pixels)
        {
            transparent |= alpha == 0;
            visible |= alpha > 0;
        }
        return transparent && visible;
    };
    const auto validOverview = [&](const auto& overview)
    {
        return overview.detailWidth >= 2 &&
               overview.detailWidth <= OverviewMaximumDetailWidth &&
               validWaveformMask(overview.summary,
                                 OverviewSummaryWidth * OverviewHeight) &&
               validWaveformMask(overview.detail,
                                 overview.detailWidth * OverviewHeight);
    };
    if(!validOverview(pair->song.overview) || !validOverview(pair->video.overview))
    {
        std::cerr << "Prepared overview does not contain valid summary/detail masks\n";
        return 1;
    }
    PcmCache::ClearUnused(root / "sync-cache"); // old fixture generations may be evicted
    if(!std::filesystem::exists(pair->song.audition->path) ||
       !std::filesystem::exists(pair->video.audition->path))
        return 1; // current entries remain pinned across cleanup
    Analysis analysis;
    analysis.Start(1, "fixture",
                   [pair](const auto& context) { return Analyze(*pair, 5, 2, context); });
    if(!Wait(analysis))
        return 1;
    const auto result = analysis.TakeResult(1, "fixture");
    if(!result)
        return 1;
    std::cout << result->explanation << '\n';
    for(const auto& anchor : result->anchors)
        std::cout << "anchor " << anchor.songTime << " -> " << anchor.videoTime
                  << " score=" << anchor.strength << " heldout=" << anchor.heldOut << '\n';
    if(result->timing)
        std::cout << "offset=" << result->timing->offsetSeconds
                  << " rate=" << result->timing->playbackRate
                  << " validation=" << result->validationMaxSeconds << '\n';
    if(!positive)
    {
        if(result->timing && !result->incompatible && result->confidence != Confidence::Low)
        {
            std::cerr << "Incorrect confident match for " << scenario << '\n';
            return 1;
        }
        return 0;
    }
    const double expectedOffset = identity ? 0.0 : .3;
    const double expectedRate = identity ? 1.0 : 16000.0 / 16327;
    if(!result->timing ||
       std::abs(result->timing->offsetSeconds - expectedOffset) > .015 ||
       std::abs(result->timing->playbackRate - expectedRate) > .001 ||
       result->validationCount < 1)
        return 1;
    return 0;
}
