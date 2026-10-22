#include <EStudioUI/UiHandle.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
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

template <typename Callback> void Throws(Callback callback, const std::string& message)
{
    bool caught = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        caught = true;
    }
    Check(caught, message);
}

void DrainAll(UiDispatcher& dispatcher)
{
    while (dispatcher.HasPending())
        dispatcher.Drain(100000, 2s);
}

void BasicDelivery()
{
    Throws([] { UiDispatcher dispatcher(0); }, "zero capacity rejected");
    UiDispatcher dispatcher(2);
    int value = 0;
    const auto first = dispatcher.Post([&] { value = 7; });
    Check(value == 0 && first.GetStatus() == DispatchStatus::Pending, "owner posts are asynchronous");
    Throws([&] { first.WaitFor(0ms); }, "owner cannot wait for pending UI work");
    const auto second = dispatcher.Post([&] { value += 3; });
    const auto overflow = dispatcher.Post([] {});
    Check(overflow.GetStatus() == DispatchStatus::Rejected &&
              overflow.GetRejectionReason() == DispatchRejectionReason::Capacity,
          "full queue has an explicit rejection outcome");
    Check(dispatcher.Drain(1, 2s) == 1 && value == 7 && first.IsReady() && !second.IsReady(),
          "count budget preserves queued work");
    Check(first.WaitFor(0ms), "terminal owner waits are nonblocking");
    DrainAll(dispatcher);
    Check(value == 10 && second.GetStatus() == DispatchStatus::Completed, "FIFO callbacks complete normally");
    const auto stats = dispatcher.GetStats();
    Check(stats.m_Accepted == 2 && stats.m_Completed == 2 && stats.m_Rejected == 1 &&
              stats.m_Pending == 0 && stats.m_HighWaterMark == 2,
          "queue diagnostics account for accepted and rejected work");
    dispatcher.Close();
    const auto closed = dispatcher.Post([] {});
    Check(closed.GetStatus() == DispatchStatus::Rejected &&
              closed.GetRejectionReason() == DispatchRejectionReason::Closed,
          "closed endpoints reject without executing");
    dispatcher.Close();
    Check(dispatcher.Drain() == 0 && !dispatcher.IsOpen(), "closure is idempotent");
    Throws([&] { dispatcher.SetWakeup([] {}); }, "closed endpoint cannot install native wakeup");
}

void CancellationAndErrors()
{
    UiDispatcher dispatcher(2);
    int delivered = 0;
    const auto cancelled = dispatcher.Post([&] { ++delivered; });
    const auto kept = dispatcher.Post([&] { ++delivered; });
    Check(cancelled.Cancel() && !cancelled.Cancel(), "pending cancellation settles once");
    const auto replacement = dispatcher.Post([&] { ++delivered; });
    Check(replacement.GetStatus() == DispatchStatus::Pending, "cancelled records do not permanently occupy capacity");
    DrainAll(dispatcher);
    Check(delivered == 2 && kept.IsReady() && replacement.IsReady(), "cancelled callbacks never execute");
    const auto failed = dispatcher.Post([] { throw std::runtime_error("expected callback failure"); });
    const auto after = dispatcher.Post([&] { ++delivered; });
    DrainAll(dispatcher);
    Check(failed.GetStatus() == DispatchStatus::Failed && failed.GetError() && after.IsReady(),
          "callback exceptions are observable without abandoning following work");
    Throws([&] { failed.RethrowIfFailed(); }, "callback error can be rethrown deliberately");
    const auto running = dispatcher.Post([&] { ++delivered; });
    int observations = 0;
    running.OnFinished([&](DispatchStatus status, std::exception_ptr error)
                       {
                           Check(status == DispatchStatus::Completed && !error, "completion outcome is owned");
                           ++observations;
                           dispatcher.Post([&] { ++delivered; });
                           throw std::runtime_error("observer failure");
                       });
    running.OnFinished([&](DispatchStatus, std::exception_ptr) { ++observations; });
    dispatcher.Drain(2, 2s);
    Check(observations == 2 && running.GetObserverError() && dispatcher.HasPending(),
          "observers run outside locks and isolate their exceptions");
    running.OnFinished([&](DispatchStatus, std::exception_ptr) { ++observations; });
    Check(observations == 3, "late observers receive one immediate terminal notification");
    DrainAll(dispatcher);
    dispatcher.Close();
}

