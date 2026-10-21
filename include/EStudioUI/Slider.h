#pragma once

#include "Widget.h"

namespace EStudioUI
{
//! A keyboard-accessible scalar slider painted with analytic GPU primitives.
class Slider : public Widget
{
  public:
    //! Creates a horizontal or vertical slider over the range zero through one hundred.
    explicit Slider(Axis orientation = Axis::Horizontal);

    //! Sets finite inclusive bounds and clamps the current value; equal bounds disable movement.
    void SetRange(double minimum, double maximum);

    //! Returns the inclusive minimum.
    double GetMinimum() const;

    //! Returns the inclusive maximum.
    double GetMaximum() const;

    //! Sets the finite positive pointer/key step.
    void SetStep(double step);

    //! Returns the current step.
    double GetStep() const;

    //! Sets and clamps a finite value without applying pointer-step quantization.
    void SetValue(double value);

    //! Returns the committed scalar value.
    double GetValue() const;

    //! Changes horizontal/vertical orientation and requests layout.
    void SetOrientation(Axis orientation);

    //! Returns the current orientation.
    Axis GetOrientation() const;

    //! Emits once after each actual value change, including programmatic changes.
    Signal<double>& ValueChanged();

    //! Emits after a completed pointer gesture, even when its value is unchanged.
    Signal<>& SliderReleased();

    //! Allows keyboard navigation when enabled.
    bool IsFocusable() const override;

  protected:
    //! Returns an intrinsic track size before padding and explicit contracts.
    Point Measure(FontFace& font) const override;

    //! Paints the groove, active range, and round handle.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

    //! Starts pointer tracking and updates the value immediately.
    void OnPointerDown(const PointerEvent& event) override;

    //! Updates the current value during pointer capture.
    void OnPointerMove(const PointerEvent& event) override;

    //! Commits the final pointer value before releasing the gesture.
    void OnPointerUp(const PointerEvent& event) override;

    //! Cancels dragging without emitting a completion signal.
    void OnCaptureLost() override;

    //! Applies arrow, PageUp/PageDown, Home, and End commands.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Steps only while this slider has keyboard focus.
    bool OnScroll(Point delta, Modifiers modifiers) override;

  private:
    //! Maps a pointer coordinate onto the range and quantizes to the configured step.
    void SetFromPointer(Point point);

    //! Adds a bounded number of configured steps without overflowing arithmetic.
    void StepBy(int steps);

    //! Returns a normalized position using overflow-resistant range arithmetic.
    double Fraction() const;

    //! Inclusive minimum.
    double m_Minimum = 0;

    //! Inclusive maximum.
    double m_Maximum = 100;

    //! Committed value.
    double m_Value = 0;

    //! Positive step used for interaction.
    double m_Step = 1;

    //! Track orientation.
    Axis m_Orientation;

    //! Whether a pointer gesture owns capture.
    bool m_Dragging = false;

    //! Delivers scalar changes.
    Signal<double> m_ValueChanged;

    //! Delivers pointer gesture completion.
    Signal<> m_SliderReleased;
};
} // namespace EStudioUI
