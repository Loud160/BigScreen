#include "BigScreen/ErrorManager.hpp"

#include <stdexcept>
#include <functional>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "BigScreen/CoreLogic.hpp"
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

    void ErrorManager::RecordError(
        const std::string& context,
        const std::string& detail) noexcept
    {
        try
        {
            std::scoped_lock lock(persistentLogMutex_);
            const std::filesystem::path logPath(PersistentErrorLog);
            std::filesystem::create_directories(logPath.parent_path());
            std::ofstream output(logPath, std::ios::app);
            if(!output)
                return;

            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            const std::tm* local = std::localtime(&time);
            if(local)
                output << '[' << std::put_time(local, "%Y-%m-%d %H:%M:%S") << "] ";
            output << context << ": " << detail << '\n';
            output.flush();
        }
        catch(...)
        {
            // Diagnostics are fail-open. Reporting a logging failure through
            // this class would recurse and could turn a storage issue into a
            // circuit-breaker event.
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
            const std::tm* local = std::localtime(&time);
            output << "\n[";
            if(local)
                output << std::put_time(local, "%Y-%m-%d %H:%M:%S");
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
        const std::string& detail)
    {
        PaperLogger.error("{}: {}", title, detail);
        RecordError(title, detail);
        std::scoped_lock lock(mutex_);
        // Keep only the newest message. A single modal is useful; a backlog of
        // stale popups can prevent the player from reaching the disable switch.
        pendingDialog_ = std::make_pair(title, detail);
        nextDialogAttempt_ = {};
        dialogFailureLogged_ = false;
    }

    void ErrorManager::ReportInternal(
        const std::string& context,
        const std::string& detail)
    {
        PaperLogger.error("Internal failure in {}: {}", context, detail);
        RecordError("Internal failure in " + context, detail);
        const auto now = std::chrono::steady_clock::now();
        const auto signature = context + ": " + detail;
        {
            std::scoped_lock lock(mutex_);
            const bool secondFailure =
                lastInternalError_ != std::chrono::steady_clock::time_point{} &&
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
                    signature + "\n\nThe error was recorded in " + PersistentErrorLog + ".");
            }
            if(!gameplayActive_)
                menuExitRequested_ = true;
            // During gameplay the first failure remains log-only. If a second
            // failure follows soon after, one circuit-breaker dialog waits
            // until gameplay ends.
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
            Settings::Instance().SetModEnabled(false);
            // The circuit breaker must perform the same immediate teardown as
            // the menu's master switch. Merely persisting false would leave a
            // decoder, preview, or undocked editor raycaster alive because the
            // Update hook stops dispatching mod work as soon as it sees false.
            SelectionVideoToggle::Instance().ModEnabledChanged(false);
            ScreenPreview::Instance().SetEnabled(false);
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
        dialogFailureLogged_ = false;
        nextDialogAttempt_ = {};
    }
}
