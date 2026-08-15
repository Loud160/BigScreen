// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "BigScreen/PlaybackSession.hpp"

namespace BigScreen {
    /// Records low-frequency power and CPU observations for controlled A/B
    /// tests. Tick only captures values in memory; files are appended after the
    /// gameplay session ends so storage latency never enters the real-time map
    /// loop.
    class PowerBenchmark final {
    public:
        static PowerBenchmark& Instance();

        void Prepare(
            std::string levelId,
            std::string songName,
            std::string songArtist,
            std::string characteristic,
            int difficulty) noexcept;
        void Start(
            bool videoActive,
            bool showcaseActive,
            const PlaybackDiagnostics& diagnostics) noexcept;
        void Tick(
            double songTimeSeconds,
            const PlaybackDiagnostics& diagnostics) noexcept;
        void Finish(
            const std::optional<PlaybackResultsData>& results) noexcept;
        void Cancel() noexcept;
        /// Performs one low-cost main-menu read and logs the actual values.
        /// This lets a tester verify Quest BatteryManager access before
        /// spending time and energy on a controlled pair of gameplay runs.
        void ProbeBatteryAccessOnce() noexcept;

    private:
        struct BatterySample {
            std::optional<std::int64_t> chargeMicroampHours;
            std::optional<std::int64_t> currentNowMicroamps;
            std::optional<std::int64_t> currentAverageMicroamps;
            std::optional<std::int64_t> energyNanowattHours;
            std::optional<int> capacityPercent;
            std::optional<int> status;
            std::optional<bool> charging;
        };

        struct Sample {
            double elapsedSeconds = 0.0;
            double songTimeSeconds = 0.0;
            double processCpuMilliseconds = 0.0;
            double decoderCpuMilliseconds = 0.0;
            PlaybackDiagnostics diagnostics;
            BatterySample battery;
        };

        PowerBenchmark() = default;
        BatterySample ReadBattery() noexcept;
        void CaptureSample(
            double songTimeSeconds,
            const PlaybackDiagnostics& diagnostics);
        void AppendFiles(
            const std::optional<PlaybackResultsData>& results);
        void DisableCurrentSession(const char* operation) noexcept;

        std::string levelId_;
        std::string songName_;
        std::string songArtist_;
        std::string characteristic_;
        int difficulty_ = -1;
        std::string sessionId_;
        bool prepared_ = false;
        bool active_ = false;
        bool videoActive_ = false;
        bool showcaseActive_ = false;
        bool batteryWarningLogged_ = false;
        bool batteryProbeAttempted_ = false;
        std::chrono::steady_clock::time_point startedAt_{};
        std::chrono::steady_clock::time_point nextSampleAt_{};
        std::chrono::system_clock::time_point startedUtc_{};
        double processCpuBaselineMilliseconds_ = 0.0;
        std::vector<Sample> samples_;
    };
}
