#include <EStudioUI/UiTimer.h>
#include <EStudioUI/UiDispatcher.h>
#include "UiTimers.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace EStudioUI
{
namespace Detail
{
struct UiTimerRecord
{
    double m_Deadline = 0;
    bool m_Pending = true;
    std::function<void()> m_Callback;
    std::exception_ptr m_Error;
};

struct UiTimers::Data
{
    std::map<std::pair<double, std::uint64_t>, std::shared_ptr<UiTimerRecord>> m_Pending;
    bool m_Alive = true;
};

UiTimers::UiTimers() : m_Data(std::make_shared<Data>())
{
}

UiTimers::~UiTimers()
{
    m_Data->m_Alive = false;
    for (auto& [key, record] : m_Data->m_Pending)
    {
        (void)key;
        record->m_Pending = false;
        record->m_Callback = {};
    }
}

UiTimer UiTimers::Add(double now, double delay, std::function<void()> callback)
{
    if (!std::isfinite(delay) || delay < 0 || delay > 31536000 || !std::isfinite(now + delay) || !callback)
    {
        throw std::invalid_argument("A UI timer requires a callback and a finite delay from zero through one year.");
    }
    std::erase_if(m_Data->m_Pending, [](const auto& entry) { return !entry.second->m_Pending; });
    if (m_Data->m_Pending.size() >= 1024)
    {
        throw std::length_error("A UI context accepts at most 1024 pending timers.");
    }
    auto record = std::make_shared<UiTimerRecord>();
    record->m_Deadline = now + delay;
    record->m_Callback = std::move(callback);
    m_Data->m_Pending.emplace(std::pair{record->m_Deadline, UiDispatcher::AllocateTargetKey()}, record);
    return UiTimer(std::move(record));
}

void UiTimers::Pump(double now)
{
    const auto data = m_Data;
    std::vector<std::shared_ptr<UiTimerRecord>> ready;
    auto& pending = data->m_Pending;
    for (auto i = pending.begin(); i != pending.end() && i->first.first <= now && ready.size() < 64;)
    {
        if (i->second->m_Pending)
        {
            ready.push_back(i->second);
        }
        i = pending.erase(i);
    }
    for (const auto& record : ready)
    {
        if (!data->m_Alive)
        {
            for (const auto& remaining : ready)
            {
                remaining->m_Pending = false;
                remaining->m_Callback = {};
            }
            break;
        }
        if (!record->m_Pending)
        {
            continue;
        }
        record->m_Pending = false;
        auto callback = std::exchange(record->m_Callback, {});
        try
        {
            callback();
        }
        catch (...)
        {
            record->m_Error = std::current_exception();
        }
    }
}

double UiTimers::NextDeadline() const
{
    const auto& pending = m_Data->m_Pending;
    for (const auto& [key, record] : pending)
    {
        if (record->m_Pending)
        {
            return key.first;
        }
    }
    return -1;
}
} // namespace Detail

UiTimer::UiTimer() = default;

UiTimer::UiTimer(std::shared_ptr<Detail::UiTimerRecord> record) : m_Record(std::move(record))
{
}

UiTimer::~UiTimer()
{
    Cancel();
}

UiTimer::UiTimer(UiTimer&& other) noexcept : m_Affinity(other.m_Affinity), m_Record(std::move(other.m_Record))
{
}

UiTimer& UiTimer::operator=(UiTimer&& other) noexcept
{
    if (this != &other)
    {
        Cancel();
        m_Affinity = other.m_Affinity;
        m_Record = std::move(other.m_Record);
    }
    return *this;
}

bool UiTimer::Cancel()
{
    m_Affinity.VerifyAccess("UI timer cancellation");
    if (!m_Record || !m_Record->m_Pending)
    {
        return false;
    }
    m_Record->m_Pending = false;
    m_Record->m_Callback = {};
    return true;
}

bool UiTimer::IsPending() const
{
    m_Affinity.VerifyAccess("UI timer access");
    return m_Record && m_Record->m_Pending;
}

std::exception_ptr UiTimer::GetError() const
{
    m_Affinity.VerifyAccess("UI timer result access");
    return m_Record ? m_Record->m_Error : std::exception_ptr{};
}
} // namespace EStudioUI
