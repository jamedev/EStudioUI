#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/State.h>

#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Error = std::exception, typename Callback> bool Rejects(Callback&& callback)
{
    try
    {
        callback();
        return false;
    }
    catch (const Error&)
    {
        return true;
    }
}

template <typename Callback> bool RejectsWorker(Callback&& callback)
{
    bool rejected = false;
    std::exception_ptr unexpected;
    std::thread worker(
        [&]
        {
            try
            {
                rejected = Rejects<std::logic_error>(callback);
            }
            catch (...)
            {
                unexpected = std::current_exception();
            }
        });
    worker.join();
    if (unexpected)
        std::rethrow_exception(unexpected);
    return rejected;
}

void ValuesAndValidation()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    Check(store.GetRevision() == 0 && store.GetMaxNotificationRounds() == 64, "store defaults are usable");
    Check(Rejects<std::invalid_argument>([&] { StateStore invalid(dispatcher, 0); }), "feedback limit cannot be zero");
    State<int> count(store, 2, [](int value)
                     {
                         if (value < 0)
                             throw std::invalid_argument("negative count");
                     });
    std::vector<StateChange<int>> changes;
    const auto connection = count.Subscribe([&](const StateChange<int>& change) { changes.push_back(change); });
    const auto original = count.GetSnapshot();
    Check(count.Get() == 2 && count.GetRevision() == 0 && *original == 2, "state owns its initial value");
    Check(!count.Set(2) && changes.empty() && store.GetRevision() == 0, "equal writes are silent no-ops");
    Check(count.Set(3) && count.Get() == 3 && store.GetRevision() == 1, "direct owner writes publish synchronously");
    Check(changes.size() == 1 && changes[0].GetBefore() == 2 && changes[0].GetAfter() == 3 &&
              changes[0].GetRevision() == 1 && count.GetRevision() == 1,
          "notifications own before/after snapshots and the commit revision");
    Check(*original == 2 && *changes[0].GetAfterSnapshot() == 3, "owned snapshots survive later writes");
    Check(Rejects<std::invalid_argument>([&] { count.Set(-1); }) && count.Get() == 3 && changes.size() == 1 &&
              store.GetRevision() == 1,
          "validation rejects before value, revision, or notifications change");
    const auto extra = count.AddValidator([](int value)
                                         {
                                             if (value > 4)
                                                 throw std::invalid_argument("too large");
                                         });
    Check(Rejects<std::invalid_argument>([&] { count.Set(5); }) && count.Get() == 3,
          "dynamic validators participate before commit");
    extra.Disconnect();
    Check(count.Set(5), "disconnecting a validator removes its constraint");
    connection.Disconnect();
    count.Set(6);
    Check(changes.size() == 2 && !connection.IsConnected(), "connections explicitly disconnect");
    {
        ScopedConnection scoped(count.Subscribe([&](const StateChange<int>&) { changes.push_back(changes[0]); }));
        count.Set(7);
    }
    count.Set(8);
    Check(changes.size() == 3, "scoped connections disconnect at scope exit");
    Check(Rejects<std::invalid_argument>([&] { State<int> invalid(store, -1, [](int value)
                                                                  {
                                                                      if (value < 0)
                                                                          throw std::invalid_argument("initial");
                                                                  }); }),
          "initial values are validated before registration");
    dispatcher.Close();
}

struct OpaqueValue
{
    int m_Number;
};

void OwnedValuesAndEquality()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<OpaqueValue> custom(store, OpaqueValue{1}, {},
                              [](const OpaqueValue& left, const OpaqueValue& right)
                              { return left.m_Number == right.m_Number; });
    Check(!custom.Set(OpaqueValue{1}) && custom.Set(OpaqueValue{2}) && custom.Get().m_Number == 2,
          "custom equality supports application types without operator==");
    State<std::unique_ptr<int>> owned(store, std::make_unique<int>(1));
    const auto old = owned.GetSnapshot();
    int before = 0;
    int after = 0;
    owned.Subscribe([&](const StateChange<std::unique_ptr<int>>& change)
                    {
                        before = *change.GetBefore();
                        after = *change.GetAfter();
                    });
    owned.Set(std::make_unique<int>(2));
    Check(**old == 1 && before == 1 && after == 2 && *owned.Get() == 2,
          "immutable change snapshots broadcast move-only owned values without copying the value");
    bool throwEquality = false;
    State<int> unstable(store, 0, {}, [&](int left, int right)
                         {
                             if (throwEquality)
                                 throw std::runtime_error("equality");
                             return left == right;
                         });
    Check(Rejects<std::runtime_error>([&]
          { store.Batch([&] { custom.Set(OpaqueValue{3}); unstable.Set(1); throwEquality = true; }); }) &&
              custom.Get().m_Number == 2 && unstable.Get() == 0,
          "throwing commit equality rolls back already prepared values");
    dispatcher.Close();
}

