#include <EStudioUI/UndoHistory.h>
#include <EStudioUI/TextBuffer.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Validate(HistoryLimits limits)
{
    if (!limits.m_MaxCommands || limits.m_MaxCommands > 4096 || limits.m_MaxBytes < 256)
    {
        throw std::invalid_argument("History limits require 1..4096 commands and at least 256 bytes.");
    }
}

void ValidateLabel(const std::string& label)
{
    if (label.empty() || label.size() > 1024 || TextBuffer::Sanitize(label) != label)
    {
        throw std::invalid_argument("An undo label must be single-line UTF-8 with 1..1024 bytes.");
    }
}
} // namespace

struct UndoHistory::Data : std::enable_shared_from_this<Data>
{
    struct Command
    {
        std::string m_Label;
        std::function<void()> m_Redo, m_Undo;
        std::size_t m_Bytes = 0;
        std::uint64_t m_Before = 0, m_After = 0;
    };

    struct Timeline
    {
        std::vector<std::shared_ptr<const Command>> m_Commands;
        std::size_t m_Cursor = 0;
        std::uint64_t m_Position = 1, m_Saved = 1;
        HistoryLimits m_Limits;

        bool operator==(const Timeline& other) const
        {
            return m_Commands == other.m_Commands && m_Cursor == other.m_Cursor && m_Position == other.m_Position &&
                   m_Saved == other.m_Saved && m_Limits.m_MaxCommands == other.m_Limits.m_MaxCommands &&
                   m_Limits.m_MaxBytes == other.m_Limits.m_MaxBytes;
        }
    };

    std::weak_ptr<Detail::StateStoreData> m_Domain;
    std::unique_ptr<State<Timeline>> m_Timeline;
    UndoHistory* m_Owner;
    UiDispatcher m_Dispatcher;
    std::uint64_t m_Identity = UiDispatcher::AllocateTargetKey(), m_NextPosition = 2;
    std::stop_source m_Lifetime;
    bool m_Invoking = false;
    unsigned m_GroupDepth = 0;

    Data(StateStore& store, UndoHistory* owner, HistoryLimits limits)
        : m_Domain(store.m_Data), m_Owner(owner), m_Dispatcher(store.GetDispatcher())
    {
        Timeline initial;
        initial.m_Limits = limits;
        m_Timeline = std::make_unique<State<Timeline>>(store, std::move(initial));
    }

    std::shared_ptr<Detail::StateStoreData> Domain() const
    {
        m_Dispatcher.VerifyAccess();
        auto domain = m_Domain.lock();
        if (!domain || !m_Owner)
        {
            throw std::logic_error("Undo history requires its live state domain and owner.");
        }
        Detail::VerifyStateAccess(domain);
        return domain;
    }

    std::size_t Bytes(const Timeline& timeline) const
    {
        std::size_t bytes = 0;
        for (const auto& command : timeline.m_Commands)
        {
            if (command->m_Bytes > std::numeric_limits<std::size_t>::max() - bytes)
            {
                throw std::overflow_error("Undo history byte accounting overflow.");
            }
            bytes += command->m_Bytes;
        }
        return bytes;
    }

    HistoryState Present(const Timeline& timeline) const
    {
        HistoryState result;
        result.m_UndoCount = timeline.m_Cursor;
        result.m_RedoCount = timeline.m_Commands.size() - timeline.m_Cursor;
        result.m_Bytes = Bytes(timeline);
        if (timeline.m_Cursor)
        {
            result.m_UndoText = timeline.m_Commands[timeline.m_Cursor - 1]->m_Label;
        }
        if (timeline.m_Cursor < timeline.m_Commands.size())
        {
            result.m_RedoText = timeline.m_Commands[timeline.m_Cursor]->m_Label;
        }
        result.m_Modified = timeline.m_Position != timeline.m_Saved;
        result.m_Checkpoint = {m_Identity, timeline.m_Position};
        return result;
    }

