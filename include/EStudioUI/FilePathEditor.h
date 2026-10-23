#pragma once

#include "TextBox.h"
#include "ToolButton.h"
#include "FileDialogSession.h"

namespace EStudioUI
{
//! An editable path with asynchronous browsing and bounded recent-path choices.
class FilePathEditor : public Widget
{
  public:
    //! Creates an open-file field; folder/save modes use SetDialogOptions.
    FilePathEditor();

    //! Cancels this field's pending picker before child teardown.
    ~FilePathEditor() override;

    //! Commits a path without filesystem access; application validation can use BindPath.
    void SetPath(std::filesystem::path path);

    //! Checks exact UTF-8 representability and the configured editor validator without committing.
    bool CanSetPath(const std::filesystem::path& path) const;

    //! Returns the canonical path independently of a pending picker result.
    const std::filesystem::path& GetPath() const;

    //! Replaces validated picker configuration; this field always selects one path.
    void SetDialogOptions(FileDialogOptions options);

    //! Returns an owned picker configuration.
    FileDialogOptions GetDialogOptions() const;

    //! Starts browsing, coalescing repeated presses while a picker remains pending.
    FileDialogSession Browse();

    //! Returns whether this field owns a pending asynchronous picker.
    bool IsBrowsing() const;

    //! Replaces up to sixteen distinct recent paths without filesystem probes.
    void SetRecentPaths(std::vector<std::filesystem::path> paths);

    //! Returns an owned snapshot of recent choices.
    std::vector<std::filesystem::path> GetRecentPaths() const;

    //! Returns the normal text editor for placeholder, validation and input configuration.
    TextBox& Editor() const;

    //! Reports canonical path changes after state is committed.
    Signal<const std::filesystem::path&>& PathChanged();

    //! Reports each terminal native picker outcome, including cancellation/failure.
    Signal<const FileDialogResult&>& BrowseFinished();

  protected:
    //! Creates a field with a specific CSS type and picker kind.
    FilePathEditor(std::string type, FileDialogKind kind);

  private:
    //! Updates the path from validated UTF-8 editor bytes.
    void ReadEditor();

    //! Delivers bounded reentrant path notifications through owned snapshots.
    void PublishPath();

    //! Opens recent choices through the ordinary owner-bound popup stack.
    void ShowRecent();

    //! Observes the owned editor.
    TextBox* m_Editor = nullptr;

    //! Observes the owned browse command.
    ToolButton* m_Browse = nullptr;

    //! Observes the owned recent-path command.
    ToolButton* m_Recent = nullptr;

    //! Canonical value with no implicit normalization or disk access.
    std::filesystem::path m_Path;

    //! Last notified canonical value.
    std::filesystem::path m_Notified;

    //! Owned validated native options.
    FileDialogOptions m_Options;

    //! Pending native selection, independent of field address.
    FileDialogSession m_Session;

    //! Bounded most-recent-first path history.
    std::vector<std::filesystem::path> m_RecentPaths;

    //! Weak identity of the reusable root-owned recent menu.
    WidgetRef m_Menu;

    //! Rejects stale native results after an intervening editor/application change.
    std::uint64_t m_Revision = 0;

    //! Prevents editor feedback during canonical synchronization.
    bool m_UpdatingEditor = false;

    //! Prevents recursive path notification delivery.
    bool m_Notifying = false;

    //! Suppresses picker callbacks before derived teardown begins.
    std::shared_ptr<int> m_CallbackLifetime = std::make_shared<int>(0);

    //! Reports canonical path changes.
    Signal<const std::filesystem::path&> m_PathChanged;

    //! Reports terminal picker results.
    Signal<const FileDialogResult&> m_BrowseFinished;
};
} // namespace EStudioUI
