#include <EStudioUI/Signal.h>

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <latch>
#include <stdexcept>
#include <string>
#include <string_view>
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

//! Collects mutations that must occur only during owner-side delivery.
struct Receiver
{
    //! Records owned strings independently of emitter storage.
    std::vector<std::string> m_Text;

    //! Records producer/index pairs in delivered order.
    std::vector<std::pair<int, int>> m_Events;

    //! Records the thread that last changed the receiver.
    std::thread::id m_DeliveryThread;
};

//! Owns an isolated UI target without requiring GLFW or a font backend.
struct Fixture
{
    //! Creates a bounded endpoint owned by this test thread.
    explicit Fixture(std::size_t capacity = 4096) : m_Dispatcher(capacity)
    {
    }

    //! Quiesces producer endpoints before the resolver's owner storage disappears.
    ~Fixture()
    {
        m_Dispatcher.Close();
    }

    //! Creates a typed endpoint whose resolver is only evaluated by Drain.
    UiHandle<Receiver> Handle()
    {
        return UiHandle<Receiver>::ForTarget(m_Dispatcher, m_TargetKey, [this] { return m_Receiver.get(); });
    }

    //! Executes the current queue snapshot with a generous deterministic count budget.
    void Drain()
    {
        m_Dispatcher.Drain(10000, 5s);
    }

    //! Owns synchronized producer state and the owner delivery operation.
    UiDispatcher m_Dispatcher;

    //! Keeps receiver lifetime independent of the endpoint.
    std::unique_ptr<Receiver> m_Receiver = std::make_unique<Receiver>();

    //! Distinguishes this target from all other registration generations.
    const std::uint64_t m_TargetKey = UiDispatcher::AllocateTargetKey();
};

//! Checks queued argument availability without instantiating a rejected body.
template <typename T>
concept QueuedArgument =
    requires(Signal<T>& signal, UiHandle<Receiver> handle) { signal.ConnectQueued(handle, [](Receiver&, auto&&) {}); };

static_assert(QueuedArgument<int>);
static_assert(QueuedArgument<const std::string&>);
static_assert(QueuedArgument<std::shared_ptr<const std::string>>);
static_assert(!QueuedArgument<std::string_view>);
static_assert(!QueuedArgument<const std::string_view&>);
static_assert(!QueuedArgument<const char*>);
static_assert(!QueuedArgument<int*>);
static_assert(!QueuedArgument<std::span<const int>>);
static_assert(!QueuedArgument<std::reference_wrapper<int>>);
static_assert(!QueuedArgument<std::initializer_list<int>>);
static_assert(!QueuedArgument<std::unique_ptr<int>>);