    void Prune(Timeline& timeline, bool finalGroup = false)
    {
        if (m_GroupDepth && !finalGroup)
        {
            if (timeline.m_Commands.size() > 4096)
            {
                throw std::length_error("One undo group supports at most 4096 retained commands.");
            }
            return;
        }
        auto bytes = Bytes(timeline);
        while (!timeline.m_Commands.empty() &&
               (timeline.m_Commands.size() > timeline.m_Limits.m_MaxCommands || bytes > timeline.m_Limits.m_MaxBytes))
        {
            // Retire the furthest redo first when there is no undo prefix left.
            if (!timeline.m_Cursor)
            {
                bytes -= timeline.m_Commands.back()->m_Bytes;
                timeline.m_Commands.pop_back();
            }
            else
            {
                bytes -= timeline.m_Commands.front()->m_Bytes;
                timeline.m_Commands.erase(timeline.m_Commands.begin());
                --timeline.m_Cursor;
            }
        }
    }

    template <typename Function> void Batch(Function function)
    {
        const auto domain = Domain();
        auto lease = Detail::AcquireStateLease(domain);
        Detail::StateUpdateScope update(domain);
        StateStore::BeginBatch(domain);
        try
        {
            function();
            if (!m_Owner)
            {
                throw std::logic_error("A command removed its history owner; the state transaction was discarded.");
            }
        }
        catch (...)
        {
            StateStore::RollbackBatch(domain);
            throw;
        }
        StateStore::CommitBatch(domain);
    }

    void Invoke(const std::function<void()>& callback)
    {
        if (m_Invoking)
        {
            throw std::logic_error("Undo command bodies cannot recursively manipulate their history.");
        }

        struct Scope
        {
            bool& m_Active;

            ~Scope()
            {
                m_Active = false;
            }
        } scope{m_Invoking};

        m_Invoking = true;
        callback();
    }
};

UndoHistory::UndoHistory(StateStore& store, HistoryLimits limits)
{
    Validate(limits);
    m_Data = std::make_shared<Data>(store, this, limits);
}

UndoHistory::~UndoHistory()
{
    if (!m_Data->m_Dispatcher.IsOwnerThread())
    {
        std::terminate();
    }
    m_Data->m_Owner = nullptr;
    m_Data->m_Lifetime.request_stop();
}

void UndoHistory::VerifyDomain(const std::shared_ptr<Detail::StateStoreData>& domain) const
{
    const auto data = m_Data;
    if (data->Domain() != domain)
    {
        throw std::invalid_argument("Undo values and history must belong to the same StateStore.");
    }
    if (data->m_Invoking)
    {
        throw std::logic_error("Undo command bodies cannot recursively manipulate their history.");
    }
}

void UndoHistory::Execute(std::string label, std::function<void()> redo, std::function<void()> undo,
                          std::size_t payloadBytes)
{
    if (!redo)
    {
        throw std::invalid_argument("An undo command requires a redo body.");
    }
    auto apply = [redo]
    {
        redo();
        return true;
    };
    (void)ApplyValue(std::move(label), std::move(apply), std::move(redo), std::move(undo), payloadBytes);
}

void UndoHistory::RecordApplied(std::string label, std::function<void()> redo, std::function<void()> undo,
                                std::size_t payloadBytes)
{
    (void)ApplyValue(std::move(label), [] { return true; }, std::move(redo), std::move(undo), payloadBytes);
}

