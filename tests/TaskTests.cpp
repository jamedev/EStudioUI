#include <EStudioUI/BackgroundTasks.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <latch>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace EStudioUI;
using namespace std::chrono_literals;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Predicate> void Until(Predicate predicate, std::string message)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(std::move(message));
        std::this_thread::yield();
    }
}

template <typename Predicate> void Pump(UiDispatcher& dispatcher, Predicate predicate)
{
    Until(
        [&]
        {
            dispatcher.Drain(1024, 20ms);
            return predicate();
        },
        "task did not settle while pumping UI work");
}

struct Target
{
    int m_Value = 0;
    int m_Callbacks = 0;
    double m_Progress = -1;
};

UiHandle<Target> Handle(UiDispatcher& dispatcher, const std::shared_ptr<Target>& target, std::stop_token lifetime = {})
{
    return UiHandle<Target>::ForTarget(
        dispatcher, UiDispatcher::AllocateTargetKey(),
        [weak = std::weak_ptr<Target>(target)]
        {
            const auto live = weak.lock();
            return live.get();
        },
        lifetime);
}

void ResultsAndSignatures()
{
    UiDispatcher dispatcher;
    auto target = std::make_shared<Target>();
    auto handle = Handle(dispatcher, target);
    BackgroundTasks workers({1, 16});
    const auto owner = std::this_thread::get_id();
    std::thread::id workerThread;
    std::thread::id callbackThread;
    auto task = workers.Run(
        handle,
        [input = std::make_unique<int>(41), &workerThread]() mutable
        {
            workerThread = std::this_thread::get_id();
            return std::make_unique<int>(*input + 1);
        },
        [marker = std::make_unique<int>(7), &callbackThread](Target& receiver, std::unique_ptr<int> result)
        {
            callbackThread = std::this_thread::get_id();
            receiver.m_Value = *result + *marker;
            ++receiver.m_Callbacks;
        });
    Until([&] { return task.GetDelivery().GetStatus() == DispatchStatus::Pending; }, "worker did not queue a result");
    Check(target->m_Callbacks == 0 && !task.IsReady(), "worker result never invokes UI callbacks inline");
    Pump(dispatcher, [&] { return task.IsReady(); });
    Check(target->m_Value == 49 && target->m_Callbacks == 1, "move-only work, callback, and result are supported");
    Check(workerThread != owner && callbackThread == owner, "computation and delivery run on their intended threads");
    Check(task.GetStatus() == TaskStatus::Completed && task.GetDelivery().GetStatus() == DispatchStatus::Completed,
          "task completes after its UI callback");
    Check(!task.Cancel(), "completed task cannot be cancelled retroactively");

    auto tokenTask = workers.Run(
        handle, [](std::stop_token stop) { return stop.stop_requested() ? 0 : 8; },
        [](Target& receiver, int value) { receiver.m_Value = value; });
    Pump(dispatcher, [&] { return tokenTask.IsReady(); });
    Check(target->m_Value == 8, "stop_token work signature is usable without a context");
    auto voidTask = workers.Run(handle, [] {}, [](Target& receiver) { ++receiver.m_Callbacks; });
    Pump(dispatcher, [&] { return voidTask.IsReady(); });
    Check(voidTask.GetStatus() == TaskStatus::Completed && target->m_Callbacks == 2,
          "void work uses a target-only completion");
}

