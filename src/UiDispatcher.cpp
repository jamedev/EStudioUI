#include <EStudioUI/UiDispatcher.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace EStudioUI
{
namespace
{
std::atomic<std::uint64_t> s_NextTarget{1};
} // namespace

struct UiDispatcher::State
{
    struct Work
    {
        DispatchReceipt m_Receipt;
        std::function<DispatchStatus()> m_Callback;
        std::optional<PublicationKey> m_Key;
        std::chrono::steady_clock::time_point m_Posted;
        std::uint64_t m_Sequence = 0;
    };

    const std::thread::id m_Owner = std::this_thread::get_id();
    const std::size_t m_Capacity;
    std::atomic<bool> m_Open{true};
    std::stop_source m_StopSource;
    std::atomic<std::size_t> m_Pending{0};
    mutable std::mutex m_QueueMutex;
    std::list<Work> m_Queue;
    std::map<PublicationKey, std::list<Work>::iterator> m_Publications;
    std::uint64_t m_NextSequence = 1;
    bool m_Draining = false;
    std::function<void()> m_DrainGuard;
    std::weak_ptr<void> m_OwnerLifetime;
    mutable std::mutex m_WakeMutex;
    std::function<void()> m_Wakeup;
    std::exception_ptr m_WakeupError;
    mutable std::mutex m_StatsMutex;
    DispatchStats m_Stats;

    explicit State(std::size_t capacity) : m_Capacity(capacity)
    {
    }

    ~State()
    {
        m_StopSource.request_stop();
        for (auto& work : m_Queue)
            work.m_Receipt.Cancel();
    }

    void Settled(DispatchStatus status)
    {
        std::lock_guard lock(m_StatsMutex);
        --m_Stats.m_Pending;
        m_Pending.store(m_Stats.m_Pending, std::memory_order_release);
        switch (status)
        {
        case DispatchStatus::Completed:
            ++m_Stats.m_Completed;
            break;
        case DispatchStatus::Cancelled:
            ++m_Stats.m_Cancelled;
            break;
        case DispatchStatus::Superseded:
            ++m_Stats.m_Superseded;
            break;
        case DispatchStatus::TargetExpired:
            ++m_Stats.m_TargetExpired;
            break;
        case DispatchStatus::Failed:
            ++m_Stats.m_Failed;
            break;
        default:
            break;
        }
    }

    void Wake() noexcept
    {
        // The adapter callback is deliberately nonreentrant. Keeping this separate
        // from queue locking lets Close quiesce native wakeup before backend teardown.
        std::lock_guard lock(m_WakeMutex);
        if (!m_Open.load(std::memory_order_acquire) || !m_Wakeup)
            return;
        try
        {
            m_Wakeup();
        }
        catch (...)
        {
            m_WakeupError = std::current_exception();
            std::lock_guard statsLock(m_StatsMutex);
            ++m_Stats.m_WakeupFailures;
        }
    }
};

UiDispatcher::UiDispatcher(std::size_t capacity)
{
    if (!capacity)
        throw std::invalid_argument("A UI dispatcher requires a positive queue capacity.");
    m_State = std::make_shared<State>(capacity);
}

std::uint64_t UiDispatcher::AllocateTargetKey()
{
    auto next = s_NextTarget.load(std::memory_order_relaxed);
    for (;;)
    {
        if (next == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("UI target identity space is exhausted.");
        if (s_NextTarget.compare_exchange_weak(next, next + 1, std::memory_order_relaxed))
            return next;
    }
}

DispatchReceipt UiDispatcher::Post(std::function<void()> callback) const
{
    if (!callback)
        throw std::invalid_argument("A UI dispatch callback must be callable.");
    return PostWork([callback = std::move(callback)]
                    {
                        callback();
                        return DispatchStatus::Completed;
                    });
}

DispatchReceipt UiDispatcher::PostLatest(std::string key, std::function<void()> callback) const
{
    if (key.empty() || !callback)
        throw std::invalid_argument("A coalesced UI publication requires a nonempty key and callback.");
    return PostWork(
        [callback = std::move(callback)]
        {
            callback();
            return DispatchStatus::Completed;
        },
        PublicationKey{0, std::move(key)});
}

DispatchReceipt UiDispatcher::PostWork(std::function<DispatchStatus()> callback,
                                     std::optional<PublicationKey> key) const
{
    const auto state = m_State;
    const auto receipt = DispatchReceipt::Pending(state->m_Owner);
    std::list<State::Work> fresh, retired;
    fresh.push_back({receipt, std::move(callback), std::move(key), std::chrono::steady_clock::now(), 0});
    DispatchReceipt superseded;
    DispatchRejectionReason rejection = DispatchRejectionReason::None;
    bool shouldWake = false;
    {
        std::lock_guard lock(state->m_QueueMutex);
        if (!state->m_Open.load(std::memory_order_acquire))
            rejection = DispatchRejectionReason::Closed;
        else
        {
            if (state->m_Queue.size() >= state->m_Capacity)
            {
                for (auto iterator = state->m_Queue.begin(); iterator != state->m_Queue.end();)
                {
                    const auto current = iterator++;
                    if (!current->m_Receipt.IsReady())
                        continue;
                    if (current->m_Key)
                        state->m_Publications.erase(*current->m_Key);
                    retired.splice(retired.end(), state->m_Queue, current);
                }
            }
            auto previous = fresh.front().m_Key ? state->m_Publications.find(*fresh.front().m_Key)
                                               : state->m_Publications.end();
            if (state->m_Queue.size() >= state->m_Capacity && previous == state->m_Publications.end())
                rejection = DispatchRejectionReason::Capacity;
            else
            {
                if (state->m_NextSequence == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("UI submission sequence space is exhausted.");
                shouldWake = state->m_Queue.empty();
                auto next = fresh.begin();
                if (previous != state->m_Publications.end())
                {
                    superseded = previous->second->m_Receipt;
                    retired.splice(retired.end(), state->m_Queue, previous->second);
                    previous->second = next;
                }
                else if (next->m_Key)
                    state->m_Publications.emplace(*next->m_Key, next);
                next->m_Sequence = state->m_NextSequence++;
                state->m_Queue.splice(state->m_Queue.end(), fresh, next);
                std::lock_guard statsLock(state->m_StatsMutex);
                ++state->m_Stats.m_Accepted;
                ++state->m_Stats.m_Pending;
                state->m_Pending.store(state->m_Stats.m_Pending, std::memory_order_release);
                state->m_Stats.m_HighWaterMark = std::max(state->m_Stats.m_HighWaterMark, state->m_Queue.size());
            }
        }
        if (rejection != DispatchRejectionReason::None)
        {
            std::lock_guard statsLock(state->m_StatsMutex);
            ++state->m_Stats.m_Rejected;
        }
    }
    if (rejection != DispatchRejectionReason::None)
        return DispatchReceipt::Rejected(rejection);
    receipt.OnFinished([weak = std::weak_ptr<State>(state)](DispatchStatus status, std::exception_ptr)
                       {
                           if (const auto live = weak.lock())
                               live->Settled(status);
                       });
    superseded.Finish(DispatchStatus::Superseded);
    if (shouldWake)
        state->Wake();
    return receipt;
}

std::size_t UiDispatcher::Drain(std::size_t maxCount, std::chrono::steady_clock::duration maxDuration)
{
    VerifyAccess();
    const auto state = m_State;
    // Keep the owning context alive until callbacks, retired work, and the
    // draining flag have all unwound, even when a callback deletes its window.
    const auto ownerLifetime = state->m_OwnerLifetime.lock();
    if (state->m_Draining)
        throw std::logic_error("A UI dispatcher cannot recursively drain its own queue.");
    if (maxDuration < std::chrono::steady_clock::duration::zero())
        throw std::invalid_argument("A dispatch drain duration must be nonnegative.");
    if (!state->m_Open.load(std::memory_order_acquire) || !maxCount ||
        maxDuration == std::chrono::steady_clock::duration::zero())
        return 0;
    struct Scope
    {
        bool& m_Draining;

        explicit Scope(bool& draining) : m_Draining(draining)
        {
            m_Draining = true;
        }

        ~Scope()
        {
            m_Draining = false;
        }
    } scope(state->m_Draining);
    if (const auto guard = state->m_DrainGuard)
        guard();
    std::uint64_t boundary;
    {
        std::lock_guard lock(state->m_QueueMutex);
        boundary = state->m_NextSequence - 1;
    }
    const auto started = std::chrono::steady_clock::now();
    std::size_t processed = 0;
    while (processed < maxCount && std::chrono::steady_clock::now() - started < maxDuration)
    {
        std::list<State::Work> current;
        {
            std::lock_guard lock(state->m_QueueMutex);
            if (state->m_Queue.empty() || state->m_Queue.front().m_Sequence > boundary)
                break;
            const auto next = state->m_Queue.begin();
            if (next->m_Key)
                state->m_Publications.erase(*next->m_Key);
            current.splice(current.end(), state->m_Queue, next);
        }
        ++processed;
        auto& work = current.front();
        if (!work.m_Receipt.Start())
            continue;
        {
            const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - work.m_Posted);
            std::lock_guard statsLock(state->m_StatsMutex);
            ++state->m_Stats.m_Started;
            state->m_Stats.m_MaxLatency = std::max(state->m_Stats.m_MaxLatency, latency);
        }
        try
        {
            const auto outcome = work.m_Callback();
            if (outcome != DispatchStatus::Completed && outcome != DispatchStatus::TargetExpired)
                throw std::logic_error("A UI dispatch callback returned an invalid delivery outcome.");
            work.m_Receipt.Finish(outcome);
        }
        catch (...)
        {
            work.m_Receipt.Finish(DispatchStatus::Failed, std::current_exception());
        }
    }
    return processed;
}

bool UiDispatcher::HasPending() const noexcept
{
    return m_State->m_Pending.load(std::memory_order_acquire) != 0;
}

DispatchStats UiDispatcher::GetStats() const
{
    const auto state = m_State;
    std::lock_guard lock(state->m_StatsMutex);
    return state->m_Stats;
}

bool UiDispatcher::IsOpen() const noexcept
{
    return m_State->m_Open.load(std::memory_order_acquire);
}

bool UiDispatcher::IsClosed() const noexcept
{
    return !IsOpen();
}

std::stop_token UiDispatcher::GetStopToken() const noexcept
{
    return m_State->m_StopSource.get_token();
}

bool UiDispatcher::IsOwnerThread() const noexcept
{
    return std::this_thread::get_id() == m_State->m_Owner;
}

void UiDispatcher::VerifyAccess() const
{
    if (!IsOwnerThread())
        throw std::logic_error("This UI dispatcher operation requires its owning thread; post work instead.");
}

void UiDispatcher::SetWakeup(std::function<void()> callback)
{
    VerifyAccess();
    const auto state = m_State;
    std::function<void()> retired;
    {
        std::lock_guard lock(state->m_WakeMutex);
        if (!state->m_Open.load(std::memory_order_acquire))
            throw std::logic_error("A closed UI dispatcher cannot install a native wake callback.");
        retired.swap(state->m_Wakeup);
        state->m_Wakeup = std::move(callback);
    }
    if (HasPending())
        state->Wake();
}

void UiDispatcher::SetDrainGuard(std::function<void()> callback)
{
    VerifyAccess();
    if (!IsOpen())
        throw std::logic_error("A closed UI dispatcher cannot install a drain guard.");
    m_State->m_DrainGuard = std::move(callback);
}

void UiDispatcher::SetOwnerLifetime(std::weak_ptr<void> owner)
{
    VerifyAccess();
    if (!IsOpen())
        throw std::logic_error("A closed UI dispatcher cannot associate an owner lifetime.");
    m_State->m_OwnerLifetime = std::move(owner);
}

std::exception_ptr UiDispatcher::GetWakeupError() const
{
    const auto state = m_State;
    std::lock_guard lock(state->m_WakeMutex);
    return state->m_WakeupError;
}

void UiDispatcher::Close()
{
    VerifyAccess();
    const auto state = m_State;
    std::list<State::Work> retired;
    {
        std::lock_guard lock(state->m_QueueMutex);
        state->m_Open.store(false, std::memory_order_release);
        retired.splice(retired.end(), state->m_Queue);
        state->m_Publications.clear();
    }
    state->m_DrainGuard = {};
    std::function<void()> wake;
    {
        std::lock_guard lock(state->m_WakeMutex);
        wake.swap(state->m_Wakeup);
    }
    state->m_StopSource.request_stop();
    for (auto& work : retired)
        work.m_Receipt.Finish(DispatchStatus::Cancelled);
}
} // namespace EStudioUI
