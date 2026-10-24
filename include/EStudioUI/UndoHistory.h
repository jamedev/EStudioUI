#pragma once

#include "State.h"
#include <limits>

namespace EStudioUI
{
//! Identifies the exact historical value captured by an asynchronous save.
struct HistoryCheckpoint
{
    //! Lifetime-unique history identity.
    std::uint64_t m_History = 0;
    //! Non-reused value position, independent of undo-stack pruning.
    std::uint64_t m_Position = 0;
    //! Compares owned checkpoint identities.
    bool operator==(const HistoryCheckpoint&) const = default;
};

//! Bounds retained commands; callers account for snapshot payload bytes.
struct HistoryLimits
{
    //! Maximum number of undo/redo commands retained together.
    std::size_t m_MaxCommands = 128;
    //! Maximum estimated command/payload bytes retained together.
    std::size_t m_MaxBytes = 64 * 1024 * 1024;
};

//! Owned presentation of one coherent undo/redo timeline.
struct HistoryState
{
    //! Counts available commands on each side of the cursor.
    std::size_t m_UndoCount = 0, m_RedoCount = 0;
    //! Estimated retained command and application payload bytes.
    std::size_t m_Bytes = 0;
    //! Describes the next undo and redo operations.
    std::string m_UndoText, m_RedoText;
    //! Reports whether the current position differs from the saved checkpoint.
    bool m_Modified = false;
    //! Identifies the current immutable value position.
    HistoryCheckpoint m_Checkpoint;
    //! Compares presentation values for no-op suppression.
    bool operator==(const HistoryState&) const = default;
};

//! Reusable transactional commands and bounded grouping over an existing StateStore.
class UndoHistory
{
  public:
    //! Creates an initially clean history without initializing widgets, windows or GL.
    explicit UndoHistory(StateStore& store, HistoryLimits limits = {});
    //! Expires callbacks and producer handles before releasing retained commands.
    ~UndoHistory();
    //! Histories retain stable identities and cannot duplicate subscriptions.
    UndoHistory(const UndoHistory&) = delete;
    //! Histories retain stable identities and cannot duplicate subscriptions.
    UndoHistory& operator=(const UndoHistory&) = delete;

    //! Runs redo and records one command in the same state transaction.
    void Execute(std::string label, std::function<void()> redo, std::function<void()> undo,
                 std::size_t payloadBytes = 0);
    //! Bridges an already committed external edit; prefer Set/Execute for atomic value/history publication.
    void RecordApplied(std::string label, std::function<void()> redo, std::function<void()> undo,
                       std::size_t payloadBytes = 0);
    //! Executes a bounded, optionally nested command group as one atomic undo step.
    void Group(std::string label, std::function<void()> commands);
    //! Undoes one command atomically; returns false when no command is available.
    bool Undo();
    //! Redoes one command atomically; returns false when no command is available.
    bool Redo();
    //! Clears retained commands, preserving dirty state unless explicitly marked saved.
    void Clear(bool markSaved = false);
    //! Changes budgets and prunes whole oldest commands without changing document values.
    void SetLimits(HistoryLimits limits);
    //! Returns current retention limits.
    HistoryLimits GetLimits() const;
    //! Returns an owned, coherent presentation snapshot.
    HistoryState GetState() const;
    //! Captures a value position for a subsequent asynchronous save.
    HistoryCheckpoint CaptureCheckpoint() const;
    //! Marks the current or explicitly captured value saved without clearing newer edits.
    void MarkSaved(std::optional<HistoryCheckpoint> checkpoint = {});
    //! Subscribes to coherent owned changes; one observer error cannot starve other subscribers.
    Connection Subscribe(std::function<void(HistoryState)> callback);
    //! Captures a receiver-bound endpoint for owner-side commands from workers.
    UiHandle<UndoHistory> GetHandle() const;

    //! Replaces one canonical value with a snapshot-backed command; equality is a no-op.
    template <typename T> bool Set(State<T>& state, T value, std::string label = "Edit", std::size_t payloadBytes = 0)
    {
        VerifyDomain(state.m_Node->LockStore());
        if (payloadBytes > std::numeric_limits<std::size_t>::max() - sizeof(T) * 2)
        {
            throw std::overflow_error("Undo snapshot byte accounting overflow.");
        }
        auto before = state.GetSnapshot();
        auto after = std::make_shared<const T>(std::move(value));
        const auto target = state.GetHandle();
        const auto apply = [target, receiver = &state](std::shared_ptr<const T> snapshot)
        {
            target.GetDispatcher().VerifyAccess();
            if (!target.IsConnected())
            {
                throw std::logic_error("An undo command's state receiver has expired.");
            }
            return receiver->Set(*snapshot);
        };
        return ApplyValue(
            std::move(label), [apply, after] { return apply(after); }, [apply, after] { apply(after); },
            [apply, before] { apply(before); }, payloadBytes + sizeof(T) * 2);
    }

  private:
    //! Records a preallocated command only when canonical State reports an actual change.
    bool ApplyValue(std::string label, std::function<bool()> apply, std::function<void()> redo,
                    std::function<void()> undo, std::size_t payloadBytes);
    //! Rejects mixed state domains before command application.
    void VerifyDomain(const std::shared_ptr<Detail::StateStoreData>& domain) const;
    //! Keeps the timeline alive through callbacks that remove its public owner.
    struct Data;
    //! Owns canonical history values and bounded receiver metadata.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
