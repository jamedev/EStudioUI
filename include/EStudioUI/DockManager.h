#pragma once

#include "DockWidget.h"
#include "Window.h"

#include <optional>
#include <string_view>

namespace EStudioUI
{
//! The four persistent tab groups surrounding the central workspace.
enum class DockArea
{
    //! Left of the central widget.
    Left,

    //! Right of the central widget.
    Right,

    //! Above the middle workspace.
    Top,

    //! Below the middle workspace.
    Bottom
};

//! Owns a resizable, tabbed workspace with real floating windows and bounded persistence.
class DockManager final : public Widget
{
  public:
    //! Binds floating surfaces to their containing owner window.
    explicit DockManager(Window& window);

    //! Disconnects native callbacks before destroying floating surfaces.
    ~DockManager() override;

    //! Returns the ordinary central application container.
    Widget& Central() const;

    //! Creates a uniquely identified persistent tool panel; at most 128 are admitted.
    DockWidget& AddDock(std::string id, std::string title, DockArea area = DockArea::Left);

    //! Finds a panel by stable identity, or returns null.
    DockWidget* FindDock(std::string_view id) const;

    //! Moves a panel to an edge tab group and selects it; an index controls tab order.
    void Dock(DockWidget& panel, DockArea area, int index = -1);

    //! Inserts a panel beside another panel in its tab group.
    void Tabify(DockWidget& panel, DockWidget& beside);

    //! Moves the existing panel into an owned native window with physical desktop bounds.
    void Float(DockWidget& panel, std::optional<Rect> desktopBounds = {});

    //! Shows/selects or hides a panel while retaining its widgets and values.
    void SetDockVisible(DockWidget& panel, bool visible);

    //! Returns whether a panel is open, including inactive tabs.
    bool IsDockVisible(const DockWidget& panel) const;

    //! Returns whether the panel occupies a separate native window.
    bool IsFloating(const DockWidget& panel) const;

    //! Returns the panel's remembered edge group, also while floating or closed.
    DockArea GetDockArea(const DockWidget& panel) const;

    //! Observes the floating host, or returns null for a docked panel.
    Window* GetFloatingWindow(const DockWidget& panel) const;

    //! Returns the prepared local rectangle of one edge tab group.
    Rect GetAreaBounds(DockArea area) const;

    //! Saves a bounded, versioned workspace snapshot without accessing the filesystem.
    std::string SaveState() const;

    //! Prevalidates the complete snapshot before applying it; unknown panel IDs are ignored.
    bool RestoreState(std::string_view state, std::string& error);

    //! Reports a committed workspace operation; observers may query the complete state.
    Signal<>& WorkspaceChanged();

  private:
    friend class DockWidget;

    //! Refreshes a selector caption without changing focus or tab order.
    void UpdateTitle(DockWidget& panel);

    //! Starts a grip drag using physical desktop coordinates.
    void BeginDrag(DockWidget& panel, Point screenPoint);

    //! Updates the edge drop preview without changing panel ownership.
    void UpdateDrag(Point screenPoint);

    //! Commits a grip drop to an edge or a floating surface.
    void EndDrag(Point screenPoint);

    //! Clears a cancelled grip drag and its preview.
    void CancelDrag();

    //! Owns stable entries, tab groups, native hosts and callback connections.
    struct Impl;

    //! Keeps implementation details out of application headers.
    std::shared_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
