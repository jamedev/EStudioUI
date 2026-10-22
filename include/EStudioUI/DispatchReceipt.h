#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <thread>

namespace EStudioUI
{
//! Observable lifecycle of one submitted UI operation.
enum class DispatchStatus
{
    Pending,
    Running,
    Completed,
    Cancelled,
    Superseded,
    TargetExpired,
    Rejected,
    Failed
};

//! Distinguishes overload from normal endpoint shutdown or an empty target.
enum class DispatchRejectionReason
{
    None,
    Capacity,
    Closed,
    EmptyTarget
};

//! A copyable, thread-safe acknowledgement for a queued UI operation.
class DispatchReceipt
{
  public:
    //! Creates an already rejected receipt without allocating.
    DispatchReceipt() = default;

    //! Returns the current status without blocking.
    DispatchStatus GetStatus() const noexcept;

    //! Returns why a submission was rejected; other outcomes report None.
    DispatchRejectionReason GetRejectionReason() const noexcept;

    //! Returns whether the operation has reached a terminal outcome.
    bool IsReady() const noexcept;

    //! Cancels a pending operation; a running callback cannot be interrupted.
    bool Cancel() const noexcept;

    //! Returns the callback exception for a failed operation.
    std::exception_ptr GetError() const;

    //! Returns the first exception thrown by a completion observer.
    std::exception_ptr GetObserverError() const;

    //! Rethrows the callback exception when present.
    void RethrowIfFailed() const;

    //! Waits only from a non-owner thread; an already terminal receipt never blocks.
    bool WaitFor(std::chrono::milliseconds timeout) const;

    //! Registers an exception-contained observer, invoked once outside locks on the settling/registering thread.
    void OnFinished(std::function<void(DispatchStatus, std::exception_ptr)> callback) const;

  private:
    friend class UiDispatcher;

    //! Owns synchronized receipt state independently of the queue.
    struct State;

    //! Creates a pending acknowledgement for the specified owner thread.
    static DispatchReceipt Pending(std::thread::id owner);

    //! Creates a rejected acknowledgement with an explicit failure reason.
    static DispatchReceipt Rejected(DispatchRejectionReason reason);

    //! Changes Pending to Running exactly once.
    bool Start() const;

    //! Settles an unfinished operation and notifies observers outside state locks.
    bool Finish(DispatchStatus status, std::exception_ptr error = {}) const noexcept;

    //! Retains the operation acknowledgement after its queue record is retired.
    std::shared_ptr<State> m_State;
};
} // namespace EStudioUI
