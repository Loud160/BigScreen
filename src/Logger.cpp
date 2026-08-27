// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

#include "BigScreen/Logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>

namespace {
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
            NativeLoggerOptions options;
            options.activePath =
                "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/"
                "bigscreen-native.log";
            options.previousPath =
                "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/"
                "bigscreen-native.previous.log";
            options.emitToLogcat = true;
            const std::string header =
                "Big Screen " + std::string(version) +
                " native log session started; backend mode: "
                "Big Screen native only";
            NativeLogger::Instance().Initialize(
                std::move(options),
                header);

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
        NativeLogger::Instance().Flush(std::chrono::milliseconds(200));
    }

    void LoggerFacade::Shutdown() const noexcept
    {
        NativeLogger::Instance().Flush(std::chrono::milliseconds(250));
        NativeLogger::Instance().Shutdown();
    }

    void LoggerFacade::Emit(
        LogSeverity severity,
        std::string message,
        LogSource source) noexcept
    {
        NativeLogger::Instance().Log(
            severity,
            std::move(message),
            source);
        if(severity == LogSeverity::Critical)
        {
            // Hook installation aborts immediately after a critical failure.
            // Give the writer a small, fixed opportunity to persist that final
            // diagnostic without creating an unbounded crash-path wait.
            NativeLogger::Instance().Flush(
                std::chrono::milliseconds(100));
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
