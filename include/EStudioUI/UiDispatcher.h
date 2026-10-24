#pragma once

#include "DispatchReceipt.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>

namespace EStudioUI
{
class UiContext;
template <typename T> class UiHandle;

//! A synchronized snapshot of queue occupancy and cumulative delivery work.
struct DispatchStats
{
    //! Number of accepted operations that have not reached a terminal outcome.
    std::size_t m_Pending = 0;

    //! Maximum number of queue records retained at once.
    std::size_t m_HighWaterMark = 0;

    //! Number of accepted submissions, including superseded publications.
    std::uint64_t m_Accepted = 0;

    //! Number of operations that began executing.
    std::uint64_t m_Started = 0;

    //! Number of callbacks delivered successfully.
    std::uint64_t m_Completed = 0;

    //! Number of pending operations cancelled explicitly or during closure.
    std::uint64_t m_Cancelled = 0;

    //! Number of replaceable publications superseded before execution.
    std::uint64_t m_Superseded = 0;

    //! Number of operations whose owner-side target resolver found no live target.
    std::uint64_t m_TargetExpired = 0;

    //! Number of submissions rejected because the queue was closed or full.
    std::uint64_t m_Rejected = 0;

    //! Number of callbacks that threw an exception.
    std::uint64_t m_Failed = 0;

    //! Number of platform wake callbacks that threw an exception.
    std::uint64_t m_WakeupFailures = 0;

    //! Maximum accepted enqueue-to-start delay observed by the owner.
    std::chrono::nanoseconds m_MaxLatency{};
};

//! A copyable producer endpoint for one bounded owner-thread UI queue.
class UiDispatcher
{
  public:
    //! Creates a queue owned by the current thread, rejecting zero capacity.
    explicit UiDispatcher(std::size_t capacity = 4096);

    //! Copies a producer endpoint without transferring queue ownership.
    UiDispatcher(const UiDispatcher&) = default;

    //! Replaces this producer endpoint with another shared queue endpoint.
    UiDispatcher& operator=(const UiDispatcher&) = default;

    //! Allocates a process-unique nonzero identity without exposing target addresses.
    static std::uint64_t AllocateTargetKey();

    //! Submits an owning callback; delivery is always queued, including on the owner.
    DispatchReceipt Post(std::function<void()> callback) const;

    //! Accepts move-only owning callbacks through a copyable queue record.
    template <typename Callback>
        requires(!std::is_same_v<std::decay_t<Callback>, std::function<void()>>)
    DispatchReceipt Post(Callback&& callback) const
    {
        static_assert(std::is_invocable_v<std::decay_t<Callback>&>, "A UI callback must be invocable without arguments.");
        auto work = std::make_shared<std::decay_t<Callback>>(std::forward<Callback>(callback));
        return Post(std::function<void()>([work = std::move(work)] { std::invoke(*work); }));
    }

    //! Replaces an unstarted publication with the same nonempty dispatcher-local key.
    DispatchReceipt PostLatest(std::string key, std::function<void()> callback) const;

    //! Accepts a move-only replaceable publication with owning captures.
    template <typename Callback>
        requires(!std::is_same_v<std::decay_t<Callback>, std::function<void()>>)
    DispatchReceipt PostLatest(std::string key, Callback&& callback) const
    {
        static_assert(std::is_invocable_v<std::decay_t<Callback>&>, "A UI callback must be invocable without arguments.");
        auto work = std::make_shared<std::decay_t<Callback>>(std::forward<Callback>(callback));
        return PostLatest(std::move(key), std::function<void()>([work = std::move(work)] { std::invoke(*work); }));
    }

    //! Runs only the entry snapshot within count/time budgets; nested draining is rejected.
    std::size_t Drain(std::size_t maxCount = 256,
                      std::chrono::steady_clock::duration maxDuration = std::chrono::milliseconds(2));

    //! Wakes an open owner loop without allocating queue work; safe for reserved completion slots.
    void Wake() const noexcept;

    //! Returns whether accepted unfinished work remains.
    bool HasPending() const noexcept;

    //! Returns a coherent queue/counter snapshot usable from any thread.
    DispatchStats GetStats() const;

    //! Returns whether submissions are still accepted.
    bool IsOpen() const noexcept;

    //! Returns whether submissions have been closed.
    bool IsClosed() const noexcept;

    //! Returns a lifetime token requested when this UI endpoint closes.
    std::stop_token GetStopToken() const noexcept;

    //! Returns whether the caller is the queue owner.
    bool IsOwnerThread() const noexcept;

    //! Rejects unsupported owner-thread operations with a diagnostic exception.
    void VerifyAccess() const;

    //! Installs a nonreentrant platform wake callback; owner-only, quiesced by Close.
    void SetWakeup(std::function<void()> callback);

    //! Installs an owner-side phase precondition for Drain; closure clears the guard.
    void SetDrainGuard(std::function<void()> callback);

    //! Returns the most recent platform wake exception without losing queued work.
    std::exception_ptr GetWakeupError() const;

    //! Rejects new submissions, cancels pending work, and waits for native wake callbacks to finish.
    void Close();

  private:
    friend class UiContext;
    template <typename T> friend class UiHandle;

    //! Owns producer synchronization and owner-side delivery bookkeeping.
    struct State;

    //! Separates target-scoped publication keys from dispatcher-wide publications.
    using PublicationKey = std::pair<std::uint64_t, std::string>;

    //! Submits an internal callback with a terminal result and an optional publication identity.
    DispatchReceipt PostWork(std::function<DispatchStatus()> callback,
                             std::optional<PublicationKey> key = {}) const;

    //! Associates an enclosing owner retained only while delivering queued work.
    void SetOwnerLifetime(std::weak_ptr<void> owner);

    //! Retains the queue while copied producer endpoints exist; the owner must Close before teardown.
    std::shared_ptr<State> m_State;
};
} // namespace EStudioUI
