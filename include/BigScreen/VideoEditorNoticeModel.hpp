// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace BigScreen {
    /// Pure lifecycle model for the Video Library editor's one-line operation
    /// notice. Each opening of an editor receives a unique token, even when the
    /// same map is reopened. Callers must present that token and the exact map
    /// ID for every change, preventing delayed work from a preceding editor
    /// visit from publishing into the current one.
    ///
    /// This class deliberately has no Unity, downloader, or TextMeshPro
    /// dependency. It decides whether an event belongs to the active editor;
    /// the menu remains responsible for rendering Current() on Unity's main
    /// thread.
    class VideoEditorNoticeModel final {
    public:
        struct VisitToken {
            std::uint64_t value = 0;

            bool operator==(const VisitToken&) const = default;
            explicit operator bool() const noexcept { return value != 0; }
        };

        struct RevisionToken {
            VisitToken visit;
            std::uint64_t value = 0;

            bool operator==(const RevisionToken&) const = default;
            explicit operator bool() const noexcept {
                return static_cast<bool>(visit) && value != 0;
            }
        };

        struct TransferToken {
            VisitToken visit;
            std::uint64_t value = 0;

            bool operator==(const TransferToken&) const = default;
            explicit operator bool() const noexcept {
                return static_cast<bool>(visit) && value != 0;
            }
        };

        /// Opens a new editor visit and always starts it with a blank notice.
        /// Opening the same level twice still returns two different tokens.
        [[nodiscard]] VisitToken Enter(std::string levelId)
        {
            Reset();
            currentVisit_ = VisitToken{++nextVisitValue_};
            currentLevelId_ = std::move(levelId);
            return currentVisit_;
        }

        /// Unconditionally retires the current visit and every string/token it
        /// owns without rewinding the monotonically increasing token counters.
        /// The menu uses this at each map-exit boundary so even inconsistent
        /// controller state cannot preserve an operation from the old map.
        void Reset() noexcept
        {
            currentVisit_ = {};
            currentLevelId_.clear();
            notice_.clear();
            currentRevision_ = 0;
            activeTransfer_.reset();
            activeTransferRevision_ = 0;
        }

        /// Closes the visit only when the supplied token still identifies the
        /// active editor. A delayed close from an older controller is ignored.
        bool Leave(VisitToken visit)
        {
            if(visit != currentVisit_)
                return false;

            Reset();
            return true;
        }

        [[nodiscard]] bool IsCurrent(
            VisitToken visit,
            std::string_view levelId) const noexcept
        {
            return static_cast<bool>(visit) && visit == currentVisit_ &&
                !levelId.empty() && levelId == currentLevelId_;
        }

        /// Publishes an ordinary editor event and returns its revision. An
        /// ordinary event supersedes any transfer notice that was still
        /// awaiting its next UI poll. This is what prevents a delayed terminal
        /// downloader snapshot from replacing a newer toggle/slider result.
        /// Preview preparation retains this revision and calls
        /// ClearIfCurrent() later; if another action has replaced the text in
        /// the meantime, the old preview completion cannot erase the newer
        /// notice.
        [[nodiscard]] std::optional<RevisionToken> Publish(
            VisitToken visit,
            std::string_view levelId,
            std::string message)
        {
            if(!IsCurrent(visit, levelId))
                return std::nullopt;

            activeTransfer_.reset();
            activeTransferRevision_ = 0;
            notice_ = std::move(message);
            currentRevision_ = ++nextRevisionValue_;
            return RevisionToken{visit, currentRevision_};
        }

        /// Clears the active notice only when it is still the exact revision
        /// produced by the caller. This supplies ordering without assigning
        /// categories, priorities, or owners to status messages.
        bool ClearIfCurrent(RevisionToken revision)
        {
            if(!revision || revision.visit != currentVisit_ ||
               revision.value != currentRevision_)
                return false;

            notice_.clear();
            currentRevision_ = ++nextRevisionValue_;
            return true;
        }

        /// Unconditionally clears the notice for the exact active visit/map.
        bool Clear(VisitToken visit, std::string_view levelId)
        {
            if(!IsCurrent(visit, levelId))
                return false;

            notice_.clear();
            currentRevision_ = ++nextRevisionValue_;
            return true;
        }

        /// Begins the sole transfer tracked by this editor visit. The token is
        /// intentionally independent from a map ID alone: a retained terminal
        /// snapshot for an older probe or download cannot impersonate a newly
        /// started transfer for the same map.
        [[nodiscard]] std::optional<TransferToken> BeginTransfer(
            VisitToken visit,
            std::string_view levelId)
        {
            if(!IsCurrent(visit, levelId) || activeTransfer_)
                return std::nullopt;

            const TransferToken token{visit, ++nextTransferValue_};
            activeTransfer_ = token;
            activeTransferRevision_ = 0;
            return token;
        }

        /// Publishes progress only for the exact transfer currently attached
        /// to the active visit. Foreign, superseded, and post-terminal events
        /// are ignored.
        [[nodiscard]] std::optional<RevisionToken> PublishTransfer(
            TransferToken transfer,
            std::string message)
        {
            if(!TransferIsCurrent(transfer))
                return std::nullopt;

            notice_ = std::move(message);
            currentRevision_ = ++nextRevisionValue_;
            activeTransferRevision_ = currentRevision_;
            return RevisionToken{currentVisit_, currentRevision_};
        }

        /// Publishes one terminal result and retires the transfer before any
        /// later refresh can repeat it. The retained downloader snapshot may
        /// still exist, but this token can no longer write through the model.
        [[nodiscard]] std::optional<RevisionToken> FinishTransfer(
            TransferToken transfer,
            std::string message)
        {
            if(!TransferIsCurrent(transfer))
                return std::nullopt;

            notice_ = std::move(message);
            currentRevision_ = ++nextRevisionValue_;
            activeTransfer_.reset();
            activeTransferRevision_ = 0;
            return RevisionToken{currentVisit_, currentRevision_};
        }

        /// Stops accepting events for a transfer without altering whichever
        /// notice is currently visible. This is useful when the menu leaves a
        /// downloader running after its editor visit is closed.
        bool ForgetTransfer(TransferToken transfer)
        {
            if(!activeTransfer_ || *activeTransfer_ != transfer)
                return false;
            activeTransfer_.reset();
            activeTransferRevision_ = 0;
            return true;
        }

        /// Abandons a transfer whose downloader mailbox no longer belongs to
        /// this editor. Its text is cleared only when that transfer still owns
        /// the visible revision, so a newer ordinary event is never erased.
        bool AbandonTransfer(TransferToken transfer)
        {
            if(!TransferIsCurrent(transfer))
                return false;

            const bool ownsCurrentNotice = activeTransferRevision_ != 0 &&
                activeTransferRevision_ == currentRevision_;
            activeTransfer_.reset();
            activeTransferRevision_ = 0;
            if(ownsCurrentNotice)
            {
                notice_.clear();
                currentRevision_ = ++nextRevisionValue_;
            }
            return ownsCurrentNotice;
        }

        [[nodiscard]] bool TransferIsCurrent(
            TransferToken transfer) const noexcept
        {
            return static_cast<bool>(transfer) && activeTransfer_ &&
                *activeTransfer_ == transfer &&
                transfer.visit == currentVisit_;
        }

        [[nodiscard]] std::string_view Current(
            VisitToken visit,
            std::string_view levelId) const noexcept
        {
            return IsCurrent(visit, levelId)
                ? std::string_view(notice_)
                : std::string_view{};
        }

    private:
        VisitToken currentVisit_;
        std::string currentLevelId_;
        std::string notice_;
        std::optional<TransferToken> activeTransfer_;
        std::uint64_t activeTransferRevision_ = 0;
        std::uint64_t currentRevision_ = 0;
        std::uint64_t nextVisitValue_ = 0;
        std::uint64_t nextRevisionValue_ = 0;
        std::uint64_t nextTransferValue_ = 0;
    };
}
