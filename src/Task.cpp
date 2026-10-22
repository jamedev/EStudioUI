#include <EStudioUI/Task.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace EStudioUI
{
namespace
{
bool Terminal(TaskStatus status)
{
    return status == TaskStatus::Completed || status == TaskStatus::Cancelled || status == TaskStatus::Superseded ||
           status == TaskStatus::TargetExpired || status == TaskStatus::Rejected || status == TaskStatus::Failed;
}
} // namespace

struct Task::State
{
    mutable std::mutex m_Mutex;
    TaskStatus m_Status = TaskStatus::Queued;
    std::stop_source m_Stop;
    RequestToken m_Request;
    std::exception_ptr m_Error;
    DispatchReceipt m_Delivery;
    std::vector<std::unique_ptr<std::stop_callback<std::function<void()>>>> m_Watches;
};

struct RequestToken::State
{
    mutable std::mutex m_Mutex;
    std::uint64_t m_Revision = 0;
    std::vector<std::weak_ptr<Task::State>> m_Tasks;
};

bool RequestToken::IsCurrent() const
{
    return IfCurrent([] { return true; });
}

std::uint64_t RequestToken::GetRevision() const noexcept
{
    return m_Revision;
}

bool RequestToken::IfCurrent(const std::function<bool()>& operation) const
{
    if (m_Revision == 0)
        return operation();
    const auto state = m_State.lock();
    if (!state)
        return false;
    std::lock_guard lock(state->m_Mutex);
    return state->m_Revision == m_Revision && operation();
}

void RequestToken::Register(const Task& task) const
{
    if (m_Revision == 0)
        return;
    const auto state = m_State.lock();
    if (state)
    {
        std::lock_guard lock(state->m_Mutex);
        if (state->m_Revision == m_Revision)
        {
            std::erase_if(state->m_Tasks, [](const auto& weak) { return weak.expired(); });
            state->m_Tasks.push_back(task.m_State);
            return;
        }
    }
    task.Finish(TaskStatus::Superseded);
}

RequestScope::RequestScope() : m_State(std::make_shared<RequestToken::State>())
{
}

RequestScope::~RequestScope()
{
    Cancel();
}

RequestScope::RequestScope(RequestScope&& other) noexcept : m_State(std::move(other.m_State))
{
}

RequestScope& RequestScope::operator=(RequestScope&& other) noexcept
{
    if (this != &other)
    {
        Cancel();
        m_State = std::move(other.m_State);
    }
    return *this;
}

RequestToken RequestScope::Next()
{
    if (!m_State)
        throw std::logic_error("A moved-from request scope cannot start work.");
    std::vector<std::weak_ptr<Task::State>> retired;
    RequestToken token;
    {
        std::lock_guard lock(m_State->m_Mutex);
        ++m_State->m_Revision;
        if (m_State->m_Revision == 0)
            ++m_State->m_Revision;
        retired.swap(m_State->m_Tasks);
        token.m_State = m_State;
        token.m_Revision = m_State->m_Revision;
    }
    for (const auto& weak : retired)
        if (auto state = weak.lock())
        {
            Task task;
            task.m_State = std::move(state);
            task.Finish(TaskStatus::Superseded);
        }
    return token;
}

void RequestScope::Cancel()
{
    if (!m_State)
        return;
    std::vector<std::weak_ptr<Task::State>> retired;
    {
        std::lock_guard lock(m_State->m_Mutex);
        ++m_State->m_Revision;
        if (m_State->m_Revision == 0)
            ++m_State->m_Revision;
        retired.swap(m_State->m_Tasks);
    }
    for (const auto& weak : retired)
        if (auto state = weak.lock())
        {
            Task task;
            task.m_State = std::move(state);
            task.Cancel();
        }
}

Task Task::Create(RequestToken request)
{
    Task result;
    result.m_State = std::make_shared<State>();
    result.m_State->m_Request = std::move(request);
    result.m_State->m_Request.Register(result);
    return result;
}

TaskStatus Task::GetStatus() const
{
    if (!m_State)
        return TaskStatus::Rejected;
    std::lock_guard lock(m_State->m_Mutex);
    return m_State->m_Status;
}

bool Task::IsReady() const
{
    return Terminal(GetStatus());
}

bool Task::Cancel() const
{
    return Finish(TaskStatus::Cancelled);
}

std::exception_ptr Task::GetError() const
{
    if (!m_State)
        return {};
    std::lock_guard lock(m_State->m_Mutex);
    return m_State->m_Error;
}

void Task::RethrowIfFailed() const
{
    if (const auto error = GetError())
        std::rethrow_exception(error);
}

DispatchReceipt Task::GetDelivery() const
{
    if (!m_State)
        return {};
    std::lock_guard lock(m_State->m_Mutex);
    return m_State->m_Delivery;
}

bool Task::Start() const
{
    if (!m_State)
        return false;
    const bool started = m_State->m_Request.IfCurrent(
        [&]
        {
            std::lock_guard lock(m_State->m_Mutex);
            if (m_State->m_Status != TaskStatus::Queued)
                return false;
            m_State->m_Status = TaskStatus::Running;
            return true;
        });
    if (!started && !m_State->m_Request.IsCurrent())
        Finish(TaskStatus::Superseded);
    return started;
}

bool Task::CanPublish() const
{
    if (!m_State)
        return false;
    const bool current = m_State->m_Request.IfCurrent(
        [&]
        {
            std::lock_guard lock(m_State->m_Mutex);
            return !Terminal(m_State->m_Status);
        });
    if (!current && !m_State->m_Request.IsCurrent())
        Finish(TaskStatus::Superseded);
    return current;
}

bool Task::AwaitUi(std::exception_ptr error) const
{
    if (!m_State)
        return false;
    const bool awaiting = m_State->m_Request.IfCurrent(
        [&]
        {
            std::lock_guard lock(m_State->m_Mutex);
            if (m_State->m_Status != TaskStatus::Running)
                return false;
            m_State->m_Status = TaskStatus::AwaitingUi;
            m_State->m_Error = std::move(error);
            return true;
        });
    if (!awaiting && !m_State->m_Request.IsCurrent())
        Finish(TaskStatus::Superseded);
    return awaiting;
}

bool Task::BeginDelivery() const
{
    if (!m_State)
        return false;
    const bool delivering = m_State->m_Request.IfCurrent(
        [&]
        {
            std::lock_guard lock(m_State->m_Mutex);
            if (m_State->m_Status != TaskStatus::AwaitingUi)
                return false;
            m_State->m_Status = TaskStatus::Delivering;
            return true;
        });
    if (!delivering && !m_State->m_Request.IsCurrent())
        Finish(TaskStatus::Superseded);
    return delivering;
}

void Task::CompleteDelivery() const
{
    std::lock_guard lock(m_State->m_Mutex);
    if (m_State->m_Status == TaskStatus::Delivering)
        m_State->m_Status = m_State->m_Error ? TaskStatus::Failed : TaskStatus::Completed;
}

void Task::Fail(std::exception_ptr error) const
{
    if (!m_State)
        return;
    DispatchReceipt delivery;
    {
        std::lock_guard lock(m_State->m_Mutex);
        if (Terminal(m_State->m_Status))
            return;
        m_State->m_Status = TaskStatus::Failed;
        if (!m_State->m_Error)
            m_State->m_Error = std::move(error);
        delivery = m_State->m_Delivery;
    }
    m_State->m_Stop.request_stop();
    delivery.Cancel();
}

bool Task::Finish(TaskStatus status, std::exception_ptr error) const
{
    if (!m_State)
        return false;
    DispatchReceipt delivery;
    {
        std::lock_guard lock(m_State->m_Mutex);
        if (Terminal(m_State->m_Status) || m_State->m_Status == TaskStatus::Delivering)
            return false;
        m_State->m_Status = status;
        if (!m_State->m_Error)
            m_State->m_Error = std::move(error);
        delivery = m_State->m_Delivery;
    }
    m_State->m_Stop.request_stop();
    delivery.Cancel();
    return true;
}

void Task::SetDelivery(DispatchReceipt receipt) const
{
    bool retired = false;
    {
        std::lock_guard lock(m_State->m_Mutex);
        m_State->m_Delivery = receipt;
        retired = Terminal(m_State->m_Status);
    }
    if (retired)
        receipt.Cancel();
    try
    {
        receipt.OnFinished(
            [task = *this](DispatchStatus status, std::exception_ptr error)
            {
                switch (status)
                {
                case DispatchStatus::Cancelled:
                    task.Finish(TaskStatus::Cancelled);
                    break;
                case DispatchStatus::Superseded:
                    task.Finish(TaskStatus::Superseded);
                    break;
                case DispatchStatus::TargetExpired:
                    task.Finish(TaskStatus::TargetExpired);
                    break;
                case DispatchStatus::Rejected:
                    task.Finish(TaskStatus::Rejected);
                    break;
                case DispatchStatus::Failed:
                    task.Fail(std::move(error));
                    break;
                default:
                    break;
                }
            });
    }
    catch (...)
    {
        Fail(std::current_exception());
    }
}

std::stop_token Task::GetStopToken() const
{
    return m_State ? m_State->m_Stop.get_token() : std::stop_token{};
}

void Task::Watch(std::stop_token lifetime, TaskStatus status) const
{
    if (!lifetime.stop_possible())
        return;
    auto callback =
        std::make_unique<std::stop_callback<std::function<void()>>>(lifetime,
                                                                    [weak = std::weak_ptr<State>(m_State), status]
                                                                    {
                                                                        if (auto state = weak.lock())
                                                                        {
                                                                            Task task;
                                                                            task.m_State = std::move(state);
                                                                            task.Finish(status);
                                                                        }
                                                                    });
    std::lock_guard lock(m_State->m_Mutex);
    m_State->m_Watches.push_back(std::move(callback));
}

TaskContext::TaskContext(Task task, std::function<bool(double)> progress)
    : m_Task(std::move(task)), m_Progress(std::move(progress))
{
}

std::stop_token TaskContext::GetStopToken() const
{
    return m_Task.GetStopToken();
}

bool TaskContext::IsCancellationRequested() const
{
    return GetStopToken().stop_requested() || !m_Task.CanPublish();
}

void TaskContext::ThrowIfCancellationRequested() const
{
    if (IsCancellationRequested())
        throw std::runtime_error("Background task was cancelled.");
}

bool TaskContext::ReportProgress(double fraction) const
{
    if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
        throw std::invalid_argument("Task progress must be finite and in [0,1].");
    if (IsCancellationRequested())
        return false;
    return !m_Progress || m_Progress(fraction);
}
} // namespace EStudioUI
