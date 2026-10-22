#include <EStudioUI/PopupSession.h>
#include <EStudioUI/ThreadAffinity.h>

#include <stdexcept>
#include <vector>

namespace EStudioUI
{
struct PopupSession::Data
{
    ThreadAffinity m_Affinity;
    std::optional<PopupResult> m_Result;
    std::vector<std::function<void(PopupResult)>> m_Callbacks;
    std::exception_ptr m_Error;

    void Invoke(const std::function<void(PopupResult)>& callback)
    {
        try
        {
            callback(*m_Result);
        }
        catch (...)
        {
            if (!m_Error)
                m_Error = std::current_exception();
        }
    }
};

PopupOptions PopupOptions::Modal()
{
    PopupOptions options;
    options.m_Modal = true;
    options.m_DismissOnOutside = false;
    return options;
}

PopupSession::PopupSession() = default;

PopupSession PopupSession::Create()
{
    PopupSession result;
    result.m_Data = std::make_shared<Data>();
    return result;
}

bool PopupSession::IsPending() const
{
    return m_Data && !GetResult();
}

std::optional<PopupResult> PopupSession::GetResult() const
{
    if (!m_Data)
        return {};
    m_Data->m_Affinity.VerifyAccess("Popup result access");
    return m_Data->m_Result;
}

void PopupSession::OnFinished(std::function<void(PopupResult)> callback) const
{
    const auto data = m_Data;
    if (!data || !callback)
        throw std::invalid_argument("A popup completion requires a valid session and callback.");
    data->m_Affinity.VerifyAccess("Popup result subscription");
    if (data->m_Result)
        data->Invoke(callback);
    else
        data->m_Callbacks.push_back(std::move(callback));
}

std::exception_ptr PopupSession::GetCallbackError() const
{
    (void)GetResult();
    return m_Data ? m_Data->m_Error : std::exception_ptr{};
}

void PopupSession::Complete(PopupResult result) const
{
    const auto data = m_Data;
    if (!data || data->m_Result)
        return;
    data->m_Affinity.VerifyAccess("Popup completion");
    data->m_Result = result;
    const auto callbacks = std::move(data->m_Callbacks);
    for (const auto& callback : callbacks)
        data->Invoke(callback);
}
} // namespace EStudioUI
