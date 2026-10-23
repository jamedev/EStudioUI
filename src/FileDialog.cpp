#include <EStudioUI/FileDialog.h>
#include <EStudioUI/UiContext.h>

namespace EStudioUI
{
FileDialogSession FileDialog::Open(Widget& owner, FileDialogOptions options)
{
    options.m_Kind = FileDialogKind::OpenFile;
    return Start(owner, std::move(options));
}

FileDialogSession FileDialog::Save(Widget& owner, FileDialogOptions options)
{
    options.m_Kind = FileDialogKind::SaveFile;
    options.m_Multiple = false;
    return Start(owner, std::move(options));
}

FileDialogSession FileDialog::SelectFolder(Widget& owner, FileDialogOptions options)
{
    options.m_Kind = FileDialogKind::SelectFolder;
    options.m_Multiple = false;
    options.m_Filters.clear();
    options.m_DefaultExtension.clear();
    return Start(owner, std::move(options));
}

FileDialogSession FileDialog::Start(Widget& owner, FileDialogOptions options)
{
    owner.VerifyAccess();
    auto* context = owner.GetContext();
    if (!context)
        throw std::logic_error("A file picker owner must belong to a live UiContext.");
    options.Validate();
    const auto handle = context->GetHandle(owner);
    auto session = context->Platform().ShowFileDialog(std::move(options));
    if (session.IsPending())
    {
        using Registration = std::stop_callback<std::function<void()>>;
        auto lifetime = std::make_shared<Registration>(handle.GetTargetStopToken(), [session] { session.Cancel(); });
        session.OnFinished([lifetime = std::move(lifetime)](const FileDialogResult&) {});
    }
    return session;
}
} // namespace EStudioUI
