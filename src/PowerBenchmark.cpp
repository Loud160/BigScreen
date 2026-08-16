// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/PowerBenchmark.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <time.h>

#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/Settings.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/AndroidJavaClass.hpp"
#include "UnityEngine/AndroidJavaObject.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils-boxing.hpp"
#include "beatsaber-hook/shared/utils/typedefs-array.hpp"
#include "beatsaber-hook/shared/utils/typedefs-string.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr const char* LogDirectory =
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs";
        constexpr const char* SummaryPath =
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/power-benchmark-summary.csv";
        constexpr const char* SamplesPath =
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/power-benchmark-samples.csv";

        // Android BatteryManager property IDs are API constants. Keep the
        // names beside the values because Quest firmware can report any one as
        // unsupported even when the others work.
        constexpr int ChargeCounterProperty = 1;
        constexpr int CurrentNowProperty = 2;
        constexpr int CurrentAverageProperty = 3;
        constexpr int CapacityProperty = 4;
        constexpr int EnergyCounterProperty = 5;
        constexpr int StatusProperty = 6;

        double ProcessCpuMilliseconds()
        {
#if defined(CLOCK_PROCESS_CPUTIME_ID)
            timespec value{};
            if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) == 0)
            {
                return static_cast<double>(value.tv_sec) * 1000.0 +
                       static_cast<double>(value.tv_nsec) / 1'000'000.0;
            }