void Coalescing()
{
    UiDispatcher dispatcher(4);
    std::vector<int> values;
    const auto old = dispatcher.PostLatest("progress", [&] { values.push_back(1); });
    dispatcher.Post([&] { values.push_back(2); });
    const auto latest = dispatcher.PostLatest("progress", [&] { values.push_back(3); });
    Check(old.GetStatus() == DispatchStatus::Superseded && latest.GetStatus() == DispatchStatus::Pending,
          "replaced publications settle as superseded");
    DrainAll(dispatcher);
    Check(values == std::vector<int>{2, 3}, "latest publications retain their new submission order");
    int wakeups = 0;
    dispatcher.SetWakeup([&] { ++wakeups; });
    int progress = -1;
    for (int publication = 0; publication < 100000; ++publication)
        dispatcher.PostLatest("flood", [&, publication] { progress = publication; });
    Check(wakeups == 1 && dispatcher.GetStats().m_HighWaterMark <= 4,
          "100000 replaceable publications retain bounded records and one native wake");
    Check(dispatcher.GetStats().m_Pending == 1 && progress == -1,
          "coalescing publishes one pending latest value");
    DrainAll(dispatcher);
    Check(progress == 99999 && dispatcher.GetStats().m_Superseded == 100000,
          "flooded progress delivers its exact latest value and terminal outcomes");
    dispatcher.Close();
}

void SnapshotAndPhaseBoundaries()
{
    UiDispatcher dispatcher;
    int count = 0;
    dispatcher.Post([&]
                    {
                        ++count;
                        dispatcher.Post([&] { ++count; });
                        Throws([&] { dispatcher.Drain(); }, "recursive draining rejected");
                    });
    Check(dispatcher.Drain(10, 2s) == 1 && count == 1 && dispatcher.HasPending(),
          "callbacks posted during drain wait for the next turn");
    DrainAll(dispatcher);
    bool allowed = false;
    dispatcher.SetDrainGuard([&]
                             {
                                 if (!allowed)
                                     throw std::logic_error("traversal in progress");
                             });
    dispatcher.Post([&] { ++count; });
    Throws([&] { dispatcher.Drain(); }, "phase guard rejects unsafe delivery without consuming work");
    allowed = true;
    DrainAll(dispatcher);
    Check(count == 3, "phase failure preserves queued operation");
    dispatcher.SetDrainGuard({});
    dispatcher.Post([&]
                    {
                        ++count;
                        std::this_thread::sleep_for(2ms);
                    });
    dispatcher.Post([&] { ++count; });
    Check(dispatcher.Drain(10, 1ms) == 1 && count == 4, "time budget is checked between callbacks");
    Check(dispatcher.Drain(10, 0ms) == 0, "zero duration performs no work");
    Throws([&] { dispatcher.Drain(10, -1ms); }, "negative drain duration rejected");
    DrainAll(dispatcher);
    dispatcher.Close();
}

void TypedTargets()
{
    struct Target
    {
        int m_Value = 0;
    };
    UiDispatcher dispatcher;
    const auto owner = std::this_thread::get_id();
    auto target = std::make_unique<Target>();
    auto lifetime = std::make_shared<int>(0);
    const auto handle = UiHandle<Target>::ForTarget(
        dispatcher, UiDispatcher::AllocateTargetKey(),
        [weak = std::weak_ptr<int>(lifetime), pointer = target.get(), owner]
        {
            if (std::this_thread::get_id() != owner)
                throw std::logic_error("resolver ran on worker");
            return weak.expired() ? nullptr : pointer;
        });
    DispatchReceipt result;
    std::thread worker([&]
                       {
                           result = handle.Post([value = std::make_unique<int>(42)](Target& current)
                                                { current.m_Value = *value; });
                       });
    worker.join();
    DrainAll(dispatcher);
    Check(result.GetStatus() == DispatchStatus::Completed && target->m_Value == 42,
          "typed owning callbacks resolve only on the owner");
    const auto expired = handle.Post([](Target& current) { current.m_Value = 99; });
    lifetime.reset();
    target.reset();
    DrainAll(dispatcher);
    Check(expired.GetStatus() == DispatchStatus::TargetExpired, "dead targets settle without a dereference");
    Target first, second;
    auto firstHandle = UiHandle<Target>::ForTarget(dispatcher, UiDispatcher::AllocateTargetKey(), [&] { return &first; });
    auto secondHandle = UiHandle<Target>::ForTarget(dispatcher, UiDispatcher::AllocateTargetKey(), [&] { return &second; });
    const auto a = firstHandle.PostLatest("value", [](Target& current) { current.m_Value = 1; });
    const auto b = secondHandle.PostLatest("value", [](Target& current) { current.m_Value = 2; });
    DrainAll(dispatcher);
    Check(first.m_Value == 1 && second.m_Value == 2 && a.IsReady() && b.IsReady(),
          "publication keys are scoped to target identity");
    UiHandle<Target> empty;
    const auto rejected = empty.Post([](Target&) {});
    Check(rejected.GetStatus() == DispatchStatus::Rejected &&
              rejected.GetRejectionReason() == DispatchRejectionReason::EmptyTarget,
          "empty handles reject naturally");
    Throws([&] { empty.GetDispatcher(); }, "empty handle has no dispatcher");
    Throws([&] { handle.PostLatest("", [](Target&) {}); }, "empty publication key rejected");
    dispatcher.Close();
    Check(!handle.IsConnected(), "handle observes endpoint closure without querying its target");
}

