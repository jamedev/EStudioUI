#pragma once

#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/Models.h>
#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Exercises shared typed records, projections, selection, editing, and owned worker publication.
class ModelGallery
{
  public:
    //! Builds working collection views over one 100000-record application model.
    ModelGallery(Window& window, Widget& page, std::filesystem::path assets);

    //! Cancels delivery and cooperatively joins the value-only worker before gallery teardown.
    ~ModelGallery();

    //! Runs actual viewport, editing, snapshot, lifetime, and retained/full framebuffer checks.
    void Verify(const std::filesystem::path& output);

  private:
    //! Publishes an owned replacement on a bounded worker with stale-revision checks.
    void Reload();

    //! Applies 10000 final field edits in one coherent model revision.
    void ApplyBatch();

    //! Toggles the right-hand view's invalid-entity filter.
    void ToggleFilter();

    //! Removes the selected canonical record and prunes every shared selection view.
    void RemoveSelected();

    //! Waits for the latest worker with bounded owner-side event service in the verification path.
    void Await();

    //! Updates the weakly observed gallery status.
    void Status(std::string message);

    //! Counts actual external storage accesses across all views.
    struct Reads;

    //! Shares thread-safe access instrumentation with immutable snapshots.
    std::shared_ptr<Reads> m_Reads;

    //! Observes the containing owner window.
    Window& m_Window;

    //! Locates the installed font for native lifetime probes.
    std::filesystem::path m_Assets;

    //! Owns canonical typed records independently of their displayed order.
    std::shared_ptr<ItemModel> m_Model;

    //! Shares one filtered/sorted mapping with the right table.
    std::shared_ptr<SortFilterModel> m_Filtered;

    //! Shares current/selected identities across four collection views.
    std::shared_ptr<SelectionModel> m_Selection;

    //! Observes the source table without extending widget lifetime.
    WidgetRef m_Table;

    //! Observes the projected table.
    WidgetRef m_ProjectTable;

    //! Observes the shared list.
    WidgetRef m_List;

    //! Observes the shared hierarchy view.
    WidgetRef m_Tree;

    //! Observes the current record readout.
    WidgetRef m_Summary;

    //! Observes the status label.
    WidgetRef m_Status;

    //! Observes the invalid-only toolbar toggle for programmatic verification.
    WidgetRef m_FilterToggle;

    //! Keeps model and selection callbacks scoped to this gallery's lifetime.
    std::vector<ScopedConnection> m_Connections;

    //! Lazily owns one worker only when publication is requested.
    std::unique_ptr<BackgroundTasks> m_Tasks;

    //! Supersedes older reload results.
    RequestScope m_Requests;

    //! Exposes computation and delivery outcome.
    Task m_Task;

    //! Records whether the invalid-only filter is enabled.
    bool m_InvalidOnly = false;
};
} // namespace EStudioUI
