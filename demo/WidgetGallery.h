#pragma once

#include "DemoScene.h"

#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Adds interactive collection, 3D, and explicit-layout pages to the demo.
class WidgetGallery
{
  public:
    //! Builds pages after the existing Inputs page.
    WidgetGallery(Window& window, TabWidget& tabs);

    //! Runs interaction/geometry/rendering checks and saves real framebuffer captures.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Creates the data/control gallery page.
    void BuildCollections(Widget& page);

    //! Creates independently rendered 3D surfaces and camera controls.
    void BuildScenes(Widget& page);

    //! Creates the fixed/content/fill layout playground.
    void BuildLayouts(Widget& page);

    //! Appends a table row and updates the status label.
    void AddTableRow();

    //! Copies the selected item from the last context-menu source.
    void CopyCurrentItem();

    //! Displays measured layout geometry.
    void UpdateLayoutReadout();

    //! Checks texture sharing, state isolation, resize/DPI, and failure recovery.
    void VerifyContexts(const std::filesystem::path& assets, const std::filesystem::path& output);

    //! Observes the containing native window.
    Window& m_Window;

    //! Observes the demo's page owner.
    TabWidget& m_Tabs;

    //! Observes the collection hierarchy.
    TreeWidget* m_Tree = nullptr;

    //! Observes the large list.
    ListWidget* m_List = nullptr;

    //! Observes the editable data table.
    TableWidget* m_Table = nullptr;

    //! Observes the shared command popup.
    ContextMenu* m_Menu = nullptr;

    //! Observes the copy action for dynamic enablement.
    ToolButton* m_CopyAction = nullptr;

    //! Observes the currently selected context-menu source.
    WidgetRef m_MenuSource;

    //! Observes the collection-page status line.
    Label* m_Status = nullptr;

    //! Observes the Add row command for interaction tests.
    ToolButton* m_AddRow = nullptr;

    //! Observes the compact row toggle.
    ToolButton* m_Compact = nullptr;

    //! Owns the primary scene state, retained by its surface callbacks.
    std::shared_ptr<DemoScene> m_MainScene;

    //! Owns the secondary scene state.
    std::shared_ptr<DemoScene> m_SecondaryScene;

    //! Observes the primary embedded surface.
    OpenGLWidget* m_MainSurface = nullptr;

    //! Observes the secondary embedded surface.
    OpenGLWidget* m_SecondarySurface = nullptr;

    //! Observes the fixed-width layout example.
    Widget* m_LayoutFixed = nullptr;

    //! Observes the capped Fill example.
    Widget* m_LayoutCapped = nullptr;

    //! Observes the remaining Fill example.
    Widget* m_LayoutFluid = nullptr;

    //! Observes the geometry readout.
    Label* m_LayoutReadout = nullptr;
};
} // namespace EStudioUI
