#pragma once
#include "ValidationMessage.h"
#include <memory>

namespace EStudioUI
{
class LogView;

//! One bounded, owned log entry; timestamp is supplied by the application when needed.
struct LogEntry
{
    //! Message text, at most 16 KiB.
    std::string m_Message;
    //! Optional source/category, at most 256 bytes.
    std::string m_Source;
    //! Severity for filtering and presentation.
    FeedbackSeverity m_Severity = FeedbackSeverity::Information;
};

//! Copyable thread-safe producer with bounded storage and explicit overload reporting.
class LogSink
{
  public:
    //! Creates a disconnected endpoint.
    LogSink() = default;
    //! Enqueues without touching UI objects; returns false on closure, invalid size or overload.
    bool Append(LogEntry entry) const;
    //! Returns the number of rejected messages on this endpoint, from any thread.
    std::uint64_t GetDroppedCount() const;

  private:
    friend class LogView;
    //! CPU-only synchronized inbox, safe for the last reference to die on a worker.
    struct Data;
    //! Owns producer storage independently of the widget.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