void CoherentTransactions()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> lower(store, 0);
    State<int> upper(store, 10);
    lower.AddValidator([&](int value)
                       {
                           if (value > upper.Get())
                               throw std::invalid_argument("lower exceeds upper");
                       });
    upper.AddValidator([&](int value)
                       {
                           if (value < lower.Get())
                               throw std::invalid_argument("upper below lower");
                       });
    std::vector<std::string> order;
    lower.Subscribe([&](const StateChange<int>& change)
                    {
                        Check(lower.Get() == 20 && upper.Get() == 30, "first observer sees every committed value");
                        Check(change.GetRevision() == store.GetRevision(), "observer sees coherent revision");
                        order.push_back("lower");
                    });
    upper.Subscribe([&](const StateChange<int>&)
                    {
                        Check(lower.Get() == 20 && upper.Get() == 30, "last observer sees the same coherent values");
                        order.push_back("upper");
                    });
    store.Batch([&]
                {
                    lower.Set(20);
                    Check(lower.Get() == 20 && upper.Get() == 10 && order.empty(),
                          "explicit transaction reads its candidates without notifying");
                    lower.Set(21);
                    lower.Set(20);
                    upper.Set(30);
                });
    Check(order == std::vector<std::string>({"lower", "upper"}) && store.GetRevision() == 1,
          "final candidates validate together and notify once in first-touch order");
    Check(Rejects<std::invalid_argument>([&] { store.Batch([&] { lower.Set(100); upper.Set(40); }); }) &&
              lower.Get() == 20 && upper.Get() == 30 && order.size() == 2 && store.GetRevision() == 1,
          "one invalid candidate rolls back every value before all notifications");
    store.Batch([&] { lower.Set(21); lower.Set(20); });
    Check(order.size() == 2 && store.GetRevision() == 1, "returning to the original value is a final no-op");
    dispatcher.Close();
}

void SavepointsAndRollback()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> a(store, 0);
    State<int> b(store, 0);
    int published = 0;
    a.Subscribe([&](const StateChange<int>&) { ++published; });
    std::vector<std::string> restored;
    a.SubscribeReconcile([&](int value)
                        {
                            Check(a.Get() == value && b.Get() == 0, "rollback pops all candidates before observers");
                            restored.push_back("a:" + std::to_string(value));
                        });
    b.SubscribeReconcile([&](int value)
                        {
                            Check(value == 0 && b.Get() == 0, "rollback restores each affected value");
                            restored.push_back("b");
                        });
    store.Batch([&]
                {
                    a.Set(1);
                    Check(Rejects<std::runtime_error>([&]
                          { store.Batch([&] { a.Set(2); b.Set(2); throw std::runtime_error("body"); }); }),
                          "nested body failure propagates");
                    Check(a.Get() == 1 && b.Get() == 0 && published == 0, "nested rollback preserves outer candidates");
                    store.Batch([&] { a.Set(3); });
                    Check(a.Get() == 3, "successful nested batch merges into its parent");
                });
    Check(a.Get() == 3 && b.Get() == 0 && published == 1 && store.GetRevision() == 1 &&
              restored == std::vector<std::string>({"a:1", "b"}),
          "outer commit publishes once after deterministic nested reconciliation");
    Check(Rejects<std::runtime_error>([&]
          { store.Batch([&] { a.Set(4); b.Set(4); throw std::runtime_error("outer"); }); }) &&
              a.Get() == 3 && b.Get() == 0 && published == 1 && restored.back() == "b",
          "outer body failure restores every value without a change notification");
    const auto throwing = a.SubscribeReconcile([](int) { throw std::runtime_error("observer"); });
    bool following = false;
    a.SubscribeReconcile([&](int)
                        {
                            following = true;
                            Check(Rejects<std::logic_error>([&] { b.Set(20); }), "rollback callbacks cannot mutate state");
                        });
    bool original = false;
    try
    {
        store.Batch([&] { a.Set(5); throw std::invalid_argument("original"); });
    }
    catch (const std::invalid_argument& error)
    {
        original = std::string(error.what()) == "original";
    }
    Check(original && following && store.GetReconciliationError() && a.Get() == 3 && b.Get() == 0,
          "reconciliation errors stay inspectable and do not mask the original failure");
    throwing.Disconnect();
    dispatcher.Close();
}

