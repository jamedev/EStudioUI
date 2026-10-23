#include <EStudioUI/FilePathEditor.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/FileDialog.h>
#include <EStudioUI/UiContext.h>

#include "WindowCommandButton.h"

#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
std::string PathText(const std::filesystem::path& path)
{
    const auto bytes = path.u8string();
    if (bytes.size() > 65536 || std::find(bytes.begin(), bytes.end(), u8'\0') != bytes.end())
        throw std::invalid_argument("A path must contain at most 65536 UTF-8 bytes without embedded zeroes.");
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (TextBuffer::Sanitize(text) != text)
        throw std::invalid_argument("Paths must be representable as single-line UTF-8.");
    return text;
}
} // namespace

FilePathEditor::FilePathEditor() : FilePathEditor("filepatheditor", FileDialogKind::OpenFile)
{
}

FilePathEditor::FilePathEditor(std::string type, FileDialogKind kind) : Widget(std::move(type))
{
    m_Options.m_Kind = kind;
    SetLayoutDirection(Axis::Horizontal);
    SetGap(4);
    m_Editor = &Add<TextBox>();
    m_Editor->AddClass("path-editor");
    m_Editor->SetMaxBytes(65536);
    m_Editor->SetWidth(SizeRule::Fill());
    m_Editor->SetHeight(SizeRule::Fill());
    m_Editor->SetAccessibleName(kind == FileDialogKind::SelectFolder ? "Folder path" : "File path");
    m_Browse = &Add<ToolButton>("…");
    m_Browse->AddClass("path-browse");
    m_Browse->SetAccessibleName("Browse for a path");
    m_Browse->SetWidth(SizeRule::Fixed(36));
    m_Browse->SetHeight(SizeRule::Fill());
    m_Recent = &Add<Detail::WindowCommandButton>(Detail::WindowCommand::DropDown);
    m_Recent->AddClass("path-recent");
    m_Recent->SetAccessibleName("Recent paths");
    m_Recent->SetWidth(SizeRule::Fixed(28));
    m_Recent->SetHeight(SizeRule::Fill());
    m_Recent->SetEnabled(false);
    const WidgetRef self(this);
    m_Editor->TextChanged().Connect(
        [self](const std::string&)
        {
            if (auto* live = dynamic_cast<FilePathEditor*>(self.Get()))
                live->ReadEditor();
        });
    m_Browse->Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<FilePathEditor*>(self.Get()))
                live->Browse();
        });
    m_Recent->Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<FilePathEditor*>(self.Get()))
                live->ShowRecent();
        });
}

FilePathEditor::~FilePathEditor()
{
    m_CallbackLifetime.reset();
    ++m_Revision;
    m_Session.Cancel();
}

void FilePathEditor::SetPath(std::filesystem::path path)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const auto text = PathText(path);
    if (!m_Editor->CanSetText(text))
        throw std::invalid_argument("The path is rejected by this field's text validator.");
    if (path == m_Path)
        return;
    m_Path = std::move(path);
    ++m_Revision;
    const WidgetRef self(this);
    m_UpdatingEditor = true;
    try
    {
        m_Editor->SetText(text);
        if (!self.Get())
            return;
        m_UpdatingEditor = false;
    }
    catch (...)
    {
        if (self.Get())
            m_UpdatingEditor = false;
        throw;
    }
    ReadEditor();
    PublishPath();
}

void FilePathEditor::ReadEditor()
{
    if (m_UpdatingEditor)
        return;
    const auto& bytes = m_Editor->GetText();
    const auto path =
        std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.data()), bytes.size()));
    (void)PathText(path);
    if (path == m_Path)
        return;
    m_Path = path;
    ++m_Revision;
    PublishPath();
}

void FilePathEditor::PublishPath()
{
    if (m_Notifying)
        return;
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    m_Notifying = true;
    try
    {
        unsigned turns = 0;
        while (m_Notified != m_Path)
        {
            if (++turns > 64)
                throw std::logic_error("Path observers exceeded 64 reentrant changes.");
            m_Notified = m_Path;
            const auto snapshot = m_Path;
            m_PathChanged.Emit(snapshot);
            if (!self.Get())
                return;
        }
        m_Notifying = false;
    }
    catch (...)
    {
        if (self.Get())
            m_Notifying = false;
        throw;
    }
}

bool FilePathEditor::CanSetPath(const std::filesystem::path& path) const
{
    VerifyAccess();
    try
    {
        return m_Editor->CanSetText(PathText(path));
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return false;
    }
}

const std::filesystem::path& FilePathEditor::GetPath() const
{
    VerifyAccess();
    return m_Path;
}

void FilePathEditor::SetDialogOptions(FileDialogOptions options)
{
    VerifyStructuralMutation();
    options.Validate();
    if (options.m_Multiple)
        throw std::invalid_argument("A FilePathEditor accepts exactly one path.");
    m_Options = std::move(options);
    ++m_Revision;
}

