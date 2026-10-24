#pragma once
#include "Widget.h"

namespace EStudioUI
{
//! Atomic, ordered slider endpoints.
struct RangeValues
{
    //! Inclusive lower endpoint.
    double m_Lower = 25;
    //! Inclusive upper endpoint.
    double m_Upper = 75;
    //! Compares complete interval values.
    bool operator==(const RangeValues&) const = default;
};

//! GPU dual-handle slider with capture, keyboard input and virtual accessible endpoints.
class RangeSlider : public Widget
{
  public:
    //! Creates a horizontal interval over zero through one hundred.
    explicit RangeSlider(Axis orientation = Axis::Horizontal);
    //! Sets finite ordered bounds and clamps both endpoints coherently.
    void SetRange(double minimum, double maximum);
    //! Returns the inclusive outer minimum.
    double GetMinimum() const;
    //! Returns the inclusive outer maximum.
    double GetMaximum() const;
    //! Sets finite ordered endpoints, clamping to the configured range.
    void SetValues(RangeValues values);
    //! Returns the complete committed interval.
    RangeValues GetValues() const;
    //! Sets a finite positive interaction increment.
    void SetStep(double step);
    //! Chooses horizontal or vertical presentation.
    void SetOrientation(Axis orientation);
    //! Reports one coherent interval per actual change.
    Signal<RangeValues>& ValuesChanged();
    //! Reports the end of a completed pointer gesture.
    Signal<>& SliderReleased();
    //! Participates in keyboard navigation.
    bool IsFocusable() const override;

  protected:
    //! Supplies bounded intrinsic track dimensions.
    Point Measure(FontFace& font) const override;
    //! Draws the track, selected interval and two styled handles.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;
    //! Captures the nearest endpoint.
    void OnPointerDown(const PointerEvent& event) override;
    //! Updates the captured endpoint.
    void OnPointerMove(const PointerEvent& event) override;
    //! Completes a pointer gesture.
    void OnPointerUp(const PointerEvent& event) override;
    //! Releases a cancelled gesture.
    void OnCaptureLost() override;
    //! Uses arrows, Home/End and Page keys on the active endpoint.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;
    //! Visits both endpoints with Tab before leaving the control.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;
    //! Supplies interval grouping metadata.
    SemanticInfo DescribeSemantics() const override;
    //! Exposes two stable virtual sliders.
    std::size_t GetSemanticChildCount(SemanticKey parent) const override;
    //! Resolves a virtual endpoint by index.
    std::optional<SemanticKey> GetSemanticChildAt(SemanticKey parent, std::size_t index) const override;
    //! Resolves a virtual endpoint index.
    std::optional<std::size_t> GetSemanticChildIndex(SemanticKey key) const override;
    //! Describes a virtual endpoint and its dependent bounds.
    std::optional<SemanticVirtualNode> DescribeSemanticChild(SemanticKey key) const override;
    //! Returns the focused virtual endpoint without expanding siblings.
    std::optional<SemanticKey> GetSemanticFocus() const override;
    //! Applies canonical endpoint changes.
    SemanticStatus OnSemanticChildAction(SemanticKey key, const SemanticCommand& command) override;

  private:
    //! Synchronizes orientation, active endpoint and fixed-part motion state.
    void UpdateParts();
    //! Overflow-safe normalized coordinate.
    double Fraction(double value) const;
    //! Returns the styled track or one handle in layout coordinates.
    Rect PartBounds(int handle) const;
    //! Quantizes and changes the active endpoint.
    void FromPointer(Point point);
    //! Changes the active endpoint without crossing the other.
    void Change(double value);
    //! Inclusive outer bounds.
    double m_Minimum = 0, m_Maximum = 100;
    //! Positive step.
    double m_Step = 1;
    //! Atomic endpoint value.
    RangeValues m_Values;
    //! Axis policy.
    Axis m_Orientation = Axis::Horizontal;
    //! Active handle, zero for lower or one for upper.
    int m_Active = 0;
    //! Resolves the first movement when a gesture begins with coincident handles.
    bool m_ChoosingHandle = false;
    //! Captured gesture state.
    bool m_Dragging = false;
    //! Bounds synchronous observer feedback.
    unsigned m_NotificationDepth = 0;
    //! Interval observers.
    Signal<RangeValues> m_ValuesChanged;
    //! Gesture completion observers.
    Signal<> m_Released;
};
} // namespace EStudioUI
