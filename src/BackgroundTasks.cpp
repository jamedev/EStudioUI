#include <EStudioUI/BackgroundTasks.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace EStudioUI
{
struct BackgroundTasks::Impl
{
    struct WorkItem
    {
        Task m_Task;
        std::function<void()> m_Operation;
    };

    struct Shared
    {
        mutable std::mutex m_Mutex;
        std::condition_variable m_Changed;
        std::deque<WorkItem> m_Queue;
        std::vector<Task> m_Tasks;
        BackgroundTaskStats m_Stats;
        std::size_t m_Capacity = 0;
        bool m_Open = true;
    };

    std::shared_ptr<Shared> m_Shared = std::make_shared<Shared>();
    std::vector<std::thread> m_Workers;
    Executor m_Executor;

    explicit Impl(BackgroundTaskOptions options)
    {
        if (!options.m_Capacity)
            throw std::invalid_argument("A background queue requires positive capacity.");
        m_Shared->m_Capacity = options.m_Capacity;
        const auto automatic = std::min(4U, std::max(1U, std::thread::hardware_concurrency()));
        const auto count = options.m_Workers ? options.m_Workers : automatic;
        try
        {
            for (std::size_t i = 0; i < count; ++i)
                m_Workers.emplace_back([shared = m_Shared] { Worker(shared); });
        }
        catch (...)
        {
            Close();
            for (auto& worker : m_Workers)
                worker.join();
            throw;
        }
    }

    Impl(Executor executor, std::size_t capacity) : m_Executor(std::move(executor))
    {
        if (!m_Executor || !capacity)
            throw std::invalid_argument("An injected executor requires a callback and positive capacity.");
        m_Shared->m_Capacity = capacity;
    }

    ~Impl()
    {
        Close();
        for (auto& worker : m_Workers)
            if (worker.get_id() == std::this_thread::get_id())
                worker.detach();
            else
                worker.join();
    }

    static void Worker(const std::shared_ptr<Shared>& shared)
    {
        while (true)
        {
            WorkItem item;
            {
                std::unique_lock lock(shared->m_Mutex);
                shared->m_Changed.wait(lock, [&] { return !shared->m_Open || !shared->m_Queue.empty(); });
                if (shared->m_Queue.empty())
                    return;
                item = std::move(shared->m_Queue.front());
                shared->m_Queue.pop_front();
                --shared->m_Stats.m_Queued;
                ++shared->m_Stats.m_Running;
            }
            try
            {
                item.m_Operation();
            }
            catch (...)
            {
                item.m_Task.Fail(std::current_exception());
            }
            {
                std::lock_guard lock(shared->m_Mutex);
                --shared->m_Stats.m_Running;
                std::erase_if(shared->m_Tasks, [](const auto& task) { return task.IsReady(); });
            }
        }
    }

    void Close()
    {
        std::vector<Task> tasks;
        std::deque<WorkItem> retired;
        {
            std::lock_guard lock(m_Shared->m_Mutex);
            if (!m_Shared->m_Open)
                return;
            m_Shared->m_Open = false;
            tasks.swap(m_Shared->m_Tasks);
            retired.swap(m_Shared->m_Queue);
            m_Shared->m_Stats.m_Queued -= retired.size();
        }
        for (const auto& task : tasks)
            task.Cancel();
        m_Shared->m_Changed.notify_all();
    }

    void Submit(const Task& task, std::function<void()> operation)
    {
        bool rejected = false;
        {
            std::lock_guard lock(m_Shared->m_Mutex);
            std::erase_if(m_Shared->m_Tasks, [](const auto& tracked) { return tracked.IsReady(); });
            const auto occupied = m_Shared->m_Stats.m_Queued + (m_Executor ? m_Shared->m_Stats.m_Running : 0);
            if (!m_Shared->m_Open || occupied >= m_Shared->m_Capacity)
            {
                ++m_Shared->m_Stats.m_Rejected;
                rejected = true;
            }
            else
            {
                m_Shared->m_Tasks.push_back(task);
                if (!m_Executor)
                {
                    try
                    {
                        m_Shared->m_Queue.push_back({task, std::move(operation)});
                    }
                    catch (...)
                    {
                        m_Shared->m_Tasks.pop_back();
                        throw;
                    }
                }
                ++m_Shared->m_Stats.m_Accepted;
                ++m_Shared->m_Stats.m_Queued;
                m_Shared->m_Stats.m_HighWaterMark =
                    std::max(m_Shared->m_Stats.m_HighWaterMark, m_Shared->m_Stats.m_Queued);
            }
        }
        if (rejected)
        {
            task.Finish(TaskStatus::Rejected);
            return;
        }
        if (!m_Executor)
        {
            m_Shared->m_Changed.notify_one();
            return;
        }

        // The injected executor may run synchronously. Admission is complete before invoking it.
        auto pending = std::make_shared<std::atomic<bool>>(true);
        auto work = [shared = m_Shared, task, operation = std::move(operation), pending]() mutable
        {
            if (!pending->exchange(false))
                return;
            {
                std::lock_guard lock(shared->m_Mutex);
                --shared->m_Stats.m_Queued;
                ++shared->m_Stats.m_Running;
            }
            try
            {
                operation();
            }
            catch (...)
            {
                task.Fail(std::current_exception());
            }
            {
                std::lock_guard lock(shared->m_Mutex);
                --shared->m_Stats.m_Running;
                std::erase_if(shared->m_Tasks, [](const auto& tracked) { return tracked.IsReady(); });
            }
        };
        std::exception_ptr error;
        bool accepted = false;
        try
        {
            accepted = m_Executor(std::move(work));
        }
        catch (...)
        {
            error = std::current_exception();
        }
        if (!accepted)
        {
            if (pending->exchange(false))
            {
                std::lock_guard lock(m_Shared->m_Mutex);
                --m_Shared->m_Stats.m_Queued;
                ++m_Shared->m_Stats.m_Rejected;
                --m_Shared->m_Stats.m_Accepted;
            }
            task.Finish(TaskStatus::Rejected, error);
        }
    }
};

BackgroundTasks::BackgroundTasks(BackgroundTaskOptions options) : m_Impl(std::make_unique<Impl>(options))
{
}

BackgroundTasks::BackgroundTasks(Executor executor, std::size_t capacity)
    : m_Impl(std::make_unique<Impl>(std::move(executor), capacity))
{
}

BackgroundTasks::~BackgroundTasks() = default;

BackgroundTasks::BackgroundTasks(BackgroundTasks&& other) noexcept = default;

BackgroundTasks& BackgroundTasks::operator=(BackgroundTasks&& other) noexcept = default;

void BackgroundTasks::Submit(const Task& task, std::function<void()> operation)
{
    if (!m_Impl)
        task.Finish(TaskStatus::Rejected);
    else
        m_Impl->Submit(task, std::move(operation));
}

void BackgroundTasks::Shutdown()
{
    if (m_Impl)
        m_Impl->Close();
}

bool BackgroundTasks::IsOpen() const
{
    if (!m_Impl)
        return false;
    std::lock_guard lock(m_Impl->m_Shared->m_Mutex);
    return m_Impl->m_Shared->m_Open;
}

BackgroundTaskStats BackgroundTasks::GetStats() const
{
    if (!m_Impl)
        return {};
    std::lock_guard lock(m_Impl->m_Shared->m_Mutex);
    return m_Impl->m_Shared->m_Stats;
}
} // namespace EStudioUI
