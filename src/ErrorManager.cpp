#include "BigScreen/ErrorManager.hpp"

#include <stdexcept>
#include <functional>

#include "BigScreen/CoreLogic.hpp"
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

    void ErrorManager::ReportUserVisible(
        const std::string& title,
        const std::string& detail)
    {
        PaperLogger.error("{}: {}", title, detail);
        std::scoped_lock lock(mutex_);
        // Keep only the newest message. A single modal is useful; a backlog of
        // stale popups can prevent the player from reaching the disable switch.
        pendingDialog_ = std::make_pair(title, detail);
    }

    void ErrorManager::ReportInternal(
        const std::string& context,
        const std::string& detail)
    {
        PaperLogger.error("Internal failure in {}: {}", context, detail);
        const auto now = std::chrono::steady_clock::now();
        const auto signature = context + ": " + detail;
        bool disable = false;
        {
            std::scoped_lock lock(mutex_);
            const bool repeated =
                lastInternalError_ != std::chrono::steady_clock::time_point{} &&
                CoreLogic::IsRepeatedWithin(
                    lastSignature_, signature, now - lastInternalError_);
            lastSignature_ = signature;
            lastInternalError_ = now;
            if(repeated && !disabledByCircuitBreaker_)
            {
                disabledByCircuitBreaker_ = true;
                disable = true;
                pendingDialog_ = std::make_pair(
                    "Big Screen disabled itself",
                    "Big Screen encountered the same internal error twice within three minutes, so it turned itself off to protect Beat Saber.\n\nLast error: " +
                    signature + "\n\nLogs: " + LogFolder +
                    "\n\nYou can turn the mod back on from its General tab after reviewing the log.");
            }
            else if(!gameplayActive_)
            {
                pendingDialog_ = std::make_pair(
                    "Big Screen error",
                    "Big Screen could not complete an internal operation.\n\n" +
                    signature + "\n\nThe error was recorded in " + LogFolder + ".");
            }
            // During gameplay the first failure remains log-only. If it later
            // repeats, the one circuit-breaker dialog waits until gameplay ends.
        }
        if(disable)
            Settings::Instance().SetModEnabled(false);
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
        std::pair<std::string, std::string> message;
        {
            std::scoped_lock lock(mutex_);
            if(gameplayActive_ || dialogVisible_ || !pendingDialog_)
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
                        }}));
            flow->PresentViewController(
                prompt,
                nullptr,
                HMUI::ViewController::AnimationDirection::Horizontal,
                false);
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error("Could not show Big Screen error dialog: {}", exception.what());
            std::scoped_lock lock(mutex_);
            dialogVisible_ = false;
            // Retain the message for the dedicated Big Screen modal or the
            // next main-flow frame instead of silently losing it.
            pendingDialog_ = std::move(message);
        }
    }
}
