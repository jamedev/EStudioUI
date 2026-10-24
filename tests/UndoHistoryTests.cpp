#include <EStudioUI/UndoHistory.h>

#include <iostream>
#include <stdexcept>
#include <thread>

using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <typename Callback> void Throws(Callback callback, const char* message)
{
    bool failed = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    Check(failed, message);
}

void Timeline()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> value(store, 0,
                     [](int candidate)
                     {
                         if (candidate < 0 || candidate > 100)
                         {
                             throw std::invalid_argument("range");
                         }
                     });
    UndoHistory history(store);
    Check(!history.GetState().m_Modified && !history.Undo() && !history.Redo(), "Empty history starts clean");
    const auto initial = history.CaptureCheckpoint();
    int notifications = 0;
    history.Subscribe(
        [&](HistoryState info)
        {
            Check(info == history.GetState(), "Observers see canonical history metadata");
            ++notifications;
        });
    Check(history.Set(value, 1, "Set load"), "Set records a real value change");
    Check(value.Get() == 1 && history.GetState().m_UndoCount == 1 && history.GetState().m_Modified,
          "Values and undo metadata publish together");
    Check(!history.Set(value, 1) && notifications == 1, "Equality creates neither a command nor notification");
    const auto saved = history.CaptureCheckpoint();
    history.MarkSaved(saved);
    history.Set(value, 2, "Second load");
    Check(history.Undo() && value.Get() == 1 && !history.GetState().m_Modified, "Undo returns to the clean checkpoint");
    Check(history.Redo() && value.Get() == 2 && history.GetState().m_Modified, "Redo restores dirty state");
    history.MarkSaved(saved);
    Check(history.GetState().m_Modified, "An older asynchronous save cannot clear newer edits");
    const auto before = history.GetState();
    Throws([&] { history.Set(value, 500); }, "Validation rejects an entire document/history candidate");
    Check(value.Get() == 2 && history.GetState() == before, "Validation failure preserves value, cursor and labels");
    history.Undo();
    history.Set(value, 3, "Branch");
    Check(!history.Redo() && history.GetState().m_UndoText == "Branch", "Editing after undo discards the redo branch");
    history.MarkSaved(initial);
    Check(history.GetState().m_Modified, "Old checkpoints retain their original identity across branches");
    UndoHistory other(store);
    Throws([&] { other.MarkSaved(saved); }, "Checkpoints cannot be applied to another history");
    StateStore foreign(dispatcher);
    State<int> outside(foreign, 0);
    Throws([&] { history.Set(outside, 4); }, "A command cannot silently cross state domains");
    bool wrongThread = false;
    std::thread worker(
        [&]
        {
            try
            {
                history.Undo();
            }
            catch (const std::logic_error&)
            {
                wrongThread = true;
            }
        });
    worker.join();
    Check(wrongThread && value.Get() == 3, "Foreign-thread access rejects before mutation");
    const auto receipt = history.GetHandle().Post([](UndoHistory& live) { live.Undo(); });
    dispatcher.Drain();
    Check(receipt.GetStatus() == DispatchStatus::Completed && value.Get() == 1,
          "Worker endpoints resolve on the owner");
}

void Transactions()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> x(store, 0), y(store, 0);
    UndoHistory history(store);
    int notifications = 0;
    history.Subscribe([&](HistoryState) { ++notifications; });
    history.Group("Move vector",
                  [&]
                  {
                      history.Set(x, 10, "X");
                      history.Group("Y group", [&] { history.Set(y, 20, "Y"); });
                  });
    Check(x.Get() == 10 && y.Get() == 20 && history.GetState().m_UndoCount == 1 && notifications == 1,
          "Nested groups publish one history command and one notification");
    Check(history.Undo() && x.Get() == 0 && y.Get() == 0, "Grouped undo restores every canonical value");
    Check(history.Redo() && x.Get() == 10 && y.Get() == 20, "Grouped redo applies the original order");
    const auto before = history.GetState();
    Throws(
        [&]
        {
            history.Group("Rejected group",
                          [&]
                          {
                              history.Set(x, 30);
                              throw std::runtime_error("abort");
                          });
        },
        "A throwing group is discarded");
    Check(history.GetState() == before && x.Get() == 10, "Failed groups cannot leave partial data/history");
    bool undoFailure = true;
    history.Execute(
        "Guarded", [&] { x.Set(40); },
        [&]
        {
            x.Set(10);
            if (undoFailure)
            {
                throw std::runtime_error("undo failed");
            }
        });
    const auto failedUndo = history.GetState();
    Throws([&] { history.Undo(); }, "Undo callback failures are observable");
    Check(x.Get() == 40 && history.GetState() == failedUndo, "Failed undo rolls back document and timeline together");
    undoFailure = false;
    Check(history.Undo() && x.Get() == 10, "A failed undo can be retried after its cause is resolved");
    store.Batch(
        [&]
        {
            history.Set(x, 15);
            history.Set(y, 25);
        });
    Check(history.GetState().m_UndoCount == 3 && x.Get() == 15 && y.Get() == 25,
          "History participates in an application's existing state batch");
    const auto beforeOuterFailure = history.GetState();
    Throws(
        [&]
        {
            store.Batch(
                [&]
                {
                    history.Set(x, 35);
                    throw std::runtime_error("outer");
                });
        },
        "Outer rollback discards staged history too");
    Check(history.GetState() == beforeOuterFailure && x.Get() == 15,
          "No external history bookkeeping escapes rollback");
    history.Subscribe(
        [&](HistoryState state)
        {
            if (state.m_UndoText == "cascade")
            {
                history.Set(y, 60, "followup");
            }
        });
    history.Set(x, 50, "cascade");
    Check(x.Get() == 50 && y.Get() == 60 && history.GetState().m_UndoText == "followup",
          "Notification-triggered edits follow the shared bounded state publication rules");
}

