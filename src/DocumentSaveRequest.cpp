#include <EStudioUI/DocumentSaveRequest.h>
#include <EStudioUI/TextBuffer.h>

#include <atomic>
#include <mutex>
#include <stdexcept>

namespace EStudioUI
{
struct DocumentSaveRequest::Data
{
    std::string m_Id;
    HistoryCheckpoint m_Checkpoint;
    UiDispatcher m_Dispatcher;
    std::function<void()> m_Notify;
    mutable std::mutex m_Mutex;
    std::optional<DocumentSaveResult> m_Result;
    bool m_Completed = false;
    std::stop_source m_Lifetime;

    Data(std::string id, HistoryCheckpoint checkpoint, UiDispatcher dispatcher, std::function<void()> notify)
        : m_Id(std::move(id)), m_Checkpoint(checkpoint), m_Dispatcher(std::move(dispatcher)),
          m_Notify(std::move(notify))
    {
    }
};

DocumentSaveRequest::DocumentSaveRequest() = default;

DocumentSaveRequest DocumentSaveRequest::Create(std::string id, HistoryCheckpoint checkpoint, UiDispatcher dispatcher,
                                                std::function<void()> notify)
{
    dispatcher.VerifyAccess();
    DocumentSaveRequest request;
    request.m_Data = std::make_shared<Data>(std::move(id), checkpoint, std::move(dispatcher), std::move(notify));
    return request;
}

const std::string& DocumentSaveRequest::GetDocumentId() const
{
    static const std::string empty;
    return m_Data ? m_Data->m_Id : empty;
}

HistoryCheckpoint DocumentSaveRequest::GetCheckpoint() const
{
    return m_Data ? m_Data->m_Checkpoint : HistoryCheckpoint{};
}

std::stop_token DocumentSaveRequest::GetStopToken() const
{
    return m_Data ? m_Data->m_Lifetime.get_token() : std::stop_token{};
}

bool DocumentSaveRequest::IsPending() const
{
    const auto data = m_Data;
    if (!data || data->m_Lifetime.stop_requested() || data->m_Dispatcher.IsClosed())
    {
        return false;
    }
    const std::lock_guard lock(data->m_Mutex);
    return !data->m_Completed;
}

bool DocumentSaveRequest::Complete(DocumentSaveResult result) const
{
    const auto data = m_Data;
    if (!data || data->m_Lifetime.stop_requested() || data->m_Dispatcher.IsClosed())
    {
        return false;
    }
    if (result.m_Status < DocumentSaveStatus::Saved || result.m_Status > DocumentSaveStatus::Failed ||
        result.m_Message.size() > 8192 || TextBuffer::Sanitize(result.m_Message) != result.m_Message)
    {
        throw std::invalid_argument("A save result requires a valid status and at most 8192 single-line UTF-8 bytes.");
    }
    {
        const std::lock_guard lock(data->m_Mutex);
        if (data->m_Completed || data->m_Lifetime.stop_requested())
        {
            return false;
        }
        data->m_Result = std::move(result);
        data->m_Completed = true;
    }
    data->m_Notify();
    data->m_Dispatcher.Wake();
    return true;
}

std::optional<DocumentSaveResult> DocumentSaveRequest::TakeResult() const
{
    if (!m_Data)
    {
        return {};
    }
    m_Data->m_Dispatcher.VerifyAccess();
    const std::lock_guard lock(m_Data->m_Mutex);
    return std::exchange(m_Data->m_Result, {});
}

void DocumentSaveRequest::Expire() const
{
    if (m_Data)
    {
        m_Data->m_Lifetime.request_stop();
    }
}
} // namespace EStudioUI
