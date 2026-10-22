#include <EStudioUI/StateStore.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace EStudioUI
{
namespace
{
thread_local std::size_t s_StateUpdateDepth = 0;
}

namespace Detail
{
class StateStoreData
{
  public:
    struct Edit
    {
        std::shared_ptr<StateNode> m_Node;
        std::shared_ptr<const void> m_Value;
    };

    struct Notification
    {
        std::shared_ptr<StateNode> m_Node;
        std::shared_ptr<const void> m_Before;
        std::shared_ptr<const void> m_After;
    };

    using Frame = std::vector<Edit>;

    StateStoreData(UiDispatcher dispatcher, std::size_t maxRounds)
        : m_Dispatcher(std::move(dispatcher)), m_MaxRounds(maxRounds)
    {
    }

    void VerifyAccess() const
    {
        m_Dispatcher.VerifyAccess();
        if (m_Closed)
            throw std::logic_error("State access requires its live owning StateStore.");
    }

    void VerifyWritable() const
    {
        VerifyAccess();
        if (m_AccessGuard)
            m_AccessGuard();
        if (m_Preparing)
            throw std::logic_error("State mutation is forbidden during state validation or equality comparison.");
        if (m_Reconciling)
            throw std::logic_error("State mutation is forbidden during discarded-candidate reconciliation.");
    }

    std::shared_ptr<const void> Read(const std::shared_ptr<StateNode>& node) const
    {
        VerifyAccess();
        if (!node->IsAlive())
            throw std::logic_error("State access requires a live State value.");
        for (auto frame = m_Frames.rbegin(); frame != m_Frames.rend(); ++frame)
            for (const auto& edit : *frame)
                if (edit.m_Node == node)
                    return edit.m_Value;
        // Implicit edits made by observers belong to the next round, not to other
        // observers in this round. Explicit Batch reads its own staged savepoints.
        return node->ReadCommitted();
    }

    std::shared_ptr<const void> ReadCandidate(const std::shared_ptr<StateNode>& node) const
    {
        for (auto frame = m_Frames.rbegin(); frame != m_Frames.rend(); ++frame)
            for (const auto& edit : *frame)
                if (edit.m_Node == node)
                    return edit.m_Value;
        if (m_Notifying)
            for (const auto& edit : m_NextFrame)
                if (edit.m_Node == node)
                    return edit.m_Value;
        return node->ReadCommitted();
    }

    static void Merge(Frame& destination, const Frame& source)
    {
        Frame prepared = destination;
        prepared.reserve(prepared.size() + source.size());
        for (const auto& edit : source)
        {
            const auto existing = std::find_if(prepared.begin(), prepared.end(),
                                               [&](const Edit& current) { return current.m_Node == edit.m_Node; });
            if (existing == prepared.end())
                prepared.push_back(edit);
            else
                existing->m_Value = edit.m_Value;
        }
        destination = std::move(prepared);
    }

    bool Stage(Frame& frame, const std::shared_ptr<StateNode>& node, std::shared_ptr<const void> value)
    {
        const auto existing = std::find_if(frame.begin(), frame.end(),
                                           [&](const Edit& edit) { return edit.m_Node == node; });
        const auto previous = existing == frame.end() ? ReadCandidate(node) : existing->m_Value;
        m_Preparing = true;
        bool equal = false;
        try
        {
            equal = node->Equal(previous, value);
        }
        catch (...)
        {
            m_Preparing = false;
            throw;
        }
        m_Preparing = false;
        if (m_Closed || !node->IsAlive())
            throw std::logic_error("A State or its StateStore expired during equality comparison.");
        if (equal)
            return false;
        if (existing == frame.end())
            frame.push_back({node, std::move(value)});
        else
            existing->m_Value = std::move(value);
        return true;
    }

    std::vector<Notification> PrepareAndPublish(Frame& reconciliation)
    {
        auto& frame = m_Frames.back();
        std::vector<Notification> changes;
        changes.reserve(frame.size());
        reconciliation.reserve(frame.size());
        m_Preparing = true;
        try
        {
            // All candidates are visible while every validator runs. Equality,
            // validation and allocations finish before the no-throw swap phase.
            for (const auto& edit : frame)
            {
                if (!edit.m_Node->IsAlive())
                    continue;
                edit.m_Node->Validate(edit.m_Value);
                if (m_Closed || !edit.m_Node->IsAlive())
                    throw std::logic_error("A State or its StateStore expired during validation.");
                const auto before = edit.m_Node->ReadCommitted();
                const bool equal = edit.m_Node->Equal(before, edit.m_Value);
                if (m_Closed || !edit.m_Node->IsAlive())
                    throw std::logic_error("A State or its StateStore expired during equality comparison.");
                if (!equal)
                    changes.push_back({edit.m_Node, before, edit.m_Value});
                else
                    reconciliation.push_back({edit.m_Node, before});
            }
            if (m_Closed || std::any_of(changes.begin(), changes.end(),
                                       [](const auto& change) { return !change.m_Node->IsAlive(); }) ||
                std::any_of(reconciliation.begin(), reconciliation.end(),
                            [](const auto& edit) { return !edit.m_Node->IsAlive(); }))
                throw std::logic_error("A State or its StateStore expired during validation or equality comparison.");
        }
        catch (...)
        {
            m_Preparing = false;
            throw;
        }
        m_Preparing = false;
        if (changes.empty())
            return changes;
        if (m_Revision == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("StateStore revision counter exhausted.");
        ++m_Revision;
        for (const auto& change : changes)
            change.m_Node->Publish(change.m_After, m_Revision);
        return changes;
    }

    void RememberError(std::exception_ptr error) noexcept
    {
        if (!m_Error)
            m_Error = std::move(error);
    }

    void Notify(std::vector<Notification> changes)
    {
        if (changes.empty())
            return;
        m_Notifying = true;
        m_Error = {};
        std::size_t rounds = 0;
        while (!changes.empty() && !m_Closed)
        {
            ++rounds;
            const auto revision = m_Revision;
            for (const auto& change : changes)
            {
                if (m_Closed)
                    break;
                try
                {
                    change.m_Node->Notify(change.m_Before, change.m_After, revision);
                }
                catch (...)
                {
                    RememberError(std::current_exception());
                }
            }
            changes.clear();
            if (m_Closed || m_NextFrame.empty())
                break;
            if (rounds >= m_MaxRounds)
            {
                try
                {
                    m_Frames.push_back(std::move(m_NextFrame));
                    RollbackBatch();
                    throw std::logic_error("StateStore feedback exceeded " + std::to_string(m_MaxRounds) +
                                           " notification rounds; deferred edits were rolled back.");
                }
                catch (...)
                {
                    RememberError(std::current_exception());
                }
                break;
            }
            try
            {
                m_Frames.push_back(std::move(m_NextFrame));
                Frame reconciliation;
                changes = PrepareAndPublish(reconciliation);
                m_Frames.pop_back();
                Reconcile(std::move(reconciliation));
            }
            catch (...)
            {
                RollbackBatch();
                m_NextFrame.clear();
                RememberError(std::current_exception());
                break;
            }
        }
        m_NextFrame.clear();
        m_Notifying = false;
        const auto error = std::exchange(m_Error, {});
        if (error)
            std::rethrow_exception(error);
    }

    void Reconcile(Frame affected) noexcept
    {
        if (affected.empty())
            return;
        m_ReconciliationError = {};
        m_Reconciling = true;
        for (const auto& edit : affected)
        {
            if (m_Closed || !edit.m_Node->IsAlive())
                continue;
            try
            {
                edit.m_Node->NotifyReconcile(Read(edit.m_Node));
            }
            catch (...)
            {
                if (!m_ReconciliationError)
                    m_ReconciliationError = std::current_exception();
            }
        }
        m_Reconciling = false;
    }

    void RollbackBatch() noexcept
    {
        if (m_Frames.empty())
            return;
        auto affected = std::move(m_Frames.back());
        m_Frames.pop_back();
        Reconcile(std::move(affected));
    }

    void CommitBatch()
    {
        std::vector<Notification> changes;
        Frame reconciliation;
        try
        {
            VerifyWritable();
            if (m_Frames.size() > 1)
            {
                Merge(m_Frames[m_Frames.size() - 2], m_Frames.back());
                m_Frames.pop_back();
                return;
            }
            if (m_Notifying)
            {
                Merge(m_NextFrame, m_Frames.back());
                m_Frames.pop_back();
                return;
            }
            changes = PrepareAndPublish(reconciliation);
            m_Frames.pop_back();
        }
        catch (...)
        {
            RollbackBatch();
            throw;
        }
        Reconcile(std::move(reconciliation));
        // Observers see committed values; their errors cannot undo a published revision.
        Notify(std::move(changes));
    }

    UiDispatcher m_Dispatcher;
    std::function<void()> m_AccessGuard;
    std::function<std::shared_ptr<void>()> m_OwnerLease;
    std::vector<std::weak_ptr<StateNode>> m_Nodes;
    std::vector<Frame> m_Frames;
    Frame m_NextFrame;
    std::exception_ptr m_Error;
    std::exception_ptr m_ReconciliationError;
    std::uint64_t m_Revision = 0;
    std::size_t m_MaxRounds;
    std::size_t m_PruneAt = 64;
    bool m_Closed = false;
    bool m_Preparing = false;
    bool m_Notifying = false;
    bool m_Reconciling = false;
};

StateUpdateScope::StateUpdateScope(const std::shared_ptr<StateStoreData>& data)
{
    data->VerifyWritable();
    ++s_StateUpdateDepth;
}

StateUpdateScope::~StateUpdateScope()
{
    --s_StateUpdateDepth;
}

void VerifyStateAccess(const std::shared_ptr<StateStoreData>& data)
{
    data->VerifyAccess();
}

void VerifyStateMutationAccess(const std::shared_ptr<StateStoreData>& data)
{
    data->VerifyWritable();
}

std::shared_ptr<void> AcquireStateLease(const std::shared_ptr<StateStoreData>& data)
{
    data->VerifyAccess();
    return data->m_OwnerLease ? data->m_OwnerLease() : std::shared_ptr<void>{};
}

UiDispatcher GetStateDispatcher(const std::shared_ptr<StateStoreData>& data)
{
    data->VerifyAccess();
    return data->m_Dispatcher;
}

void ValidateInitialState(const std::shared_ptr<StateStoreData>& data, const std::shared_ptr<StateNode>& node)
{
    data->VerifyWritable();
    data->m_Preparing = true;
    try
    {
        node->Validate(node->ReadCommitted());
    }
    catch (...)
    {
        data->m_Preparing = false;
        throw;
    }
    data->m_Preparing = false;
}

void RegisterState(const std::shared_ptr<StateStoreData>& data, const std::shared_ptr<StateNode>& node)
{
    data->VerifyWritable();
    if (data->m_Nodes.size() >= data->m_PruneAt)
    {
        data->m_Nodes.erase(std::remove_if(data->m_Nodes.begin(), data->m_Nodes.end(),
                                         [](const auto& current) { return current.expired(); }),
                            data->m_Nodes.end());
        const auto maximum = std::numeric_limits<std::size_t>::max();
        data->m_PruneAt = std::max(std::size_t{64}, data->m_Nodes.size() > maximum / 2 ? maximum
                                                                                 : data->m_Nodes.size() * 2);
    }
    data->m_Nodes.push_back(node);
}

std::shared_ptr<const void> ReadState(const std::shared_ptr<StateStoreData>& data,
                                    const std::shared_ptr<StateNode>& node)
{
    return data->Read(node);
}

bool WriteState(const std::shared_ptr<StateStoreData>& data, const std::shared_ptr<StateNode>& node,
                std::shared_ptr<const void> value)
{
    data->VerifyWritable();
    if (!node->IsAlive())
        throw std::logic_error("State mutation requires a live State value.");
    if (!data->m_Frames.empty())
        return data->Stage(data->m_Frames.back(), node, std::move(value));
    if (data->m_Notifying)
        return data->Stage(data->m_NextFrame, node, std::move(value));
    data->m_Frames.emplace_back();
    bool changed = false;
    try
    {
        changed = data->Stage(data->m_Frames.back(), node, std::move(value));
    }
    catch (...)
    {
        data->RollbackBatch();
        throw;
    }
    data->CommitBatch();
    return changed;
}

void RecordStateError(const std::weak_ptr<StateStoreData>& data, std::exception_ptr error) noexcept
{
    if (const auto current = data.lock())
        current->RememberError(std::move(error));
}
void RecordStateReconciliationError(const std::weak_ptr<StateStoreData>& data, std::exception_ptr error) noexcept
{
    if (const auto current = data.lock())
        if (!current->m_ReconciliationError)
            current->m_ReconciliationError = std::move(error);
}
} // namespace Detail

StateStore::StateStore(UiDispatcher dispatcher, std::size_t maxNotificationRounds)
{
    dispatcher.VerifyAccess();
    if (!maxNotificationRounds)
        throw std::invalid_argument("StateStore requires a positive notification-round limit.");
    m_Data = std::make_shared<Detail::StateStoreData>(std::move(dispatcher), maxNotificationRounds);
}

StateStore::~StateStore()
{
    const auto data = m_Data;
    if (!data->m_Dispatcher.IsOwnerThread())
        std::terminate();
    data->m_Closed = true;
    data->m_AccessGuard = {};
    data->m_OwnerLease = {};
    for (const auto& weak : data->m_Nodes)
        if (const auto node = weak.lock())
            node->Expire();
}

std::uint64_t StateStore::GetRevision() const
{
    VerifyAccess();
    return m_Data->m_Revision;
}

std::exception_ptr StateStore::GetReconciliationError() const
{
    VerifyAccess();
    return m_Data->m_ReconciliationError;
}

UiDispatcher StateStore::GetDispatcher() const
{
    return Detail::GetStateDispatcher(m_Data);
}

void StateStore::VerifyAccess() const
{
    Detail::VerifyStateAccess(m_Data);
}

std::size_t StateStore::GetMaxNotificationRounds() const
{
    VerifyAccess();
    return m_Data->m_MaxRounds;
}

void StateStore::BeginBatch(const std::shared_ptr<Detail::StateStoreData>& data)
{
    data->VerifyWritable();
    data->m_Frames.emplace_back();
}

void StateStore::CommitBatch(const std::shared_ptr<Detail::StateStoreData>& data)
{
    data->CommitBatch();
}

void StateStore::RollbackBatch(const std::shared_ptr<Detail::StateStoreData>& data) noexcept
{
    data->RollbackBatch();
}

bool StateStore::IsUpdatingOnThisThread() noexcept
{
    return s_StateUpdateDepth != 0;
}

void StateStore::SetAccessGuard(std::function<void()> callback)
{
    VerifyAccess();
    m_Data->m_AccessGuard = std::move(callback);
}

void StateStore::SetOwnerLease(std::function<std::shared_ptr<void>()> callback)
{
    VerifyAccess();
    m_Data->m_OwnerLease = std::move(callback);
}
} // namespace EStudioUI
