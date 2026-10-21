#pragma once

#include "CanvasWidget.h"
#include "SketchDocument.h"

namespace EStudioUI
{
//! Interactive commands supported by the first two-dimensional CAD editor.
enum class SketchTool
{
    Select,
    Pan,
    Line,
    Rectangle,
    Circle
};

//! A viewport and pointer/keyboard editor for an undoable sketch document.
class SketchWidget : public CanvasWidget
{
  public:
    //! Creates a document, shared item styles, and the selection tool.
    SketchWidget();

    //! Returns the GUI-thread document; call SceneChanged after direct model edits.
    SketchDocument& GetDocument();

    //! Returns the sketch document without permitting mutation.
    const SketchDocument& GetDocument() const;

    //! Changes tools and cancels any uncommitted pointer gesture.
    void SetTool(SketchTool tool);

    //! Returns the current editing tool.
    SketchTool GetTool() const;

    //! Enables endpoint/center and ten-unit grid snapping for new geometry.
    void SetSnapping(bool enabled);

    //! Returns whether endpoint and grid snapping are enabled.
    bool IsSnapping() const;

    //! Undoes one command after canceling any pointer preview.
    bool Undo();

    //! Reapplies one undone command after canceling any pointer preview.
    bool Redo();

    //! Removes the selected shapes as one undoable command.
    bool DeleteSelection();

    //! Returns the shared CSS style handle used for created sketch geometry.
    std::uint32_t GetShapeStyle() const;

    //! Reports committed editing, selection, and tool changes.
    Signal<>& Changed();

  protected:
    //! Starts creation, picking, selection dragging, or a marquee.
    void OnPointerDown(const PointerEvent& event) override;

    //! Updates geometry previews without mutating the spatial index.
    void OnPointerMove(const PointerEvent& event) override;

    //! Commits the gesture as one undoable command.
    void OnPointerUp(const PointerEvent& event) override;

    //! Handles tools, deletion, cancellation, and undo/redo shortcuts.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Cancels uncommitted geometry when keyboard focus is lost.
    void OnFocus(bool focused) override;

    //! Cancels uncommitted geometry when pointer capture is lost.
    void OnCaptureLost() override;

  private:
    //! Keeps the displayed scene bound to this editor's document.
    using CanvasWidget::SetScene;

    //! Identifies the uncommitted pointer operation.
    enum class Gesture
    {
        None,
        Create,
        Move,
        Marquee
    };

    //! Clears temporary drawing and resets the pointer operation.
    void CancelGesture();

    //! Converts pointer motion into an optionally snapped model coordinate.
    CanvasPoint GesturePoint(const PointerEvent& event) const;

    //! Builds the current new shape from the gesture endpoints.
    CanvasGeometry PreviewShape() const;

    //! Updates temporary shapes or the marquee for the current gesture.
    void UpdatePreview(const PointerEvent& event);

    //! Invalidates canvas caches before notifying observers, which may delete the widget.
    void NotifyChanged();

    //! Keeps model storage alive while its scene is referenced by the viewport.
    std::shared_ptr<SketchDocument> m_Document;

    //! Stores the selected tool independently of focus.
    SketchTool m_Tool = SketchTool::Select;

    //! Records the current pointer operation.
    Gesture m_Gesture = Gesture::None;

    //! Stores the gesture's original model point.
    CanvasPoint m_Start;

    //! Stores the latest preview endpoint or move displacement endpoint.
    CanvasPoint m_Current;

    //! Distinguishes a deliberate drag from a click in logical pixels.
    Point m_StartScreen;

    //! Preserves Shift selection semantics throughout a marquee gesture.
    bool m_Additive = false;

    //! Enables geometric and grid snapping.
    bool m_Snapping = true;

    //! Records whether pointer movement passed the drag threshold.
    bool m_Moved = false;

    //! Uses one compiled normal/selected CSS style for all ordinary geometry.
    std::uint32_t m_ShapeStyle = 0;

    //! Uses one compiled translucent style for uncommitted geometry.
    std::uint32_t m_PreviewStyle = 0;

    //! Delivers committed model and tool changes.
    Signal<> m_Changed;
};
} // namespace EStudioUI
