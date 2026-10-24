#pragma once
#include <EStudioUI/ListWidget.h>

namespace EStudioUI
{
//! Virtual palette with analytic GPU icons and ordinary captured list input.
class WidgetPalette final : public ListWidget
{
  public:
    //! Creates a keyboard-accessible, scrollable widget catalog.
    WidgetPalette();
    //! Replaces owned type names without allocating one widget per entry.
    void SetEntries(std::vector<std::string> entries);
    //! Reports a type, window-coordinate pointer and completed/cancelled gesture; empty type cancels.
    Signal<const std::string&, Point, bool>& Dragged();

  protected:
    //! Adds bounded vector icons after the list's cached text and rows.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;
    //! Retains ordinary row selection and starts a potential drag.
    void OnPointerDown(const PointerEvent& event) override;
    //! Starts dragging after a logical-pixel threshold.
    void OnPointerMove(const PointerEvent& event) override;
    //! Completes an owned drag without synthesizing a click.
    void OnPointerUp(const PointerEvent& event) override;
    //! Clears preview feedback when capture is cancelled.
    void OnCaptureLost() override;
    //! Cancels a drag with Escape while retaining normal list navigation.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Owns catalog text used by bounded visible-row icon rendering.
    std::vector<std::string> m_Entries;
    //! Captures the immutable type for a gesture across selection/filter changes.
    std::string m_DragType;
    //! Records the initial window coordinate.
    Point m_Start;
    //! Tracks the threshold transition.
    bool m_Dragging = false;
    //! Reports owned drag state synchronously at the input boundary.
    Signal<const std::string&, Point, bool> m_Dragged;
};
} // namespace EStudioUI