void NetNoopReconciliation()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> a(store, 0);
    State<int> b(store, 0);
    int changes = 0;
    std::vector<std::string> order;
    a.Subscribe([&](const StateChange<int>&) { ++changes; });
    a.SubscribeReconcile([&](int restored)
                         {
                             Check(restored == 0 && a.Get() == 0 && b.Get() == 1,
                                   "successful no-op reconciliation sees every canonical committed value");
                             Check(Rejects<std::logic_error>([&] { a.Set(2); }),
                                   "net-no-op reconciliation is read-only");
                             order.push_back("reconcile");
                         });
    b.Subscribe([&](const StateChange<int>&) { order.push_back("changed"); });
    store.Batch([&] { a.Set(1); a.Set(0); b.Set(1); });
    Check(changes == 0 && store.GetRevision() == 1 && order == std::vector<std::string>({"reconcile", "changed"}),
          "unchanged candidates reconcile even in a mixed changed/no-op commit without duplicate change events");
    State<int> only(store, 0);
    int reconciliations = 0;
    only.SubscribeReconcile([&](int restored)
                            {
                                Check(restored == 0, "pure net-no-op reconciliation restores canonical value");
                                ++reconciliations;
                            });
    store.Batch([&] { only.Set(1); only.Set(0); });
    Check(reconciliations == 1 && store.GetRevision() == 1,
          "pure net-no-op batches reconcile once without advancing revision");
    State<int> trigger(store, 0);
    trigger.Subscribe([&](const StateChange<int>&) { only.Set(1); });
    trigger.Subscribe([&](const StateChange<int>&) { store.Batch([&] { only.Set(0); }); });
    trigger.Set(1);
    Check(reconciliations == 2 && only.Get() == 0 && store.GetRevision() == 2,
          "discarded reentrant no-op candidates reconcile eager views after the current round");
    dispatcher.Close();
}

void ReentrantRounds()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> a(store, 0);
    State<int> b(store, 0);
    std::vector<std::string> order;
    a.Subscribe([&](const StateChange<int>& change)
                {
                    order.push_back("first:" + std::to_string(change.GetAfter()));
                    if (change.GetAfter() == 1)
                    {
                        a.Set(2);
                        b.Set(2);
                        Check(a.Get() == 1 && b.Get() == 1, "implicit callback writes remain invisible this round");
                    }
                });
    a.Subscribe([&](const StateChange<int>& change)
                {
                    Check(a.Get() == change.GetAfter() && b.Get() == change.GetAfter(),
                          "later observers retain the same coherent round");
                    order.push_back("second:" + std::to_string(change.GetAfter()));
                });
    b.Subscribe([&](const StateChange<int>& change)
                {
                    Check(a.Get() == change.GetAfter(), "other changed values retain current-round state");
                    order.push_back("b:" + std::to_string(change.GetAfter()));
                });
    store.Batch([&] { a.Set(1); b.Set(1); });
    Check(order == std::vector<std::string>({"first:1", "second:1", "b:1", "first:2", "second:2", "b:2"}) &&
              a.Get() == 2 && b.Get() == 2 && store.GetRevision() == 2,
          "reentrant edits merge into one next-round coherent transaction");

    State<int> explicitValue(store, 0);
    explicitValue.Subscribe([&](const StateChange<int>& change)
                            {
                                if (change.GetAfter() != 1)
                                    return;
                                store.Batch([&]
                                            {
                                                explicitValue.Set(2);
                                                Check(explicitValue.Get() == 2, "explicit reentrant batch reads its own candidates");
                                            });
                                Check(explicitValue.Get() == 1, "leaving explicit batch restores this round's observer view");
                            });
    explicitValue.Set(1);
    Check(explicitValue.Get() == 2, "explicit reentrant batch commits after the current round");

    State<int> overwritten(store, 0);
    State<int> trigger(store, 0);
    trigger.Subscribe([&](const StateChange<int>&) { overwritten.Set(1); });
    trigger.Subscribe([&](const StateChange<int>&)
                      {
                          store.Batch([&]
                                      {
                                          Check(overwritten.Get() == 0, "explicit reads retain the current notification round");
                                          store.Batch([&] { overwritten.Set(0); });
                                      });
                      });
    const auto beforeOverwrite = store.GetRevision();
    trigger.Set(1);
    Check(overwritten.Get() == 0 && store.GetRevision() == beforeOverwrite + 1,
          "later nested explicit writes override earlier deferred candidates even when equal to committed state");

    StateStore bounded(dispatcher, 4);
    State<int> loop(bounded, 0);
    int observed = 0;
    loop.Subscribe([&](const StateChange<int>& change) { ++observed; loop.Set(change.GetAfter() + 1); });
    int rollbackValue = 0;
    loop.SubscribeReconcile([&](int restored) { rollbackValue = restored; });
    std::string diagnostic;
    try
    {
        loop.Set(1);
    }
    catch (const std::logic_error& error)
    {
        diagnostic = error.what();
    }
    Check(observed == 4 && loop.Get() == 4 && rollbackValue == 4 && bounded.GetRevision() == 4 &&
              diagnostic.find("feedback") != std::string::npos,
          "feedback stops deterministically at its configured round bound before the next commit");
    dispatcher.Close();
}

