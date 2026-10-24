#include <EStudioUI/DocumentCloseSession.h>
#include <EStudioUI/ThreadAffinity.h>

#include <stdexcept>

namespace EStudioUI
{
struct DocumentCloseSession::Data
{
    ThreadAffinity m_Affinity;
    std::optional<DocumentCloseResult> m_Result;
    std::vector<std::function<void(DocumentCloseResult)>> m_Callbacks;
    std::exception_ptr m_Error;

    void Invoke(const std::function<void(DocumentCloseResult)>& callback)
    {
        try
        {
            callback(*m_Result);
        }
        catch (...)
        {
            if (!m_Error)
            {
                m_Error = std::current_exception();
            }
        }
    }
};

DocumentCloseSession::DocumentCloseSession() = default;

DocumentCloseSession DocumentCloseSession::Create()
{
    DocumentCloseSession session;
    session.m_Data = std::make_shared<Data>();
    return session;
}

bool DocumentCloseSession::IsPending() const
{
    return m_Data && !GetResult();
}

std::optional<DocumentCloseResult> DocumentCloseSession::GetResult() const
{
    if (!m_Data)
    {
        return {};
    }
    m_Data->m_Affinity.VerifyAccess("Document close result access");
    return m_Data->m_Result;
}

void DocumentCloseSession::OnFinished(std::function<void(DocumentCloseResult)> callback) const
{
    const auto data = m_Data;
    if (!data || !callback)
    {
        throw std::invalid_argument("A document close subscription requires a valid session and callback.");
    }
    data->m_Affinity.VerifyAccess("Document close result subscription");
    if (data->m_Result)
    {
        data->Invoke(callback);
    }
    else
    {
        data->m_Callbacks.push_back(std::move(callback));
    }
}

std::exception_ptr DocumentCloseSession::GetCallbackError() const
{
    (void)GetResult();
    return m_Data ? m_Data->m_Error : std::exception_ptr{};
}

void DocumentCloseSession::Complete(DocumentCloseResult result) const
{
    const auto data = m_Data;
    if (!data)
    {
        return;
    }
    data->m_Affinity.VerifyAccess("Document close result completion");
    if (data->m_Result)
    {
        return;
    }
    data->m_Result = std::move(result);
    const auto callbacks = std::move(data->m_Callbacks);
    for (const auto& callback : callbacks)
    {
        data->Invoke(callback);
    }
}
} // namespace EStudioUI
