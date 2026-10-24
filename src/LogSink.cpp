#include <EStudioUI/LogSink.h>
#include "LogInbox.h"

namespace EStudioUI
{
bool LogSink::Append(LogEntry entry) const
{
    const auto data = m_Data;
    if (!data)
    {
        return false;
    }
    {
        const std::lock_guard lock(data->m_Mutex);
        const auto size = entry.m_Message.size() + entry.m_Source.size();
        if (data->m_Closed || entry.m_Message.size() > 16384 || entry.m_Source.size() > 256 ||
            entry.m_Severity < FeedbackSeverity::Information || entry.m_Severity > FeedbackSeverity::Error ||
            data->m_Pending.size() >= 512 || data->m_Bytes + size > 1048576)
        {
            ++data->m_Dropped;
            return false;
        }
        data->m_Bytes += size;
        data->m_Pending.push_back(std::move(entry));
    }
    data->m_Dispatcher.Wake();
    return true;
}

std::uint64_t LogSink::GetDroppedCount() const
{
    if (!m_Data)
    {
        return 0;
    }
    const std::lock_guard lock(m_Data->m_Mutex);
    return m_Data->m_Dropped;
}
} // namespace EStudioUI