FileDialogOptions FilePathEditor::GetDialogOptions() const
{
    VerifyAccess();
    return m_Options;
}

FileDialogSession FilePathEditor::Browse()
{
    VerifyStructuralMutation();
    if (!GetContext())
        throw std::logic_error("Attach a path editor before browsing.");
    if (!IsEnabled() || !IsVisible())
        throw std::logic_error("A hidden or disabled path editor cannot start a picker.");
    if (m_Session.IsPending())
        return m_Session;
    auto options = m_Options;
    if (!m_Path.empty())
    {
        if (options.m_InitialFolder.empty())
            options.m_InitialFolder = options.m_Kind == FileDialogKind::SelectFolder ? m_Path : m_Path.parent_path();
        if (options.m_Kind == FileDialogKind::SaveFile && options.m_FileName.empty())
            options.m_FileName = m_Path.filename();
    }
    const WidgetRef self(this);
    const auto revision = m_Revision;
    auto* origin = GetContext();
    m_Session = options.m_Kind == FileDialogKind::SaveFile       ? FileDialog::Save(*this, options)
                : options.m_Kind == FileDialogKind::SelectFolder ? FileDialog::SelectFolder(*this, options)
                                                                 : FileDialog::Open(*this, options);
    const auto session = m_Session;
    const std::weak_ptr<int> lifetime = m_CallbackLifetime;
    m_Session.OnFinished(
        [self, revision, lifetime, origin](const FileDialogResult& result)
        {
            if (lifetime.expired())
                return;
            auto* live = dynamic_cast<FilePathEditor*>(self.Get());
            if (!live)
                return;
            const auto lease = live->RetainOwner();
            if (result.m_Status == FileDialogStatus::Accepted && live->m_Revision == revision &&
                live->GetContext() == origin && !result.m_Paths.empty())
            {
                const auto chosen = result.m_Paths.front();
                live->SetPath(chosen);
                live = dynamic_cast<FilePathEditor*>(self.Get());
                if (!live)
                    return;
                auto recent = live->m_RecentPaths;
                std::erase(recent, chosen);
                recent.insert(recent.begin(), chosen);
                if (recent.size() > 16)
                    recent.resize(16);
                live->SetRecentPaths(std::move(recent));
            }
            live->m_BrowseFinished.Emit(result);
        });
    return session;
}

bool FilePathEditor::IsBrowsing() const
{
    VerifyAccess();
    return m_Session.IsPending();
}

void FilePathEditor::SetRecentPaths(std::vector<std::filesystem::path> paths)
{
    VerifyStructuralMutation();
    if (paths.size() > 16)
        throw std::invalid_argument("Recent paths are limited to sixteen entries.");
    std::vector<std::filesystem::path> unique;
    for (auto& path : paths)
    {
        (void)PathText(path);
        if (!path.empty() && std::find(unique.begin(), unique.end(), path) == unique.end())
            unique.push_back(std::move(path));
    }
    m_RecentPaths = std::move(unique);
    m_Recent->SetEnabled(!m_RecentPaths.empty());
}

std::vector<std::filesystem::path> FilePathEditor::GetRecentPaths() const
{
    VerifyAccess();
    return m_RecentPaths;
}

TextBox& FilePathEditor::Editor() const
{
    VerifyAccess();
    return *m_Editor;
}

Signal<const std::filesystem::path&>& FilePathEditor::PathChanged()
{
    VerifyAccess();
    return m_PathChanged;
}

Signal<const FileDialogResult&>& FilePathEditor::BrowseFinished()
{
    VerifyAccess();
    return m_BrowseFinished;
}

void FilePathEditor::ShowRecent()
{
    VerifyStructuralMutation();
    if (!GetContext() || m_RecentPaths.empty())
        return;
    auto* menu = dynamic_cast<ContextMenu*>(m_Menu.Get());
    if (!menu)
    {
        auto popup = std::make_unique<ContextMenu>();
        menu = &static_cast<ContextMenu&>(GetContext()->AddOwnedPopup(std::move(popup), *this));
        m_Menu = WidgetRef(menu);
    }
    menu->ClearChildren();
    const WidgetRef self(this);
    for (const auto& path : m_RecentPaths)
        menu->AddAction(PathText(path), CanSetPath(path))
            .Clicked()
            .Connect(
                [self, path]
                {
                    if (auto* live = dynamic_cast<FilePathEditor*>(self.Get()); live && live->CanSetPath(path))
                        live->SetPath(path);
                });
    PopupOptions options;
    options.m_AnchorWidget = m_Recent;
    options.m_Placement = PopupPlacement::Below;
    GetContext()->ShowPopup(*menu, {}, options);
}
} // namespace EStudioUI