void FailureIsolation()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> a(store, 0);
    State<int> b(store, 0);
    bool sameValueObserver = false;
    bool otherValueObserver = false;
    a.Subscribe([](const StateChange<int>&) { throw std::runtime_error("listener"); });
    a.Subscribe([&](const StateChange<int>&) { sameValueObserver = true; });
    b.Subscribe([&](const StateChange<int>&) { otherValueObserver = true; });
    Check(Rejects<std::runtime_error>([&] { store.Batch([&] { a.Set(1); b.Set(1); }); }) && a.Get() == 1 &&
              b.Get() == 1 && sameValueObserver && otherValueObserver,
          "observer failures preserve the commit and do not skip remaining observers");

    State<int> guarded(store, 0);
    guarded.AddValidator([&](int value)
                         {
                             if (value == 1)
                                 b.Set(99);
                         });
    Check(Rejects<std::logic_error>([&] { guarded.Set(1); }) && guarded.Get() == 0 && b.Get() == 1,
          "validators cannot mutate other values before commit");
    State<int> initialGuard(store, 0);
    Check(Rejects<std::logic_error>([&] { State<int> invalid(store, 1, [&](int) { initialGuard.Set(2); }); }) &&
              initialGuard.Get() == 0,
          "initial validation cannot mutate an existing value");
    State<int> late(store, 0, [](int value)
                    {
                        if (value == 2)
                            throw std::invalid_argument("deferred");
                    });
    late.Subscribe([&](const StateChange<int>& change)
                   {
                       if (change.GetAfter() == 1)
                       {
                           late.Set(2);
                           initialGuard.Set(2);
                       }
                   });
    Check(Rejects<std::invalid_argument>([&] { late.Set(1); }) && late.Get() == 1 && initialGuard.Get() == 0,
          "invalid reentrant candidates roll back the complete next round");
    auto expiring = std::make_unique<State<int>>(store, 0);
    State<int> killer(store, 0);
    killer.AddValidator([&](int candidate)
                        {
                            if (candidate == 1)
                                expiring.reset();
                        });
    const auto revision = store.GetRevision();
    Check(Rejects<std::logic_error>([&] { store.Batch([&] { expiring->Set(1); killer.Set(1); }); }) &&
              !expiring && killer.Get() == 0 && store.GetRevision() == revision,
          "validation that destroys an already prepared value cannot publish a partial revision");
    dispatcher.Close();
}

struct Receiver
{
    int m_Value = 0;
    std::vector<int> m_History;
};