void AppliedAndObserverErrors()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> value(store, 0);
    UndoHistory history(store);
    value.Set(5);
    history.RecordApplied("External edit", [&] { value.Set(5); }, [&] { value.Set(0); });
    Check(history.Undo() && value.Get() == 0 && history.Redo() && value.Get() == 5,
          "Already-applied integrations preserve real undo and redo bodies");
    auto failing = history.Subscribe([](HistoryState) { throw std::runtime_error("observer"); });
    int delivered = 0;
    history.Subscribe([&](HistoryState) { ++delivered; });
    Throws([&] { history.Set(value, 6); }, "Post-commit observer errors remain observable");
    Check(value.Get() == 6 && history.GetState().m_UndoCount == 2 && delivered == 1,
          "An observer failure cannot roll back committed data or starve other subscribers");
    failing.Disconnect();
    history.Undo();
    Check(value.Get() == 5, "Committed commands remain replayable after observer failure");
    Throws([&] { history.Set(value, 7, "Overflow", std::numeric_limits<std::size_t>::max()); },
           "Snapshot byte arithmetic rejects overflow before application mutation");
    Check(value.Get() == 5, "Byte-accounting overflow preserves canonical data");
}

void BudgetsAndLifetime()
{
    UiDispatcher dispatcher;
    StateStore store(dispatcher);
    State<int> value(store, 0);
    UndoHistory history(store, {2, 10000});
    history.Set(value, 1);
    history.Set(value, 2);
    history.Set(value, 3);
    Check(history.GetState().m_UndoCount == 2, "Command count prunes whole oldest entries");
    history.Undo();
    history.Undo();
    Check(value.Get() == 1 && !history.Undo() && history.GetState().m_Modified,
          "Pruning does not pretend the unreachable original value is clean");
    history.SetLimits({2, 256});
    history.Set(value, 9, "Large snapshot", 5000);
    Check(history.GetState().m_Bytes <= 256 && history.GetState().m_UndoCount == 0 && value.Get() == 9,
          "Oversized commands commit data but cannot exceed the retention budget");
    history.Clear();
    Check(history.GetState().m_Modified, "Clearing retention does not clear unsaved work");
    history.Clear(true);
    Check(!history.GetState().m_Modified, "An explicit reset can establish a new clean baseline");
    auto target = std::make_unique<State<int>>(store, 0);
    history.Set(*target, 5);
    target.reset();
    const auto retained = history.GetState();
    Throws([&] { history.Undo(); }, "Expired state receivers reject replay safely");
    Check(history.GetState() == retained, "Receiver expiry leaves the timeline coherent");
    auto removable = std::make_unique<UndoHistory>(store);
    const auto endpoint = removable->GetHandle();
    Throws(
        [&]
        {
            removable->Execute(
                "Remove owner",
                [&]
                {
                    value.Set(8);
                    removable.reset();
                },
                [&] { value.Set(9); });
        },
        "A command may delete its history owner without leaving a partial transaction");
    Check(value.Get() == 9 && !endpoint.IsConnected(), "Owner removal rolls back data and expires endpoints");
}
} // namespace

int main()
{
    try
    {
        Timeline();
        Transactions();
        BudgetsAndLifetime();
        AppliedAndObserverErrors();
        std::cout << "Undo history: " << s_Checks << " checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Undo history: " << error.what() << '\n';
        return 1;
    }
}
