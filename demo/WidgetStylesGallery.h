#pragma once

#include "CustomWidgetGallery.h"
#include "DemoScene.h"

#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/TreeWidget.h>

#include <array>

namespace EStudioUI
{
//! Exercises rich CSS on working controls, virtualized collections, and GPU surfaces.
class WidgetStylesGallery
{
  public:
    //! Builds three switchable style presets without replacing the containing stylesheet.
    WidgetStylesGallery(Window& window, Widget& page, const std::filesystem::path& assets);

    //! Disconnects callbacks before releasing gallery state.
    ~WidgetStylesGallery();

    //! Runs actual input, theme, virtualization, and retained-frame checks and saves captures.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Builds validated text, scalar inputs, and composed commands.
    void BuildInputs(Widget& page);

    //! Builds virtualized list, hierarchy, and editable table examples.
    void BuildCollections(Widget& page);

    //! Builds the infinite canvas, an independent 3D context, and a custom painted control.
    void BuildSurfaces(Widget& page);

    //! Switches compiled preset selectors using one owner class.
    void SetPreset(int index);

    //! Synchronizes the numeric input, horizontal slider, and status without recursion.
    void SetValue(double value);

    //! Updates the visible interaction readout.
    void SetStatus(std::string message);

    //! Observes the containing native window.
    Window& m_Window;

    //! Observes the styled page owned by the main tab widget.
    Widget& m_Page;

    //! Observes the nested category tabs.
    TabWidget* m_Tabs = nullptr;

    //! Observes the active-preset commands.
    std::array<ToolButton*, 3> m_Presets{};

    //! Records the active selector preset.
    int m_Preset = 0;

    //! Observes the ordinary placeholder editor.
    TextBox* m_Name = nullptr;

    //! Observes a validator-driven editor initially containing invalid legacy text.
    TextBox* m_Validated = nullptr;

    //! Observes the composed numeric editor.
    NumericInput* m_Number = nullptr;

    //! Observes the scalar slider paired with the numeric editor.
    Slider* m_Slider = nullptr;

    //! Observes the independently styled vertical slider.
    Slider* m_Vertical = nullptr;

    //! Observes the composed default-action selector.
    SplitToolButton* m_Split = nullptr;

    //! Observes the command that opens the owned popup.
    ToolButton* m_MenuButton = nullptr;

    //! Observes the root-owned popup with logical selector ancestry.
    ContextMenu* m_Menu = nullptr;

    //! Observes the optional animation toggle.
    ToolButton* m_Motion = nullptr;

    //! Observes the CSS keyframe example, animated only after explicit opt-in.
    Button* m_MotionSample = nullptr;

    //! Observes the list's virtual model view.
    ListWidget* m_List = nullptr;

    //! Observes the hierarchy's virtual model view.
    TreeWidget* m_Tree = nullptr;

    //! Records the expandable root used by the native verification.
    TreeWidget::NodeId m_TreeRoot = 0;

    //! Observes the editable virtualized table.
    TableWidget* m_Table = nullptr;

    //! Observes the double-precision canvas surface.
    CanvasWidget* m_Canvas = nullptr;

    //! Observes the independently rendered scene surface.
    OpenGLWidget* m_Surface = nullptr;

    //! Owns the context-specific scene state through shared callback captures.
    std::shared_ptr<DemoScene> m_Scene;

    //! Observes a derived custom-painted control using the same stylesheet.
    DemoDial* m_Dial = nullptr;

    //! Observes the compact interaction readout.
    Label* m_Status = nullptr;

    //! Suppresses recursive value synchronization.
    bool m_Updating = false;

    //! Counts real action invocations for verification.
    unsigned m_Actions = 0;

    //! Releases callbacks safely when the containing gallery is destroyed.
    std::vector<Connection> m_Connections;
};
} // namespace EStudioUI