void ProgressAndErrors()
{
    UiDispatcher dispatcher(16);
    auto target = std::make_shared<Target>();
    auto handle = Handle(dispatcher, target);
    BackgroundTasks workers({1, 16});
    int progressCallbacks = 0;
    UiTaskOptions<Target> options;
    options.m_Progress = [&](Target& receiver, double fraction)
    {
        receiver.m_Progress = fraction;
        ++progressCallbacks;
    };
    auto task = workers.Run(
        handle,
        [](TaskContext& context)
        {
            for (int i = 1; i <= 100000; ++i)
                if (!context.ReportProgress(static_cast<double>(i) / 100000))
                    throw std::runtime_error("progress unexpectedly retired");
            return 23;
        },
        [](Target& receiver, int value) { receiver.m_Value = value; }, options);
    Until([&] { return task.GetDelivery().GetStatus() == DispatchStatus::Pending || task.IsReady(); },
          "progress producer stalled");
    Check(!task.IsReady(), "progress flood reaches its result without overrunning the UI queue");
    const auto stats = dispatcher.GetStats();
    Check(stats.m_Pending == 2 && stats.m_HighWaterMark <= 2 && stats.m_Superseded == 99999,
          "100000 progress publications retain one latest update and one result");
    Pump(dispatcher, [&] { return task.IsReady(); });
    Check(progressCallbacks == 1 && target->m_Progress == 1 && target->m_Value == 23,
          "latest progress is delivered before the task result");

    auto failed = workers.Run(handle, []() -> int { throw std::runtime_error("worker failure"); }, [](Target&, int) {});
    Until([&] { return failed.IsReady(); }, "worker failure did not settle");
    Check(failed.GetStatus() == TaskStatus::Failed && failed.GetError(),
          "worker errors remain observable without handlers");
    bool rethrown = false;
    try
    {
        failed.RethrowIfFailed();
    }
    catch (const std::runtime_error& error)
    {
        rethrown = std::string(error.what()) == "worker failure";
    }
    Check(rethrown, "worker exceptions preserve their original diagnostic");

    int errors = 0;
    const auto owner = std::this_thread::get_id();
    options.m_Progress = {};
    options.m_Error = [&](Target& receiver, std::exception_ptr error)
    {
        Check(std::this_thread::get_id() == owner, "task error callback uses the UI executor");
        (void)error;
        ++receiver.m_Callbacks;
        ++errors;
    };
    auto handled =
        workers.Run(handle, []() -> int { throw std::runtime_error("handled"); }, [](Target&, int) {}, options);
    Pump(dispatcher, [&] { return handled.IsReady(); });
    Check(errors == 1 && handled.GetStatus() == TaskStatus::Failed && handled.GetError(),
          "an error handler runs once on the UI while the task retains failure");
    auto callbackFailed =
        workers.Run(handle, [] { return 2; }, [](Target&, int) { throw std::runtime_error("UI failure"); });
    Pump(dispatcher, [&] { return callbackFailed.IsReady(); });
    Check(callbackFailed.GetStatus() == TaskStatus::Failed &&
              callbackFailed.GetDelivery().GetStatus() == DispatchStatus::Failed,
          "UI callback exceptions settle both task and dispatch receipt");
    auto invalidProgress = workers.Run(
        handle, [](TaskContext& context) { context.ReportProgress(std::numeric_limits<double>::quiet_NaN()); },
        [](Target&) {});
    Until([&] { return invalidProgress.IsReady(); }, "invalid progress did not settle");
    Check(invalidProgress.GetStatus() == TaskStatus::Failed, "invalid progress is diagnosed as task failure");
}

