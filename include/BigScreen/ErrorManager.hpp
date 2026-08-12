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

        void SetGameplayActive(bool active);
        void ReportInternal(const std::string& context, const std::string& detail);
        void ReportUserVisible(const std::string& title, const std::string& detail);
        std::optional<std::pair<std::string, std::string>> TakePendingDialog();
        /// Presents one pending message through Beat Saber's normal main-flow
        /// dialog. Must be called from a Unity main-thread Update hook.
        void TickMainThread();
        bool GameplayActive() const;
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

        static constexpr const char* LogFolder =
            "/sdcard/ModData/com.beatgames.beatsaber/logs";

    private:
        ErrorManager() = default;

        mutable std::mutex mutex_;
        bool gameplayActive_ = false;
        bool disabledByCircuitBreaker_ = false;
        bool disableRequested_ = false;
        std::chrono::steady_clock::time_point lastInternalError_{};
        std::optional<std::pair<std::string, std::string>> pendingDialog_;
        bool dialogVisible_ = false;
        bool dialogFailureLogged_ = false;
        std::chrono::steady_clock::time_point nextDialogAttempt_{};
    };
}
