// SPDX-License-Identifier: GPL-3.0-only
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// SPDX-FileCopyrightText: © 2026 Loud160 and the Big Screen contributors
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

namespace BigScreen::AudioSync
{
/// One producer (Unity main thread OR filter callback), one disk worker.
/// Push only copies/downmixes bounded native samples; no allocation, lock,
/// I/O, logging or managed call occurs here. Antialiased resampling belongs
/// to the worker, not the audio callback. Full PCM is never kept in RAM.
struct CaptureBuffer final
{
    static constexpr std::size_t Capacity = 32768;
    std::array<float, Capacity> samples{};
    std::atomic<std::uint64_t> read{0}, write{0};
    std::atomic<bool> configured{false}, accepting{false}, stop{false}, done{false},
        overflow{false};
    std::atomic<int> failure{0}; // 1: invalid clip metadata; 2: GetData failed
    int rate = 0;
    double duration = 0;
    std::uint64_t maximum = 0, inputMaximum = 0;
    void Configure(int inputRate, double seconds)
    {
        rate = inputRate;
        duration = seconds;
        maximum = static_cast<std::uint64_t>(std::llround(seconds * 16000));
        inputMaximum = static_cast<std::uint64_t>(std::llround(seconds * inputRate));
        configured.store(true, std::memory_order_release);
    }
    void Push(std::span<const float> interleaved, int channels) noexcept
    {
        if(stop.load() || done.load() || !accepting.load() || channels < 1)
            return;
        auto position = write.load(std::memory_order_relaxed);
        const auto consumed = read.load(std::memory_order_acquire);
        for(std::size_t i = 0; i + channels <= interleaved.size() && position < inputMaximum;
            i += channels)
        {
            if(position - consumed >= Capacity)
            {
                overflow.store(true);
                stop.store(true);
                return;
            }
            double mono = 0;
            for(int channel = 0; channel < channels; ++channel)
                mono += std::isfinite(interleaved[i + channel]) ? interleaved[i + channel] : 0;
            samples[position++ % Capacity] = static_cast<float>(mono / channels);
        }
        write.store(position, std::memory_order_release);
        if(position >= inputMaximum)
            done.store(true, std::memory_order_release);
    }
    std::size_t Pop(std::span<float> destination) noexcept
    {
        const auto begin = read.load(std::memory_order_relaxed);
        const auto end = write.load(std::memory_order_acquire);
        const auto size = std::min<std::uint64_t>(destination.size(), end - begin);
        for(std::size_t i = 0; i < size; ++i)
            destination[i] = samples[(begin + i) % Capacity];
        read.store(begin + size, std::memory_order_release);
        return size;
    }
    void Finish() noexcept { done.store(true, std::memory_order_release); }
};
} // namespace BigScreen::AudioSync
