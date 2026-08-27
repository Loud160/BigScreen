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
#include "HMUI/ViewController.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "custom-types/shared/delegate.hpp"
#include "System/Action_1.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
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

        struct ActiveDialogTarget {
            SafePtrUnity<HMUI::FlowCoordinator> host;
            SafePtrUnity<GlobalNamespace::SimpleDialogPromptViewController> prompt;

            ActiveDialogTarget(
                HMUI::FlowCoordinator* hostObject,
                GlobalNamespace::SimpleDialogPromptViewController* promptObject)
                : host(hostObject), prompt(promptObject)
            {}
        };

        struct ActiveDialogLifetime {
            // A dialog can span several frames and a complete flow transition.
            // Raw IL2CPP pointers are not roots, so retain the presenting flow
            // and shared prompt until dismissal is observed in the hierarchy.
            std::optional<SafePtrUnity<HMUI::FlowCoordinator>> host;
            std::optional<SafePtrUnity<
                GlobalNamespace::SimpleDialogPromptViewController>> prompt;

            void Retain(
                HMUI::FlowCoordinator* hostObject,
                GlobalNamespace::SimpleDialogPromptViewController* promptObject)
            {
                host.emplace(hostObject);
                prompt.emplace(promptObject);
            }

            HMUI::FlowCoordinator* Host() const
            {
                return host && static_cast<bool>(*host)
                    ? host->ptr()
                    : nullptr;
            }

            GlobalNamespace::SimpleDialogPromptViewController* Prompt() const
            {
                return prompt && static_cast<bool>(*prompt)
                    ? prompt->ptr()
                    : nullptr;
            }

            void Clear()
            {
                prompt.reset();
                host.reset();
            }
        };

        ActiveDialogLifetime& TrackedDialogLifetime()
        {
            static ActiveDialogLifetime lifetime;
            return lifetime;
        }

        GlobalNamespace::SimpleDialogPromptViewController*
        FindSharedDialogPrompt()
        {
            auto* main = BSML::Helpers::GetMainFlowCoordinator();
            if(!UnityW<GlobalNamespace::MainFlowCoordinator>::isAlive(main))
                return nullptr;
            auto promptValue =
                main->__cordl_internal_get__simpleDialogPromptViewController();
            auto* prompt = promptValue ? promptValue.unsafePtr() : nullptr;
            return UnityW<GlobalNamespace::SimpleDialogPromptViewController>::isAlive(
                       prompt)
                ? prompt
                : nullptr;
        }

        std::optional<ActiveDialogTarget> ResolveActiveDialogTarget()
        {
            auto* main = BSML::Helpers::GetMainFlowCoordinator();
            if(!UnityW<GlobalNamespace::MainFlowCoordinator>::isAlive(main) ||
               !main->get_isActivated() || main->get_isInTransition())
                return std::nullopt;

            auto youngest = main->YoungestChildFlowCoordinatorOrSelf();
            auto* host = youngest ? youngest.unsafePtr() : nullptr;
            if(!UnityW<HMUI::FlowCoordinator>::isAlive(host) ||
               !host->get_isActivated() || host->get_isInTransition())
                return std::nullopt;

            auto topView = host->get_topViewController();
            auto* top = topView ? topView.unsafePtr() : nullptr;
            if(!UnityW<HMUI::ViewController>::isAlive(top) ||
               !top->get_isActivated() || top->get_isInTransition() ||
               !top->get_isInViewControllerHierarchy())
                return std::nullopt;
            auto topObject = top->get_gameObject();
            if(!topObject || !topObject->get_activeInHierarchy())
                return std::nullopt;

            auto* prompt = FindSharedDialogPrompt();
            if(!prompt)
                return std::nullopt;
            return ActiveDialogTarget(host, prompt);
        }

        bool DialogPromptVisible(
            GlobalNamespace::SimpleDialogPromptViewController* prompt)
        {
            return UnityW<GlobalNamespace::SimpleDialogPromptViewController>::isAlive(
                       prompt) &&
                   (prompt->get_isInViewControllerHierarchy() ||
                    prompt->get_isInTransition());
        }

        void BringDialogPromptToFront(
            GlobalNamespace::SimpleDialogPromptViewController* prompt)
        {
            if(!UnityW<GlobalNamespace::SimpleDialogPromptViewController>::isAlive(
                   prompt))
                return;
            auto transform = prompt->get_transform();
            if(UnityW<UnityEngine::Transform>::isAlive(transform))
                transform->SetAsLastSibling();
        }

        bool TryDismissDialogPrompt(
            HMUI::FlowCoordinator* host,
            GlobalNamespace::SimpleDialogPromptViewController* prompt) noexcept
        {
            try
            {
                if(!DialogPromptVisible(prompt))
                    return true;

                if(UnityW<HMUI::FlowCoordinator>::isAlive(host))
                {
                    host->DismissViewController(
                        prompt,
                        HMUI::ViewController::AnimationDirection::Horizontal,
                        nullptr,
                        true);
                }
                else
                {
                    // The presenting flow can be destroyed before the shared
                    // prompt. Dismiss the controller directly as a last-resort
                    // cleanup so its input blocker cannot outlive its owner.
                    prompt->__DismissViewController(
                        nullptr,
                        HMUI::ViewController::AnimationDirection::Horizontal,
                        true);
                }
                return !DialogPromptVisible(prompt);
            }
            catch(...)
            {
                // Keep the rooted prompt tracked. TickMainThread retries after
                // the transition rather than forgetting a live input blocker.
                return false;
            }
        }

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
                    BigScreen::BigScreenLogger.error(
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
            // The general native logger still mirrors to logcat if Big
            // Screen's external-storage files are unavailable. Never let
            // diagnostics prevent the mod from starting.
            BigScreen::BigScreenLogger.error(
                "Could not initialize persistent Big Screen error history: {}",
                exception.what());
        }
        catch(...)
        {
            BigScreen::BigScreenLogger.error(
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
            BigScreen::BigScreenLogger.error("{}: {}", title, detail);
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
            BigScreen::BigScreenLogger.error("Internal failure in {}: {}", context, detail);
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

    void ErrorManager::RecordDialogTickFailure(
        const std::string& detail) noexcept
    {
        bool shouldLog = false;
        try
        {
            std::scoped_lock lock(mutex_);
            shouldLog = !dialogFailureLogged_;
            dialogFailureLogged_ = true;
            nextDialogAttempt_ = std::chrono::steady_clock::now() +
                std::chrono::seconds(1);
        }
        catch(...)
        {
            // The persistent record below is still safe and noexcept.
            shouldLog = true;
        }

        if(shouldLog)
        {
            try
            {
                BigScreen::BigScreenLogger.error(
                    "Could not update Big Screen's error dialog: {}",
                    detail);
            }
            catch(...)
            {
                // Paper is diagnostic-only at this failure boundary.
            }
            RecordError("Updating Big Screen error dialog", detail);
        }
    }

    void ErrorManager::ReleaseTrackedDialog(
        std::uint64_t generation,
        bool requeue) noexcept
    {
        bool released = false;
        try
        {
            std::scoped_lock lock(mutex_);
            if(!dialogVisible_ || dialogGeneration_ != generation)
                return;

            // A newer pending message wins. Otherwise move the interrupted
            // active message back without allocating another string copy.
            if(requeue && activeDialog_ && !pendingDialog_)
                pendingDialog_ = std::move(activeDialog_);
            activeDialog_.reset();
            dialogVisible_ = false;
            dialogAcknowledged_ = false;
            ++dialogGeneration_;
            dialogFailureLogged_ = false;
            nextDialogAttempt_ = requeue
                ? std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(250)
                : std::chrono::steady_clock::time_point{};
            released = true;
        }
        catch(...)
        {
            RecordError(
                "Releasing Big Screen error dialog",
                "Could not safely update the tracked dialog state");
            return;
        }

        if(released)
        {
            try
            {
                TrackedDialogLifetime().Clear();
            }
            catch(...)
            {
                // State is already released. A stale safe handle is preferable
                // to allowing cleanup code to escape into Beat Saber's hook.
                RecordError(
                    "Releasing Big Screen error dialog",
                    "Could not release a retained Unity dialog handle");
            }
        }
    }

    void ErrorManager::AcknowledgeDialog(
        std::uint64_t generation) noexcept
    {
        try
        {
            {
                std::scoped_lock lock(mutex_);
                if(!dialogVisible_ || dialogGeneration_ != generation)
                    return;
                // Do not forget the prompt until Unity confirms that it left
                // the hierarchy. If immediate dismissal races a transition,
                // TickMainThread will retry without re-showing the message.
                dialogAcknowledged_ = true;
            }

            auto& lifetime = TrackedDialogLifetime();
            auto* host = lifetime.Host();
            auto* prompt = lifetime.Prompt();
            if(TryDismissDialogPrompt(host, prompt))
            {
                ReleaseTrackedDialog(generation, false);
                return;
            }
            BringDialogPromptToFront(prompt);
        }
        catch(const std::exception& exception)
        {
            RecordDialogTickFailure(exception.what());
        }
        catch(...)
        {
            RecordDialogTickFailure(
                "Unknown native exception while acknowledging the dialog");
        }
    }

    void ErrorManager::TickMainThread() noexcept
    {
        try
        {
            TickMainThreadImpl();
        }
        catch(const std::exception& exception)
        {
            // This method is called directly from a Beat Saber Update hook.
            // No Cordl/IL2CPP transition exception may cross that boundary.
            RecordDialogTickFailure(exception.what());
        }
        catch(...)
        {
            RecordDialogTickFailure(
                "Unknown native exception while resolving the active UI flow");
        }
    }

    void ErrorManager::TickMainThreadImpl()
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

        // The stock prompt is shared by MainFlowCoordinator, but it must be
        // PRESENTED by the youngest active flow. Presenting it through MainFlow
        // while Solo, Campaign, or another child is visible puts the picture
        // behind that child while the prompt's blocker still consumes input.
        // That exact mismatch looks like a frozen game. A transition is not a
        // valid host: keep the message queued until both the flow and its top
        // controller are active, in-hierarchy, and stable.
        auto target = ResolveActiveDialogTarget();

        bool validateVisibleDialog = false;
        bool dialogAcknowledged = false;
        std::uint64_t previousGeneration = 0;
        {
            std::scoped_lock lock(mutex_);
            validateVisibleDialog = dialogVisible_;
            dialogAcknowledged = dialogAcknowledged_;
            previousGeneration = dialogGeneration_;
        }
        if(validateVisibleDialog)
        {
            auto& lifetime = TrackedDialogLifetime();
            auto* previousHost = lifetime.Host();
            auto* prompt = lifetime.Prompt();
            const bool promptVisible = DialogPromptVisible(prompt);
            const bool correctFrontHost = target &&
                target->host.ptr() == previousHost &&
                target->prompt.ptr() == prompt &&
                !IsBigScreenMenuActive();
            if(!promptVisible)
            {
                // Beat Saber can remove its shared prompt during a scene
                // transition without invoking the button callback.
                ReleaseTrackedDialog(
                    previousGeneration,
                    !dialogAcknowledged);
                target.reset();
            }
            else if(dialogAcknowledged || !correctFrontHost)
            {
                // Dismiss from the old owner before re-presenting. Crucially,
                // retain ownership if Unity is still transitioning or throws:
                // clearing now would orphan the prompt's input blocker.
                if(TryDismissDialogPrompt(previousHost, prompt))
                {
                    ReleaseTrackedDialog(
                        previousGeneration,
                        !dialogAcknowledged);
                    target.reset();
                }
                else
                {
                    BringDialogPromptToFront(prompt);
                    return;
                }
            }
            else
            {
                // Other UI can append canvases while the dialog is open.
                // Reassert the shared prompt's sibling order so its visible
                // surface and raycast blocker remain together at the front.
                BringDialogPromptToFront(prompt);
                return;
            }
        }

        // Big Screen's settings controller owns a dedicated same-panel modal
        // and ShowModalInFront keeps it above that panel. Leave a queued error
        // for SettingsMenu rather than placing the shared stock prompt over a
        // different controller inside the three-screen mod flow.
        if(IsBigScreenMenuActive())
            return;

        if(!target)
        {
            auto resolved = ResolveActiveDialogTarget();
            if(resolved)
                target.emplace(std::move(*resolved));
        }
        if(!target || DialogPromptVisible(target->prompt.ptr()))
            return;

        std::pair<std::string, std::string> message;
        std::uint64_t generation = 0;
        {
            std::scoped_lock lock(mutex_);
            if(gameplayActive_ || dialogVisible_ || !pendingDialog_ ||
               std::chrono::steady_clock::now() < nextDialogAttempt_)
                return;

            // Root the Unity objects before the pending message changes state.
            // If creating either root fails, the outer failure boundary leaves
            // the pending message untouched for a later safe retry.
            message = *pendingDialog_;
            TrackedDialogLifetime().Retain(
                target->host.ptr(),
                target->prompt.ptr());
            pendingDialog_.reset();
            activeDialog_ = message;
            dialogVisible_ = true;
            dialogAcknowledged_ = false;
            generation = ++dialogGeneration_;
        }

        try
        {
            auto& lifetime = TrackedDialogLifetime();
            auto* host = lifetime.Host();
            auto* prompt = lifetime.Prompt();
            if(!UnityW<HMUI::FlowCoordinator>::isAlive(host) ||
               !UnityW<GlobalNamespace::SimpleDialogPromptViewController>::isAlive(
                   prompt))
                throw std::runtime_error(
                    "The active Beat Saber dialog host became unavailable");
            prompt->Init(
                message.first,
                message.second,
                "OK",
                custom_types::MakeDelegate<System::Action_1<int>*>(
                    std::function<void(int)>{
                        [this, generation](int)
                        {
                            AcknowledgeDialog(generation);
                        }}));
            host->PresentViewController(
                prompt,
                nullptr,
                HMUI::ViewController::AnimationDirection::Horizontal,
                true);
            BringDialogPromptToFront(prompt);
            std::scoped_lock lock(mutex_);
            dialogFailureLogged_ = false;
        }
        catch(const std::exception& exception)
        {
            RecordDialogTickFailure(exception.what());
            bool confirmedHidden = false;
            try
            {
                confirmedHidden = !DialogPromptVisible(
                    TrackedDialogLifetime().Prompt());
            }
            catch(...)
            {
                // Preserve ownership when liveness itself cannot be proven.
            }
            if(confirmedHidden)
                ReleaseTrackedDialog(generation, true);
        }
        catch(...)
        {
            RecordDialogTickFailure(
                "Unknown native exception while presenting the dialog");
            bool confirmedHidden = false;
            try
            {
                confirmedHidden = !DialogPromptVisible(
                    TrackedDialogLifetime().Prompt());
            }
            catch(...)
            {
                // Preserve ownership when liveness itself cannot be proven.
            }
            if(confirmedHidden)
                ReleaseTrackedDialog(generation, true);
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
        // Do not clear an active prompt here. Reset may be requested while a
        // shared Beat Saber dialog is still in the hierarchy; forgetting its
        // owner would leave an untracked input blocker. TickMainThread owns
        // prompt liveness and safely requeues it if the flow changes.
        dialogFailureLogged_ = false;
        nextDialogAttempt_ = {};
    }
}
