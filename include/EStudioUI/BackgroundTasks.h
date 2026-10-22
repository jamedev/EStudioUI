#pragma once

#include "Task.h"
#include "UiHandle.h"

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>

namespace EStudioUI
{
//! Configures an owned worker pool; queue capacity excludes already running workers.
struct BackgroundTaskOptions
{
    //! Worker count, or zero for an automatic count capped at four.
    std::size_t m_Workers = 0;

    //! Maximum number of waiting work items.
    std::size_t m_Capacity = 256;
};

//! Captures bounded executor occupancy and submission totals.
struct BackgroundTaskStats
{
    //! Accepted work items waiting for execution.
    std::size_t m_Queued = 0;

    //! Work items currently computing.
    std::size_t m_Running = 0;

    //! Maximum waiting occupancy observed.
    std::size_t m_HighWaterMark = 0;

    //! Total work items accepted for execution.
    std::size_t m_Accepted = 0;

    //! Work items rejected during shutdown or overload.
    std::size_t m_Rejected = 0;
};

//! Optional target-bound callbacks and revision policy for a background operation.
template <typename T> struct UiTaskOptions
{
    //! Optional request generation; newer requests suppress old computation results.
    RequestToken m_Request;

    //! Optional UI-side progress callback; pending values coalesce per task.
    std::function<void(T&, double)> m_Progress;

    //! Optional UI-side worker-error callback; Task still records a Failed outcome.
    std::function<void(T&, std::exception_ptr)> m_Error;
};

//! Runs owned computations and delivers results through typed, lifetime-aware UI handles.
class BackgroundTasks
{
  public:
    //! An injected executor accepts an owning work item or rejects it without running it.
    using Executor = std::function<bool(std::function<void()>)>;

    //! Creates a bounded owned worker pool.
    explicit BackgroundTasks(BackgroundTaskOptions options = {});

    //! Uses an application-owned executor; capacity bounds accepted unfinished computations.
    explicit BackgroundTasks(Executor executor, std::size_t capacity = 256);

    //! Cancels delivery, requests stop, and joins owned workers; injected workers are never joined.
    ~BackgroundTasks();

    //! The executor's shutdown has one owner.
    BackgroundTasks(const BackgroundTasks&) = delete;

    //! The executor's shutdown has one owner.
    BackgroundTasks& operator=(const BackgroundTasks&) = delete;

    //! Transfers owned executor responsibility.
    BackgroundTasks(BackgroundTasks&& other) noexcept;

    //! Shuts down the previous executor before transferring ownership.
    BackgroundTasks& operator=(BackgroundTasks&& other) noexcept;

