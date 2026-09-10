// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once
#include "BigScreen/AudioSyncService.hpp"
#include <array>
#include <atomic>

namespace BigScreen::AudioSync
{
struct AuditionRequest
{
    std::shared_ptr<const PreparedPair> sources;
    Profile profile;
    double songStart = 0;
    double songEnd = 0;
    double auditionSpeed = 1;
    int outputRate = 48000;
};
/// Stereo frames produced by one worker and consumed by one Unity audio
/// callback. Monotonic indices publish complete frames with release/acquire;
/// the callback never allocates, decodes, touches files, locks, or logs.
/// Both channels advance together. An underrun emits silence WITHOUT
/// advancing either source clock, rather than losing alignment invisibly.
class Audition final
{
  public:
    struct State
    {
        static constexpr std::size_t Capacity = 192000;
        std::array<std::array<float, 2>, Capacity> ring{};
        std::atomic<std::uint64_t> read{0}, write{0}, underruns{0};
        std::atomic<bool> stop{false}, complete{false}, paused{true};
        const double songStart, speed;
        const int rate;
        State(double start, double playbackSpeed, int outputRate)
            : songStart(start), speed(playbackSpeed), rate(outputRate)
        {
        }
        void Render(float* output, std::size_t frames, int channels) noexcept;
        double SongTime() const
        {
            return songStart + read.load(std::memory_order_relaxed) * speed / rate;
        }
        std::size_t Buffered() const
        {
            // Read the consumer first. A UI observer loading write first
            // could then see a newer read beyond that stale write and
            // underflow to an enormous apparent buffer size.
            const auto consumed = read.load(std::memory_order_acquire);
            return write.load(std::memory_order_acquire) - consumed;
        }
    };
    bool Start(AuditionRequest request, std::string& error);
    void Stop();
    bool Busy() const { return worker_.Busy(); }
    std::shared_ptr<State> Output() const { return state_; }
    std::optional<OperationProgress> Progress() const { return worker_.Poll(); }

  private:
    Operation<bool> worker_;
    std::shared_ptr<State> state_;
    std::uint64_t generation_ = 0;
};
} // namespace BigScreen::AudioSync