#endif
            return 0.0;
        }

        std::string Csv(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size() + 2);
            escaped.push_back('"');
            for(const char character : value)
            {
                if(character == '"')
                    escaped.push_back('"');
                escaped.push_back(character);
            }
            escaped.push_back('"');
            return escaped;
        }

        template<class T>
        std::string OptionalNumber(const std::optional<T>& value)
        {
            if(!value)
                return {};
            std::ostringstream text;
            text << *value;
            return text.str();
        }

        std::string OptionalBool(const std::optional<bool>& value)
        {
            if(!value)
                return {};
            return *value ? "1" : "0";
        }

        std::string UtcTimestamp(std::chrono::system_clock::time_point point)
        {
            const std::time_t time = std::chrono::system_clock::to_time_t(point);
            std::tm utc{};
            gmtime_r(&time, &utc);
            std::ostringstream text;
            text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            return text.str();
        }

        void AppendHeaderIfEmpty(
            const std::filesystem::path& path,
            std::string_view header)
        {
            std::error_code error;
            bool needsHeader = !std::filesystem::exists(path, error) ||
                std::filesystem::file_size(path, error) == 0;

            // The decode_method column changes the CSV schema. Preserve any
            // measurements written by older development builds instead of
            // appending wider rows beneath their shorter header. This runs
            // only during gameplay teardown, under the benchmark's existing
            // no-I/O-during-gameplay contract.
            if(!needsHeader)
            {
                std::ifstream existing(path);
                std::string existingHeader;
                std::getline(existing, existingHeader);
                if(existingHeader != header)
                {
                    const auto timestamp = std::chrono::system_clock::to_time_t(
                        std::chrono::system_clock::now());
                    const auto archived = path.parent_path() /
                        (path.stem().string() + "-legacy-" +
                         std::to_string(timestamp) + path.extension().string());
                    std::filesystem::rename(path, archived, error);
                    if(error)
                        throw std::runtime_error(
                            "Could not preserve the previous benchmark CSV schema: " +
                            error.message());
                    PaperLogger.info(
                        "Archived older power benchmark schema as '{}'",
                        archived.filename().string());
                    needsHeader = true;
                }
            }
            std::ofstream output(path, std::ios::app);
            if(!output)
                throw std::runtime_error("Could not open " + path.string());
            if(needsHeader)
                output << header << '\n';
        }

        template<class Container, class Projection>
        std::optional<double> AverageSupported(
            const Container& samples,
            Projection projection)
        {
            double total = 0.0;
            std::size_t count = 0;
            for(const auto& sample : samples)
            {
                const auto value = projection(sample);
                if(value)
                {
                    total += static_cast<double>(*value);
                    ++count;
                }
            }
            return count > 0
                ? std::optional<double>(total / static_cast<double>(count))
                : std::nullopt;
        }

        std::string OptionalFixed(
            const std::optional<double>& value,
            int precision)
        {
            if(!value)
                return {};
            std::ostringstream text;
            text << std::fixed << std::setprecision(precision) << *value;
            return text.str();
        }

        template<class T>
        void DisposeQuietly(T*& object) noexcept
        {
            if(!object)
                return;
            try
            {
                object->Dispose();
            }
            catch(...)
            {
                // Battery telemetry is optional. Cleanup must never let a Java
                // bridge exception escape into Beat Saber's gameplay hook.
            }
            object = nullptr;
        }
    }

    PowerBenchmark& PowerBenchmark::Instance()
    {
        static PowerBenchmark benchmark;
        return benchmark;
    }

    void PowerBenchmark::Prepare(
        std::string levelId,
        std::string songName,
        std::string songArtist,
        std::string characteristic,
        int difficulty) noexcept
    {
        Cancel();
        if(!Settings::Instance().ModEnabled() ||
           !Settings::Instance().PowerBenchmarkEnabled())
            return;

        levelId_ = std::move(levelId);
        songName_ = std::move(songName);
        songArtist_ = std::move(songArtist);
        characteristic_ = std::move(characteristic);
        difficulty_ = difficulty;
        prepared_ = !levelId_.empty();
    }

    void PowerBenchmark::Start(
        bool videoActive,
        bool showcaseActive,
        const PlaybackDiagnostics& diagnostics) noexcept
    {
        try
        {
            if(!prepared_ || !Settings::Instance().PowerBenchmarkEnabled())
                return;

            videoActive_ = videoActive;
            showcaseActive_ = showcaseActive;
            active_ = true;
            batteryWarningLogged_ = false;
            samples_.clear();
            // Reserve one hour of one-second observations up front. This is
            // well under one megabyte and prevents vector growth during even
            // unusually long maps or Replay sessions.
            samples_.reserve(3600);
            startedAt_ = std::chrono::steady_clock::now();
            startedUtc_ = std::chrono::system_clock::now();
            nextSampleAt_ = startedAt_ + std::chrono::seconds(1);
            processCpuBaselineMilliseconds_ = ProcessCpuMilliseconds();

            const auto epochMilliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    startedUtc_.time_since_epoch()).count();
            sessionId_ = std::to_string(epochMilliseconds) + "-" + levelId_;
            CaptureSample(0.0, diagnostics);
            PaperLogger.info(
                "Power benchmark started for '{}' (video {})",
                songName_,
                videoActive_ ? "on" : "off");
        }
        catch(...)
        {
            DisableCurrentSession("starting the power benchmark");
        }
    }

    void PowerBenchmark::Tick(
        double songTimeSeconds,
        const PlaybackDiagnostics& diagnostics) noexcept
    {
        if(!active_)
            return;
        try
        {
            const auto now = std::chrono::steady_clock::now();
            if(now < nextSampleAt_)
                return;

            CaptureSample(songTimeSeconds, diagnostics);
            // Never issue a burst of Java calls after a pause or hitch. One
            // current sample per second is enough for battery trend analysis.
            nextSampleAt_ = now + std::chrono::seconds(1);
        }
        catch(...)
        {
            DisableCurrentSession("sampling the power benchmark");
        }
    }

    void PowerBenchmark::Finish(
        const std::optional<PlaybackResultsData>& results) noexcept
    {
        if(!active_)
        {
            Cancel();
            return;
        }

        try
        {
            const PlaybackDiagnostics finalDiagnostics = results
                ? results->video
                : (samples_.empty()
                    ? PlaybackDiagnostics{}
                    : samples_.back().diagnostics);
            CaptureSample(
                samples_.empty() ? 0.0 : samples_.back().songTimeSeconds,
                finalDiagnostics);
            AppendFiles(results);
            PaperLogger.info(
                "Power benchmark saved to '{}' and '{}'",
                SummaryPath,
                SamplesPath);
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not save the power benchmark: {}",
                exception.what());
            ErrorManager::Instance().ReportUserVisible(
                "Power benchmark was not saved",
                std::string(exception.what()) +
                    "\n\nSee Big Screen's error log for details.");
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not save the power benchmark because of an unknown native exception");
            ErrorManager::Instance().ReportUserVisible(
                "Power benchmark was not saved",
                "An unexpected file error occurred. See Big Screen's error log for details.");
        }
        Cancel();
    }

    void PowerBenchmark::Cancel() noexcept
    {
        active_ = false;
        prepared_ = false;
        videoActive_ = false;
        showcaseActive_ = false;
        batteryWarningLogged_ = false;
        samples_.clear();
        sessionId_.clear();
    }

    void PowerBenchmark::ProbeBatteryAccessOnce() noexcept
    {
        if(batteryProbeAttempted_ ||
           !Settings::Instance().PowerBenchmarkEnabled())
            return;

        batteryProbeAttempted_ = true;
        const auto battery = ReadBattery();
        const bool hasAnyProperty =
            battery.chargeMicroampHours.has_value() ||
            battery.currentNowMicroamps.has_value() ||
            battery.currentAverageMicroamps.has_value() ||
            battery.energyNanowattHours.has_value() ||
            battery.capacityPercent.has_value() ||
            battery.status.has_value() ||
            battery.charging.has_value();
        if(!hasAnyProperty)
        {
            PaperLogger.error(
                "Quest battery probe completed without any readable properties");
            return;
        }

        // Log the raw units supplied by Android. Unsupported optional
        // properties are deliberately blank instead of being invented or
        // reported as zero, which could invalidate a power comparison.
        PaperLogger.info(
            "Quest battery probe succeeded: charge_uah='{}', current_now_ua='{}', current_average_ua='{}', energy_nwh='{}', capacity_percent='{}', status='{}', charging='{}'",
            OptionalNumber(battery.chargeMicroampHours),
            OptionalNumber(battery.currentNowMicroamps),
            OptionalNumber(battery.currentAverageMicroamps),
            OptionalNumber(battery.energyNanowattHours),
            OptionalNumber(battery.capacityPercent),
            OptionalNumber(battery.status),
            OptionalBool(battery.charging));
    }

    PowerBenchmark::BatterySample PowerBenchmark::ReadBattery() noexcept
    {
        BatterySample result;
        UnityEngine::AndroidJavaClass* unityPlayer = nullptr;
        UnityEngine::AndroidJavaObject* activity = nullptr;
        UnityEngine::AndroidJavaObject* manager = nullptr;
        try
        {
            unityPlayer = UnityEngine::AndroidJavaClass::New_ctor(
                "com.unity3d.player.UnityPlayer");
            activity = unityPlayer->GetStatic<
                UnityEngine::AndroidJavaObject*>("currentActivity");
            if(!activity)
                throw std::runtime_error("Unity did not expose the Android activity");

            const StringW batteryService("batterymanager");
            auto* serviceArguments = Array<::System::Object*>::New({
                reinterpret_cast<::System::Object*>(
                    static_cast<Il2CppString*>(batteryService))});
            manager = activity->Call<UnityEngine::AndroidJavaObject*>(
                "getSystemService",
                serviceArguments);
            if(!manager)
                throw std::runtime_error("Android did not expose BatteryManager");

            const auto readInt = [manager](int property) -> std::optional<std::int64_t>
            {
                // AndroidJavaObject mirrors Unity's C# params object[] API.
                // Passing Array<int> selects a generated generic overload that
                // IL2CPP cannot execute here. Box the primitive property ID and
                // use the ordinary object[] overload, exactly as Unity's Java
                // bridge expects for BatteryManager.getIntProperty(int).
                int boxedProperty = property;
                auto* arguments = Array<::System::Object*>::New({
                    reinterpret_cast<::System::Object*>(
                        il2cpp_utils::Box(&boxedProperty))});
                const int value = manager->Call<int>(
                    "getIntProperty",
                    arguments);
                if(value == std::numeric_limits<int>::min())
                    return std::nullopt;
                return static_cast<std::int64_t>(value);
            };
            result.chargeMicroampHours = readInt(ChargeCounterProperty);
            result.currentNowMicroamps = readInt(CurrentNowProperty);
            result.currentAverageMicroamps = readInt(CurrentAverageProperty);
            if(const auto capacity = readInt(CapacityProperty))
                result.capacityPercent = static_cast<int>(*capacity);
            if(const auto status = readInt(StatusProperty))
                result.status = static_cast<int>(*status);

            int boxedEnergyProperty = EnergyCounterProperty;
            auto* energyArguments = Array<::System::Object*>::New({
                reinterpret_cast<::System::Object*>(
                    il2cpp_utils::Box(&boxedEnergyProperty))});
            const std::int64_t energy = manager->Call<std::int64_t>(
                "getLongProperty",
                energyArguments);
            if(energy != std::numeric_limits<std::int64_t>::min())
                result.energyNanowattHours = energy;
            result.charging = manager->Call<bool>(
                "isCharging",
                Array<::System::Object*>::New({}));

        }
        catch(const std::exception& exception)
        {
            if(!batteryWarningLogged_)
            {
                batteryWarningLogged_ = true;
                PaperLogger.warn(
                    "Quest battery properties are unavailable for this benchmark: {}",
                    exception.what());
            }
        }
        catch(...)
        {
            if(!batteryWarningLogged_)
            {
                batteryWarningLogged_ = true;
                PaperLogger.warn(
                    "Quest battery properties are unavailable for this benchmark");
            }
        }
        DisposeQuietly(manager);
        DisposeQuietly(activity);
        DisposeQuietly(unityPlayer);
        return result;
    }

    void PowerBenchmark::CaptureSample(
        double songTimeSeconds,
        const PlaybackDiagnostics& diagnostics)
    {
        Sample sample;
        sample.elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startedAt_).count();
        sample.songTimeSeconds = std::max(0.0, songTimeSeconds);
        sample.processCpuMilliseconds = std::max(
            0.0,
            ProcessCpuMilliseconds() - processCpuBaselineMilliseconds_);
        sample.decoderCpuMilliseconds = diagnostics.decoderCpuMilliseconds;
        sample.diagnostics = diagnostics;
        sample.battery = ReadBattery();
        samples_.push_back(std::move(sample));
    }

    void PowerBenchmark::AppendFiles(
        const std::optional<PlaybackResultsData>& results)
    {
        if(samples_.empty())
            throw std::runtime_error("The benchmark contained no samples");

        std::filesystem::create_directories(LogDirectory);
        AppendHeaderIfEmpty(
            SamplesPath,
            "session_id,timestamp_utc,elapsed_s,song_time_s,video_active,showcase_active,decode_method,codec,process_cpu_ms,decoder_cpu_ms,charge_uah,current_now_ua,current_average_ua,energy_nwh,capacity_percent,battery_status,is_charging,video_width,video_height,source_fps,fps_limit,expected_frames,presented_frames,decode_average_ms,decode_peak_ms,automatic_reductions");
        {
            std::ofstream output(SamplesPath, std::ios::app);
            if(!output)
                throw std::runtime_error("Could not append the power sample log");
            output << std::fixed << std::setprecision(3);
            for(const auto& sample : samples_)
            {
                output
                    << Csv(sessionId_) << ','
                    << Csv(UtcTimestamp(
                           startedUtc_ + std::chrono::milliseconds(
                               static_cast<std::int64_t>(sample.elapsedSeconds * 1000.0)))) << ','
                    << sample.elapsedSeconds << ','
                    << sample.songTimeSeconds << ','
                    << (videoActive_ ? 1 : 0) << ','
                    << (showcaseActive_ ? 1 : 0) << ','
                    << Csv(sample.diagnostics.decodeMethod.empty()
                           ? "none" : sample.diagnostics.decodeMethod) << ','
                    << Csv(sample.diagnostics.codec.empty()
                           ? "none" : sample.diagnostics.codec) << ','
                    << sample.processCpuMilliseconds << ','
                    << sample.decoderCpuMilliseconds << ','
                    << OptionalNumber(sample.battery.chargeMicroampHours) << ','
                    << OptionalNumber(sample.battery.currentNowMicroamps) << ','
                    << OptionalNumber(sample.battery.currentAverageMicroamps) << ','
                    << OptionalNumber(sample.battery.energyNanowattHours) << ','
                    << OptionalNumber(sample.battery.capacityPercent) << ','
                    << OptionalNumber(sample.battery.status) << ','
                    << OptionalBool(sample.battery.charging) << ','
                    << sample.diagnostics.videoWidth << ','
                    << sample.diagnostics.videoHeight << ','
                    << sample.diagnostics.sourceFps << ','
                    << sample.diagnostics.outputFpsLimit << ','
                    << sample.diagnostics.expectedFrames << ','
                    << sample.diagnostics.presentedFrames << ','
                    << sample.diagnostics.averageDecodeMilliseconds << ','
                    << sample.diagnostics.peakDecodeMilliseconds << ','
                    << sample.diagnostics.automaticReductions << '\n';
            }
        }

        const auto& first = samples_.front();
        const auto& last = samples_.back();
        const double wallSeconds = std::max(0.0, last.elapsedSeconds);
        const double processCpu = last.processCpuMilliseconds;
        const double decoderCpu = last.decoderCpuMilliseconds;
        const auto averageCurrentNow = AverageSupported(
            samples_, [](const Sample& sample)
            {
                return sample.battery.currentNowMicroamps;
            });
        const auto averageReportedCurrent = AverageSupported(
            samples_, [](const Sample& sample)
            {
                return sample.battery.currentAverageMicroamps;
            });
        std::optional<std::int64_t> minimumCurrentNow;
        std::optional<std::int64_t> maximumCurrentNow;
        for(const auto& sample : samples_)
        {
            if(!sample.battery.currentNowMicroamps)
                continue;
            minimumCurrentNow = minimumCurrentNow
                ? std::min(*minimumCurrentNow, *sample.battery.currentNowMicroamps)
                : sample.battery.currentNowMicroamps;
            maximumCurrentNow = maximumCurrentNow
                ? std::max(*maximumCurrentNow, *sample.battery.currentNowMicroamps)
                : sample.battery.currentNowMicroamps;
        }

        std::optional<double> consumedMicroampHours;
        std::optional<double> drainMahPerHour;
        if(first.battery.chargeMicroampHours &&
           last.battery.chargeMicroampHours)
        {
            consumedMicroampHours = CoreLogic::ChargeConsumedMicroampHours(
                *first.battery.chargeMicroampHours,
                *last.battery.chargeMicroampHours);
            drainMahPerHour = CoreLogic::ConsumptionRateMahPerHour(
                *consumedMicroampHours,
                wallSeconds);
        }

        const PlaybackDiagnostics finalDiagnostics = results
            ? results->video
            : last.diagnostics;
        AppendHeaderIfEmpty(
            SummaryPath,
            "session_id,started_utc,level_id,song_name,song_artist,characteristic,difficulty,video_active,showcase_active,decode_method,codec,duration_s,process_cpu_ms,process_cpu_percent_one_core,process_equivalent_cores,decoder_cpu_ms,decoder_cpu_percent_one_core,charge_start_uah,charge_end_uah,charge_consumed_uah,estimated_drain_mah_per_hour,current_now_min_ua,current_now_average_ua,current_now_max_ua,current_average_property_ua,capacity_start_percent,capacity_end_percent,charging_start,charging_end,video_width,video_height,source_fps,fps_limit,expected_frames,presented_frames,missed_frames,missed_percent,gameplay_fps_min,gameplay_fps_average,gameplay_fps_max,decode_average_ms,decode_peak_ms,rgba_allocations,automatic_reductions");
        std::ofstream summary(SummaryPath, std::ios::app);
        if(!summary)
            throw std::runtime_error("Could not append the power summary log");
        summary << std::fixed << std::setprecision(3)
            << Csv(sessionId_) << ','
            << Csv(UtcTimestamp(startedUtc_)) << ','
            << Csv(levelId_) << ','
            << Csv(songName_) << ','
            << Csv(songArtist_) << ','
            << Csv(characteristic_) << ','
            << difficulty_ << ','
            << (videoActive_ ? 1 : 0) << ','
            << (showcaseActive_ ? 1 : 0) << ','
            << Csv(finalDiagnostics.decodeMethod.empty()
                   ? "none" : finalDiagnostics.decodeMethod) << ','
            << Csv(finalDiagnostics.codec.empty()
                   ? "none" : finalDiagnostics.codec) << ','
            << wallSeconds << ','
            << processCpu << ','
            << CoreLogic::CpuPercentOfOneCore(processCpu, wallSeconds) << ','
            << CoreLogic::EquivalentCpuCores(processCpu, wallSeconds) << ','
            << decoderCpu << ','
            << CoreLogic::CpuPercentOfOneCore(decoderCpu, wallSeconds) << ','
            << OptionalNumber(first.battery.chargeMicroampHours) << ','
            << OptionalNumber(last.battery.chargeMicroampHours) << ','
            << OptionalFixed(consumedMicroampHours, 3) << ','
            << OptionalFixed(drainMahPerHour, 3) << ','
            << OptionalNumber(minimumCurrentNow) << ','
            << OptionalFixed(averageCurrentNow, 3) << ','
            << OptionalNumber(maximumCurrentNow) << ','
            << OptionalFixed(averageReportedCurrent, 3) << ','
            << OptionalNumber(first.battery.capacityPercent) << ','
            << OptionalNumber(last.battery.capacityPercent) << ','
            << OptionalBool(first.battery.charging) << ','
            << OptionalBool(last.battery.charging) << ','
            << finalDiagnostics.videoWidth << ','
            << finalDiagnostics.videoHeight << ','
            << finalDiagnostics.sourceFps << ','
            << finalDiagnostics.outputFpsLimit << ','
            << finalDiagnostics.expectedFrames << ','
            << finalDiagnostics.presentedFrames << ','
            << (results ? results->missedVideoFrames : 0) << ','
            << (results ? results->missedVideoFramePercent : 0.0) << ','
            << (results ? results->minimumGameplayFps : 0.0) << ','
            << (results ? results->averageGameplayFps : 0.0) << ','
            << (results ? results->maximumGameplayFps : 0.0) << ','
            << finalDiagnostics.averageDecodeMilliseconds << ','
            << finalDiagnostics.peakDecodeMilliseconds << ','
            << finalDiagnostics.rgbaBufferAllocations << ','
            << finalDiagnostics.automaticReductions << '\n';
    }

    void PowerBenchmark::DisableCurrentSession(const char* operation) noexcept
    {
        PaperLogger.error(
            "Power benchmark stopped while {}; gameplay will continue",
            operation ? operation : "processing a sample");
        ErrorManager::Instance().RecordError(
            "Power benchmark",
            std::string("Benchmark stopped while ") +
                (operation ? operation : "processing a sample") +
                "; gameplay continued");
        active_ = false;
        samples_.clear();
    }
}
