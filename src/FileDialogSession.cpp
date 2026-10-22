#include <EStudioUI/FileDialogSession.h>
#include <EStudioUI/ClipboardData.h>
#include <EStudioUI/ThreadAffinity.h>

#include <stdexcept>

namespace EStudioUI
{
void FileDialogOptions::Validate() const
{
    if (m_Kind < FileDialogKind::OpenFile || m_Kind > FileDialogKind::SelectFolder ||
        (m_Multiple && m_Kind != FileDialogKind::OpenFile) ||
        (m_Kind == FileDialogKind::SelectFolder && (!m_FileName.empty() || !m_Filters.empty())) ||
        (!m_DefaultExtension.empty() && m_Kind != FileDialogKind::SaveFile))
        throw std::invalid_argument("Invalid native file-dialog option combination.");
    if (m_Title.size() > 4096 || m_Filters.size() > 32 || m_DefaultExtension.size() > 64)
        throw std::length_error("Native dialog captions, filters, or extensions exceed their budget.");
    ValidatePlatformText(m_Title);
    ValidatePlatformText(m_DefaultExtension);
    if ((!m_DefaultExtension.empty() && m_DefaultExtension.front() == '.') ||
        m_DefaultExtension.find_first_of("/\\;*?:<>|\"") != std::string::npos)
        throw std::invalid_argument("A default file extension cannot contain shell path or wildcard characters.");
    ClipboardData paths;
    if (!m_InitialFolder.empty())
        paths.m_Files.push_back(m_InitialFolder);
    if (!m_FileName.empty())
    {
        paths.m_Files.push_back(m_FileName);
        const auto name = m_FileName.u8string();
        if (m_FileName.has_parent_path() || name.find_first_of(u8"/\\") != std::u8string::npos)
            throw std::invalid_argument("The initial dialog filename must be a name without a directory.");
    }
    paths.Validate();
    for (const auto& filter : m_Filters)
    {
        if (filter.m_Name.empty() || filter.m_Pattern.empty() || filter.m_Name.size() > 4096 ||
            filter.m_Pattern.size() > 4096 || filter.m_Pattern.find_first_of("/\\\r\n") != std::string::npos)
            throw std::invalid_argument("A native file filter requires bounded captions and filename patterns.");
        ValidatePlatformText(filter.m_Name);
        ValidatePlatformText(filter.m_Pattern);
    }
}

struct FileDialogSession::Data
{
    ThreadAffinity m_Affinity;
    std::stop_source m_Stop;
    std::optional<FileDialogResult> m_Result;
    std::vector<std::function<void(const FileDialogResult&)>> m_Callbacks;
    std::exception_ptr m_Error;

    void Invoke(const std::function<void(const FileDialogResult&)>& callback)
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

FileDialogSession::FileDialogSession() = default;

FileDialogSession FileDialogSession::Create()
{
    FileDialogSession session;
    session.m_Data = std::make_shared<Data>();
    return session;
}

bool FileDialogSession::IsPending() const
{
    if (!m_Data)
        return false;
    m_Data->m_Affinity.VerifyAccess("File-dialog pending access");
    return !m_Data->m_Result;
}

std::optional<FileDialogResult> FileDialogSession::GetResult() const
{
    if (!m_Data)
        return {};
    m_Data->m_Affinity.VerifyAccess("File-dialog result access");
    return m_Data->m_Result;
}

void FileDialogSession::OnFinished(std::function<void(const FileDialogResult&)> callback) const
{
    const auto data = m_Data;
    if (!data || !callback)
        throw std::invalid_argument("A file-dialog observer requires a valid session and callback.");
    data->m_Affinity.VerifyAccess("File-dialog subscription");
    if (data->m_Result)
        data->Invoke(callback);
    else if (data->m_Callbacks.size() < 256)
        data->m_Callbacks.push_back(std::move(callback));
    else
        throw std::length_error("A file-dialog session supports at most 256 observers.");
}

bool FileDialogSession::Cancel() const
{
    if (!IsPending())
        return false;
    Complete({FileDialogStatus::Cancelled});
    return true;
}

std::exception_ptr FileDialogSession::GetCallbackError() const
{
    if (m_Data)
        m_Data->m_Affinity.VerifyAccess("File-dialog observer-error access");
    return m_Data ? m_Data->m_Error : std::exception_ptr{};
}

std::stop_token FileDialogSession::GetStopToken() const
{
    return m_Data->m_Stop.get_token();
}

void FileDialogSession::Complete(FileDialogResult result) const
{
    const auto data = m_Data;
    if (!data)
        return;
    data->m_Affinity.VerifyAccess("File-dialog completion");
    if (data->m_Result)
        return;
    try
    {
        if (result.m_Status < FileDialogStatus::Accepted || result.m_Status > FileDialogStatus::Failed ||
            (result.m_Status == FileDialogStatus::Accepted && result.m_Paths.empty()) ||
            (result.m_Status != FileDialogStatus::Accepted && !result.m_Paths.empty()))
            throw std::invalid_argument("Invalid native dialog result.");
        ClipboardData paths;
        paths.m_Files = result.m_Paths;
        paths.Validate();
        ValidatePlatformText(result.m_Message);
    }
    catch (const std::exception& error)
    {
        result = {FileDialogStatus::Failed, {}, error.what()};
    }
    data->m_Result = std::move(result);
    data->m_Stop.request_stop();
    const auto callbacks = std::move(data->m_Callbacks);
    for (const auto& callback : callbacks)
        data->Invoke(callback);
}
} // namespace EStudioUI