bool UndoHistory::ApplyValue(std::string label, std::function<bool()> apply, std::function<void()> redo,
                             std::function<void()> undo, std::size_t payloadBytes)
{
    const auto data = m_Data;
    (void)data->Domain();
    ValidateLabel(label);
    if (!redo || !undo || data->m_Invoking)
    {
        throw std::invalid_argument("Undo commands require nonrecursive redo and undo bodies.");
    }
    if (payloadBytes > std::numeric_limits<std::size_t>::max() - sizeof(Data::Command) - label.size())
    {
        throw std::overflow_error("Undo payload byte accounting overflow.");
    }
    if (data->m_NextPosition == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Undo checkpoint identity space is exhausted.");
    }
    auto command = std::make_shared<Data::Command>();
    command->m_Label = std::move(label);
    command->m_Redo = std::move(redo);
    command->m_Undo = std::move(undo);
    command->m_Bytes = sizeof(Data::Command) + command->m_Label.size() + payloadBytes;
    command->m_After = data->m_NextPosition++;
    bool changed = false;
    data->Batch(
        [&]
        {
            data->Invoke([&] { changed = apply(); });
            if (!changed)
            {
                return;
            }
            auto candidate = data->m_Timeline->Get();
            command->m_Before = candidate.m_Position;
            candidate.m_Commands.resize(candidate.m_Cursor);
            candidate.m_Commands.push_back(command);
            ++candidate.m_Cursor;
            candidate.m_Position = command->m_After;
            data->Prune(candidate);
            data->m_Timeline->Set(std::move(candidate));
        });
    return changed;
}

void UndoHistory::Group(std::string label, std::function<void()> commands)
{
    const auto data = m_Data;
    (void)data->Domain();
    ValidateLabel(label);
    if (!commands || data->m_Invoking || data->m_GroupDepth >= 16)
    {
        throw std::logic_error("Undo groups require a callback and at most 16 nonrecursive levels.");
    }
    data->Batch(
        [&]
        {
            const auto before = data->m_Timeline->GetSnapshot();
            struct Scope
            {
                unsigned& m_Depth;
                ~Scope()
                {
                    --m_Depth;
                }
            } scope{data->m_GroupDepth};
            ++data->m_GroupDepth;
            commands();
            if (!data->m_Owner)
            {
                return;
            }
            auto candidate = data->m_Timeline->Get();
            if (candidate.m_Position == before->m_Position)
            {
                return;
            }
            std::vector<std::shared_ptr<const Data::Command>> children(
                candidate.m_Commands.begin() + static_cast<std::ptrdiff_t>(before->m_Cursor),
                candidate.m_Commands.end());
            auto group = std::make_shared<Data::Command>();
            group->m_Label = std::move(label);
            group->m_Before = before->m_Position;
            group->m_After = candidate.m_Position;
            group->m_Bytes = sizeof(Data::Command) + group->m_Label.size();
            for (const auto& child : children)
            {
                if (child->m_Bytes > std::numeric_limits<std::size_t>::max() - group->m_Bytes)
                {
                    throw std::overflow_error("Undo group byte accounting overflow.");
                }
                group->m_Bytes += child->m_Bytes;
            }
            group->m_Redo = [children]
            {
                for (const auto& child : children)
                {
                    child->m_Redo();
                }
            };
            group->m_Undo = [children]
            {
                for (auto child = children.rbegin(); child != children.rend(); ++child)
                {
                    (*child)->m_Undo();
                }
            };
            candidate.m_Commands.resize(before->m_Cursor);
            candidate.m_Commands.push_back(std::move(group));
            candidate.m_Cursor = candidate.m_Commands.size();
            data->Prune(candidate, data->m_GroupDepth == 1);
            data->m_Timeline->Set(std::move(candidate));
        });
}

bool UndoHistory::Undo()
{
    const auto data = m_Data;
    (void)data->Domain();
    if (data->m_GroupDepth || data->m_Invoking)
    {
        throw std::logic_error("Cannot replay history inside a command or open group.");
    }
    bool changed = false;
    data->Batch(
        [&]
        {
            auto candidate = data->m_Timeline->Get();
            if (!candidate.m_Cursor)
            {
                return;
            }
            const auto command = candidate.m_Commands[--candidate.m_Cursor];
            candidate.m_Position = command->m_Before;
            data->m_Timeline->Set(std::move(candidate));
            data->Invoke(command->m_Undo);
            changed = true;
        });
    return changed;
}

