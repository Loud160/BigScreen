// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace BigScreen {
    /// The menu preview has exactly one transport owner at a time. Keeping the
    /// wait/play/pause modes mutually exclusive prevents a late audio or video
    /// callback from reviving a transport that a newer user action stopped.
    enum class PreviewTransportState {
        Stopped,
        WaitingForAudio,
        WaitingForVideo,
        Playing,
        Paused
    };

    class PreviewTransport final {
    public:
        struct Clock {
            double songTime = 0.0;
            double realtime = 0.0;
        };

        [[nodiscard]] PreviewTransportState State() const noexcept
        {
            return state_;
        }

        [[nodiscard]] bool Is(PreviewTransportState expected) const noexcept
        {
            return state_ == expected;
        }

        [[nodiscard]] bool IsWaiting() const noexcept
        {
            return Is(PreviewTransportState::WaitingForAudio) ||
                   Is(PreviewTransportState::WaitingForVideo);
        }

        [[nodiscard]] bool WantsPlayback() const noexcept
        {
            return IsWaiting() || Is(PreviewTransportState::Playing);
        }

        void Set(PreviewTransportState state) noexcept
        {
            state_ = state;
        }

        void Stop() noexcept
        {
            state_ = PreviewTransportState::Stopped;
            preRollDeadline_.reset();
            clock_.reset();
        }

        void ArmPreRoll(double readyRealtime) noexcept
        {
            if(!preRollDeadline_)
                preRollDeadline_ = readyRealtime;
        }

        void ClearPreRoll() noexcept
        {
            preRollDeadline_.reset();
        }

        [[nodiscard]] bool PreRollComplete(double nowRealtime) const noexcept
        {
            return !preRollDeadline_ || nowRealtime >= *preRollDeadline_;
        }

        void ResetClock(double songTime, double realtime) noexcept
        {
            clock_ = Clock{songTime, realtime};
        }

        void ClearClock() noexcept
        {
            clock_.reset();
        }

        [[nodiscard]] const std::optional<Clock>& CurrentClock() const noexcept
        {
            return clock_;
        }

        void UpdateClock(double songTime, double realtime) noexcept
        {
            clock_ = Clock{songTime, realtime};
        }

    private:
        PreviewTransportState state_ = PreviewTransportState::Stopped;
        std::optional<double> preRollDeadline_;
        std::optional<Clock> clock_;
    };

    /// Owns the one terminal progress bar that may survive a downloader
    /// operation. Callers can clear or transfer ownership, but cannot mutate a
    /// free-floating level-id string that later leaks into another map.
    class TerminalDownloadProgressOwner final {
    public:
        void RetainFor(std::string levelId)
        {
            levelId_ = std::move(levelId);
        }

        void Reset() noexcept
        {
            levelId_.clear();
        }

        [[nodiscard]] bool IsRetainedFor(
            std::string_view levelId) const noexcept
        {
            return !levelId.empty() && levelId_ == levelId;
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return levelId_.empty();
        }

    private:
        std::string levelId_;
    };
}
