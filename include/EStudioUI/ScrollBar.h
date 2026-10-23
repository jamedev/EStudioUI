#pragma once

#include "Widget.h"

namespace EStudioUI
{
//! A proportional viewport scroll control using the collection scrollbar geometry.
class ScrollBar final : public Widget
{
  public:
    //! Creates a vertical scrollbar over zero through one hundred, with page step ten.
    explicit ScrollBar(Axis orientation = Axis::Vertical);
    //! Sets finite inclusive value limits, clamping the value; equal limits fill the track.
    void SetRange(double minimum, double maximum);
    //! Returns the inclusive lower limit.
    double GetMinimum() const;
    //! Returns the inclusive upper limit.
    double GetMaximum() const;
    //! Commits a finite, clamped value without quantizing programmatic updates.
    void SetValue(double value);
    //! Returns the committed value.
    double GetValue() const;
    //! Sets a finite positive increment for arrow keys and wheel notches.
    void SetSingleStep(double step);
    //! Returns the arrow-key increment.
    double GetSingleStep() const;
    //! Sets the finite nonnegative visible extent; also used for track and page-key movement.
    void SetPageStep(double step);
    //! Returns the visible extent used to size the proportional thumb.
    double GetPageStep() const;
    //! Changes orientation and cancels the current drag.
    void SetOrientation(Axis orientation);
    //! Returns the axis, with values increasing rightward or downward.
    Axis GetOrientation() const;
    //! Returns the styled track in prepared layout coordinates.
    Rect GetTrackBounds() const;
    //! Returns the proportional thumb in prepared layout coordinates.
    Rect GetThumbBounds() const;
    //! Reports actual committed value changes, including programmatic changes.
    Signal<double>& ValueChanged();
    //! Accepts ordinary keyboard focus and range commands.
    bool IsFocusable() const override;

  protected:
    //! Measures CSS track thickness without depending on the current value.
    Point Measure(FontFace& font) const override;
    //! Draws the root, track and thumb through the GPU style renderer.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;
    //! Starts a thumb drag or moves one page towards a track press.
    void OnPointerDown(const PointerEvent& event) override;
    //! Updates captured dragging and local part hover.
    void OnPointerMove(const PointerEvent& event) override;
    //! Ends the captured gesture.
    void OnPointerUp(const PointerEvent& event) override;
    //! Cancels a gesture after deactivation or capture replacement.
    void OnCaptureLost() override;
    //! Applies arrows, PageUp/PageDown, Home and End.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;
    //! Steps this axis and bubbles unchanged movement at the range endpoints.
    bool OnScroll(Point delta, Modifiers modifiers) override;
    //! Exposes the canonical numeric range as a scrollbar.
    SemanticInfo DescribeSemantics() const override;
    //! Applies finite, in-range accessibility requests through SetValue.
    SemanticStatus OnSemanticAction(const SemanticCommand& command) override;

  private:
    //! Updates the root's orientation selectors.
    void UpdateOrientation();
    //! Returns the dynamic track/thumb state, including stationary-pointer hover.
    unsigned PartState(bool thumb) const;
    //! Returns the styled track interior.
    Rect TrackContent() const;
    //! Computes page / (value span + page) without overflowing the range.
    double VisibleFraction() const;
    //! Adds a signed increment, clamping arithmetic overflow at an endpoint.
    void StepBy(double amount);
    //! Drains bounded reentrant change notifications without recursive emission.
    void PublishValue();
    //! Inclusive lower bound.
    double m_Minimum = 0;
    //! Inclusive upper bound.
    double m_Maximum = 100;
    //! Canonical value.
    double m_Value = 0;
    //! Last emitted value.
    double m_Notified = 0;
    //! Arrow-key and wheel increment.
    double m_SingleStep = 1;
    //! Visible extent and track/page-key increment.
    double m_PageStep = 10;
    //! Current axis.
    Axis m_Orientation;
    //! Whether this thumb owns a gesture.
    bool m_Dragging = false;
    //! Whether value notification delivery is active.
    bool m_Notifying = false;
    //! Last observed thumb state, avoiding redundant same-part pointer repaints.
    unsigned m_HoverState = 0;
    //! Pointer coordinate at gesture start.
    float m_DragOrigin = 0;
    //! Normalized value at gesture start.
    double m_FractionOrigin = 0;
    //! Canonical value notifications.
    Signal<double> m_ValueChanged;
};
} // namespace EStudioUI
