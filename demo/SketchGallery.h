#pragma once

#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/SketchWidget.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/SplitToolButton.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/Window.h>

#include <array>
#include <utility>

namespace EStudioUI
{
//! A reusable CAD editing playground with precision inputs and a 200,000-item workload.
class SketchGallery
{
  public:
    //! Builds toolbars, an editable geometry inspector, and the sample sketch.
    SketchGallery(Window& window, Widget& page);

    //! Disconnects viewport notifications before gallery state is destroyed.
    ~SketchGallery();

    //! Exercises native input, geometry commands, and real framebuffer capture.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Names the bounded numeric editors in the geometry inspector.
    enum Field : std::size_t
    {
        X,
        Y,
        EndX,
        EndY,
        Dimension,
        StartAngle,
        SweepAngle,
        PointIndex,
        FontSize,
        FieldCount
    };

    //! Constructs the inspector's fixed, reusable control tree.
    void BuildInspector(Widget& parent);

    //! Loads the selected item's values without rebuilding widgets during pointer previews.
    void UpdateInspector();

    //! Loads one local spline control or path vertex into the coordinate editors.
    void UpdatePointFields();

    //! Returns the single selected item compatible with the active inspector tool.
    const CanvasItem* InspectedItem() const;

    //! Creates or replaces geometry using the validated inspector values.
    void CommitInspector(bool create);

    //! Applies the dimension editor to selected line lengths or circular radii.
    void SetDimension();

    //! Invalidates the canvas and refreshes controls after an explicit document command.
    void DocumentChanged();

    //! Restores an editable plate and examples of every curve primitive.
    void ResetSample();

    //! Builds 200,000 lightweight items and keeps a local neighborhood visible.
    void LoadStress();

    //! Saves the document to the user-editable path and reports I/O errors.
    void SaveDocument();

    //! Atomically loads a versioned sketch from the user-editable path.
    void OpenDocument();

    //! Updates selection, tool, model-size, and zoom readouts.
    void UpdateStatus();

    //! Moves keyboard focus back to the canvas after a toolbar command.
    void FocusCanvas();

    //! Observes the containing native window.
    Window& m_Window;

    //! Observes the canvas owned by the gallery page.
    SketchWidget* m_Sketch = nullptr;

    //! Observes the readout below the canvas.
    Label* m_Status = nullptr;

    //! Observes the concise instructions for the current tool.
    Label* m_Help = nullptr;

    //! Observes the editable native sketch file path.
    TextBox* m_FilePath = nullptr;

    //! Observes the sparse widget anchored in world coordinates.
    Widget* m_Note = nullptr;

    //! Observes the toolbar's selected-tool indicators.
    std::vector<std::pair<SketchTool, ToolButton*>> m_Tools;

    //! Observes the remembered multi-action curve tool.
    SplitToolButton* m_CurveTools = nullptr;

    //! Observes the inspector's geometry type label.
    Label* m_InspectorTitle = nullptr;

    //! Observes the numeric controls by semantic role.
    std::array<NumericInput*, FieldCount> m_Fields{};

    //! Observes the endpoint or size coordinate labels.
    std::array<Label*, 2> m_EndLabels{};

    //! Observes the type-dependent endpoint row.
    Widget* m_EndRow = nullptr;

    //! Observes the type-dependent length or radius row.
    Widget* m_DimensionRow = nullptr;

    //! Observes the length or radius label.
    Label* m_DimensionLabel = nullptr;

    //! Observes the arc angles row.
    Widget* m_AngleRow = nullptr;

    //! Observes the curve control index row.
    Widget* m_PointRow = nullptr;

    //! Observes the annotation text row.
    Widget* m_TextRow = nullptr;

    //! Observes the annotation font size row.
    Widget* m_FontRow = nullptr;

    //! Observes the annotation text editor.
    TextBox* m_Annotation = nullptr;

    //! Observes the path closure toggle.
    ToolButton* m_ClosePath = nullptr;

    //! Observes the precision creation command.
    ToolButton* m_Create = nullptr;

    //! Observes the precision edit command.
    ToolButton* m_Apply = nullptr;

    //! Observes the camera zoom slider.
    Slider* m_Zoom = nullptr;

    //! Stores the currently displayed primitive type.
    CanvasPrimitive m_InspectorKind = CanvasPrimitive::Line;

    //! Prevents programmatic inspector changes from writing back through callbacks.
    bool m_UpdatingInspector = false;

    //! Prevents camera/slider synchronization from recursing.
    bool m_UpdatingZoom = false;

    //! Disconnects sketch notifications before gallery teardown.
    Connection m_EditConnection;

    //! Disconnects camera notifications before gallery teardown.
    Connection m_ViewConnection;

    //! Stores the last explicitly requested bulk-load duration.
    double m_LoadMilliseconds = 0;

    //! Reports the most recent explicit operation or validation error.
    std::string m_FileStatus;
};
} // namespace EStudioUI