void CancellationAndBounds()
{
    UiDispatcher dispatcher;
    auto target = std::make_shared<Target>();
    auto handle = Handle(dispatcher, target);
    BackgroundTasks workers({1, 1});
    std::latch started(1);
    std::latch stopped(1);
    auto task = workers.Run(
        handle,
        [&](TaskContext& context)
        {
            std::mutex mutex;
            std::condition_variable_any changed;
            std::unique_lock lock(mutex);
            started.count_down();
            changed.wait(lock, context.GetStopToken(), [] { return false; });
            stopped.count_down();
            context.ThrowIfCancellationRequested();
            return 1;
        },
        [](Target& receiver, int) { ++receiver.m_Callbacks; });
    started.wait();
    int queuedCalls = 0;
    auto queued = workers.Run(
        handle,
        [&]
        {
            ++queuedCalls;
            return 2;
        },
        [](Target&, int) {});
    auto rejected = workers.Run(handle, [] { return 3; }, [](Target&, int) {});
    Check(rejected.GetStatus() == TaskStatus::Rejected, "owned worker queue rejects overload explicitly");
    Check(queued.Cancel() && queued.GetStatus() == TaskStatus::Cancelled,
          "queued work can be retired before execution");
    Check(task.Cancel() && task.GetStatus() == TaskStatus::Cancelled, "running work cancels delivery immediately");
    stopped.wait();
    Until(
        [&]
        {
            const auto stats = workers.GetStats();
            return stats.m_Queued == 0 && stats.m_Running == 0;
        },
        "cancelled jobs did not retire");
    dispatcher.Drain();
    Check(queuedCalls == 0 && target->m_Callbacks == 0, "cancelled work and UI completions are suppressed");
    Check(workers.GetStats().m_HighWaterMark == 1 && workers.GetStats().m_Rejected == 1,
          "executor diagnostics preserve bounded admission evidence");

    auto pendingDelivery = workers.Run(handle, [] { return 4; }, [](Target& receiver, int) { ++receiver.m_Callbacks; });
    Until([&] { return pendingDelivery.GetDelivery().GetStatus() == DispatchStatus::Pending; },
          "result was not queued");
    Check(pendingDelivery.Cancel(), "queued result can be cancelled before delivery");
    dispatcher.Drain();
    Check(target->m_Callbacks == 0 && pendingDelivery.GetStatus() == TaskStatus::Cancelled,
          "cancelled UI result cannot revive a task");

    Task inFlight;
    bool cancelledInCallback = true;
    inFlight = workers.Run(
        handle, [] { return 5; },
        [&](Target& receiver, int)
        {
            cancelledInCallback = inFlight.Cancel();
            ++receiver.m_Callbacks;
        });
    Pump(dispatcher, [&] { return inFlight.IsReady(); });
    Check(!cancelledInCallback && inFlight.GetStatus() == TaskStatus::Completed,
          "cancellation does not retroactively interrupt a started UI callback");
}

void RequestsAndReceiverLifetime()
{
    UiDispatcher dispatcher;
    auto target = std::make_shared<Target>();
    auto handle = Handle(dispatcher, target);
    BackgroundTasks workers({2, 16});
    RequestScope scope;
    std::latch oldStarted(1);
    std::latch releaseOld(1);
    std::atomic<bool> oldStopped = false;
    auto old = workers.RunLatest(
        scope, handle,
        [&](std::stop_token stop)
        {
            oldStarted.count_down();
            releaseOld.wait();
            oldStopped = stop.stop_requested();
            return 1;
        },
        [](Target& receiver, int value)
        {
            receiver.m_Value = value;
            ++receiver.m_Callbacks;
        });
    oldStarted.wait();
    auto current = workers.RunLatest(
        scope, handle, [] { return 42; },
        [](Target& receiver, int value)
        {
            receiver.m_Value = value;
            ++receiver.m_Callbacks;
        });
    Check(old.GetStatus() == TaskStatus::Superseded, "new requests retire the older task immediately");
    Pump(dispatcher, [&] { return current.IsReady(); });
    releaseOld.count_down();
    Until([&] { return oldStopped.load(); }, "old worker did not observe its stop request");
    Until([&] { return workers.GetStats().m_Running == 0; }, "superseded computation did not return");
    dispatcher.Drain();
    Check(target->m_Value == 42 && target->m_Callbacks == 1 && old.GetStatus() == TaskStatus::Superseded,
          "a late old result cannot overwrite a newer result on the same live target");

    auto scoped = std::make_unique<RequestScope>();
    UiTaskOptions<Target> options;
    options.m_Request = scoped->Next();
    auto scopedTask =
        workers.Run(handle, [] { return 99; }, [](Target& receiver, int) { ++receiver.m_Callbacks; }, options);
    Until([&] { return scopedTask.GetDelivery().GetStatus() == DispatchStatus::Pending; },
          "scoped result was not queued");
    scoped.reset();
    dispatcher.Drain();
    Check(scopedTask.GetStatus() == TaskStatus::Cancelled && target->m_Callbacks == 1,
          "scope destruction cancels queued deliveries without waiting for UI");

    auto expired = workers.Run(handle, [] { return 100; }, [](Target&, int) {});
    Until([&] { return expired.GetDelivery().GetStatus() == DispatchStatus::Pending; },
          "receiver result was not queued");
    target.reset();
    Pump(dispatcher, [&] { return expired.IsReady(); });
    Check(expired.GetStatus() == TaskStatus::TargetExpired,
          "target expiry settles the task without executing a callback");

    target = std::make_shared<Target>();
    handle = Handle(dispatcher, target);
    auto closed = workers.Run(handle, [] { return 101; }, [](Target&, int) {});
    Until([&] { return closed.GetDelivery().GetStatus() == DispatchStatus::Pending; }, "closing result was not queued");
    dispatcher.Close();
    Check(closed.GetStatus() == TaskStatus::Cancelled, "dispatcher closure settles accepted queued tasks");
    auto afterClose = workers.Run(handle, [] { return 102; }, [](Target&, int) {});
    Check(afterClose.GetStatus() == TaskStatus::Rejected, "a closed endpoint rejects work before computation");
}

