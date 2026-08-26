// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace BigScreen {
    enum class LogSeverity : std::uint8_t {
        Debug,
        Info,
        Warning,
        Error,
        Critical,
    };

    struct LogSource {
        // LoggerFacade supplies std::source_location strings, whose storage is
        // static for the process lifetime. Keeping these non-owning avoids two
        // extra allocations on every producer call; direct native-backend
        // callers must provide strings that outlive the asynchronous write.
        const char* file = "";
        const char* function = "";
        std::uint_least32_t line = 0;
    };

    struct NativeLoggerOptions {
        std::filesystem::path activePath;
        std::filesystem::path previousPath;
        std::size_t maxFileBytes = 5u * 1024u * 1024u;
        std::size_t maxQueueBytes = 1024u * 1024u;
        std::size_t maxQueueEntries = 2048u;
        std::size_t urgentReserveBytes = 128u * 1024u;
        std::size_t urgentReserveEntries = 128u;
        std::chrono::milliseconds flushInterval{250};
        std::chrono::milliseconds reopenInterval{5000};
        bool emitToLogcat = true;
    };

    struct NativeLoggerStatistics {
        std::uint64_t acceptedMessages = 0;
        std::uint64_t fileMessages = 0;
        std::uint64_t droppedMessages = 0;
        std::uint64_t fileFailures = 0;
        std::uint64_t rotations = 0;
        std::size_t peakQueueBytes = 0;
        std::size_t peakQueueEntries = 0;
    };

    /// Big Screen's private general-purpose logger backend.
    ///
    /// Calls emit immediately to Android logcat and enqueue one already-
    /// formatted record for the owned file-writer thread. The queue and files
    /// are bounded, all methods fail open, and no exception is allowed to
    /// escape into a hook, Unity callback, decoder worker, or download worker.
    class NativeLogger final {
      public:
        static NativeLogger& Instance() noexcept;

        NativeLogger(const NativeLogger&) = delete;
        NativeLogger& operator=(const NativeLogger&) = delete;

        bool Initialize(
            NativeLoggerOptions options,
            std::string sessionHeader) noexcept;
        void Log(
            LogSeverity severity,
            std::string message,
            LogSource source = {}) noexcept;
        bool Flush(std::chrono::milliseconds timeout) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] NativeLoggerStatistics Statistics() const noexcept;

      private:
        NativeLogger() noexcept;
        ~NativeLogger();

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
