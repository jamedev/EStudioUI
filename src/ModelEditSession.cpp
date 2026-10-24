#include <EStudioUI/ModelEditSession.h>
#include <EStudioUI/ThreadAffinity.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace EStudioUI
{
struct ModelEditSession::Data
{
    ThreadAffinity m_Affinity;
    std::optional<ModelEditResult> m_Result;
    std::vector<std::function<void(ModelEditResult)>> m_Callbacks;
    std::exception_ptr m_Error;

    void Invoke(const std::function<void(ModelEditResult)>& callback)
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

ModelEditSession::ModelEditSession() = default;

ModelEditSession ModelEditSession::Create()
{
    ModelEditSession session;
    session.m_Data = std::make_shared<Data>();
    return session;
}

bool ModelEditSession::IsPending() const
{
    return m_Data && !GetResult();
}

std::optional<ModelEditResult> ModelEditSession::GetResult() const
{
    if (!m_Data)
    {
        return {};
    }
    m_Data->m_Affinity.VerifyAccess("Model edit result access");
    return m_Data->m_Result;
}

void ModelEditSession::OnFinished(std::function<void(ModelEditResult)> callback) const
{
    const auto data = m_Data;
    if (!data || !callback)
    {
        throw std::invalid_argument("A model edit subscription requires a valid session and callback.");
    }
    data->m_Affinity.VerifyAccess("Model edit result subscription");
    if (data->m_Result)
    {
        data->Invoke(callback);
    }
    else
    {
        if (data->m_Callbacks.size() >= 256)
        {
            throw std::length_error("A model edit accepts at most 256 completion observers.");
        }
        data->m_Callbacks.push_back(std::move(callback));
    }
}

std::exception_ptr ModelEditSession::GetCallbackError() const
{
    (void)GetResult();
    return m_Data ? m_Data->m_Error : std::exception_ptr{};
}

void ModelEditSession::Complete(ModelEditResult result) const
{
    const auto data = m_Data;
    if (!data)
    {
        return;
    }
    data->m_Affinity.VerifyAccess("Model edit result completion");
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
