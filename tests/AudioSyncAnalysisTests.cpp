// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncAnalysis.hpp"
#include "BigScreen/AudioSyncOperation.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <thread>

namespace
{
int failures = 0;
void Expect(bool condition, const char* description)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}
template <class T> void Wait(BigScreen::AudioSync::Operation<T>& op)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while(op.Busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Expect(!op.Busy(), "bounded operation completion");
}
double Signal(double time)
{
    return .3 * std::sin(2 * std::numbers::pi * (103 * time + 21 * time * time)) +
           .2 * std::sin(2 * std::numbers::pi * (331 * time + 73 * time * time));
}
} // namespace

int main()
{
    using namespace BigScreen::AudioSync;
    std::string error;
    FeatureBuilder builder;
    std::vector<float> pcm(16000);
    Expect(builder.Append(pcm, 5.0, error), "preserve source start timestamp");
    for(std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<float>(Signal(i / 16000.0));
    Expect(builder.Append(pcm, 6.0, error), "append contiguous audio");
    auto index = builder.Finish();
    Expect(!index.frames.empty() && index.frames.front().time == 5,
           "features never re-zero origin");
    Expect(index.activeStart && *index.activeStart >= 5.95,
           "active range preserves silent lead-in");
    Expect(index.activeEnd && *index.activeEnd >= 6.9, "active ending detected");
    Expect(index.frames.size() < 70, "bounded compact feature representation");
    FeatureBuilder capped(1);
    Expect(!capped.Append(pcm, 0, error), "index budget enforced");
    FeatureBuilder invalid;
    pcm[0] = std::numeric_limits<float>::quiet_NaN();
    Expect(!invalid.Append(pcm, 0, error), "invalid PCM rejected");

    std::vector<Anchor> anchors;
    for(int i = 0; i < 8; ++i)
        anchors.push_back({10.0 + i * 20, 3.25 + .98 * (10 + i * 20), .95, .2, i == 3 || i == 6});
    auto fit = FitAnchors(anchors, .01);
    Expect(fit.timing && std::abs(fit.timing->playbackRate - .98) < 1e-9 &&
               std::abs(fit.timing->offsetSeconds - 3.25) < 1e-9,
           "robust affine fit recovers timing");
    Expect(fit.confidence == Confidence::High && fit.validationCount == 2,
           "independent held-out validation");
    anchors[2].videoTime += 30;
    fit = FitAnchors(anchors, .01);
    Expect(fit.timing && std::abs(fit.timing->playbackRate - .98) < 1e-9 && fit.rejected == 1,
           "outlier cannot bias regression");
    anchors[3].videoTime += 1.5;
    fit = FitAnchors(anchors, .01);
    Expect(fit.incompatible && fit.confidence == Confidence::Low,
           "held-out discontinuity vetoes apparently perfect fit");
    Expect(fit.timing && std::abs(fit.timing->offsetSeconds - 3.25) < 1e-9,
           "held-out point excluded from final fit");
    for(auto& a : anchors)
    {
        a.videoTime = a.songTime + 3;
        a.ambiguity = .001;
    }
    Expect(FitAnchors(anchors, .01).confidence == Confidence::Low,
           "repeated-chorus ambiguity is not high confidence");
    for(auto& a : anchors)
        a.heldOut = false;
    Expect(FitAnchors(anchors, .01).confidence == Confidence::Low,
           "no validation cannot be confident");
    Expect(FindCandidates(index, index, 6.5, 2, [] { return true; }).empty(),
           "coarse matching cancellation");

    // Distinct chirps supply a known-answer rate-aware refinement fixture.
    // The video has a 120ms intro and a 0.98x media/song timing relationship.
    const int rate = 4000;
    std::vector<float> map(rate * 2), video(rate * 3);
    for(std::size_t i = 0; i < map.size(); ++i)
        map[i] = static_cast<float>(Signal(i / double(rate)));
    for(std::size_t i = 0; i < video.size(); ++i)
        video[i] = static_cast<float>(Signal((i / double(rate) - .12) / .98));
    auto refined = Refine(map, video, rate, 0, 0, {.1, .98}, .05, {});
    Expect(refined && std::abs(refined->videoTime - .12) <= 1.0 / rate,
           "rate-aware PCM refinement");
    Expect(!Refine(map, video, rate, 0, 0, {.1, .98}, .05, [] { return true; }),
           "refinement cancellation");

    Operation<int> operation;
    Expect(operation.Start(1, "map-a/source-a",
                           [](const auto& context) -> std::optional<int>
                           {
                               context.Progress("Analysing", 5, 10);
                               return 42;
                           }),
           "start worker");
    Wait(operation);
    Expect(!operation.TakeResult(2, "map-a/source-a"), "reject stale generation");
    Expect(!operation.TakeResult(1, "map-b/source-b"), "reject stale source");
    auto result = operation.TakeResult(1, "map-a/source-a");
    Expect(result && *result == 42, "publish immutable result to correct owner");
    Expect(!operation.TakeResult(1, "map-a/source-a"), "completion consumed once");
    Expect(operation.Start(2, "b", [](const auto&) -> std::optional<int>
                           { throw std::runtime_error("fixture"); }),
           "start exception fixture");
    Wait(operation);
    Expect(operation.Poll()->failure == OperationFailure::Internal,
           "worker exception classified for existing circuit breaker");
    Expect(operation.Start(3, "c",
                           [](const auto& context) -> std::optional<int>
                           {
                               context.Fail("Unsupported audio codec");
                               return {};
                           }),
           "start expected failure");
    Wait(operation);
    Expect(operation.Poll()->failure == OperationFailure::External,
           "external failure does not trip internal breaker");
    std::atomic<bool> started{false};
    Expect(operation.Start(4, "d",
                           [&](const auto& context) -> std::optional<int>
                           {
                               started = true;
                               while(!context.Cancelled())
                                   std::this_thread::yield();
                               return 7;
                           }),
           "start cancellable operation");
    while(!started)
        std::this_thread::yield();
    Expect(!operation.Start(5, "e", [](const auto&) -> std::optional<int> { return 1; }),
           "no unbounded worker fanout");
    operation.Cancel();
    Wait(operation);
    Expect(operation.Poll()->status == OperationStatus::Cancelled && !operation.TakeResult(4, "d"),
           "cancellation prevents publication");
    return failures ? 1 : 0;
}