void InjectedExecutorsAndShutdown()
{
    UiDispatcher dispatcher;
    auto target = std::make_shared<Target>();
    auto handle = Handle(dispatcher, target);
    std::vector<std::function<void()>> queue;
    auto workers = std::make_unique<BackgroundTasks>(
        [&](std::function<void()> operation)
        {
            queue.push_back(std::move(operation));
            return true;
        },
        2);
    int calls = 0;
    auto first = workers->Run(
        handle,
        [&]
        {
            ++calls;
            return 1;
        },
        [](Target&, int) {});
    auto second = workers->Run(
        handle,
        [&]
        {
            ++calls;
            return 2;
        },
        [](Target&, int) {});
    auto rejected = workers->Run(handle, [] { return 3; }, [](Target&, int) {});
    Check(queue.size() == 2 && rejected.GetStatus() == TaskStatus::Rejected,
          "injected executor admission is bounded independently of its queue");
    workers->Shutdown();
    Check(!workers->IsOpen() && first.GetStatus() == TaskStatus::Cancelled &&
              second.GetStatus() == TaskStatus::Cancelled,
          "shutdown cancels injected deliveries and rejects new work immediately");
    workers.reset();
    for (auto& operation : queue)
        operation();
    dispatcher.Drain();
    Check(calls == 0, "injected closures remain safe after executor owner destruction");

    BackgroundTasks declined([](std::function<void()>) { return false; });
    auto declinedTask = declined.Run(handle, [] { return 1; }, [](Target&, int) {});
    Check(declinedTask.GetStatus() == TaskStatus::Rejected, "executor rejection settles a task");
    BackgroundTasks throwing([](std::function<void()>) -> bool { throw std::runtime_error("executor failure"); });
    auto throwingTask = throwing.Run(handle, [] { return 1; }, [](Target&, int) {});
    Check(throwingTask.GetStatus() == TaskStatus::Rejected && throwingTask.GetError(),
          "executor errors remain inspectable without escaping submission");

    BackgroundTasks inlineExecutor(
        [](std::function<void()> operation)
        {
            operation();
            return true;
        });
    auto inlineTask =
        inlineExecutor.Run(handle, [] { return 17; }, [](Target& receiver, int value) { receiver.m_Value = value; });
    Check(inlineTask.GetStatus() == TaskStatus::AwaitingUi && target->m_Value == 0,
          "even inline injected execution leaves UI delivery consistently queued");
    Pump(dispatcher, [&] { return inlineTask.IsReady(); });
    Check(target->m_Value == 17, "an application-owned executor can deliver normal typed results");
}

