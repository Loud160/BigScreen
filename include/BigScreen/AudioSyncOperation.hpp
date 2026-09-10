// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace BigScreen::AudioSync
{
enum class OperationStatus
{
    Running,
    Cancelling,
    Cancelled,
    Succeeded,
    Failed
};
enum class OperationFailure
{
    None,
    External,
    Internal
};

struct OperationProgress
{
    OperationStatus status = OperationStatus::Running;
    OperationFailure failure = OperationFailure::None;
    std::string stage;
    std::string error;
    // Null means indeterminate. It must render as an animated progress bar,
    // not a made-up percentage. Work reports its own actual units.
    std::optional<double> fraction;
    double wallSeconds = 0.0;
};

/// One non-blocking owner, one worker, one map/source generation. No queue
/// of old analyses is retained. Calls to Start/Cancel/Poll/TakeResult are
/// owner-thread operations; Context is the worker's value-only interface.
///
/// Detached here does NOT mean unmanaged lifetime: the worker owns Shared
/// until all cleanup has finished. Shared contains no Unity references or
/// references to this owner. Destroying a menu cancels without joining a
/// decoder/network call on Unity's thread. Start refuses another job until
/// that cleanup completes, bounding CPU and memory even after rapid clicks.
/// Runtime owners must remain retained across page close/reopen, and must
/// capture only native source snapshots, never raw UI/IL2CPP pointers.
template <class Result> class Operation final
{
    struct Shared
    {
        std::atomic<bool> cancelled{false};
        std::atomic<bool> finished{false};
        std::mutex mutex;
        OperationProgress progress;
        std::shared_ptr<const Result> result;
        std::uint64_t generation = 0;
        std::string sourceKey;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    };

  public:
    class Context final
    {
      public:
        bool Cancelled() const { return shared_->cancelled.load(std::memory_order_relaxed); }
        void Progress(std::string stage, std::uint64_t done = 0, std::uint64_t total = 0) const
        {
            if(Cancelled())
                return;
            std::scoped_lock lock(shared_->mutex);
            if(shared_->cancelled.load(std::memory_order_relaxed))
                return;
            shared_->progress.stage = std::move(stage);
            shared_->progress.fraction =
                total > 0
                    ? std::optional<double>{static_cast<double>(std::min(done, total)) / total}
                    : std::nullopt;
        }
        // Expected source/network/storage failures go through the normal
        // user-error path, not the internal-error circuit breaker. Thrown
        // unexpected exceptions are classified Internal at the boundary.
        void Fail(std::string detail) const
        {
            std::scoped_lock lock(shared_->mutex);
            shared_->progress.failure = OperationFailure::External;
            shared_->progress.error = std::move(detail);
        }

      private:
        friend class Operation;
        explicit Context(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {}
        std::shared_ptr<Shared> shared_;
    };

    Operation() = default;
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;
    ~Operation() { Cancel(); }

    bool Busy() const { return shared_ && !shared_->finished.load(std::memory_order_acquire); }

    /// Work returns no result on cancellation/expected failure, using
    /// Context::Fail for the latter. All captured resources must be owned
    /// by the callable. A filesystem publish, if any, still requires its
    /// own final generation/cancellation check before atomic replacement.
    bool Start(std::uint64_t generation, std::string sourceKey,
               std::function<std::optional<Result>(const Context&)> work)
    {
        if(Busy())
            return false;
        auto state = std::make_shared<Shared>();
        state->generation = generation;
        state->sourceKey = std::move(sourceKey);
        state->progress.stage = "Starting";
        // Construct the thread before replacing owner state. If thread
        // creation fails, the UI's established Guard sees that exception
        // and there is no permanently Running phantom operation.
        std::thread worker(
            [state, work = std::move(work)]() mutable
            {
                try
                {
                    const Context context(state);
                    auto value = work(context);
                    // Release the work closure (and decoder/file captures)
                    // before announcing completion. Cancellation cleanup is
                    // part of the operation, not hidden after a 100% report.
                    work = {};
                    std::shared_ptr<const Result> result;
                    if(value && !context.Cancelled())
                        result = std::make_shared<Result>(std::move(*value));
                    std::scoped_lock lock(state->mutex);
                    if(context.Cancelled())
                    {
                        state->progress.status = OperationStatus::Cancelled;
                        state->progress.failure = OperationFailure::None;
                        state->progress.error.clear();
                    }
                    else if(result && state->progress.failure == OperationFailure::None)
                    {
                        state->result = std::move(result);
                        state->progress.status = OperationStatus::Succeeded;
                        state->progress.fraction = 1.0;
                    }
                    else
                    {
                        state->progress.status = OperationStatus::Failed;
                        if(state->progress.failure == OperationFailure::None)
                        {
                            state->progress.failure = OperationFailure::Internal;
                            state->progress.error = "Audio operation ended without a result.";
                        }
                    }
                }
                catch(const std::exception& exception)
                {
                    std::scoped_lock lock(state->mutex);
                    state->progress.status = OperationStatus::Failed;
                    state->progress.failure = OperationFailure::Internal;
                    state->progress.error = exception.what();
                }
                catch(...)
                {
                    std::scoped_lock lock(state->mutex);
                    state->progress.status = OperationStatus::Failed;
                    state->progress.failure = OperationFailure::Internal;
                    state->progress.error = "Unknown exception in audio synchronization worker.";
                }
                work = {};
                {
                    std::scoped_lock lock(state->mutex);
                    // Cancel can race the tiny interval after result selection
                    // but before completion publication. Normalize under the
                    // same mutex as Cancel, then publish finished before
                    // releasing it, so a terminal job cannot stay Cancelling.
                    if(state->cancelled.load(std::memory_order_relaxed))
                    {
                        state->result.reset();
                        state->progress.status = OperationStatus::Cancelled;
                        state->progress.failure = OperationFailure::None;
                        state->progress.error.clear();
                        state->progress.fraction.reset();
                    }
                    state->progress.wallSeconds =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                      state->started)
                            .count();
                    state->finished.store(true, std::memory_order_release);
                }
            });
        worker.detach();
        shared_ = std::move(state);
        return true;
    }

    void Cancel()
    {
        if(!shared_)
            return;
        shared_->cancelled.store(true, std::memory_order_relaxed);
        std::scoped_lock lock(shared_->mutex);
        shared_->result.reset();
        shared_->progress.status = shared_->finished.load(std::memory_order_acquire)
                                       ? OperationStatus::Cancelled
                                       : OperationStatus::Cancelling;
        shared_->progress.fraction.reset();
    }

    std::optional<OperationProgress> Poll() const
    {
        if(!shared_)
            return std::nullopt;
        std::scoped_lock lock(shared_->mutex);
        auto progress = shared_->progress;
        if(!shared_->finished.load(std::memory_order_acquire))
            progress.wallSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - shared_->started)
                    .count();
        return progress;
    }

    std::shared_ptr<const Result> TakeResult(std::uint64_t generation, const std::string& sourceKey)
    {
        if(!shared_ || !shared_->finished.load(std::memory_order_acquire) ||
           shared_->cancelled.load(std::memory_order_relaxed) ||
           shared_->generation != generation || shared_->sourceKey != sourceKey)
            return {};
        std::scoped_lock lock(shared_->mutex);
        // Move a pointer, not a potentially large feature index, across
        // the main-thread boundary. The same result cannot publish twice.
        return std::exchange(shared_->result, {});
    }

  private:
    std::shared_ptr<Shared> shared_;
};
} // namespace BigScreen::AudioSync
