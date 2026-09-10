// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncService.hpp"
#include "BigScreen/AudioSyncVisualization.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace BigScreen::AudioSync
{
namespace
{
std::optional<PreparedSource> PrepareOne(const Source& source, const char* label,
                                         const Preparation::Context& context,
                                         const std::filesystem::path& cacheRoot,
                                         std::size_t maximumFeatures)
{
    PreparedSource prepared;
    prepared.source = source;
    FeatureBuilder builder(maximumFeatures);
    std::string builderError;
    ReadRequest request;
    request.path = source.path;
    request.sourceToFinalShiftSeconds = source.timelineShift;
    request.cancelled = [&] { return context.Cancelled(); };
    context.Progress(label);
    prepared.audio = ProbeAudio(source.path, request.cancelled);
    if(context.Cancelled())
        return std::nullopt;
    if(!prepared.audio.available)
    {
        context.Fail(prepared.audio.error);
        return std::nullopt;
    }
    const double sourceEnd = prepared.audio.streamStartSeconds +
                             prepared.audio.durationSeconds + source.timelineShift;
    if(!cacheRoot.empty())
    {
        prepared.audition =
            source.captured
                ? source.captured
                : PcmCache::Prepare(
                      request, cacheRoot,
                      [&](double time)
                      {
                          context.Progress(
                              label, static_cast<std::uint64_t>(std::max(0.0, time) * 1000),
                              static_cast<std::uint64_t>(std::max(0.0, sourceEnd) * 1000));
                      },
                      builderError);
        if(context.Cancelled())
            return std::nullopt;
        if(!prepared.audition)
        {
            context.Fail(builderError);
            return std::nullopt;
        }
        // The cached PCM already includes the timeline shift/padding.
        // Applying source.timelineShift again would double the offset.
        request.path = prepared.audition->path;
        request.sourceToFinalShiftSeconds = 0;
        prepared.source.path = request.path;
        prepared.source.timelineShift = 0;
    }
    // Keep progress truthful when a container has no trustworthy
    // duration. Work remains bounded by the feature builder in either
    // case, and no complete PCM track is held between callbacks.
    const auto read = ReadAudio(
        request,
        [&](std::span<const float> samples, double time)
        {
            if(context.Cancelled())
                return false;
            if(!builder.Append(samples, time, builderError))
                return false;
            if(sourceEnd > 0 && std::isfinite(sourceEnd))
                context.Progress(label, static_cast<std::uint64_t>(std::max(0.0, time) * 1000),
                                 static_cast<std::uint64_t>(sourceEnd * 1000));
            return true;
        });
    if(context.Cancelled() || read.cancelled)
        return std::nullopt;
    if(!builderError.empty() || !read.error.empty())
    {
        context.Fail(builderError.empty() ? read.error : builderError);
        return std::nullopt;
    }
    prepared.features = builder.Finish();
    return prepared;
}

struct Region
{
    double songTime = 0;
    bool heldOut = false;
    std::vector<Candidate> candidates;
};

std::optional<std::pair<std::vector<float>, double>>
Window(const Source& source, double start, double end, const Analysis::Context& context)
{
    ReadRequest request;
    request.path = source.path;
    request.sourceToFinalShiftSeconds = source.timelineShift;
    request.startSeconds = std::max(0.0, start);
    request.endSeconds = end;
    request.outputRate = 4000;
    request.cancelled = [&] { return context.Cancelled(); };
    std::vector<float> samples;
    // Two local mono windows, never whole-track PCM. Even at the
    // maximum 8x rate and 20s window each buffer stays below 3 MB.
    constexpr std::size_t MaximumSamples = 4000 * 170;
    double origin = 0;
    bool oversized = false, discontinuous = false;
    const auto read =
        ReadAudio(request,
                  [&](std::span<const float> chunk, double time)
                  {
                      if(samples.empty())
                          origin = time;
                      else if(std::abs(time - origin - samples.size() / 4000.0) > .001)
                      {
                          discontinuous = true;
                          return false;
                      }
                      if(chunk.size() > MaximumSamples - samples.size())
                      {
                          oversized = true;
                          return false;
                      }
                      samples.insert(samples.end(), chunk.begin(), chunk.end());
                      return !context.Cancelled();
                  });
    if(read.cancelled || context.Cancelled())
        return std::nullopt;
    if(!read.error.empty() || oversized || discontinuous)
    {
        context.Fail(oversized ? "Audio refinement exceeds its memory budget."
                     : discontinuous
                         ? "Audio timestamps are discontinuous inside the matching window."
                         : read.error);
        return std::nullopt;
    }
    return std::pair{std::move(samples), origin};
}
} // namespace

std::optional<PreparedPair> Prepare(const Source& song, const Source& video,
                                    const Preparation::Context& context,
                                    const std::filesystem::path& cacheRoot,
                                    std::size_t maximumFeatures)
{
    auto first = PrepareOne(song, "Preparing map audio", context, cacheRoot, maximumFeatures);
    if(!first)
        return std::nullopt;
    auto second =
        PrepareOne(video, "Preparing video audio", context, cacheRoot, maximumFeatures);
    if(!second)
        return std::nullopt;
    const Bounds bounds{
        first->audition ? first->audition->duration : first->audio.durationSeconds,
        second->audition ? second->audition->duration : second->audio.durationSeconds};
    // Build each tintable waveform once, after both authoritative media
    // bounds are known. PrepareOne used to render an intermediate tile that
    // the integration layer immediately replaced; the larger zoom-capable
    // masks make avoiding that duplicate worker work worthwhile.
    first->overview = BuildOverview(first->features, bounds.songDuration);
    second->overview = BuildOverview(second->features, bounds.videoDuration);
    return PreparedPair{std::move(*first), std::move(*second), bounds};
}

std::optional<MatchResult> Analyze(const PreparedPair& pair, int desiredAnchors, double window,
                                   const Analysis::Context& context)
{
    if(desiredAnchors < 5 || desiredAnchors > 16 || !std::isfinite(window) || window < 2 ||
       window > 20)
    {
        context.Fail("Analysis effort is outside the supported limits.");
        return std::nullopt;
    }
    MatchResult unavailable;
    unavailable.explanation = "Not enough distinct audio to establish a reliable "
                              "alignment. Try manual synchronization.";
    const auto& song = pair.song.features;
    const auto& video = pair.video.features;
    if(!song.activeStart || !song.activeEnd || !video.activeStart || !video.activeEnd ||
       *song.activeEnd - *song.activeStart < window * 2)
        return unavailable;
    std::vector<Region> regions;
    const int count = desiredAnchors + 2;
    const double firstTime = *song.activeStart + window * .5;
    const double lastTime = *song.activeEnd - window * .5;
    const double spacing = (lastTime - firstTime) / (count - 1);
    std::vector<Timing> timelinePriors;
    // Coarse spectral fingerprints intentionally discard mastering and volume
    // differences. That makes them cheap and robust, but on highly repetitive
    // music their four highest local matches can all be the wrong chorus. Keep
    // two independently derived timeline hypotheses in the bounded candidate
    // set so an already aligned (or uniformly stretched) pair cannot lose its
    // true correspondence before the accurate PCM refinement stage runs.
    // These priors are evidence seeds, not accepted matches: their neutral .5
    // score can only establish a global hypothesis, and every resulting anchor
    // still has to pass localized normalized cross-correlation plus held-out
    // validation before confidence can rise above Low.
    if(pair.bounds.songDuration > 0 && pair.bounds.videoDuration > 0)
        timelinePriors.push_back(
            {0.0, pair.bounds.videoDuration / pair.bounds.songDuration});
    const double songActiveSpan = *song.activeEnd - *song.activeStart;
    const double videoActiveSpan = *video.activeEnd - *video.activeStart;
    if(songActiveSpan > 0 && videoActiveSpan > 0)
    {
        const double rate = videoActiveSpan / songActiveSpan;
        timelinePriors.push_back(
            {*video.activeStart - rate * *song.activeStart, rate});
    }
    for(int regionIndex = 0; regionIndex < count; ++regionIndex)
    {
        if(context.Cancelled())
            return std::nullopt;
        context.Progress("Finding distinct audio regions", regionIndex, count);
        const double nominal = firstTime + spacing * regionIndex;
        // Inspect cheap alternatives around each distributed position,
        // choosing transient-rich evidence instead of blindly trusting an
        // equally spaced silent frame. Distribution still includes useful
        // material near the end; active endpoints are not correspondences.
        double selected = nominal, bestInformation = -1;
        for(const auto& frame : song.frames)
        {
            if(std::abs(frame.time - nominal) > spacing * .3 || frame.time < firstTime ||
               frame.time > lastTime)
                continue;
            const double information = frame.rms * (.1 + frame.onset);
            if(information > bestInformation)
            {
                bestInformation = information;
                selected = frame.time;
            }
        }
        const bool heldOut = regionIndex == count / 2 || regionIndex == count - 2;
        auto candidates =
            FindCandidates(song, video, selected, window, [&] { return context.Cancelled(); });
        for(const auto& prior : timelinePriors)
        {
            const auto predicted = VideoTime(prior, selected);
            if(!predicted || *predicted < 0 || *predicted > pair.bounds.videoDuration)
                continue;
            const auto nearby = std::find_if(
                candidates.begin(), candidates.end(),
                [&](const Candidate& candidate)
                { return std::abs(candidate.videoTime - *predicted) <= .3; });
            if(nearby == candidates.end())
                candidates.push_back({*predicted, .5});
        }
        regions.push_back({selected, heldOut, std::move(candidates)});
    }
    if(context.Cancelled())
        return std::nullopt;
    context.Progress("Checking cross-track timing agreement");
    std::optional<Timing> bestTiming;
    double bestScore = -1, alternateScore = -1;
    // Each coarse hypothesis comes from two different training regions.
    // Held-out regions are absent from BOTH hypothesis generation and
    // scoring: otherwise validation would merely recheck its own answer.
    for(std::size_t i = 0; i < regions.size(); ++i)
    {
        if(context.Cancelled())
            return std::nullopt;
        if(regions[i].heldOut)
            continue;
        for(std::size_t j = i + 1; j < regions.size(); ++j)
        {
            if(regions[j].heldOut)
                continue;
            for(const auto& a : regions[i].candidates)
                for(const auto& b : regions[j].candidates)
                {
                    const auto timing = FitMarkers(
                        {regions[i].songTime, regions[j].songTime, a.videoTime, b.videoTime});
                    if(!timing)
                        continue;
                    double score = 0;
                    for(const auto& region : regions)
                    {
                        if(region.heldOut)
                            continue;
                        double match = 0;
                        const double target = *VideoTime(*timing, region.songTime);
                        for(const auto& c : region.candidates)
                            if(std::abs(c.videoTime - target) <= .3)
                                match = std::max(match, c.score);
                        score += match;
                    }
                    if(score > bestScore)
                    {
                        if(bestTiming &&
                           std::abs(bestTiming->offsetSeconds - timing->offsetSeconds) > .5)
                            alternateScore = bestScore;
                        bestScore = score;
                        bestTiming = timing;
                    }
                    else if(bestTiming &&
                            std::abs(bestTiming->offsetSeconds - timing->offsetSeconds) > .5)
                        alternateScore = std::max(alternateScore, score);
                }
        }
    }
    if(!bestTiming || bestScore < desiredAnchors * .5)
        return unavailable;
    std::vector<Anchor> refined;
    for(std::size_t i = 0; i < regions.size(); ++i)
    {
        if(context.Cancelled())
            return std::nullopt;
        context.Progress("Refining audio alignment", i, regions.size());
        const auto& region = regions[i];
        const double begin = region.songTime - window * .5;
        const double predictedBegin = *VideoTime(*bestTiming, begin);
        // Repeated phrases can produce a mathematically consistent placement
        // that puts another region outside the physical source. That is failed
        // alignment evidence, not a decoder/file failure. A held-out miss must
        // remain in validation so it cannot disappear and inflate confidence.
        const double audioEnd = pair.video.audition ? pair.video.audition->duration
                                                    : pair.video.audio.streamStartSeconds +
                                                          pair.video.audio.durationSeconds +
                                                          pair.video.source.timelineShift;
        if(predictedBegin < 0 || predictedBegin + window * bestTiming->playbackRate > audioEnd)
        {
            if(region.heldOut)
                refined.push_back({begin, predictedBegin, 0, 0, true});
            continue;
        }
        auto mapPcm = Window(pair.song.source, begin, begin + window, context);
        auto videoPcm =
            Window(pair.video.source, predictedBegin - .35,
                   predictedBegin + window * bestTiming->playbackRate + .35, context);
        if(!mapPcm || !videoPcm)
            return std::nullopt;
        const auto match =
            Refine(mapPcm->first, videoPcm->first, 4000, mapPcm->second, videoPcm->second,
                   *bestTiming, .35, [&] { return context.Cancelled(); });
        // Ambiguity belongs to the complete affine placement, not to the rank
        // of one local coarse fingerprint. A correct chorus can be the fifth
        // best local spectral match while still being the only placement that
        // agrees across the whole song. Comparing that local score with the
        // strongest unrelated chorus incorrectly marked exact, aligned tracks
        // as ambiguous. Use the competing whole-track hypothesis instead.
        const double margin = alternateScore >= 0 && bestScore > 0
                                  ? std::max(0.0, (bestScore - alternateScore) / bestScore)
                                  : 1.0;
        if(match)
            refined.push_back(
                {mapPcm->second, match->videoTime, match->score, margin, region.heldOut});
        else if(region.heldOut)
            refined.push_back({mapPcm->second, predictedBegin, 0, 0, true});
    }
    if(context.Cancelled())
        return std::nullopt;
    auto result = FitAnchors(refined, .015);
    // A globally competitive alternate placement is a repeated-section
    // ambiguity even if the selected placement refines beautifully.
    if(alternateScore >= bestScore * .97)
    {
        result.confidence = Confidence::Low;
        result.explanation = "Several whole-track alignments are similarly "
                             "plausible. Preview and choose manually.";
    }
    context.Progress("Analysis complete", 1, 1);
    return result;
}
} // namespace BigScreen::AudioSync
