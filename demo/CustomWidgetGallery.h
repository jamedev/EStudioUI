#pragma once

#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/PaintWidget.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! An interactive custom-painted control illustrating widget derivation.
class DemoDial : public PaintWidget
{
  public:
    //! Creates a zero-to-one-hundred dial with cached vector geometry.
    DemoDial();

    //! Clamps and updates a finite value, emitting only after an actual change.
    void SetValue(double value);

    //! Returns the current dial value.
    double GetValue() const;

    //! Reports changes from pointer, keyboard, and application code.
    Signal<double>& ValueChanged();

    //! Includes this interactive custom control in keyboard navigation.
    bool IsFocusable() const override;

  protected:
    //! Supplies an intrinsic size independently of CSS padding and border.
    Point Measure(FontFace& font) const override;

    //! Records analytic GPU shapes, cached paths, and cached shaped text.
    void PaintContent(Painter& painter) const override;

    //! Starts pointer capture and chooses a value from the dial angle.
    void OnPointerDown(const PointerEvent& event) override;

    //! Updates the value while the dial owns capture.
    void OnPointerMove(const PointerEvent& event) override;

    //! Commits the final captured pointer position.
    void OnPointerUp(const PointerEvent& event) override;

    //! Cancels a pointer drag after capture is lost.
    void OnCaptureLost() override;

    //! Implements arrows, Home, and End using the same typed value setter.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Maps an untransformed layout point to a bounded dial value.
    void SetFromPointer(Point point);

    //! Stores the finite value exposed to applications.
    double m_Value = 42;

    //! Tracks active pointer capture.
    bool m_Dragging = false;

    //! Caches normalized arc geometry across paints.
    PainterPath m_Track;

    //! Caches normalized decorative Bezier geometry across paints.
    PainterPath m_Wave;

    //! Caches shaped value text rather than reshaping every frame.
    mutable TextLayout m_ValueLayout;

    //! Identifies the cached rounded value text.
    mutable std::string m_ValueText;

    //! Identifies the cached CSS font size.
    mutable float m_FontSize = -1;

    //! Delivers typed value notifications safely when a callback removes the dial.
    Signal<double> m_ValueChanged;
};

//! Combines ordinary owned widgets into a reusable labeled numeric editor.
class LabeledNumericInput : public Widget
{
  public:
    //! Creates an owned label and the existing validated numeric control.
    LabeledNumericInput(std::string label, double value);

    //! Exposes the composed control's ordinary API and typed signals.
    NumericInput& GetInput();

  private:
    //! Observes the numeric input owned by this widget.
    NumericInput* m_Input = nullptr;
};

//! Demonstrates derived GPU painting and composition using normal widget ownership.
class CustomWidgetGallery
{
  public:
    //! Builds synchronized dial, slider, and composed numeric-input examples.
    CustomWidgetGallery(Window& window, Widget& page);

    //! Disconnects callbacks before releasing gallery state.
    ~CustomWidgetGallery();

    //! Exercises actual dial input and captures the rendered custom controls.
    void Verify(const std::filesystem::path& output);

  private:
    //! Synchronizes the three controls without recursively updating the gallery.
    void SetValue(double value);

    //! Observes the demo's window.
    Window& m_Window;

    //! Observes the custom painted control.
    DemoDial* m_Dial = nullptr;

    //! Observes the slider demonstrating ordinary composition.
    Slider* m_Slider = nullptr;

    //! Observes the validated number control.
    NumericInput* m_Input = nullptr;

    //! Prevents recursive synchronization through typed change signals.
    bool m_Updating = false;

    //! Disconnects gallery callbacks when the gallery leaves scope.
    std::vector<Connection> m_Connections;
};
} // namespace EStudioUI
