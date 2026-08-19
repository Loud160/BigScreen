// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/ErrorManager.hpp"

#include <stdexcept>
#include <atomic>
#include <cstdint>
#include <functional>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/DiagnosticSessionLogger.hpp"
#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/ScreenPreview.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/SimpleDialogPromptViewController.hpp"
#include "HMUI/FlowCoordinator.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "custom-types/shared/delegate.hpp"
#include "System/Action_1.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        std::optional<std::tm> LocalTime(std::time_t value) noexcept
        {
            std::tm result{};
#if defined(_WIN32)
            return localtime_s(&result, &value) == 0
                ? std::optional<std::tm>(result) : std::nullopt;
#else
            return localtime_r(&value, &result)
                ? std::optional<std::tm>(result) : std::nullopt;
#endif
        }

        std::atomic<std::uint64_t> errorSequence{0};

        std::string NewCorrelationId(
            std::chrono::system_clock::time_point now) noexcept
        {
            try
            {
                const auto time = std::chrono::system_clock::to_time_t(now);
                const auto local = LocalTime(time);
                std::ostringstream output;
                output << "BS-ERR-";
                if(local)
                    output << std::put_time(&*local, "%Y%m%d-%H%M%S");
                else
                    output << "UNKNOWN";
                output << '-' << std::setw(6) << std::setfill('0')
                       << errorSequence.fetch_add(1);
                return output.str();
            }
            catch(...)
            {
                return "BS-ERR-UNAVAILABLE";
            }
        }
    }

    ErrorManager& ErrorManager::Instance()
    {
        static ErrorManager manager;
        return manager;
    }

    void ErrorManager::InitializePersistentLog() noexcept
    {
        try
        {
            const std::filesystem::path logPath(PersistentErrorLog);
            std::filesystem::create_directories(logPath.parent_path());
            constexpr std::uintmax_t MaximumHistoryBytes = 5u * 1024u * 1024u;
            std::error_code sizeError;
            if(std::filesystem::exists(logPath, sizeError) && !sizeError &&
               std::filesystem::file_size(logPath, sizeError) >
                   MaximumHistoryBytes && !sizeError)
            {
                const auto previous = logPath.parent_path() /
                    "error-history.previous.log";
                std::error_code rotationError;
                std::filesystem::remove(previous, rotationError);
                rotationError.clear();
                std::filesystem::rename(logPath, previous, rotationError);
                if(rotationError)
                    PaperLogger.error(
                        "Could not rotate persistent Big Screen error history: {}",
                        rotationError.message());
            }
            std::ofstream output(logPath, std::ios::app);
            if(!output)
                throw std::runtime_error("the error-history file could not be opened");
            output << "\n=== Big Screen " << VERSION
                   << " session started ===\n";
            output.flush();
        }
        catch(const std::exception& exception)
        {
            // PaperLog is the only remaining sink if external storage is not
            // writable. Never let diagnostics prevent the mod from starting.
            PaperLogger.error(
                "Could not initialize persistent Big Screen error history: {}",
                exception.what());
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not initialize persistent Big Screen error history");
        }
    }

    std::string ErrorManager::RecordError(
        const std::string& context,
        const std::string& detail) noexcept
    {
        try
        {
            const auto now = std::chrono::system_clock::now();
            const auto correlationId = NewCorrelationId(now);
            {
                std::scoped_lock lock(persistentLogMutex_);
                const std::filesystem::path logPath(PersistentErrorLog);
                std::filesystem::create_directories(logPath.parent_path());
                std::ofstream output(logPath, std::ios::app);
                if(output)
                {
                    const std::time_t time =
                        std::chrono::system_clock::to_time_t(now);
                    const auto local = LocalTime(time);
                    if(local)
                        output << '[' << std::put_time(
                            &*local, "%Y-%m-%d %H:%M:%S") << "] ";
                    output << '[' << correlationId << "] "
                           << context << ": " << detail << '\n';
                    output.flush();
                }
            }
            // This call occurs only after the persistent-log lock is released.
            // The session logger is fail-open and never calls ErrorManager,
            // preventing diagnostics from creating a recursive lock path.
            DiagnosticSessionLogger::Instance().CorrelatedError(
                correlationId, context, detail);
            return correlationId;
        }
        catch(...)
        {
            // Diagnostics are fail-open. Reporting a logging failure through
            // this class would recurse and could turn a storage issue into a
            // circuit-breaker event.
            return {};
        }
    }

    void ErrorManager::RecordPerformance(
        const std::string& mapIdentity,
        const std::string& summary) noexcept
    {
        try
        {
            std::scoped_lock lock(persistentLogMutex_);
            const std::filesystem::path logPath(PerformanceLog);
            std::filesystem::create_directories(logPath.parent_path());
            std::ofstream output(logPath, std::ios::app);
            if(!output)
                return;

            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            const auto local = LocalTime(time);
            output << "\n[";
            if(local)
                output << std::put_time(&*local, "%Y-%m-%d %H:%M:%S");
            else
                output << "unknown time";
            output << "] " << mapIdentity << '\n'
                   << summary << '\n';
            output.flush();
        }
        catch(...)
        {
            // Performance diagnostics must never affect gameplay teardown.
        }
    }

    void ErrorManager::SetGameplayActive(bool active)
    {
        std::scoped_lock lock(mutex_);
        gameplayActive_ = active;
    }

    bool ErrorManager::GameplayActive() const
    {
        std::scoped_lock lock(mutex_);
        return gameplayActive_;
    }

    bool ErrorManager::MenuRecoveryActive() const
    {
        std::scoped_lock lock(mutex_);
        return menuExitRequested_ || waitingForMenuExit_;
    }

    void ErrorManager::ReportUserVisible(
        const std::string& title,
        const std::string& detail) noexcept
    {
        try
        {
            PaperLogger.error("{}: {}", title, detail);
            const auto correlationId = RecordError(title, detail);
            DiagnosticSessionLogger::Instance().MenuEvent(
                "dialog_opened", "ErrorManager", {
                    {"title", title}, {"correlationId", correlationId}});
            std::scoped_lock lock(mutex_);
            // Keep only the newest message. A single modal is useful; a
            // backlog of stale popups can prevent the player from reaching
            // the disable switch.
            pendingDialog_ = std::make_pair(title, detail);
            nextDialogAttempt_ = {};
            dialogFailureLogged_ = false;
        }
        catch(...)
        {
            // Guard() is noexcept and calls this function while recovering
            // from another failure. Diagnostics must degrade silently if an
            // allocation or logging sink also fails; never terminate Beat
            // Saber while attempting to report the original problem.
            RecordError(title, detail);
        }
    }

    void ErrorManager::ReportInternal(
        const std::string& context,
        const std::string& detail) noexcept
    {
        try
        {
            PaperLogger.error("Internal failure in {}: {}", context, detail);
            RecordError("Internal failure in " + context, detail);
            const auto now = std::chrono::steady_clock::now();
            const auto signature = context + ": " + detail;
            {
                std::scoped_lock lock(mutex_);
                const bool secondFailure =
                    lastInternalError_ !=
                        std::chrono::steady_clock::time_point{} &&
                    CoreLogic::IsSecondFailureWithin(now - lastInternalError_);
                lastInternalError_ = now;
                if(secondFailure && !disabledByCircuitBreaker_)
                {
                    disabledByCircuitBreaker_ = true;
                    disableRequested_ = true;
                    pendingDialog_ = std::make_pair(
                        "Big Screen disabled itself",
                        "Big Screen encountered two internal errors within three minutes, so it turned itself off to protect Beat Saber.\n\nLast error: " +
                        signature + "\n\nError log: " + PersistentErrorLog +
                        "\n\nYou can turn the mod back on from its General tab after reviewing the log.");
                }
                else if(!gameplayActive_)
                {
                    pendingDialog_ = std::make_pair(
                        "Big Screen error",
                        "Big Screen could not complete an internal operation.\n\n" +
                        signature + "\n\nThe error was recorded in " +
                        PersistentErrorLog + ".");
                }
                if(!gameplayActive_)
                    menuExitRequested_ = true;
                // During gameplay the first failure remains log-only. If a
                // second failure follows soon after, one circuit-breaker
                // dialog waits until gameplay ends.
            }
        }
        catch(...)
        {
            // This is the final failure boundary used by every noexcept hook.
            // RecordError is itself noexcept and catches all sink failures.
            RecordError("Internal error reporting failed", context);
        }
        // Settings and its JSON document belong to the Unity thread. Worker
        // failures request the state change here; TickMainThread applies it
        // after gameplay rather than writing config from a background thread.
    }

    std::optional<std::pair<std::string, std::string>>
    ErrorManager::TakePendingDialog()
    {
        std::scoped_lock lock(mutex_);
        if(gameplayActive_)
            return std::nullopt;
        auto result = pendingDialog_;
        pendingDialog_.reset();
        return result;
    }

    void ErrorManager::TickMainThread()
    {
        bool disable = false;
        bool requestMenuExit = false;
        bool waitingForMenuExit = false;
        {
            std::scoped_lock lock(mutex_);
            if(!gameplayActive_ && disableRequested_)
            {
                disableRequested_ = false;
                disable = true;
            }
            requestMenuExit = menuExitRequested_;
            menuExitRequested_ = false;
            waitingForMenuExit = waitingForMenuExit_;
        }
        if(disable)
        {
            // Circuit-breaker cleanup is itself recovery code. Guard every
            // teardown operation so a damaged Unity object cannot turn the
            // original handled failure into an uncaught exception on the main
            // hook and take down Beat Saber's complete mod UI.
            Guard("disabling Big Screen after repeated errors", []()
            {
                Settings::Instance().SetModEnabled(false);
                // The circuit breaker must perform the same immediate teardown
                // as the menu's master switch. Merely persisting false would
                // leave a decoder, preview, or editor raycaster alive.
                SelectionVideoToggle::Instance().ModEnabledChanged(false);
                ScreenPreview::Instance().SetEnabled(false);
            });
        }

        if(requestMenuExit && IsBigScreenMenuActive())
        {
            const bool dismissalStarted = ExitBigScreenMenuAfterError();
            std::scoped_lock lock(mutex_);
            waitingForMenuExit_ = dismissalStarted;
            // Present the explanation only after the mod flow is gone. The
            // dialog then belongs to Beat Saber's main screen and cannot be
            // trapped behind the UI that just failed.
            nextDialogAttempt_ = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(250);
            if(dismissalStarted)
                return;
        }

        if(waitingForMenuExit)
        {
            if(IsBigScreenMenuActive())
                return;
            std::scoped_lock lock(mutex_);
            waitingForMenuExit_ = false;
            nextDialogAttempt_ = {};
        }

        // Big Screen owns a dedicated modal on its settings panel. Presenting
        // MainFlowCoordinator's SimpleDialogPrompt while this child flow is
        // active places the prompt behind Big Screen, but its modal blocker
        // still captures every controller click. The visible menu then looks
        // completely frozen even though Unity is continuing to render. Leave
        // ordinary user-facing errors queued here; SettingsMenu consumes them
        // through TakePendingDialog and displays them on the active flow. A
        // recovery-requested error reaches this point only after the menu has
        // been dismissed, so it can safely use Beat Saber's main dialog.
        if(IsBigScreenMenuActive())
            return;

        // Beat Saber can dismiss its shared prompt during a scene or flow
        // transition without invoking Big Screen's OK delegate. Do not let
        // that external dismissal leave dialogVisible_ latched for the rest
        // of the game session and silently suppress every later error.
        bool validateVisibleDialog = false;
        {
            std::scoped_lock lock(mutex_);
            validateVisibleDialog = dialogVisible_;
        }
        if(validateVisibleDialog)
        {
            bool promptStillVisible = false;
            try
            {
                auto* flow = BSML::Helpers::GetMainFlowCoordinator();
                auto prompt = flow
                    ? flow->__cordl_internal_get__simpleDialogPromptViewController()
                    : nullptr;
                promptStillVisible = prompt &&
                    (prompt->get_isInViewControllerHierarchy() ||
                     prompt->get_isInTransition());
            }
            catch(...)
            {
                // A failed liveness probe must reopen the error channel. The
                // later presentation path remains guarded and retry-limited.
            }
            if(!promptStillVisible)
            {
                std::scoped_lock lock(mutex_);
                dialogVisible_ = false;
            }
        }

        std::pair<std::string, std::string> message;
        {
            std::scoped_lock lock(mutex_);
            if(gameplayActive_ || dialogVisible_ || !pendingDialog_ ||
               std::chrono::steady_clock::now() < nextDialogAttempt_)
                return;
            message = *pendingDialog_;
            pendingDialog_.reset();
            dialogVisible_ = true;
        }

        try
        {
            auto* flow = BSML::Helpers::GetMainFlowCoordinator();
            auto prompt = flow
                ? flow->__cordl_internal_get__simpleDialogPromptViewController()
                : nullptr;
            if(!flow || !prompt)
                throw std::runtime_error("Beat Saber's main dialog is unavailable");
            if(prompt->get_isInViewControllerHierarchy() || prompt->get_isInTransition())
            {
                // Do not replace Beat Saber's own confirmation/error prompt.
                // Put this message back and retry after that dialog closes.
                std::scoped_lock lock(mutex_);
                dialogVisible_ = false;
                pendingDialog_ = std::move(message);
                nextDialogAttempt_ = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(250);
                return;
            }
            prompt->Init(
                message.first,
                message.second,
                "OK",
                custom_types::MakeDelegate<System::Action_1<int>*>(
                    std::function<void(int)>{
                        [this, flow, prompt](int)
                        {
                            flow->DismissViewController(
                                prompt,
                                HMUI::ViewController::AnimationDirection::Horizontal,
                                nullptr,
                                false);
                            std::scoped_lock lock(mutex_);
                            dialogVisible_ = false;
                            dialogFailureLogged_ = false;
                        }}));
            flow->PresentViewController(
                prompt,
                nullptr,
                HMUI::ViewController::AnimationDirection::Horizontal,
                false);
            std::scoped_lock lock(mutex_);
            dialogFailureLogged_ = false;
        }
        catch(const std::exception& exception)
        {
            std::scoped_lock lock(mutex_);
            if(!dialogFailureLogged_)
            {
                PaperLogger.error(
                    "Could not show Big Screen error dialog: {}",
                    exception.what());
                RecordError(
                    "Showing Big Screen error dialog",
                    exception.what());
                dialogFailureLogged_ = true;
            }
            dialogVisible_ = false;
            // Retain the message for the dedicated Big Screen modal or the
            // next main-flow frame instead of silently losing it.
            pendingDialog_ = std::move(message);
            nextDialogAttempt_ = std::chrono::steady_clock::now() +
                std::chrono::seconds(1);
        }
        catch(...)
        {
            std::scoped_lock lock(mutex_);
            if(!dialogFailureLogged_)
            {
                PaperLogger.error(
                    "Could not show Big Screen error dialog: unknown exception");
                RecordError(
                    "Showing Big Screen error dialog",
                    "Unknown native exception");
                dialogFailureLogged_ = true;
            }
            dialogVisible_ = false;
            pendingDialog_ = std::move(message);
            nextDialogAttempt_ = std::chrono::steady_clock::now() +
                std::chrono::seconds(1);
        }
    }

    void ErrorManager::ResetCircuitBreaker()
    {
        std::scoped_lock lock(mutex_);
        disabledByCircuitBreaker_ = false;
        disableRequested_ = false;
        menuExitRequested_ = false;
        waitingForMenuExit_ = false;
        lastInternalError_ = {};
        dialogVisible_ = false;
        dialogFailureLogged_ = false;
        nextDialogAttempt_ = {};
    }
}