void QueuedPublicationAndReceivers()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<std::string> text(store, "initial");
    const auto handle = text.GetHandle();
    DispatchReceipt queued;
    std::thread worker([&]
                       {
                           queued = handle.Post([](State<std::string>& live) { live.Set(std::string("owned")); });
                       });
    worker.join();
    Check(text.Get() == "initial" && queued.GetStatus() == DispatchStatus::Pending,
          "worker publication stays queued until its owner boundary");
    dispatcher.Drain();
    Check(text.Get() == "owned" && queued.GetStatus() == DispatchStatus::Completed,
          "owned worker publication updates on the owner");
    const auto stale = handle.PostLatest("progress", [](State<std::string>& live) { live.Set("stale"); });
    const auto latest = handle.PostLatest("progress", [](State<std::string>& live) { live.Set("latest"); });
    Check(stale.GetStatus() == DispatchStatus::Superseded, "replaceable state publications expose supersession");
    dispatcher.Drain();
    Check(text.Get() == "latest" && latest.GetStatus() == DispatchStatus::Completed,
          "latest publication coalesces without implicit direct delivery");

    State<int> value(store, 0);
    Receiver receiver;
    std::stop_source lifetime;
    auto target = UiHandle<Receiver>::ForTarget(dispatcher, UiDispatcher::AllocateTargetKey(), [&] { return &receiver; },
                                                lifetime.get_token());
    std::vector<DispatchReceipt> receipts;
    auto connection = value.Subscribe(target,
                                      [](Receiver& live, const StateChange<int>& change)
                                      {
                                          live.m_Value = change.GetAfter();
                                          live.m_History.push_back(change.GetAfter());
                                      },
                                      [&](DispatchReceipt receipt) { receipts.push_back(std::move(receipt)); });
    value.Set(1);
    value.Set(2);
    Check(receiver.m_Value == 0 && receipts.size() == 2, "receiver subscriptions always queue owned change snapshots");
    dispatcher.Drain();
    Check(receiver.m_History == std::vector<int>({1, 2}), "queued receivers retain each historical committed value");
    value.Set(3);
    connection.Disconnect();
    dispatcher.Drain();
    Check(receiver.m_Value == 2 && !connection.IsConnected(), "disconnection suppresses pending receiver delivery");
    auto expiring = value.Subscribe(target, [](Receiver& live, const StateChange<int>& change) { live.m_Value = change.GetAfter(); });
    value.Set(4);
    lifetime.request_stop();
    dispatcher.Drain();
    Check(receiver.m_Value == 2 && !expiring.IsConnected(), "receiver expiry disconnects and suppresses queued callbacks");

    UiDispatcher small(1);
    StateStore smallStore(small);
    State<int> constrained(smallStore, 0);
    Receiver busy;
    const auto busyTarget = UiHandle<Receiver>::ForTarget(small, UiDispatcher::AllocateTargetKey(), [&] { return &busy; });
    constrained.Subscribe(busyTarget, [](Receiver&, const StateChange<int>&) {});
    bool afterRejected = false;
    constrained.Subscribe([&](const StateChange<int>&) { afterRejected = true; });
    const auto occupier = small.Post([] {});
    Check(Rejects<std::runtime_error>([&] { constrained.Set(1); }) && constrained.Get() == 1 && afterRejected,
          "queue overload reports a committed notification error and still runs later direct observers");
    Check(occupier.Cancel(), "capacity test retains observable queued work");
    small.Close();
    dispatcher.Close();
}

void LifetimeAndAffinity()
{
    UiDispatcher dispatcher;
    auto store = std::make_unique<StateStore>(dispatcher);
    auto value = std::make_unique<State<int>>(*store, 0);
    const auto stateHandle = value->GetHandle();
    auto connection = value->Subscribe([](const StateChange<int>&) {});
    const auto pending = stateHandle.Post([](State<int>& live) { live.Set(1); });
    const auto snapshot = value->GetSnapshot();
    Check(RejectsWorker([&] { value->Set(1); }) && value->Get() == 0, "foreign direct writes reject before mutation");
    Check(RejectsWorker([&] { (void)value->Get(); }) && RejectsWorker([&] { (void)value->GetSnapshot(); }),
          "foreign direct reads cannot observe live owner state");
    Check(RejectsWorker([&] { (void)value->GetHandle(); }) && RejectsWorker([&] { value->Subscribe([](const StateChange<int>&) {}); }),
          "workers capture handles and subscriptions before leaving the owner");
    Check(RejectsWorker([&] { store->Batch([] {}); }) && RejectsWorker([&] { (void)store->GetRevision(); }),
          "store entry points enforce owner access");
    bool ownedRead = false;
    std::thread snapshotReader([&] { ownedRead = *snapshot == 0; });
    snapshotReader.join();
    Check(ownedRead, "immutable owned snapshots can be read on workers");
    value.reset();
    dispatcher.Drain();
    Check(stateHandle.GetTargetStopToken().stop_requested() && !connection.IsConnected() &&
              pending.GetStatus() == DispatchStatus::TargetExpired,
          "value destruction expires pending publications and subscriptions");
    value = std::make_unique<State<int>>(*store, 0);
    const auto replacement = value->GetHandle();
    Check(replacement.GetTargetKey() != stateHandle.GetTargetKey(), "replacement values cannot inherit expired endpoint identity");
    const auto storePending = replacement.Post([](State<int>& live) { live.Set(2); });
    auto storeConnection = value->Subscribe([](const StateChange<int>&) {});
    store.reset();
    dispatcher.Drain();
    Check(dispatcher.IsOpen() && replacement.GetTargetStopToken().stop_requested() && !storeConnection.IsConnected() &&
              storePending.GetStatus() == DispatchStatus::TargetExpired &&
              Rejects<std::logic_error>([&] { (void)value->Get(); }),
          "store destruction expires values and handles without closing its shared dispatcher");
    value.reset();

    StateStore selfStore(dispatcher);
    auto self = std::make_unique<State<int>>(selfStore, 0);
    auto selfConnection = self->Subscribe([&](const StateChange<int>&) { self.reset(); });
    State<int>* active = self.get();
    active->Set(1);
    Check(!self && !selfConnection.IsConnected(), "an observer may destroy its sender without invalidating dispatch");
    auto disposable = std::make_unique<StateStore>(dispatcher);
    State<int> survives(*disposable, 0);
    survives.Subscribe([&](const StateChange<int>&) { disposable.reset(); });
    survives.Set(1);
    Check(!disposable && Rejects<std::logic_error>([&] { (void)survives.Get(); }),
          "an observer may close its store while internal notification state remains alive");
    dispatcher.Close();
}

