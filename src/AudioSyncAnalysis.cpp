// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#if defined(__ANDROID__)
#include <chrono>
#include <thread>
#endif

namespace BigScreen::AudioSync
{
namespace
{
constexpr double Pi = std::numbers::pi;
constexpr std::array<int, 16> BandCenters{2,  3,  5,  7,   10,  14,  20,  28,
                                          40, 56, 80, 112, 144, 176, 208, 256};

bool Cancelled(const CancelCheck& check) { return check && check(); }

void YieldToRealtimePlayback(std::size_t iteration)
{
#if defined(__ANDROID__)
    // Automatic matching is intentionally lower urgency than the live game,
    // video decoder, and Unity audio thread. Quest 2 can otherwise spend a
    // complete scheduler slice evaluating spectral candidates and miss video
    // presentation deadlines even though analysis itself is asynchronous.
    // One millisecond after each small batch adds only modest wall time to an
    // explicitly progress-reported operation while regularly returning CPU to
    // playback. Host tests remain unthrottled.
    if((iteration & 7u) == 7u)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#else
    (void)iteration;
#endif
}

double Similarity(const Feature& a, const Feature& b, double rate)
{
    // Unit-length log spectral vectors suppress gain/mastering changes;
    // silence never supplies positive correspondence evidence.
    if(a.rms < 1e-5f || b.rms < 1e-5f)
        return 0.0;
    double dot = 0.0, norm = 0.0;
    for(std::size_t i = 0; i < a.bands.size(); ++i)
    {
        // A rate-changing source also changes pitch. Compare the
        // corresponding frequency, not the same band number: with
        // V=.98*S+b, a map tone f lives near f/.98 in video audio.
        const double bin = BandCenters[i] / rate;
        const auto upper = std::upper_bound(BandCenters.begin(), BandCenters.end(), bin);
        double value;
        if(upper == BandCenters.begin())
            value = b.bands.front();
        else if(upper == BandCenters.end())
            value = b.bands.back();
        else
        {
            const auto right = static_cast<std::size_t>(upper - BandCenters.begin());
            const double fraction =
                (bin - BandCenters[right - 1]) / (BandCenters[right] - BandCenters[right - 1]);
            value = b.bands[right - 1] * (1 - fraction) + b.bands[right] * fraction;
        }
        dot += a.bands[i] * value;
        norm += value * value;
    }
    return norm > 1e-12 ? dot / std::sqrt(norm) : 0.0;
}

const Feature* At(const FeatureIndex& index, double time)
{
    const auto it = std::lower_bound(index.frames.begin(), index.frames.end(), time,
                                     [](const Feature& feature, double target)
                                     { return feature.time < target; });
    if(it == index.frames.end())
        return nullptr;
    if(it != index.frames.begin() && time - (it - 1)->time < it->time - time)
        return &*(it - 1);
    return &*it;
}
} // namespace

FeatureBuilder::FeatureBuilder(std::size_t maximumFrames) : maximumFrames_(maximumFrames)
{
    if(maximumFrames == 0 || maximumFrames > 32000)
        throw std::invalid_argument("Audio feature budget exceeds the bounded index limit");
    // Reserve only modest initial capacity. At the hard bound the compact
    // vector is about 3 MB, independent of source channel count/sample rate.
    index_.frames.reserve(std::min<std::size_t>(maximumFrames, 4096));
}

bool FeatureBuilder::Append(std::span<const float> mono, double firstSampleTime, std::string& error)
{
    error.clear();
    if(!std::isfinite(firstSampleTime))
    {
        error = "Audio timestamps are invalid.";
        return false;
    }
    // A decoder must explicitly account for gaps/discontinuities rather
    // than concatenate PCM and shift all later features. Flush a partial
    // window at a gap, retaining absolute times on both sides.
    if(nextTime_ && firstSampleTime < *nextTime_ - 1.0 / 16000)
    {
        error = "Audio timestamps moved backwards.";
        return false;
    }
    if(nextTime_ && std::abs(firstSampleTime - *nextTime_) > 1.0 / 16000 && used_)
    {
        if(index_.frames.size() == maximumFrames_)
        {
            error = "Audio exceeds the analysis index budget.";
            return false;
        }
        Emit(used_);
    }
    for(std::size_t i = 0; i < mono.size(); ++i)
    {
        if(index_.frames.size() == maximumFrames_)
        {
            error = "Audio exceeds the analysis index budget.";
            return false;
        }
        if(!std::isfinite(mono[i]))
        {
            error = "Decoded audio contains invalid samples.";
            return false;
        }
        if(used_ == 0)
            frameStart_ = firstSampleTime + static_cast<double>(i) / 16000;
        samples_[used_++] = mono[i];
        if(used_ == samples_.size())
            Emit(used_);
    }
    nextTime_ = firstSampleTime + static_cast<double>(mono.size()) / 16000;
    return true;
}

void FeatureBuilder::Emit(std::size_t count)
{
    std::array<std::complex<float>, 512> fft{};
    Feature feature;
    feature.time = frameStart_;
    double energy = 0.0;
    for(std::size_t i = 0; i < count; ++i)
    {
        energy += samples_[i] * samples_[i];
        feature.peak = std::max(feature.peak, std::abs(samples_[i]));
        fft[i] = samples_[i] * static_cast<float>(0.5 - 0.5 * std::cos(2 * Pi * i / 511.0));
    }
    feature.rms = static_cast<float>(std::sqrt(energy / count));
    // In-place radix-2 FFT, with a Hann window and no per-frame allocation.
    // This is coarse analysis only; waveform display uses peak/rms and
    // spectrogram display reuses these bands at UI resolution.
    for(std::size_t i = 1, j = 0; i < fft.size(); ++i)
    {
        std::size_t bit = fft.size() >> 1;
        for(; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if(i < j)
            std::swap(fft[i], fft[j]);
    }
    for(std::size_t length = 2; length <= fft.size(); length <<= 1)
    {
        const auto root = std::polar(1.0f, static_cast<float>(-2 * Pi / length));
        for(std::size_t start = 0; start < fft.size(); start += length)
        {
            std::complex<float> factor{1, 0};
            for(std::size_t j = 0; j < length / 2; ++j)
            {
                const auto even = fft[start + j];
                const auto odd = fft[start + j + length / 2] * factor;
                fft[start + j] = even + odd;
                fft[start + j + length / 2] = even - odd;
                factor *= root;
            }
        }
    }
    // Overlapping triangular filters retain within-band movement. Hard
    // octave buckets discard that information: two moving frequencies can
    // stay in the same bucket for seconds, creating a falsely confident
    // but badly displaced coarse match even on a known-answer fixture.
    constexpr std::array<int, 16> centers{2,  3,  5,  7,   10,  14,  20,  28,
                                          40, 56, 80, 112, 144, 176, 208, 256};
    for(int bin = 2; bin < 256; ++bin)
    {
        const auto upper = std::upper_bound(centers.begin(), centers.end(), bin);
        const auto right = static_cast<std::size_t>(upper - centers.begin());
        const auto left = right - 1;
        const float weight = float(bin - centers[left]) / (centers[right] - centers[left]);
        const float power = std::norm(fft[bin]);
        feature.bands[left] += power * (1 - weight);
        feature.bands[right] += power * weight;
    }
    float norm = 0.0f;
    for(std::size_t band = 0; band < feature.bands.size(); ++band)
    {
        feature.bands[band] = std::log1p(feature.bands[band]);
        norm += feature.bands[band] * feature.bands[band];
    }
    norm = std::sqrt(norm);
    for(std::size_t band = 0; band < feature.bands.size(); ++band)
    {
        if(norm > 1e-8f)
            feature.bands[band] /= norm;
        feature.onset += std::max(0.0f, feature.bands[band] - previousBands_[band]);
    }
    previousBands_ = feature.bands;
    index_.frames.push_back(feature);
    used_ = 0;
}

FeatureIndex FeatureBuilder::Finish()
{
    if(used_ && index_.frames.size() < maximumFrames_)
        Emit(used_);
    float peakRms = 0.0f;
    for(const auto& feature : index_.frames)
        peakRms = std::max(peakRms, feature.rms);
    // Sustained energy avoids treating a single click as the musical
    // endpoint. Low/high thresholds give 6 dB hysteresis; boundaries remain
    // metadata on the original clock, never a trim/re-zero instruction.
    const float high = std::max(1e-4f, peakRms * .01f);
    const float low = high * .5f;
    int sustained = 0;
    bool active = false;
    for(std::size_t i = 0; i < index_.frames.size(); ++i)
    {
        const auto& feature = index_.frames[i];
        sustained = feature.rms >= high ? sustained + 1 : 0;
        if(!active && sustained >= 3)
        {
            active = true;
            if(!index_.activeStart)
                index_.activeStart = index_.frames[i - 2].time;
        }
        if(active && feature.rms >= low)
            index_.activeEnd = feature.time + index_.stepSeconds;
        if(active && feature.rms < low)
            active = false;
    }
    return std::move(index_);
}

MatchResult FitAnchors(std::span<const Anchor> anchors, double tolerance)
{
    MatchResult result;
    result.explanation = "Insufficient independent matching audio evidence.";
    if(anchors.size() > 64 || !std::isfinite(tolerance) || tolerance <= 0.0 || tolerance > .5)
        return result;
    result.anchors.assign(anchors.begin(), anchors.end());
    std::vector<std::size_t> training;
    for(std::size_t i = 0; i < anchors.size(); ++i)
    {
        const auto& a = anchors[i];
        if(!std::isfinite(a.songTime) || !std::isfinite(a.videoTime) ||
           !std::isfinite(a.strength) || !std::isfinite(a.ambiguity) || a.songTime < 0 ||
           a.videoTime < 0 || a.strength < 0 || a.strength > 1)
            return result;
        if(!a.heldOut && a.strength >= .5)
            training.push_back(i);
    }
    if(training.size() < 3)
        return result;
    std::vector<std::size_t> best;
    double bestError = std::numeric_limits<double>::infinity();
    // Bounded exhaustive RANSAC: with <=64 anchors there are <=2016
    // hypotheses. Determinism makes repeated-chorus fixtures reproducible.
    for(std::size_t i = 0; i < training.size(); ++i)
        for(std::size_t j = i + 1; j < training.size(); ++j)
        {
            auto a = anchors[training[i]], b = anchors[training[j]];
            if(b.songTime < a.songTime)
                std::swap(a, b);
            const auto timing = FitMarkers({a.songTime, b.songTime, a.videoTime, b.videoTime});
            if(!timing)
                continue;
            std::vector<std::size_t> inliers;
            double error = 0;
            for(const auto index : training)
            {
                const auto& point = anchors[index];
                const double residual = point.videoTime - *VideoTime(*timing, point.songTime);
                if(std::abs(residual) <= tolerance)
                {
                    inliers.push_back(index);
                    error += residual * residual;
                }
            }
            if(inliers.size() > best.size() || (inliers.size() == best.size() && error < bestError))
            {
                best = std::move(inliers);
                bestError = error;
            }
        }
    if(best.size() < 3)
        return result;
    double sum = 0, meanSong = 0, meanVideo = 0;
    for(const auto index : best)
    {
        const auto& a = anchors[index];
        sum += a.strength;
        meanSong += a.songTime * a.strength;
        meanVideo += a.videoTime * a.strength;
    }
    meanSong /= sum;
    meanVideo /= sum;
    double covariance = 0, variance = 0;
    for(const auto index : best)
    {
        const auto& a = anchors[index];
        covariance += a.strength * (a.songTime - meanSong) * (a.videoTime - meanVideo);
        variance += a.strength * (a.songTime - meanSong) * (a.songTime - meanSong);
    }
    if(variance < 1e-6)
        return result;
    const Timing fitted{meanVideo - covariance / variance * meanSong, covariance / variance};
    if(!VideoTime(fitted, 0))
        return result;
    result.timing = fitted;
    result.anchors.assign(anchors.begin(), anchors.end());
    result.accepted = best.size();
    result.rejected = training.size() - best.size();
    double squared = 0, minTime = std::numeric_limits<double>::infinity(), maxTime = 0;
    double minimumMargin = 1;
    for(const auto index : best)
    {
        const auto& a = anchors[index];
        const double residual = a.videoTime - *VideoTime(fitted, a.songTime);
        squared += residual * residual;
        minTime = std::min(minTime, a.songTime);
        maxTime = std::max(maxTime, a.songTime);
        minimumMargin = std::min(minimumMargin, a.ambiguity);
    }
    result.fitRmsSeconds = std::sqrt(squared / best.size());
    for(const auto& a : anchors)
        if(a.heldOut)
        {
            ++result.validationCount;
            result.validationMaxSeconds =
                std::max(result.validationMaxSeconds,
                         std::abs(a.videoTime - *VideoTime(fitted, a.songTime)));
            if(a.strength < .5)
                result.validationMaxSeconds = std::numeric_limits<double>::infinity();
        }
    // Small fit residuals are insufficient: a middle edit may leave both
    // fitted ends apparently perfect. Independent regions veto such a fit.
    result.incompatible = result.validationCount > 0 && result.validationMaxSeconds > tolerance * 2;
    if(result.incompatible)
        result.explanation = "Independent audio regions disagree. Timing may be discontinuous or "
                             "non-affine; use manual alignment.";
    else if(result.validationCount == 0 || maxTime - minTime < 10 || minimumMargin < .02 ||
            result.accepted * 4 < training.size() * 3)
        result.explanation =
            "Low confidence: ambiguous, narrow, or insufficient independently validated evidence.";
    else
    {
        result.confidence = result.accepted >= 5 && result.validationCount >= 2 &&
                                    minimumMargin >= .08 && result.validationMaxSeconds <= tolerance
                                ? Confidence::High
                                : Confidence::Medium;
        result.explanation =
            "Timing proposal validated against independent audio regions. Preview before applying.";
    }
    return result;
}

std::vector<Candidate> FindCandidates(const FeatureIndex& song, const FeatureIndex& video,
                                      double songTime, double window, const CancelCheck& cancelled)
{
    std::vector<Candidate> best;
    if(song.frames.empty() || video.frames.empty() || !std::isfinite(songTime) ||
       !std::isfinite(window) || window < 2 || window > 20 || song.frames.size() > 32000 ||
       video.frames.size() > 32000)
        return best;
    // 17 observations spread across the region bound cost irrespective of
    // requested refinement length. A coarse rate grid covers modest drift;
    // precise refinement is a separate operation on local PCM at fitted r.
    std::size_t candidateIteration = 0;
    for(std::size_t center = 0; center < video.frames.size(); center += 3)
    {
        if(Cancelled(cancelled))
            return {};
        YieldToRealtimePlayback(candidateIteration++);
        double score = -1;
        for(int rateIndex = 0; rateIndex <= 40; ++rateIndex)
        {
            const double rate = .9 + .005 * rateIndex;
            double sum = 0;
            int observations = 0;
            double energyA = 0, energyB = 0, energyAA = 0, energyBB = 0, energyAB = 0;
            for(int k = -8; k <= 8; ++k)
            {
                const double delta = k * window / 16;
                const auto* a = At(song, songTime + delta);
                const auto* b = At(video, video.frames[center].time + delta * rate);
                if(!a || !b || std::abs(a->time - songTime - delta) > .05 ||
                   std::abs(b->time - video.frames[center].time - delta * rate) > .05)
                    continue;
                sum += Similarity(*a, *b, rate);
                ++observations;
                const double ea = std::log1p(a->rms * 1000), eb = std::log1p(b->rms * 1000);
                energyA += ea;
                energyB += eb;
                energyAA += ea * ea;
                energyBB += eb * eb;
                energyAB += ea * eb;
            }
            if(observations >= 14)
            {
                // Transient/envelope structure provides an independent
                // temporal cue. Spectral-only matching can align equal
                // timbres from different phrases; centered log-energy
                // correlation retains dynamics without depending on gain.
                const double denominator = (energyAA - energyA * energyA / observations) *
                                           (energyBB - energyB * energyB / observations);
                const double envelope =
                    denominator > 1e-8
                        ? std::max(0.0, (energyAB - energyA * energyB / observations) /
                                            std::sqrt(denominator))
                        : 0.0;
                score = std::max(score, .5 * sum / observations + .5 * envelope);
            }
        }
        if(score < .5)
            continue;
        const Candidate candidate{video.frames[center].time, score};
        const auto near =
            std::find_if(best.begin(), best.end(), [&](const Candidate& c)
                         { return std::abs(c.videoTime - candidate.videoTime) < window * .5; });
        if(near != best.end())
        {
            if(score > near->score)
                *near = candidate;
        }
        else
            best.push_back(candidate);
        std::sort(best.begin(), best.end(),
                  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
        if(best.size() > 4)
            best.resize(4);
    }
    return best;
}

std::optional<Candidate> Refine(std::span<const float> song, std::span<const float> video,
                                int sampleRate, double songStart, double videoStart,
                                Timing predicted, double radius, const CancelCheck& cancelled)
{
    if(sampleRate < 1000 || sampleRate > 16000 || song.size() < 64 ||
       song.size() > static_cast<std::size_t>(sampleRate) * 20 ||
       video.size() > static_cast<std::size_t>(sampleRate) * 170 || !std::isfinite(radius) ||
       radius < 0 || radius > .5 || !std::isfinite(songStart) || !std::isfinite(videoStart) ||
       !VideoTime(predicted, songStart))
        return std::nullopt;
    const double base = (*VideoTime(predicted, songStart) - videoStart) * sampleRate;
    double bestScore = -1;
    int bestLag = 0;
    double bestRate = predicted.playbackRate;
    const int extent = static_cast<int>(std::ceil(radius * sampleRate));
    // A two-level lag search avoids O(window * all sub-ms offsets). First
    // search at 1 ms, then every sample around the best bin. Correlation
    // uses every fourth sample to bound cost; all samples remain available
    // for audition. This is a proposal, not guaranteed sample accuracy.
    auto search = [&](int first, int last, int step, double rate, std::size_t samples)
    {
        for(int lag = first; lag <= last; lag += step)
        {
            if(Cancelled(cancelled))
                return false;
            const double start = base + lag;
            const double end = start + (samples - 1) * rate;
            if(start < 0 || end + 1 >= video.size())
                continue;
            double sumA = 0, sumB = 0, aa = 0, bb = 0, ab = 0, n = 0;
            for(std::size_t i = 0; i < samples; i += 4)
            {
                const double coordinate = start + i * rate;
                const auto index = static_cast<std::size_t>(coordinate);
                const double fraction = coordinate - index;
                const double a = song[i],
                             b = video[index] * (1 - fraction) + video[index + 1] * fraction;
                sumA += a;
                sumB += b;
                aa += a * a;
                bb += b * b;
                ab += a * b;
                ++n;
            }
            const double energy = (aa - sumA * sumA / n) * (bb - sumB * sumB / n);
            if(energy <= 1e-12)
                continue;
            const double score = (ab - sumA * sumB / n) / std::sqrt(energy);
            if(score > bestScore)
            {
                bestScore = score;
                bestLag = lag;
                bestRate = rate;
            }
        }
        return true;
    };
    const int coarseStep = std::max(1, sampleRate / 1000);
    // Coarse spectral-bin timing alone is not accurate enough to fix the
    // rate during PCM correlation: a 0.2% error accumulates several audio
    // cycles across a long window. Recover rate and lag jointly, using
    // the envelope to select the phrase before resolving waveform phase.
    // Every inner lag iteration remains cancellable and bounded.
    // Seed phase correlation from the complete window's energy envelope,
    // not a fraction of a second of PCM. A short sustained note has many
    // equally plausible phase offsets; choosing one prematurely can lock
    // every subsequent refinement onto a neighbouring cycle/phrase. The
    // centered 20 ms RMS envelope retains dynamics without depending on
    // carrier phase. Prefix sums keep smoothing linear and allocations
    // bounded by the already bounded local windows.
    auto envelope = [sampleRate](std::span<const float> input)
    {
        std::vector<double> prefix(input.size() + 1, 0);
        for(std::size_t i = 0; i < input.size(); ++i)
            prefix[i + 1] = prefix[i] + double(input[i]) * input[i];
        std::vector<float> result(input.size());
        const auto half = static_cast<std::size_t>(sampleRate / 100);
        for(std::size_t i = 0; i < input.size(); ++i)
        {
            const auto begin = i > half ? i - half : 0;
            const auto end = std::min(input.size(), i + half + 1);
            result[i] = static_cast<float>(
                std::sqrt(std::max(0.0, (prefix[end] - prefix[begin]) / (end - begin))));
        }
        return result;
    };
    auto songEnvelope = envelope(song);
    auto videoEnvelope = envelope(video);
    const auto originalSong = song, originalVideo = video;
    song = songEnvelope;
    video = videoEnvelope;
    for(int r = -40; r <= 40; ++r)
    {
        const double rate = predicted.playbackRate + r * .001;
        if(rate < .05 || rate > 8)
            continue;
        if(!search(-extent, extent, coarseStep, rate, song.size()))
            return std::nullopt;
    }
    song = originalSong;
    video = originalVideo;
    const auto envelopeLag = bestLag;
    const auto envelopeRate = bestRate;
    bestScore = -1;
    for(int r = -30; r <= 30; ++r)
    {
        const double rate = envelopeRate + r * .0001;
        if(rate < .05 || rate > 8)
            continue;
        if(!search(std::max(-extent, envelopeLag - sampleRate / 100),
                   std::min(extent, envelopeLag + sampleRate / 100), 1, rate, song.size()))
            return std::nullopt;
    }
    for(int pass = 0; pass < 3; ++pass)
    {
        const double seedRate = bestRate;
        const int seedLag = bestLag;
        const double rateStep = pass == 0 ? .0001 : pass == 1 ? .00001 : .000001;
        const auto samples = pass == 0
                                 ? std::min(song.size(), static_cast<std::size_t>(sampleRate / 2))
                                 : song.size();
        bestScore = -1;
        for(int r = -12; r <= 12; ++r)
        {
            const double rate = seedRate + r * rateStep;
            if(rate < .05 || rate > 8)
                continue;
            if(!search(std::max(-extent, seedLag - coarseStep * 3),
                       std::min(extent, seedLag + coarseStep * 3), 1, rate, samples))
                return std::nullopt;
        }
    }
    if(bestScore < .5)
        return std::nullopt;
    return Candidate{*VideoTime(predicted, songStart) + static_cast<double>(bestLag) / sampleRate,
                     std::clamp(bestScore, 0.0, 1.0), bestRate};
}
} // namespace BigScreen::AudioSync
