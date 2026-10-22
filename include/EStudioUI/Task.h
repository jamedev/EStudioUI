#pragma once

#include "DispatchReceipt.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stop_token>

namespace EStudioUI
{
class BackgroundTasks;
class RequestScope;

//! Observable computation and UI-delivery lifecycle of one background operation.
enum class TaskStatus
{
    Queued,
    Running,
    AwaitingUi,
    Delivering,
    Completed,
    Cancelled,
    Superseded,
    TargetExpired,
    Rejected,
    Failed
};

//! Identifies the current request of a scope; the empty token imposes no revision check.
class RequestToken
{
  public:
    //! Creates an unrestricted token.
    RequestToken() = default;

    //! Returns whether this request remains current, without accessing UI state.
    bool IsCurrent() const;

    //! Returns the captured revision, or zero for an unrestricted token.
    std::uint64_t GetRevision() const noexcept;

  private:
    friend class RequestScope;
    friend class Task;

    //! Stores revisions and weak task registrations under a mutex.
    struct State;

    //! Performs an internal state transition only while the request is current.
    bool IfCurrent(const std::function<bool()>& operation) const;

    //! Registers a task for immediate cancellation or supersession on scope changes.
    void Register(const class Task& task) const;

    //! Observes the scope without extending its lifetime.
    std::weak_ptr<State> m_State;

    //! Captures one generation independently of task completion order.
    std::uint64_t m_Revision = 0;
};

//! Groups replaceable requests; new generations retire old results and request cooperative stop.
class RequestScope
{
  public:
    //! Creates a thread-safe request scope.
    RequestScope();

    //! Cancels registered tasks without waiting for their workers.
    ~RequestScope();

    //! Scope destruction has one owner.
    RequestScope(const RequestScope&) = delete;

    //! Scope destruction has one owner.
    RequestScope& operator=(const RequestScope&) = delete;

    //! Transfers scope ownership.
    RequestScope(RequestScope&& other) noexcept;

    //! Cancels current work before taking another scope.
    RequestScope& operator=(RequestScope&& other) noexcept;

    //! Supersedes previous requests and returns the next token.
    RequestToken Next();

    //! Cancels current requests while keeping the scope reusable.
    void Cancel();

  private:
    //! Owns request identity and weak registrations.
    std::shared_ptr<RequestToken::State> m_State;
};

//! A copyable, thread-safe handle for cancellation, errors, and UI-delivery outcome.
class Task
{
  public:
    //! Creates an empty, rejected task handle.
    Task() = default;

    //! Returns computation/delivery status without waiting.
    TaskStatus GetStatus() const;

    //! Returns whether computation and delivery have a terminal outcome.
    bool IsReady() const;

    //! Cancels pending work/delivery and requests cooperative stop; a started UI callback is not interrupted.
    bool Cancel() const;

    //! Returns a stored worker or UI callback exception.
    std::exception_ptr GetError() const;

    //! Rethrows the recorded failure, if present.
    void RethrowIfFailed() const;

    //! Returns the final UI-delivery receipt once submitted; otherwise it is empty/rejected.
    DispatchReceipt GetDelivery() const;

  private:
    friend class BackgroundTasks;
    friend class TaskContext;
    friend class RequestToken;
    friend class RequestScope;

    //! Holds status, cooperative stop, request identity, and delivery acknowledgement.
    struct State;

    //! Creates and registers a queued operation.
    static Task Create(RequestToken request);

    //! Marks a queued operation running if it has not been retired.
    bool Start() const;

    //! Returns whether a worker/progress update may still publish.
    bool CanPublish() const;

    //! Marks the start of waiting for UI delivery and stores an optional worker failure.
    bool AwaitUi(std::exception_ptr error = {}) const;

    //! Claims owner-side delivery without allowing cancellation to race a started callback.
    bool BeginDelivery() const;

    //! Completes a successful callback, preserving a previously recorded worker failure.
    void CompleteDelivery() const;

    //! Records a failure once and requests cooperative stop.
    void Fail(std::exception_ptr error) const;

    //! Retires an unfinished task with a non-success outcome.
    bool Finish(TaskStatus status, std::exception_ptr error = {}) const;

    //! Stores a receipt and observes cancellation, rejection, or target expiry.
    void SetDelivery(DispatchReceipt receipt) const;

    //! Returns the cooperative stop token.
    std::stop_token GetStopToken() const;

    //! Retires this task when an external lifetime ends, without retaining this state in the callback.
    void Watch(std::stop_token lifetime, TaskStatus status) const;

    //! Retains state independently of executor or target lifetime.
    std::shared_ptr<State> m_State;
};

//! Provides worker-side cancellation and replaceable progress publication.
class TaskContext
{
  public:
    //! Returns the task's cooperative stop token.
    std::stop_token GetStopToken() const;

    //! Returns whether cancellation or supersession has been requested.
    bool IsCancellationRequested() const;

    //! Stops the worker path with an exception when cancellation has been requested.
    void ThrowIfCancellationRequested() const;

    //! Publishes a finite progress fraction in [0,1]; false means delivery is no longer useful.
    bool ReportProgress(double fraction) const;

  private:
    friend class BackgroundTasks;

    //! Creates a context without exposing a live UI object to worker code.
    TaskContext(Task task, std::function<bool(double)> progress);

    //! Observes the operation's synchronized state.
    Task m_Task;

    //! Owns a replaceable progress publisher, or is empty when unused.
    std::function<bool(double)> m_Progress;
};
} // namespace EStudioUI
