// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "BigScreen/NativeLogger.hpp"

#include <fmt/format.h>

#include <exception>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace BigScreen {
    template<typename... Args>
    struct LogFormatString {
        fmt::format_string<Args...> format;
        std::source_location source;

        template<typename S>
            requires(std::is_convertible_v<
                     const S&,
                     fmt::basic_string_view<char>>)
        consteval LogFormatString(
            const S& value,
            std::source_location location =
                std::source_location::current())
            : format(value), source(location)
        {}
    };

    /// Stable logging façade used by every Big Screen call site. It formats
    /// each record once and routes it only to Big Screen's private native sink.
    class LoggerFacade final {
      public:
        static constexpr std::string_view tag = "bigscreen";

        void Initialize(std::string_view version) const noexcept;
        void Flush() const noexcept;
        void Shutdown() const noexcept;

        template<typename... Args>
        void debug(
            const LogFormatString<std::type_identity_t<Args>...>& format,
            Args&&... args) const noexcept
        {
            FormatAndEmit(
                LogSeverity::Debug,
                format,
                std::forward<Args>(args)...);
        }

        template<typename... Args>
        void info(
            const LogFormatString<std::type_identity_t<Args>...>& format,
            Args&&... args) const noexcept
        {
            FormatAndEmit(
                LogSeverity::Info,
                format,
                std::forward<Args>(args)...);
        }

        template<typename... Args>
        void warn(
            const LogFormatString<std::type_identity_t<Args>...>& format,
            Args&&... args) const noexcept
        {
            FormatAndEmit(
                LogSeverity::Warning,
                format,
                std::forward<Args>(args)...);
        }

        template<typename... Args>
        void error(
            const LogFormatString<std::type_identity_t<Args>...>& format,
            Args&&... args) const noexcept
        {
            FormatAndEmit(
                LogSeverity::Error,
                format,
                std::forward<Args>(args)...);
        }

        template<typename... Args>
        void critical(
            const LogFormatString<std::type_identity_t<Args>...>& format,
            Args&&... args) const noexcept
        {
            FormatAndEmit(
                LogSeverity::Critical,
                format,
                std::forward<Args>(args)...);
        }

      private:
        template<typename... Args>
        void FormatAndEmit(
            LogSeverity severity,
            const LogFormatString<std::type_identity_t<Args>...>& format,
            Args&&... args) const noexcept
        {
            try
            {
                Emit(
                    severity,
                    fmt::format(
                        format.format,
                        std::forward<Args>(args)...),
                    {
                        format.source.file_name(),
                        format.source.function_name(),
                        format.source.line(),
                    });
            }
            catch(const std::exception& exception)
            {
                ReportFormattingFailure(exception.what());
            }
            catch(...)
            {
                ReportFormattingFailure("unknown formatting error");
            }
        }

        static void Emit(
            LogSeverity severity,
            std::string message,
            LogSource source) noexcept;
        static void ReportFormattingFailure(const char* detail) noexcept;
    };

    inline LoggerFacade BigScreenLogger;
}
