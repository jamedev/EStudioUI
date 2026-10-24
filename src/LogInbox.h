#pragma once
#include <EStudioUI/LogSink.h>
#include <EStudioUI/UiDispatcher.h>
#include <deque>
#include <mutex>

namespace EStudioUI
{
struct LogSink::Data
{
    std::mutex m_Mutex;
    std::deque<LogEntry> m_Pending;
    std::size_t m_Bytes = 0;
    std::uint64_t m_Dropped = 0;
    bool m_Closed = false;
    UiDispatcher m_Dispatcher;
};
} // namespace EStudioUI
