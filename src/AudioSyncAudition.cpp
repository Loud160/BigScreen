// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncAudition.hpp"
#include "sonic.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace BigScreen::AudioSync
{
namespace
{
class AudioCacheReadError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};
class Sampler final
{
    std::ifstream file_;
    std::array<float, 4097> page_{};
    std::int64_t start_ = -1, total_ = 0;

  public:
    explicit Sampler(const PcmLease& lease)
        : file_(lease.path, std::ios::binary), total_(std::llround(lease.duration * PcmCache::Rate))
    {
        if(!file_)
            throw AudioCacheReadError("Prepared audio cannot be opened.");
    }
    float At(double time)
    {
        if(time < 0 || !std::isfinite(time))
            return 0;
        const double coordinate = time * PcmCache::Rate;
        const auto sample = static_cast<std::int64_t>(coordinate);
        if(sample >= total_)
            return 0;
        if(start_ < 0 || sample < start_ ||
           sample + 1 >= start_ + static_cast<std::int64_t>(page_.size()))
        {
            start_ = sample;
            page_.fill(0);
            file_.clear();
            file_.seekg(44 + start_ * 4);
            const auto length = std::min<std::int64_t>(page_.size(), total_ - start_);
            file_.read(reinterpret_cast<char*>(page_.data()), length * 4);
            if(file_.gcount() != length * 4)
                throw AudioCacheReadError("Prepared audio was truncated or removed.");
        }
        const auto index = sample - start_;
        const auto fraction = static_cast<float>(coordinate - sample);
        return page_[index] * (1 - fraction) + page_[index + 1] * fraction;
    }
};
} // namespace
void Audition::State::Render(float* output, std::size_t frames, int channels) noexcept
{
    if(!output || channels <= 0)
        return;
    std::fill_n(output, frames * channels, 0.0f);
    if(stop.load(std::memory_order_acquire) || paused.load(std::memory_order_relaxed))
        return;
    const auto begin = read.load(std::memory_order_relaxed);
    const auto available = write.load(std::memory_order_acquire) - begin;
    const auto count = std::min<std::uint64_t>(frames, available);
    for(std::uint64_t i = 0; i < count; ++i)
    {
        const auto& value = ring[(begin + i) % Capacity];
        if(channels == 1)
            output[i] = (value[0] + value[1]) * .5f;
        else
        {
            output[i * channels] = value[0];
            output[i * channels + 1] = value[1];
        }
    }
    read.store(begin + count, std::memory_order_release);
    if(count < frames && !complete.load(std::memory_order_acquire))
        underruns.fetch_add(1, std::memory_order_relaxed);
}
void Audition::Stop()
{
    if(state_)
        state_->stop.store(true, std::memory_order_release);
    worker_.Cancel();
}
bool Audition::Start(AuditionRequest request, std::string& error)
{
    if(worker_.Busy())
    {
        error = "Previous audition is still stopping.";
        return false;
    }
    if(!request.sources || !request.sources->song.audition || !request.sources->video.audition ||
       !std::isfinite(request.songStart) || !std::isfinite(request.songEnd) ||
       request.songStart < 0 || request.songEnd <= request.songStart ||
       !std::isfinite(request.auditionSpeed) || request.auditionSpeed < .25 ||
       request.auditionSpeed > 1 || request.outputRate < 8000 || request.outputRate > 96000)
    {
        error = "The audition audio or playback range is not ready.";
        return false;
    }
    const auto bounds = request.sources->bounds;
    error = Validate(request.profile, bounds);
    if(!error.empty())
        return false;
    state_ = std::make_shared<State>(request.songStart, request.auditionSpeed, request.outputRate);
    return worker_.Start(
        ++generation_, "audition",
        [request = std::move(request), state = state_](const auto& context) -> std::optional<bool>
        {
            context.Progress("Buffering audition audio");
            try
            {
                Sampler song(*request.sources->song.audition),
                    video(*request.sources->video.audition);
                std::unique_ptr<sonicStreamStruct, decltype(&sonicDestroyStream)> pitch(
                    nullptr, sonicDestroyStream);
                if(request.profile.pitchCorrection)
                {
                    pitch.reset(sonicCreateStream(request.outputRate, 1));
                    if(!pitch)
                        throw std::bad_alloc();
                    // First sample the video at r*S+b (ordinary resampling), then
                    // compensate that rate's pitch. Common audition slowdown is
                    // intentionally heard by BOTH sources and is not compensated.
                    // Sonic changes pitch at constant duration; output samples are
                    // paired with map samples by index, not by when DSP emits them.
                    sonicSetPitch(pitch.get(),
                                  std::pow(2.0, (request.profile.autoPitchSemitones +
                                                 request.profile.finePitchCents / 100) /
                                                    12));
                    sonicSetSpeed(pitch.get(), 1);
                    sonicSetRate(pitch.get(), 1);
                }
                constexpr std::size_t Block = 1024;
                std::array<float, Block> input{}, shifted{};
                std::uint64_t generated = 0, produced = 0;
                // The ring is deliberately large enough to absorb temporary
                // stalls, but filling all four seconds immediately creates an
                // avoidable CPU and storage burst beside the active video
                // decoder. Maintain a bounded 750 ms high-water mark instead.
                // That is three times the required startup reserve and leaves
                // ample recovery room without letting an audio-only preview
                // worker monopolize a Quest 2 core merely because the center
                // workspace is open.
                const auto targetBuffered = std::min<std::size_t>(
                    State::Capacity - Block,
                    std::max<std::size_t>(request.outputRate * 3 / 4,
                                          request.outputRate / 4 + Block));
                const auto total = static_cast<std::uint64_t>(
                    std::ceil((request.songEnd - request.songStart) * request.outputRate /
                              request.auditionSpeed));
                bool flushed = false;
                while(produced < total && !context.Cancelled() && !state->stop.load())
                {
                    const auto count = std::min<std::uint64_t>(Block, total - produced);
                    if(state->Buffered() + count > targetBuffered)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(3));
                        continue;
                    }
                    if(pitch)
                    {
                        while(sonicSamplesAvailable(pitch.get()) < static_cast<int>(count) &&
                              !flushed)
                        {
                            if(context.Cancelled() || state->stop.load())
                                return std::nullopt;
                            const auto incoming = std::min<std::uint64_t>(Block, total - generated);
                            for(std::size_t i = 0; i < incoming; ++i)
                            {
                                const auto time = request.songStart + (generated + i) *
                                                                          request.auditionSpeed /
                                                                          request.outputRate;
                                input[i] = video.At(request.profile.timing.playbackRate * time +
                                                    request.profile.timing.offsetSeconds);
                            }
                            if(incoming &&
                               !sonicWriteFloatToStream(pitch.get(), input.data(), incoming))
                                throw std::bad_alloc();
                            generated += incoming;
                            if(generated == total)
                            {
                                if(!sonicFlushStream(pitch.get()))
                                    throw std::bad_alloc();
                                flushed = true;
                            }
                        }
                        shifted.fill(0);
                        sonicReadFloatFromStream(pitch.get(), shifted.data(), count);
                    }
                    for(std::size_t i = 0; i < count; ++i)
                    {
                        const auto time = request.songStart + (produced + i) *
                                                                  request.auditionSpeed /
                                                                  request.outputRate;
                        const auto map = std::clamp(song.At(time), -1.0f, 1.0f);
                        const auto vid =
                            std::clamp(pitch ? shifted[i]
                                             : video.At(request.profile.timing.playbackRate * time +
                                                        request.profile.timing.offsetSeconds),
                                       -1.0f, 1.0f);
                        std::array<float, 2> frame;
                        switch(request.profile.routing)
                        {
                        case Routing::MapLeft:
                            frame = {map, vid};
                            break;
                        case Routing::VideoLeft:
                            frame = {vid, map};
                            break;
                        case Routing::MapOnly:
                            frame = {map, map};
                            break;
                        case Routing::VideoOnly:
                            frame = {vid, vid};
                            break;
                        default:
                            frame = {(map + vid) * .5f, (map + vid) * .5f};
                            break;
                        }
                        state->ring[(produced + i) % State::Capacity] = frame;
                    }
                    produced += count;
                    state->write.store(produced, std::memory_order_release);
                    context.Progress("Audition ready",
                                     std::min<std::uint64_t>(produced, request.outputRate / 4),
                                     request.outputRate / 4);
                }
                state->complete.store(true, std::memory_order_release);
                return context.Cancelled() ? std::nullopt : std::optional<bool>{true};
            }
            catch(const AudioCacheReadError& error)
            {
                context.Fail(error.what());
                state->complete.store(true, std::memory_order_release);
                return std::nullopt;
            }
        });
}
} // namespace BigScreen::AudioSync
