#pragma once

#include "DocumentCloseSession.h"
#include "DocumentSaveRequest.h"
#include "TabWidget.h"
#include "Window.h"

namespace EStudioUI
{
//! Application-supplied document identity, command history and asynchronous persistence.
struct DocumentOptions
{
    //! Stable, nonempty UTF-8 identity unique within this workspace.
    std::string m_Id;
    //! Human-readable tab and floating-window caption.
    std::string m_Title;
    //! Optional shared transactional history; its clean checkpoint drives the dirty indicator.
    std::shared_ptr<UndoHistory> m_History;
    //! Captures application data now and completes the ticket after picker/I/O finishes.
    std::function<void(DocumentSaveRequest)> m_Save;
};

//! Owned current presentation/lifecycle state of one stable document.
struct DocumentInfo
{
    //! Stable workspace identity and current caption.
    std::string m_Id, m_Title;
    //! Combines canonical history dirtiness and explicitly reported external edits.
    bool m_Modified = false;
    //! Reports an outstanding application-defined persistence operation.
    bool m_Saving = false;
    //! Reports an existing view hosted in a separate owned native window.
    bool m_Detached = false;
    //! Most recent persistence failure, cleared by the next save or successful completion.
    std::string m_SaveError;
};

//! Persistent document views with guarded close/save workflows and native detach/reattach.
class DocumentWorkspace final : public Widget
{
  public:
    //! Creates an attachable workspace; close/history behavior works without a native window.
    DocumentWorkspace();
    //! Enables native detach and automatically guards this containing window's close intent.
    explicit DocumentWorkspace(Window& owner);
    //! Cancels pending sessions/tickets and retires detached windows without joining workers.
    ~DocumentWorkspace() override;

    //! Creates a stable owned content container for one of at most 128 documents.
    Widget& AddDocument(DocumentOptions options);
    //! Returns an existing content widget without rebuilding controls or models.
    Widget& Content(std::string_view id) const;
    //! Returns document IDs in attached tab order, followed by detached insertion order.
    std::vector<std::string> GetDocumentIds() const;
    //! Returns an owned lifecycle/presentation snapshot, diagnosing an unknown identity.
    DocumentInfo GetDocumentInfo(std::string_view id) const;
    //! Changes the caption while preserving the view and stable identity.
    void SetDocumentTitle(std::string_view id, std::string title);
    //! Reports external edits; repeated true calls advance the saved-value guard.
    void SetDocumentModified(std::string_view id, bool modified = true);
    //! Replaces the application persistence handler for subsequent saves.
    void SetSaveHandler(std::string_view id, std::function<void(DocumentSaveRequest)> handler);
    //! Selects/reveals an attached document or restores its floating host.
    void SetCurrentDocument(std::string_view id);
    //! Returns the active stable identity, or an empty string when none exists.
    std::string GetCurrentDocument() const;
    //! Reorders an attached tab while preserving its actual view and selection.
    void MoveDocument(std::string_view id, int destination);
    //! Transfers the existing view to a separate shared-context window; requires a Window constructor.
    Window& DetachDocument(std::string_view id, std::optional<Rect> desktopBounds = {});
    //! Returns the existing view to the tab group at an optional insertion index.
    void AttachDocument(std::string_view id, int index = -1);
    //! Returns a detached host or null; ownership remains with this workspace.
    Window* GetFloatingWindow(std::string_view id) const;
    //! Starts or coalesces a save and returns its worker-safe completion ticket.
    DocumentSaveRequest SaveDocument(std::string_view id);
    //! Preflights save/discard/cancel before closing one document asynchronously.
    DocumentCloseSession RequestClose(std::string_view id);
    //! Preflights all current documents before removing any requested views.
    DocumentCloseSession RequestCloseAll();
    //! Cancels close preflight while leaving application saves and document views intact.
    void CancelClose();
    //! Returns shared active-document undo, redo, save and close commands.
    std::shared_ptr<Action> UndoAction();
    //! Returns the shared active-document redo command.
    std::shared_ptr<Action> RedoAction();
    //! Returns the shared active-document save command.
    std::shared_ptr<Action> SaveAction();
    //! Returns the shared active-document close command.
    std::shared_ptr<Action> CloseAction();
    //! Exposes read-only navigation structure for styling and inspection.
    const TabWidget& Tabs() const;
    //! Returns the most recent contained presentation/observer exception.
    std::exception_ptr GetCallbackError() const;
    //! Reports a coherent workspace change with bounded reentrant notification.
    Signal<>& Changed();
    //! Reports the stable active identity after selection or document lifecycle changes.
    Signal<std::string>& CurrentChanged();

  private:
    //! Connects owner-side services and commands once the widget is attached.
    void Initialize();
    //! Retains lifecycle state across callbacks that remove the workspace.
    struct Impl;
    //! Owns documents, windows, pending decisions and weak service subscriptions.
    std::shared_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