void LifetimeCancellation()
{
    BackgroundTasks workers({2, 16});
    auto target = std::make_shared<Target>();
    UiDispatcher dispatcher;
    std::stop_source lifetime;
    auto handle = Handle(dispatcher, target, lifetime.get_token());
    const auto runUntilStopped = [&](const UiHandle<Target>& endpoint, std::latch& started, std::latch& stopped)
    {
        return workers.Run(
            endpoint,
            [&](std::stop_token stop)
            {
                std::mutex mutex;
                std::condition_variable_any changed;
                std::unique_lock lock(mutex);
                started.count_down();
                changed.wait(lock, stop, [] { return false; });
                stopped.count_down();
                return 1;
            },
            [](Target& receiver, int) { ++receiver.m_Callbacks; });
    };
    std::latch targetStarted(1);
    std::latch targetStopped(1);
    auto targetTask = runUntilStopped(handle, targetStarted, targetStopped);
    targetStarted.wait();
    lifetime.request_stop();
    Check(targetTask.GetStatus() == TaskStatus::TargetExpired,
          "receiver lifetime ending retires a computing task before it publishes progress or result");
    targetStopped.wait();
    auto expired = workers.Run(handle, [] { return 1; }, [](Target&, int) {});
    Check(expired.GetStatus() == TaskStatus::TargetExpired,
          "already expired receiver suppresses work before admission");

    auto openHandle = Handle(dispatcher, target);
    std::latch windowStarted(1);
    std::latch windowStopped(1);
    auto windowTask = runUntilStopped(openHandle, windowStarted, windowStopped);
    windowStarted.wait();
    dispatcher.Close();
    Check(windowTask.GetStatus() == TaskStatus::Cancelled,
          "dispatcher closure cancels a computing task without waiting for result publication");
    windowStopped.wait();
    Check(target->m_Callbacks == 0, "lifetime cancellation never delivers a retired computation");
}

void OwnedShutdown()
{
    UiDispatcher dispatcher;
    auto target = std::make_shared<Target>();
    auto handle = Handle(dispatcher, target);
    auto workers = std::make_unique<BackgroundTasks>(BackgroundTaskOptions{1, 16});
    std::latch started(1);
    std::atomic<bool> stopped = false;
    auto task = workers->Run(
        handle,
        [&](std::stop_token stop)
        {
            std::mutex mutex;
            std::condition_variable_any changed;
            std::unique_lock lock(mutex);
            started.count_down();
            changed.wait(lock, stop, [] { return false; });
            stopped = true;
            return 1;
        },
        [](Target& receiver, int) { ++receiver.m_Callbacks; });
    started.wait();
    workers.reset();
    Check(stopped.load() && task.GetStatus() == TaskStatus::Cancelled,
          "owned worker destruction requests stop and joins without requiring UI delivery");
    dispatcher.Drain();
    Check(target->m_Callbacks == 0, "executor destruction cancels its completion rather than waiting for the UI loop");
}

void ManyProducers()
{
    UiDispatcher dispatcher(1024);
    auto target = std::make_shared<Target>();
    auto handle = Handle(dispatcher, target);
    BackgroundTasks workers({4, 512});
    std::vector<Task> tasks;
    std::mutex mutex;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer)
        producers.emplace_back(
            [&, producer]
            {
                std::vector<Task> local;
                for (int i = 0; i < 50; ++i)
                    local.push_back(workers.Run(
                        handle, [value = producer * 50 + i] { return value; },
                        [](Target& receiver, int value)
                        {
                            receiver.m_Value += value;
                            ++receiver.m_Callbacks;
                        }));
                std::lock_guard lock(mutex);
                tasks.insert(tasks.end(), local.begin(), local.end());
            });
    for (auto& producer : producers)
        producer.join();
    Pump(dispatcher,
         [&] { return std::all_of(tasks.begin(), tasks.end(), [](const auto& task) { return task.IsReady(); }); });
    Check(target->m_Callbacks == 200 && target->m_Value == 19900,
          "multiple producers and workers preserve every accepted owned result");
    Check(std::all_of(tasks.begin(), tasks.end(),
                      [](const auto& task) { return task.GetStatus() == TaskStatus::Completed; }),
          "concurrent tasks settle once after owner delivery");
}
} // namespace

int main()
{
    try
    {
        ResultsAndSignatures();
        ProgressAndErrors();
        CancellationAndBounds();
        RequestsAndReceiverLifetime();
        InjectedExecutorsAndShutdown();
        LifetimeCancellation();
        OwnedShutdown();
        ManyProducers();
        std::cout << "Background tasks PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Background tasks FAIL / " << error.what() << '\n';
        return 1;
    }
}
