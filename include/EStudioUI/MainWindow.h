#pragma once

#include "DockManager.h"
#include "StatusBar.h"
#include "ToolBar.h"
#include "WindowTitleBar.h"

namespace EStudioUI
{
//! A small application shell with title-row menus/widgets, toolbars, docks and status.
class MainWindow final : public Widget
{
  public:
    //! Creates the shell; add it directly to the containing Window's root.
    explicit MainWindow(Window& window);

    //! Returns the title row for menus and arbitrary application controls.
    WindowTitleBar& TitleBar() const;

    //! Adds an overflow-aware command row below the title bar.
    ToolBar& AddToolBar();

    //! Returns the central container for application views and document tabs.
    Widget& Central() const;

    //! Returns the workspace manager for tool panels and persistence.
    DockManager& Docks() const;

    //! Returns the message and permanent-indicator row.
    StatusBar& Status() const;

  private:
    //! Observes the owned title row.
    WindowTitleBar* m_TitleBar = nullptr;

    //! Observes the owned toolbar stack.
    Widget* m_ToolBars = nullptr;

    //! Observes the owned workspace.
    DockManager* m_Docks = nullptr;

    //! Observes the owned status row.
    StatusBar* m_Status = nullptr;
};
} // namespace EStudioUI