void ConcurrentProducers()
{
    constexpr int producers = 4, publications = 2000;
    UiDispatcher dispatcher(8192);
    const auto owner = std::this_thread::get_id();
    std::vector<std::vector<int>> observed(producers);
    std::atomic<int> finished{0};
    std::atomic<bool> rejected{false}, wrongThread{false};
    std::vector<std::thread> workers;
    for (int producer = 0; producer < producers; ++producer)
        workers.emplace_back([&, producer]
                             {
                                 for (int index = 0; index < publications; ++index)
                                 {
                                     const auto receipt = dispatcher.Post([&, producer, index]
                                                                          {
                                                                              if (std::this_thread::get_id() != owner)
                                                                                  wrongThread = true;
                                                                              observed[producer].push_back(index);
                                                                          });
                                     if (receipt.GetStatus() == DispatchStatus::Rejected)
                                         rejected = true;
                                 }
                                 ++finished;
                             });
    while (finished.load() != producers || dispatcher.HasPending())
    {
        dispatcher.Drain(64, 2ms);
        std::this_thread::yield();
    }
    for (auto& worker : workers)
        worker.join();
    Check(!rejected && !wrongThread, "concurrent producers deliver only on the owning thread");
    for (int producer = 0; producer < producers; ++producer)
    {
        Check(observed[producer].size() == publications, "all accepted producer commands are delivered");
        for (int index = 0; index < publications; ++index)
            Check(observed[producer][static_cast<std::size_t>(index)] == index, "per-producer FIFO preserved");
    }
    Check(dispatcher.GetStats().m_Completed == producers * publications && !dispatcher.HasPending(),
          "concurrent completion metrics settle exactly once");
    dispatcher.Close();
}

void OwnershipAndShutdown()
{
    UiDispatcher dispatcher;
    std::atomic<int> violations{0};
    std::thread worker([&]
                       {
                           for (const auto& operation : std::vector<std::function<void()>>{
                                    [&] { dispatcher.Drain(); }, [&] { dispatcher.Close(); },
                                    [&] { dispatcher.SetWakeup({}); }, [&] { dispatcher.SetDrainGuard({}); },
                                    [&]
                                    {
                                        UiHandle<int>::ForTarget(dispatcher, UiDispatcher::AllocateTargetKey(),
                                                                 []() -> int* { return nullptr; });
                                    }})
                           {
                               try
                               {
                                   operation();
                               }
                               catch (const std::logic_error&)
                               {
                                   ++violations;
                               }
                           }
                       });
    worker.join();
    Check(violations == 5, "owner-only operations reject worker use before mutable access");
    DispatchReceipt current;
    current = dispatcher.Post([&]
                              {
                                  Check(!current.Cancel(), "running callbacks cannot be cancelled mid-execution");
                                  dispatcher.Close();
                              });
    const auto pending = dispatcher.Post([] { throw std::runtime_error("closed work executed"); });
    dispatcher.Drain(8, 2s);
    Check(current.GetStatus() == DispatchStatus::Completed && pending.GetStatus() == DispatchStatus::Cancelled,
          "closure inside callback completes running work and cancels pending work");
    Check(dispatcher.GetStats().m_Pending == 0, "closure settles all pending acknowledgements");
}