bool UndoHistory::Redo()
{
    const auto data = m_Data;
    (void)data->Domain();
    if (data->m_GroupDepth || data->m_Invoking)
    {
        throw std::logic_error("Cannot replay history inside a command or open group.");
    }
    bool changed = false;
    data->Batch(
        [&]
        {
            auto candidate = data->m_Timeline->Get();
            if (candidate.m_Cursor == candidate.m_Commands.size())
            {
                return;
            }
            const auto command = candidate.m_Commands[candidate.m_Cursor++];
            candidate.m_Position = command->m_After;
            data->m_Timeline->Set(std::move(candidate));
            data->Invoke(command->m_Redo);
            changed = true;
        });
    return changed;
}

void UndoHistory::Clear(bool markSaved)
{
    const auto data = m_Data;
    (void)data->Domain();
    if (data->m_GroupDepth || data->m_Invoking)
    {
        throw std::logic_error("Cannot clear history inside a command or open group.");
    }
    data->Batch(
        [&]
        {
            auto candidate = data->m_Timeline->Get();
            candidate.m_Commands.clear();
            candidate.m_Cursor = 0;
            if (markSaved)
            {
                candidate.m_Saved = candidate.m_Position;
            }
            data->m_Timeline->Set(std::move(candidate));
        });
}

void UndoHistory::SetLimits(HistoryLimits limits)
{
    Validate(limits);
    const auto data = m_Data;
    (void)data->Domain();
    if (data->m_GroupDepth || data->m_Invoking)
    {
        throw std::logic_error("Cannot change retention limits inside a command or open group.");
    }
    data->Batch(
        [&]
        {
            auto candidate = data->m_Timeline->Get();
            candidate.m_Limits = limits;
            data->Prune(candidate);
            data->m_Timeline->Set(std::move(candidate));
        });
}

HistoryLimits UndoHistory::GetLimits() const
{
    const auto data = m_Data;
    (void)data->Domain();
    return data->m_Timeline->Get().m_Limits;
}

HistoryState UndoHistory::GetState() const
{
    const auto data = m_Data;
    (void)data->Domain();
    return data->Present(data->m_Timeline->Get());
}

HistoryCheckpoint UndoHistory::CaptureCheckpoint() const
{
    return GetState().m_Checkpoint;
}

void UndoHistory::MarkSaved(std::optional<HistoryCheckpoint> checkpoint)
{
    const auto data = m_Data;
    (void)data->Domain();
    if (data->m_GroupDepth || data->m_Invoking)
    {
        throw std::logic_error("Cannot mark an uncommitted command group saved.");
    }
    data->Batch(
        [&]
        {
            auto candidate = data->m_Timeline->Get();
            const auto point = checkpoint.value_or(HistoryCheckpoint{data->m_Identity, candidate.m_Position});
            if (point.m_History != data->m_Identity || !point.m_Position || point.m_Position >= data->m_NextPosition)
            {
                throw std::invalid_argument("A saved checkpoint must have been captured from this history.");
            }
            candidate.m_Saved = point.m_Position;
            data->m_Timeline->Set(std::move(candidate));
        });
}

Connection UndoHistory::Subscribe(std::function<void(HistoryState)> callback)
{
    const auto data = m_Data;
    (void)data->Domain();
    if (!callback)
    {
        throw std::invalid_argument("History subscriptions require a callback.");
    }
    return data->m_Timeline->Subscribe(
        [weak = std::weak_ptr<Data>(data), callback = std::move(callback)](const StateChange<Data::Timeline>& change)
        {
            if (const auto live = weak.lock(); live && live->m_Owner)
            {
                callback(live->Present(change.GetAfter()));
            }
        });
}

UiHandle<UndoHistory> UndoHistory::GetHandle() const
{
    const auto data = m_Data;
    (void)data->Domain();
    return UiHandle<UndoHistory>::ForTarget(
        data->m_Dispatcher, data->m_Identity,
        [weak = std::weak_ptr<Data>(data)]() -> UndoHistory*
        {
            const auto live = weak.lock();
            return live && !live->m_Domain.expired() ? live->m_Owner : nullptr;
        },
        data->m_Lifetime.get_token());
}
} // namespace EStudioUI