    //! Starts work accepting TaskContext&, stop_token, or no arguments; success receives the live target and owned
    //! result.
    template <typename T, typename Work, typename Success>
    Task Run(UiHandle<T> target, Work&& work, Success&& success, UiTaskOptions<T> options = {})
    {
        using Worker = std::decay_t<Work>;
        using Completion = std::decay_t<Success>;
        using Result = decltype(InvokeWork(std::declval<Worker&>(), std::declval<TaskContext&>()));
        static_assert(!std::is_reference_v<Result>, "A background task must return an owned value, not a reference.");
        if constexpr (std::is_void_v<Result>)
            static_assert(std::is_invocable_v<Completion&, T&>, "A void task completion must accept its live target.");
        else
            static_assert(std::is_invocable_v<Completion&, T&, Result>,
                          "A task completion must accept its live target and owned result.");

        auto task = Task::Create(options.m_Request);
        if (!task.CanPublish())
            return task;
        if (target.GetTargetStopToken().stop_requested())
        {
            task.Finish(TaskStatus::TargetExpired);
            return task;
        }
        if (!target.IsConnected())
        {
            task.Finish(TaskStatus::Rejected);
            return task;
        }
        task.Watch(target.GetDispatcher().GetStopToken(), TaskStatus::Cancelled);
        task.Watch(target.GetTargetStopToken(), TaskStatus::TargetExpired);
        if (!task.CanPublish())
            return task;
        auto worker = std::make_shared<Worker>(std::forward<Work>(work));
        auto completion = std::make_shared<Completion>(std::forward<Success>(success));
        const auto progressKey = "task-progress:" + std::to_string(UiDispatcher::AllocateTargetKey());
        auto operation = [task, target = std::move(target), worker = std::move(worker),
                          completion = std::move(completion), options = std::move(options), progressKey]() mutable
        {
            if (!task.Start())
                return;
            auto progress = [task, target, callback = options.m_Progress, progressKey](double fraction)
            {
                if (!task.CanPublish())
                    return false;
                if (!callback)
                    return true;
                auto receipt = target.PostLatest(progressKey,
                                                 [task, callback, fraction](T& receiver)
                                                 {
                                                     if (task.CanPublish())
                                                         callback(receiver, fraction);
                                                 });
                receipt.OnFinished(
                    [task](DispatchStatus status, std::exception_ptr error)
                    {
                        if (status == DispatchStatus::Failed)
                            task.Fail(std::move(error));
                        else if (status == DispatchStatus::TargetExpired)
                            task.Finish(TaskStatus::TargetExpired);
                    });
                if (receipt.GetRejectionReason() == DispatchRejectionReason::Closed ||
                    receipt.GetRejectionReason() == DispatchRejectionReason::EmptyTarget)
                {
                    task.Finish(TaskStatus::Rejected);
                    return false;
                }
                return task.CanPublish();
            };
            TaskContext context(task, std::move(progress));
            try
            {
                if constexpr (std::is_void_v<Result>)
                {
                    InvokeWork(*worker, context);
                    Deliver(task, target, [completion](T& receiver) { std::invoke(*completion, receiver); });
                }
                else
                {
                    auto result = std::make_shared<Result>(InvokeWork(*worker, context));
                    Deliver(task, target, [completion, result = std::move(result)](T& receiver)
                            { std::invoke(*completion, receiver, std::move(*result)); });
                }
            }
            catch (...)
            {
                if (task.GetStopToken().stop_requested())
                {
                    task.Cancel();
                    return;
                }
                const auto error = std::current_exception();
                if (options.m_Error)
                    Deliver(
                        task, target, [callback = options.m_Error, error](T& receiver) { callback(receiver, error); },
                        error);
                else
                    task.Fail(error);
            }
        };
        try
        {
            Submit(task, std::move(operation));
        }
        catch (...)
        {
            task.Fail(std::current_exception());
        }
        return task;
    }

    //! Starts the next replaceable request in a scope and suppresses previous results automatically.
    template <typename T, typename Work, typename Success>
    Task RunLatest(RequestScope& scope, UiHandle<T> target, Work&& work, Success&& success,
                   UiTaskOptions<T> options = {})
    {
        options.m_Request = scope.Next();
        return Run(std::move(target), std::forward<Work>(work), std::forward<Success>(success), std::move(options));
    }

    //! Cancels outstanding tasks and rejects new work without joining workers.
    void Shutdown();

    //! Returns whether new submissions are accepted.
    bool IsOpen() const;

    //! Returns coherent executor occupancy and totals.
    BackgroundTaskStats GetStats() const;

  private:
    //! Invokes a worker through its supported cancellation/context signature.
    template <typename Work> static decltype(auto) InvokeWork(Work& work, TaskContext& context)
    {
        if constexpr (std::is_invocable_v<Work&, TaskContext&>)
            return std::invoke(work, context);
        else if constexpr (std::is_invocable_v<Work&, std::stop_token>)
            return std::invoke(work, context.GetStopToken());
        else
        {
            static_assert(std::is_invocable_v<Work&>,
                          "Task work must accept TaskContext&, stop_token, or no arguments.");
            return std::invoke(work);
        }
    }

    //! Queues a completion/error callback after publishing its task state.
    template <typename T, typename Callback>
    static void Deliver(Task task, const UiHandle<T>& target, Callback callback, std::exception_ptr error = {})
    {
        if (!task.AwaitUi(error))
            return;
        auto receipt = target.Post(
            [task, callback = std::move(callback)](T& receiver) mutable
            {
                if (!task.BeginDelivery())
                    return;
                try
                {
                    std::invoke(callback, receiver);
                    task.CompleteDelivery();
                }
                catch (...)
                {
                    task.Fail(std::current_exception());
                    throw;
                }
            });
        task.SetDelivery(std::move(receipt));
    }

    //! Owns pool/executor admission and weak task tracking.
    struct Impl;

    //! Submits a copyable queue closure whose shared captures own any move-only work and result.
    void Submit(const Task& task, std::function<void()> operation);

    //! Owns executor lifetime independently of submitted operation state.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