void WakeQuiescenceAndWait()
{
    UiDispatcher dispatcher;
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    std::atomic<bool> inside{false}, closing{false};
    dispatcher.SetWakeup([&]
                         {
                             std::unique_lock lock(mutex);
                             inside = true;
                             entered = true;
                             changed.notify_all();
                             changed.wait(lock, [&] { return released; });
                             inside = false;
                         });
    DispatchReceipt receipt;
    std::thread producer([&] { receipt = dispatcher.Post([] {}); });
    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return entered; });
    }
    std::thread releaser([&]
                         {
                             while (!closing.load())
                                 std::this_thread::yield();
                             std::lock_guard lock(mutex);
                             released = true;
                             changed.notify_all();
                         });
    closing = true;
    dispatcher.Close();
    producer.join();
    releaser.join();
    Check(!inside && receipt.GetStatus() == DispatchStatus::Cancelled,
          "Close quiesces in-flight native wakeup before returning");
    Check(receipt.WaitFor(0ms), "cancelled receipt remains observable after closure");

    UiDispatcher other;
    const auto awaited = other.Post([] {});
    std::atomic<bool> ready{false};
    std::thread waiter([&] { ready = awaited.WaitFor(2s); });
    other.Drain(1, 2s);
    waiter.join();
    Check(ready, "worker wait completes when owning thread delivers");
    other.SetWakeup([] { throw std::runtime_error("native wake failure"); });
    const auto recoverable = other.Post([] {});
    Check(other.GetWakeupError() && other.GetStats().m_WakeupFailures == 1,
          "native wake failures remain diagnostic without losing accepted work");
    other.Drain(1, 2s);
    Check(recoverable.GetStatus() == DispatchStatus::Completed, "accepted work survives a failed platform wake");
    other.Close();
}

void LifetimeTokensAndOwnedValues()
{
    UiDispatcher dispatcher;
    Check(!dispatcher.GetStopToken().stop_requested(), "open dispatcher lifetime is active");
    bool stopped = false, closedAtStop = false;
    std::stop_callback closing(dispatcher.GetStopToken(),
                               [&]
                               {
                                   stopped = true;
                                   closedAtStop = dispatcher.Post([] {}).GetRejectionReason() ==
                                                  DispatchRejectionReason::Closed;
                                   dispatcher.Close();
                               });
    std::stop_source receiver;
    int target = 0;
    int resolutions = 0;
    const auto handle = UiHandle<int>::ForTarget(
        dispatcher, UiDispatcher::AllocateTargetKey(),
        [&]
        {
            ++resolutions;
            return &target;
        },
        receiver.get_token());
    const auto pending = handle.Post([](int& value) { value = 99; });
    receiver.request_stop();
    DrainAll(dispatcher);
    Check(!handle.IsConnected() && handle.GetTargetStopToken().stop_requested() && resolutions == 0 && target == 0,
          "receiver token prevents resolver invocation during owner-side expiry");
    Check(pending.GetStatus() == DispatchStatus::TargetExpired,
          "receiver stop yields a deliberate terminal delivery outcome");
    auto owned = std::make_unique<int>(81);
    dispatcher.Post([value = std::move(owned), &target] { target = *value; });
    DrainAll(dispatcher);
    Check(!owned && target == 81, "dispatcher accepts move-only owning payloads");
    const auto copied = std::move(dispatcher);
    Check(dispatcher.IsOpen() && copied.IsOpen(), "moving an endpoint preserves its cheap value semantics");
    dispatcher.Close();
    Check(stopped && closedAtStop && dispatcher.GetStopToken().stop_requested(),
          "dispatcher stop callbacks run outside locks after rejecting further posts");
}
} // namespace

int main()
{
    try
    {
        BasicDelivery();
        CancellationAndErrors();
        Coalescing();
        SnapshotAndPhaseBoundaries();
        TypedTargets();
        ConcurrentProducers();
        OwnershipAndShutdown();
        WakeQuiescenceAndWait();
        LifetimeTokensAndOwnedValues();
        std::cout << "UI dispatch PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "UI dispatch FAIL / " << error.what() << '\n';
        return 1;
    }
}
