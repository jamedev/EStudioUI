#include <EStudioUI/LogView.h>
#include <EStudioUI/Models.h>
#include <EStudioUI/UiContext.h>
#include "LogInbox.h"
#include <stdexcept>

namespace EStudioUI
{
LogView::LogView() : Widget("logview")
{
    m_Table = &Add<TableWidget>();
    m_Table->SetWidth(SizeRule::Fill());
    m_Table->SetHeight(SizeRule::Fill());
}

LogView::~LogView()
{
    if (m_Sink.m_Data)
    {
        const std::lock_guard lock(m_Sink.m_Data->m_Mutex);
        m_Sink.m_Data->m_Closed = true;
        m_Sink.m_Data->m_Pending.clear();
        m_Sink.m_Data->m_Bytes = 0;
    }
}

void LogView::EnsureAttached()
{
    if (!GetContext())
    {
        throw std::logic_error("Attach the log view before publishing messages.");
    }
    if (m_Idle)
    {
        return;
    }
    m_Sink.m_Data = std::make_shared<LogSink::Data>();
    m_Sink.m_Data->m_Dispatcher = GetContext()->GetDispatcher();
    const WidgetRef self(this);
    m_Idle.emplace(GetContext()->IdleTurn().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<LogView*>(self.Get()))
            {
                live->Drain();
            }
        }));
}

LogSink LogView::GetSink()
{
    VerifyStructuralMutation();
    EnsureAttached();
    return m_Sink;
}

void LogView::Drain()
{
    std::vector<LogEntry> entries;
    {
        const std::lock_guard lock(m_Sink.m_Data->m_Mutex);
        auto& pending = m_Sink.m_Data->m_Pending;
        while (!pending.empty())
        {
            entries.push_back(std::move(pending.front()));
            pending.pop_front();
        }
        m_Sink.m_Data->m_Bytes = 0;
    }
    if (!entries.empty())
    {
        Append(std::move(entries));
    }
}

void LogView::Append(std::vector<LogEntry> entries)
{
    VerifyStructuralMutation();
    EnsureAttached();
    if (entries.size() > 10000)
    {
        throw std::length_error("Log batches are limited to 10000 entries.");
    }
    for (const auto& entry : entries)
    {
        if (entry.m_Message.size() > 16384 || entry.m_Source.size() > 256 ||
            entry.m_Severity < FeedbackSeverity::Information || entry.m_Severity > FeedbackSeverity::Error)
        {
            throw std::invalid_argument("Log entries require a valid severity and bounded message/source text.");
        }
    }
    if (entries.empty())
    {
        return;
    }
    for (auto& entry : entries)
    {
        m_Bytes += entry.m_Message.size() + entry.m_Source.size();
        m_Entries.emplace_back(AllocateItemId(), std::move(entry));
        while (m_Entries.size() > m_Capacity || m_Bytes > 2097152)
        {
            m_Bytes -= m_Entries.front().second.m_Message.size() + m_Entries.front().second.m_Source.size();
            m_Entries.pop_front();
        }
    }
    Publish();
}

void LogView::Publish()
{
    struct Row
    {
        std::string m_Severity, m_Source, m_Message;
    };

    std::vector<Row> rows;
    std::vector<ItemId> ids;
    for (const auto& [id, entry] : m_Entries)
    {
        ids.push_back(id);
        rows.push_back({entry.m_Severity == FeedbackSeverity::Error     ? "Error"
                        : entry.m_Severity == FeedbackSeverity::Warning ? "Warning"
                        : entry.m_Severity == FeedbackSeverity::Success ? "Success"
                                                                        : "Info",
                        entry.m_Source, entry.m_Message});
    }
    auto columns = std::vector<RecordColumn<Row>>{Column("Level", &Row::m_Severity), Column("Source", &Row::m_Source),
                                                  Column("Message", &Row::m_Message)};
    for (auto& column : columns)
    {
        column.m_Set = {};
        column.m_Schema.m_Editable = false;
    }
    const auto snapshot = MakeTableSnapshot(std::move(rows), std::move(columns), std::move(ids));
    if (m_Model)
    {
        m_Model->Replace(snapshot);
    }
    else
    {
        m_Model = std::make_shared<ItemModel>(GetContext()->States(), snapshot);
        m_Table->SetModel(m_Model);
    }
    if (m_Follow && !m_Entries.empty())
    {
        m_Table->SetCurrentItemId(m_Entries.back().first);
    }
}

void LogView::SetCapacity(std::size_t entries)
{
    VerifyStructuralMutation();
    if (entries < 1 || entries > 10000)
    {
        throw std::invalid_argument("Log capacity must be between 1 and 10000.");
    }
    m_Capacity = entries;
    while (m_Entries.size() > entries)
    {
        m_Bytes -= m_Entries.front().second.m_Message.size() + m_Entries.front().second.m_Source.size();
        m_Entries.pop_front();
    }
    if (m_Model)
    {
        Publish();
    }
}

std::size_t LogView::GetCount() const
{
    VerifyAccess();
    return m_Entries.size();
}

void LogView::Clear()
{
    VerifyStructuralMutation();
    m_Entries.clear();
    m_Bytes = 0;
    if (m_Model)
    {
        Publish();
    }
}

void LogView::SetFollowTail(bool follow)
{
    VerifyStructuralMutation();
    m_Follow = follow;
}

TableWidget& LogView::GetTable() const
{
    VerifyAccess();
    return *m_Table;
}
} // namespace EStudioUI
