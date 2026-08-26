// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

#include "BigScreen/Logger.hpp"

#if BIGSCREEN_LOGGER_MODE != 1
#include "paper2_scotland2/shared/paperlog.hpp"
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>

namespace {
    using BigScreen::LogSeverity;
    using BigScreen::LogSource;
    using BigScreen::LoggerBackendMode;

    constexpr bool UsesPaper =
        BigScreen::ActiveLoggerBackendMode == LoggerBackendMode::PaperOnly ||
        BigScreen::ActiveLoggerBackendMode == LoggerBackendMode::Dual;
    constexpr bool UsesNative =
        BigScreen::ActiveLoggerBackendMode == LoggerBackendMode::NativeOnly ||
        BigScreen::ActiveLoggerBackendMode == LoggerBackendMode::Dual;

#if BIGSCREEN_LOGGER_MODE != 0
    constexpr std::string_view ModeName() noexcept
    {
        switch(BigScreen::ActiveLoggerBackendMode)
        {
            case LoggerBackendMode::PaperOnly:
                return "Paper2 only";
            case LoggerBackendMode::NativeOnly:
                return "Big Screen native only";
            case LoggerBackendMode::Dual:
                return "Paper2 plus Big Screen native";
        }
        return "invalid";
    }
#endif

#if BIGSCREEN_LOGGER_MODE != 1
    Paper::ffi::paper2_LogLevel PaperLevel(LogSeverity severity) noexcept
    {
        switch(severity)
        {
            case LogSeverity::Debug:
                return static_cast<Paper::ffi::paper2_LogLevel>(
                    Paper::LogLevel::DBG);
            case LogSeverity::Info:
                return static_cast<Paper::ffi::paper2_LogLevel>(
                    Paper::LogLevel::INF);
            case LogSeverity::Warning:
                return static_cast<Paper::ffi::paper2_LogLevel>(
                    Paper::LogLevel::WRN);
            case LogSeverity::Error:
                return static_cast<Paper::ffi::paper2_LogLevel>(
                    Paper::LogLevel::ERR);
            case LogSeverity::Critical:
                return static_cast<Paper::ffi::paper2_LogLevel>(
                    Paper::LogLevel::CRIT);
        }
        return static_cast<Paper::ffi::paper2_LogLevel>(Paper::LogLevel::ERR);
    }

    void ForwardToPaper(
        LogSeverity severity,
        std::string_view message,
        LogSource source) noexcept
    {
        try
        {
            constexpr std::string_view tag = BigScreen::LoggerFacade::tag;
            const std::string_view file = source.file ? source.file : "";
            const std::string_view function =
                source.function ? source.function : "";
            Paper::ffi::paper2_queue_log_bytes_ffi(
                PaperLevel(severity),
                reinterpret_cast<const std::uint8_t*>(tag.data()),
                tag.size(),
                reinterpret_cast<const std::uint8_t*>(message.data()),
                message.size(),
                reinterpret_cast<const std::uint8_t*>(file.data()),
                file.size(),
                source.line,
                0,
                reinterpret_cast<const std::uint8_t*>(function.data()),
                function.size());
        }
        catch(...)
        {
            // Paper remains a diagnostic sink during comparison. Its failure
            // must never escape into Big Screen or prevent the native sink
            // from receiving the same record.
        }
    }
#endif

    void ShutdownLoggerAtExit() noexcept
    {
        BigScreen::BigScreenLogger.Shutdown();
    }
}

namespace BigScreen {
    void LoggerFacade::Initialize(std::string_view version) const noexcept
    {
        try
        {
            if constexpr(UsesPaper)
            {
#if BIGSCREEN_LOGGER_MODE != 1
                Paper::Logger::RegisterFileContextId(tag);
#endif
            }

            if constexpr(UsesNative)
            {
                NativeLoggerOptions options;
                options.activePath =
                    "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/"
                    "bigscreen-native.log";
                options.previousPath =
                    "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/"
                    "bigscreen-native.previous.log";
                // Paper already mirrors the dual-mode record to logcat. Avoid
                // duplicate Android lines while retaining native logcat as the
                // complete system sink in native-only comparison builds.
                options.emitToLogcat =
                    ActiveLoggerBackendMode == LoggerBackendMode::NativeOnly;
                const std::string header =
                    "Big Screen " + std::string(version) +
                    " native log session started; backend mode: " +
                    std::string(ModeName());
                NativeLogger::Instance().Initialize(
                    std::move(options),
                    header);
            }

            static std::atomic_flag registered = ATOMIC_FLAG_INIT;
            if(!registered.test_and_set(std::memory_order_relaxed))
                std::atexit(ShutdownLoggerAtExit);
        }
        catch(...)
        {
            ReportFormattingFailure("logger initialization failed");
        }
    }

    void LoggerFacade::Flush() const noexcept
    {
        if constexpr(UsesPaper)
        {
#if BIGSCREEN_LOGGER_MODE != 1
            try
            {
                Paper::Logger::WaitForFlushTimeout(200);
            }
            catch(...)
            {}
#endif
        }
        if constexpr(UsesNative)
        {
            NativeLogger::Instance().Flush(std::chrono::milliseconds(200));
        }
    }

    void LoggerFacade::Shutdown() const noexcept
    {
        if constexpr(UsesNative)
        {
            NativeLogger::Instance().Flush(std::chrono::milliseconds(250));
            NativeLogger::Instance().Shutdown();
        }
        if constexpr(UsesPaper)
        {
#if BIGSCREEN_LOGGER_MODE != 1
            try
            {
                Paper::Logger::WaitForFlushTimeout(250);
            }
            catch(...)
            {}
#endif
        }
    }

    void LoggerFacade::Emit(
        LogSeverity severity,
        std::string message,
        LogSource source) noexcept
    {
        if constexpr(UsesPaper)
        {
#if BIGSCREEN_LOGGER_MODE != 1
            ForwardToPaper(severity, message, source);
#endif
        }
        if constexpr(UsesNative)
        {
            NativeLogger::Instance().Log(
                severity,
                std::move(message),
                source);
            if(severity == LogSeverity::Critical)
            {
                // Hook installation aborts immediately after a critical
                // failure. Give the writer a small, fixed opportunity to
                // persist that final diagnostic without creating an
                // unbounded crash-path wait.
                NativeLogger::Instance().Flush(
                    std::chrono::milliseconds(100));
            }
        }
    }

    void LoggerFacade::ReportFormattingFailure(const char* detail) noexcept
    {
        try
        {
            Emit(
                LogSeverity::Error,
                std::string("Big Screen logger could not format a message: ") +
                    (detail ? detail : "unknown error"),
                {});
        }
        catch(...)
        {
            Emit(
                LogSeverity::Error,
                "Big Screen logger could not format a message",
                {});
        }
    }
}
