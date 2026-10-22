#include <EStudioUI/DispatchReceipt.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace EStudioUI
{
namespace
{
bool Terminal(DispatchStatus status)
{
    return status != DispatchStatus::Pending && status != DispatchStatus::Running;
}
} // namespace

struct DispatchReceipt::State
{
    std::atomic<DispatchStatus> m_Status{DispatchStatus::Pending};
    DispatchRejectionReason m_Rejection = DispatchRejectionReason::None;
    std::thread::id m_Owner;
    mutable std::mutex m_Mutex;
    std::condition_variable m_Changed;
    std::exception_ptr m_Error;
    std::exception_ptr m_ObserverError;
    std::vector<std::function<void(DispatchStatus, std::exception_ptr)>> m_Observers;

    void Notify(const std::function<void(DispatchStatus, std::exception_ptr)>& observer, DispatchStatus status,
                const std::exception_ptr& error) noexcept
    {
        try
        {
            observer(status, error);
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            std::lock_guard lock(m_Mutex);
            if (!m_ObserverError)
                m_ObserverError = failure;
        }
    }
};

DispatchReceipt DispatchReceipt::Pending(std::thread::id owner)
{
    DispatchReceipt receipt;
    receipt.m_State = std::make_shared<State>();
    receipt.m_State->m_Owner = owner;
    return receipt;
}

DispatchReceipt DispatchReceipt::Rejected(DispatchRejectionReason reason)
{
    auto receipt = Pending({});
    receipt.m_State->m_Rejection = reason;
    receipt.m_State->m_Status.store(DispatchStatus::Rejected, std::memory_order_release);
    return receipt;
}

DispatchStatus DispatchReceipt::GetStatus() const noexcept
{
    return m_State ? m_State->m_Status.load(std::memory_order_acquire) : DispatchStatus::Rejected;
}

DispatchRejectionReason DispatchReceipt::GetRejectionReason() const noexcept
{
    return m_State ? m_State->m_Rejection : DispatchRejectionReason::EmptyTarget;
}

bool DispatchReceipt::IsReady() const noexcept
{
    return Terminal(GetStatus());
}

bool DispatchReceipt::Start() const
{
    const auto state = m_State;
    if (!state)
        return false;
    std::lock_guard lock(state->m_Mutex);
    if (state->m_Status.load(std::memory_order_relaxed) != DispatchStatus::Pending)
        return false;
    state->m_Status.store(DispatchStatus::Running, std::memory_order_release);
    return true;
}

bool DispatchReceipt::Finish(DispatchStatus status, std::exception_ptr error) const noexcept
{
    const auto state = m_State;
    if (!state || !Terminal(status))
        return false;
    std::vector<std::function<void(DispatchStatus, std::exception_ptr)>> observers;
    {
        std::lock_guard lock(state->m_Mutex);
        const auto current = state->m_Status.load(std::memory_order_relaxed);
        if (Terminal(current) ||
            (current == DispatchStatus::Running && status != DispatchStatus::Completed &&
             status != DispatchStatus::TargetExpired && status != DispatchStatus::Failed))
            return false;
        state->m_Error = error;
        observers.swap(state->m_Observers);
        state->m_Status.store(status, std::memory_order_release);
    }
    state->m_Changed.notify_all();
    for (const auto& observer : observers)
        state->Notify(observer, status, error);
    return true;
}

bool DispatchReceipt::Cancel() const noexcept
{
    return Finish(DispatchStatus::Cancelled);
}

std::exception_ptr DispatchReceipt::GetError() const
{
    const auto state = m_State;
    if (!state)
        return {};
    std::lock_guard lock(state->m_Mutex);
    return state->m_Error;
}

std::exception_ptr DispatchReceipt::GetObserverError() const
{
    const auto state = m_State;
    if (!state)
        return {};
    std::lock_guard lock(state->m_Mutex);
    return state->m_ObserverError;
}

void DispatchReceipt::RethrowIfFailed() const
{
    if (const auto error = GetError())
        std::rethrow_exception(error);
}

bool DispatchReceipt::WaitFor(std::chrono::milliseconds timeout) const
{
    if (timeout < std::chrono::milliseconds::zero())
        throw std::invalid_argument("A dispatch wait timeout must be nonnegative.");
    const auto state = m_State;
    if (!state || Terminal(state->m_Status.load(std::memory_order_acquire)))
        return true;
    if (std::this_thread::get_id() == state->m_Owner)
        throw std::logic_error("The UI owner cannot wait for its own pending dispatch; use a continuation.");
    std::unique_lock lock(state->m_Mutex);
    return state->m_Changed.wait_for(lock, timeout,
                                    [&] { return Terminal(state->m_Status.load(std::memory_order_acquire)); });
}

void DispatchReceipt::OnFinished(std::function<void(DispatchStatus, std::exception_ptr)> callback) const
{
    if (!callback)
        throw std::invalid_argument("A dispatch observer must be callable.");
    const auto state = m_State;
    if (!state)
    {
        try
        {
            callback(DispatchStatus::Rejected, {});
        }
        catch (...)
        {
            // Empty receipts have no storage; observers still cannot escape this notification boundary.
        }
        return;
    }
    DispatchStatus status;
    std::exception_ptr error;
    {
        std::lock_guard lock(state->m_Mutex);
        status = state->m_Status.load(std::memory_order_acquire);
        if (!Terminal(status))
        {
            state->m_Observers.push_back(std::move(callback));
            return;
        }
        error = state->m_Error;
    }
    state->Notify(callback, status, error);
}
} // namespace EStudioUI