void DirectSnapshots()
{
    Signal<int> signal;
    std::vector<int> values;
    Connection second;
    bool added = false;
    signal.Connect(
        [&](int value)
        {
            values.push_back(value);
            second.Disconnect();
            if (!added)
            {
                added = true;
                signal.Connect([&](int next) { values.push_back(next + 100); });
            }
        });
    second = signal.Connect([&](int value) { values.push_back(value + 10); });
    signal.Emit(1);
    Check(values == std::vector<int>{1}, "an emission preserves its snapshot and skips disconnected slots");
    signal.Emit(2);
    Check(values == std::vector<int>({1, 2, 102}), "new slots start in the next emission");
    Check(!second.IsConnected(), "connection activation reflects disconnection");

    Signal<int> recursive;
    int calls = 0;
    recursive.Connect(
        [&](int depth)
        {
            ++calls;
            if (depth < 3)
                recursive.Emit(depth + 1);
        });
    recursive.Emit(0);
    Check(calls == 4, "nested emissions remain independent snapshots");
    const auto throwing = recursive.Connect([](int) { throw std::runtime_error("expected direct callback error"); });
    bool caught = false;
    try
    {
        recursive.Emit(3);
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    throwing.Disconnect();
    recursive.Emit(3);
    Check(caught && calls == 6, "an exceptional emission does not poison future delivery");
    recursive.DisconnectAll();
    Check(!recursive.HasConnections(), "disconnect-all publishes an empty registry");

    Signal<> clearing;
    int clearedCalls = 0;
    clearing.Connect([&] { clearing.DisconnectAll(); });
    clearing.Connect([&] { ++clearedCalls; });
    clearing.Emit();
    Check(clearedCalls == 0 && !clearing.HasConnections(),
          "disconnect-all inside a callback suppresses the rest of its active snapshot");

    auto sender = std::make_unique<Signal<>>();
    int afterDeletion = 0;
    const auto connection = sender->Connect([&] { sender.reset(); });
    sender->Connect([&] { ++afterDeletion; });
    sender->Emit();
    Check(!sender && afterDeletion == 1 && !connection.IsConnected(),
          "active direct delivery survives sender deletion and its connection expires");
}

void QueuedOwnershipAndOrdering()
{
    Fixture fixture;
    Signal<const std::string&> signal;
    const auto ownerThread = std::this_thread::get_id();
    signal.Connect(fixture.Handle(),
                   [](Receiver& receiver, const std::string& text)
                   {
                       receiver.m_Text.push_back(text);
                       receiver.m_DeliveryThread = std::this_thread::get_id();
                   });
    signal.Emit("owner-thread emission");
    Check(fixture.m_Receiver->m_Text.empty(), "target connections always queue, even when emitted by the owner");
    std::jthread worker(
        [&]
        {
            std::string text = "temporary worker text";
            signal.Emit(text);
            text.assign("changed emitter storage");
            signal.Emit("second worker text");
        });
    worker.join();
    fixture.Drain();
    Check(fixture.m_Receiver->m_Text ==
              std::vector<std::string>{"owner-thread emission", "temporary worker text", "second worker text"},
          "queued reference-valued arguments become owning copies in accepted order");
    Check(fixture.m_Receiver->m_DeliveryThread == ownerThread,
          "worker emission mutates its receiver only on the owner");

    Signal<> empty;
    int emptyCalls = 0;
    empty.ConnectQueued(fixture.Handle(),
                        [capture = std::make_unique<int>(7), &emptyCalls](Receiver&) { emptyCalls += *capture; });
    empty.Emit();
    fixture.Drain();
    Check(emptyCalls == 7, "zero-argument queued signals support move-only callback captures");
}

void CancellationAndLifetime()
{
    Fixture fixture;
    Signal<int> signal;
    int calls = 0;
    const auto first = signal.Connect(fixture.Handle(), [&](Receiver&, int) { ++calls; });
    signal.Emit(1);
    first.Disconnect();
    fixture.Drain();
    Check(calls == 0 && !first.IsConnected(), "disconnect suppresses a previously posted target callback");
    {
        ScopedConnection scoped(signal.Connect(fixture.Handle(), [&](Receiver&, int) { ++calls; }));
        signal.Emit(2);
    }
    fixture.Drain();
    Check(calls == 0, "scoped disconnection suppresses pending queued delivery");

    std::vector<DispatchReceipt> receipts;
    const auto receiverConnection = signal.ConnectQueued(
        fixture.Handle(), [&](Receiver&, int) { ++calls; },
        [&](DispatchReceipt receipt) { receipts.push_back(std::move(receipt)); });
    signal.Emit(3);
    fixture.m_Receiver.reset();
    fixture.Drain();
    Check(calls == 0 && receipts.size() == 1 && receipts.front().GetStatus() == DispatchStatus::TargetExpired,
          "receiver deletion settles target delivery without entering its callback");
    Check(!receiverConnection.IsConnected() && !signal.HasConnections(),
          "expired receiver delivery retires the queued subscription instead of accumulating dead work");

    fixture.m_Receiver = std::make_unique<Receiver>();
    auto sender = std::make_unique<Signal<int>>();
    sender->Connect(fixture.Handle(), [&](Receiver&, int) { ++calls; });
    sender->Emit(4);
    sender.reset();
    fixture.Drain();
    Check(calls == 0, "sender destruction suppresses queued subscriptions that have not started");

    auto nestedSender = std::make_unique<Signal<int>>();
    nestedSender->Connect(fixture.Handle(), [&](Receiver&, int) { ++calls; });
    nestedSender->Connect(
        [&](int)
        {
            nestedSender.reset();
            fixture.Drain();
        });
    nestedSender->Connect([&](int) { calls += 10; });
    nestedSender->Emit(5);
    Check(calls == 10, "sender expiry suppresses queued work even while its original direct snapshot is active");
}

void ErrorsAndRejection()
{
    Fixture fixture(1);
    Signal<int> signal;
    int calls = 0;
    signal.Connect(fixture.Handle(), [&](Receiver&, int) { ++calls; });
    const auto filler = fixture.m_Dispatcher.Post([] {});
    bool overflow = false;
    try
    {
        signal.Emit(1);
    }
    catch (const std::runtime_error&)
    {
        overflow = true;
    }
    Check(overflow && calls == 0, "default queued connections report capacity rejection to their emitter");
    fixture.Drain();
    Check(filler.GetStatus() == DispatchStatus::Completed, "a rejected delivery leaves accepted work intact");

    Signal<int> observed;
    std::vector<DispatchReceipt> receipts;
    const auto emitter = std::this_thread::get_id();
    bool observedOnEmitter = false;
    observed.Connect(
        fixture.Handle(), [](Receiver&, int) { throw std::runtime_error("queued error"); },
        [&](DispatchReceipt receipt)
        {
            observedOnEmitter = std::this_thread::get_id() == emitter;
            receipts.push_back(std::move(receipt));
        });
    observed.Emit(2);
    fixture.Drain();
    Check(observedOnEmitter && receipts.back().GetStatus() == DispatchStatus::Failed && receipts.back().GetError(),
          "delivery observers receive receipts on the emitter while callback failures settle on the owner");

    fixture.m_Dispatcher.Post([] {});
    observed.Emit(3);
    Check(receipts.back().GetStatus() == DispatchStatus::Rejected &&
              receipts.back().GetRejectionReason() == DispatchRejectionReason::Capacity,
          "a receipt observer can handle bounded-queue rejection without throwing");
    fixture.m_Dispatcher.Close();
    const auto observedBeforeClosure = receipts.size();
    signal.Emit(4);
    observed.Emit(5);
    Check(receipts.size() == observedBeforeClosure && calls == 0,
          "normal endpoint shutdown suppresses delivery before allocating another post");
    Check(!signal.HasConnections() && !observed.HasConnections(),
          "closed endpoint delivery retires its queued subscriptions");
    Signal<> emptyTarget;
    emptyTarget.Connect(UiHandle<Receiver>{}, [&](Receiver&) { ++calls; });
    emptyTarget.Emit();
    Check(calls == 0, "an empty target is safely disconnected from delivery");
}

void ImmediateReceiverRetirement()
{
    Fixture fixture;
    Signal<int> signal;
    std::stop_source lifetime;
    auto target = UiHandle<Receiver>::ForTarget(
        fixture.m_Dispatcher, UiDispatcher::AllocateTargetKey(), [&] { return fixture.m_Receiver.get(); },
        lifetime.get_token());
    int calls = 0;
    auto captured = std::make_shared<int>(7);
    const std::weak_ptr<int> retained = captured;
    const auto connection = signal.Connect(target, [captured, &calls](Receiver&, int) { calls += *captured; });
    captured.reset();
    Check(connection.IsConnected(), "a new receiver-bound subscription is live");
    lifetime.request_stop();
    Check(!connection.IsConnected() && !signal.HasConnections(),
          "receiver shutdown logically retires a subscription without requiring a queued delivery");
    signal.Emit(1);
    Check(fixture.m_Dispatcher.GetStats().m_Accepted == 0 && calls == 0,
          "stopped receiver subscriptions do not enqueue dead work");
    connection.Disconnect();
    Check(retained.expired(), "explicit disconnection prunes captures even after a lifetime stop made them inactive");
}

void ConcurrentEndpointClosure()
{
    Fixture fixture;
    Signal<int> signal;
    int calls = 0;
    const auto connection = signal.Connect(fixture.Handle(), [&](Receiver&, int) { ++calls; });
    std::latch posted(1);
    std::latch closed(1);
    std::atomic<bool> workerFailed = false;
    std::jthread worker(
        [&]
        {
            try
            {
                signal.Emit(1);
                posted.count_down();
                closed.wait();
                for (int index = 0; index < 1000; ++index)
                    signal.Emit(index);
            }
            catch (...)
            {
                workerFailed.store(true);
            }
        });
    posted.wait();
    fixture.m_Dispatcher.Close();
    closed.count_down();
    worker.join();
    Check(!workerFailed.load() && !connection.IsConnected() && calls == 0,
          "worker cleanup can keep emitting while endpoint closure retires its queued subscriptions");
    Check(!fixture.m_Dispatcher.HasPending(), "endpoint closure retires previously accepted queued work");
}

void ConcurrentRegistry()
{
    Signal<int> signal;
    std::atomic<int> stableCalls = 0;
    std::atomic<int> temporaryCalls = 0;
    signal.Connect([&](int) { stableCalls.fetch_add(1, std::memory_order_relaxed); });
    constexpr int producerCount = 4;
    constexpr int emissionCount = 1000;
    std::vector<std::jthread> workers;
    for (int producer = 0; producer < producerCount; ++producer)
        workers.emplace_back(
            [&, producer]
            {
                for (int index = 0; index < emissionCount; ++index)
                    signal.Emit(producer);
            });
    for (int churn = 0; churn < 2; ++churn)
        workers.emplace_back(
            [&]
            {
                for (int index = 0; index < 500; ++index)
                {
                    const auto connection =
                        signal.Connect([&](int) { temporaryCalls.fetch_add(1, std::memory_order_relaxed); });
                    (void)signal.HasConnections();
                    connection.Disconnect();
                }
            });
    workers.clear();
    Check(stableCalls.load() == producerCount * emissionCount,
          "concurrent registry publication preserves a stable listener across all emissions");
    Check(signal.HasConnections(), "concurrent pruning retains active registrations");
    signal.DisconnectAll();
    signal.Emit(0);
    Check(!signal.HasConnections() && stableCalls.load() == producerCount * emissionCount,
          "disconnect-all retires the current concurrent snapshot");
}

void ConcurrentQueuedDelivery()
{
    Fixture fixture(2048);
    Signal<int, int> signal;
    signal.Connect(fixture.Handle(),
                   [](Receiver& receiver, int producer, int index)
                   {
                       receiver.m_Events.emplace_back(producer, index);
                       receiver.m_DeliveryThread = std::this_thread::get_id();
                   });
    constexpr int producerCount = 4;
    constexpr int emissionCount = 200;
    std::vector<std::jthread> workers;
    for (int producer = 0; producer < producerCount; ++producer)
        workers.emplace_back(
            [&, producer]
            {
                for (int index = 0; index < emissionCount; ++index)
                    signal.Emit(producer, index);
            });
    workers.clear();
    Check(fixture.m_Receiver->m_Events.empty(), "multiple producers never enter receiver code before owner delivery");
    fixture.Drain();
    std::array<int, producerCount> next{};
    bool ordered = true;
    for (const auto& [producer, index] : fixture.m_Receiver->m_Events)
        ordered &= index == next[static_cast<std::size_t>(producer)]++;
    Check(fixture.m_Receiver->m_Events.size() == producerCount * emissionCount && ordered,
          "multiple queued producers retain their own accepted command order");
    Check(fixture.m_Receiver->m_DeliveryThread == std::this_thread::get_id(),
          "all concurrent producer callbacks converge on the owner executor");
}

//! Registers another slot during captured-resource cleanup to detect registry locks held during destruction.
struct ReentrantCleanup
{
    //! Registers a no-op callback after counting one release.
    ~ReentrantCleanup()
    {
        ++*m_Releases;
        m_Signal->Connect([] {});
    }

    //! Observes the live test signal.
    Signal<>* m_Signal = nullptr;

    //! Observes a release counter owned by the test.
    int* m_Releases = nullptr;
};

void ReentrantResourceCleanup()
{
    Signal<> signal;
    int releases = 0;
    auto marker = std::make_shared<ReentrantCleanup>();
    marker->m_Signal = &signal;
    marker->m_Releases = &releases;
    const auto connection = signal.Connect([marker] {});
    marker.reset();
    connection.Disconnect();
    signal.Connect([] {});
    Check(releases == 1, "pruning destroys captured resources without holding the registry mutex");

    marker = std::make_shared<ReentrantCleanup>();
    marker->m_Signal = &signal;
    marker->m_Releases = &releases;
    signal.Connect([marker] {});
    marker.reset();
    signal.DisconnectAll();
    Check(releases == 2, "disconnect-all releases callback captures outside the registry mutex");
}
} // namespace

int main()
{
    try
    {
        DirectSnapshots();
        QueuedOwnershipAndOrdering();
        CancellationAndLifetime();
        ErrorsAndRejection();
        ImmediateReceiverRetirement();
        ConcurrentEndpointClosure();
        ConcurrentRegistry();
        ConcurrentQueuedDelivery();
        ReentrantResourceCleanup();
        std::cout << "Queued signals PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Queued signals FAIL / " << error.what() << '\n';
        return 1;
    }
}
