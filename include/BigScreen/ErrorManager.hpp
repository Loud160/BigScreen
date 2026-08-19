// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <chrono>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace BigScreen {
    /// Central boundary for failures inside Big Screen itself. Expected user,
    /// content, login, certificate, and network errors are intentionally not
    /// counted; only programming/runtime failures can trip the circuit breaker.
    class ErrorManager final {
    public:
        static ErrorManager& Instance();

        /// Creates the persistent, append-only error history before any other
        /// subsystem loads. Unlike PaperLog, this file survives game restarts.
        void InitializePersistentLog() noexcept;
        /// Records an expected or handled failure that does not need to count
        /// toward the internal-error circuit breaker.
        std::string RecordError(
            const std::string& context,
            const std::string& detail) noexcept;
        /// Appends one completed gameplay session. This is intentionally
        /// called only at teardown, never from the per-frame playback path.
        void RecordPerformance(
            const std::string& mapIdentity,
            const std::string& summary) noexcept;
        void SetGameplayActive(bool active);
        void ReportInternal(
            const std::string& context,
            const std::string& detail) noexcept;
        void ReportUserVisible(
            const std::string& title,
            const std::string& detail) noexcept;
        std::optional<std::pair<std::string, std::string>> TakePendingDialog();
        /// Presents one pending message through Beat Saber's normal main-flow
        /// dialog. Must be called from a Unity main-thread Update hook.
        void TickMainThread();
        bool GameplayActive() const;
        /// Prevents another update of the failed mod menu while Beat Saber is
        /// animating its dismissal and preparing the main-flow dialog.
        bool MenuRecoveryActive() const;
        /// Starts a fresh error window when the player deliberately turns Big
        /// Screen back on after the circuit breaker disabled it.
        void ResetCircuitBreaker();

        template<typename Function>
        bool Guard(const char* context, Function&& function) noexcept
        {
            try
            {
                function();
                return true;
            }
            catch(const std::exception& exception)
            {
                ReportInternal(context, exception.what());
            }
            catch(...)
            {
                ReportInternal(context, "Unknown native exception");
            }
            return false;
        }

        static constexpr const char* PersistentErrorLog =
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/error-history.log";
        static constexpr const char* PerformanceLog =
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/performance-history.log";

    private:
        ErrorManager() = default;

        mutable std::mutex mutex_;
        bool gameplayActive_ = false;
        bool disabledByCircuitBreaker_ = false;
        bool disableRequested_ = false;
        bool menuExitRequested_ = false;
        bool waitingForMenuExit_ = false;
        std::chrono::steady_clock::time_point lastInternalError_{};
        std::optional<std::pair<std::string, std::string>> pendingDialog_;
        bool dialogVisible_ = false;
        bool dialogFailureLogged_ = false;
        std::chrono::steady_clock::time_point nextDialogAttempt_{};
        std::mutex persistentLogMutex_;
    };
}