void NotificationInspection()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher), unrelated(dispatcher);
    State<int> value(store, 0);
    Check(!store.IsNotifying(), "notification inspection starts idle");
    int rounds = 0;
    value.Subscribe(
        [&](const StateChange<int>& change)
        {
            ++rounds;
            Check(store.IsNotifying() && !unrelated.IsNotifying(),
                  "notification inspection is owner-domain specific across every observer round");
            if (change.GetAfter() == 1)
            {
                store.Batch(
                    [&]
                    {
                        Check(store.IsNotifying(), "an explicit observer batch retains notification context");
                        value.Set(2);
                    });
            }
        });
    store.Batch(
        [&]
        {
            Check(!store.IsNotifying(), "ordinary explicit batches do not report observer dispatch");
            value.Set(1);
        });
    Check(rounds == 2 && !store.IsNotifying(), "notification inspection returns idle after bounded feedback");
    auto failing = value.Subscribe([](const StateChange<int>&) { throw std::runtime_error("observer"); });
    Check(Rejects<std::runtime_error>([&] { value.Set(3); }) && !store.IsNotifying(),
          "observer failure cannot leave notification inspection active");
    Check(RejectsWorker([&] { (void)store.IsNotifying(); }),
          "notification inspection rejects foreign-thread live-state access");
}

void TaskIntegration()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<std::string> status(store, "waiting");
    std::vector<std::function<void()>> work;
    BackgroundTasks tasks([&](std::function<void()> callback)
                          {
                              work.push_back(std::move(callback));
                              return true;
                          });
    auto task = tasks.Run(status.GetHandle(), [] { return std::string("completed"); },
                          [](State<std::string>& live, std::string value) { live.Set(std::move(value)); });
    Check(work.size() == 1 && status.Get() == "waiting", "state handles use the existing natural task completion API");
    std::thread worker(std::move(work.back()));
    work.pop_back();
    worker.join();
    Check(status.Get() == "waiting" && task.GetStatus() == TaskStatus::AwaitingUi,
          "task computation never mutates its owner state directly");
    dispatcher.Drain();
    Check(status.Get() == "completed" && task.GetStatus() == TaskStatus::Completed,
          "task completion publishes its owned result to live typed state");
    auto transient = std::make_unique<State<int>>(store, 0);
    const auto expired = tasks.Run(transient->GetHandle(), [] { return 1; }, [](State<int>& live, int value) { live.Set(value); });
    transient.reset();
    Check(expired.GetStatus() == TaskStatus::TargetExpired, "state expiry settles target-bound tasks promptly");
    for (auto& callback : work)
        callback();
    dispatcher.Close();
}
} // namespace

int main()
{
    try
    {
        ValuesAndValidation();
        OwnedValuesAndEquality();
        CoherentTransactions();
        SavepointsAndRollback();
        NetNoopReconciliation();
        ReentrantRounds();
        FailureIsolation();
        QueuedPublicationAndReceivers();
        LifetimeAndAffinity();
        NotificationInspection();
        TaskIntegration();
        std::cout << "Typed state and transaction PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Typed state and transaction FAIL / " << error.what() << '\n';
        return 1;
    }
}
