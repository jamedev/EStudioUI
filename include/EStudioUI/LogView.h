#pragma once
#include "LogSink.h"
#include "TableWidget.h"
#include <deque>

namespace EStudioUI
{
//! Virtualized read-only log with bounded retained rows and a bounded worker mailbox.
class LogView : public Widget
{
  public:
    //! Creates an empty view retaining at most 2000 entries and two MiB of message bytes.
    LogView();
    //! Closes producer delivery before releasing owner-side data.
    ~LogView() override;
    //! Returns a thread-safe sink; requires attachment to a UI context.
    LogSink GetSink();
    //! Publishes a complete owner-side batch once, preserving surviving row IDs.
    void Append(std::vector<LogEntry> entries);
    //! Sets a row bound from one through 10000, evicting oldest entries immediately.
    void SetCapacity(std::size_t entries);
    //! Returns the current retained entry count.
    std::size_t GetCount() const;
    //! Clears retained entries; producer messages arriving later remain valid.
    void Clear();
    //! Enables following newly appended rows; default is true.
    void SetFollowTail(bool follow);
    //! Returns the ordinary virtualized table for column and selection configuration.
    TableWidget& GetTable() const;

  private:
    //! Ensures owner-only model and idle subscription are bound to the current context.
    void EnsureAttached();
    //! Drains one bounded worker batch outside traversal and state notifications.
    void Drain();
    //! Replaces the bounded immutable model; rebuild cost is linear in retained entries per batch.
    void Publish();
    //! Table presentation.
    TableWidget* m_Table = nullptr;
    //! Stable canonical endpoint.
    std::shared_ptr<ItemModel> m_Model;
    //! CPU-only producer endpoint.
    LogSink m_Sink;
    //! Idle receiver connection.
    std::optional<ScopedConnection> m_Idle;
    //! Retained messages paired with lifetime-unique model IDs.
    std::deque<std::pair<ItemId, LogEntry>> m_Entries;
    //! Maximum retained row count.
    std::size_t m_Capacity = 2000;
    //! Retained payload bytes.
    std::size_t m_Bytes = 0;
    //! Automatic tail realization policy.
    bool m_Follow = true;
};
} // namespace EStudioUI
