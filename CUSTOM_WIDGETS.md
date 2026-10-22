# Custom widgets and GPU painting

Custom controls can register named CSS paint parts with `RegisterStylePart` and
read their cached typed styles through `GetPartStyle`. See
[WIDGET_STYLING.md](WIDGET_STYLING.md) for an example, supported state bits, and
the distinction between shared static parts and fixed animated parts.

EStudioUI supports both ordinary C++ inheritance and composition. `Widget`,
`Label`, `Button`, `TextBox`, `NumericInput`, and `Slider` are extension points.
Use `PaintWidget` when a control also needs its own vector drawing. Its
`PaintContent(Painter&) const` callback records commands into the **same renderer
and GPU batch** as standard controls. It does not create another OpenGL context,
texture-backed surface, or continuous rendering loop.

The working example is `demo/CustomWidgetGallery.h/.cpp`: `DemoDial` derives from
`PaintWidget`, handles pointer capture and keyboard input, emits a typed value
signal, and caches its paths and shaped text. `LabeledNumericInput` composes a
label and a standard validated editor. A slider, number input, and dial share one
value in the **Custom widgets** demo tab.

## Derive a painted control

For example, create `LevelMeter.h`:

```cpp
#pragma once

#include <EStudioUI/PaintWidget.h>

//! A CSS-styled meter drawn through the shared GPU renderer.
class LevelMeter : public EStudioUI::PaintWidget
{
  public:
    //! Creates a meter with its own CSS type selector.
    LevelMeter();

    //! Updates the normalized level and requests only a paint pass.
    void SetLevel(float value);

  protected:
    //! Supplies intrinsic content dimensions.
    EStudioUI::Point Measure(EStudioUI::FontFace& font) const override;

    //! Records local content geometry after the CSS background.
    void PaintContent(EStudioUI::Painter& painter) const override;

  private:
    //! Finite level clamped to zero through one.
    float m_Level = 0;
};
```

And `LevelMeter.cpp`:

```cpp
#include "LevelMeter.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

LevelMeter::LevelMeter() : PaintWidget("level-meter")
{
}

void LevelMeter::SetLevel(float value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("Meter level must be finite.");
    value = std::clamp(value, 0.0f, 1.0f);
    if (value == m_Level)
        return;
    m_Level = value;
    InvalidatePainting();
}

EStudioUI::Point LevelMeter::Measure(EStudioUI::FontFace&) const
{
    return {180, 24};
}

void LevelMeter::PaintContent(EStudioUI::Painter& painter) const
{
    const auto bounds = GetPaintBounds();
    painter.SetPen({0, 0, 0, 0}, 0);
    painter.SetBrush(GetStyle().m_SelectionBackground);
    painter.DrawRoundedRect(
        {0, 0, bounds.m_Width * m_Level, bounds.m_Height},
        std::min(6.0f, bounds.m_Height * 0.5f));
}
```

Add the control with `parent.Add<LevelMeter>()`, and style it normally:

```css
level-meter {
    background: #182536;
    border: 1px solid #34445a;
    border-radius: 10px;
    padding: 6px;
    selection-background-color: #49d8b1;
}
```

The custom callback's origin is the top-left **content** corner after padding and
border. `GetPaintBounds()` returns its zero-origin dimensions. Drawing is clipped
to that content box and the ancestors' clips. `overflow: hidden` also applies the
widget's rounded border-box clip. The normal CSS background and decorations,
inherited opacity, CSS transforms, and owned children still work.

If intrinsic dimensions change, call inherited `Invalidate()` so layout is
recomputed. For a value that only affects drawing, call `InvalidatePainting()`.
Override `IsFocusable()` and the normal `OnPointerDown`, `OnPointerMove`,
`OnPointerUp`, `OnCaptureLost`, and `OnKey` hooks for interactive controls.
Pointer events already account for inverse CSS transforms, but are in layout
coordinates: subtract `GetContentBounds().m_X/m_Y` to obtain local coordinates.

## Compose existing controls

Composition uses the same ownership and styling contracts as any container:

```cpp
class LabeledLength : public EStudioUI::Widget
{
  public:
    //! Creates a reusable label and validated length editor.
    LabeledLength() : Widget("labeled-length")
    {
        SetLayoutDirection(EStudioUI::Axis::Vertical);
        SetGap(8);
        Add<EStudioUI::Label>("Length (mm)");
        m_Input = &Add<EStudioUI::NumericInput>(10);
        m_Input->SetRange(0, 10000);
        m_Input->SetDecimals(3);
        m_Input->SetStep(0.1);
        m_Input->SetWidth(EStudioUI::SizeRule::Fill());
    }

    //! Exposes the existing editor API and ValueChanged signal.
    EStudioUI::NumericInput& GetInput() { return *m_Input; }

  private:
    //! Observes the editor owned by this container.
    EStudioUI::NumericInput* m_Input = nullptr;
};
```

Include `Label.h` and `NumericInput.h` for this example. Real public classes
should keep non-template implementations in a corresponding `.cpp` file.
`Add<T>` transfers child ownership to the parent; do not also own or delete the
returned reference. Existing typed signals require no reflection or code
generator. Use `ScopedConnection` for callbacks whose receiver can disappear
before their sender. If an emitted callback can delete the emitting widget,
emit as the last operation or use `WidgetRef` before touching the widget again.

## Painter commands and state

| Operation | Behavior |
| --- | --- |
| `Save()` / `Restore()` | Save/restore pen, brush, local opacity, transform, and clips; unmatched `Restore()` throws. |
| `Translate(x,y)`, `Scale(x,y)`, `Rotate(radians)`, `Transform(Affine)` | Compose local transforms; rotation is clockwise in the downward-Y coordinate system; transforms must remain finite and invertible. |
| `ClipRect(rect, radius)` | Intersect with a rectangle or rounded rectangle expressed in the current local coordinates. |
| `SetPen(color,width)` | Set stroke/text color and local stroke width; zero width disables shape borders and line strokes. |
| `SetBrush(color)` | Set a solid fill and clear previous brush effects; alpha zero disables the fill. |
| `SetBrush(style)` | Use typed CSS fill, gradient, and shadow settings; the pen supplies the border. No CSS is parsed in the paint call. |
| `SetOpacity(alpha)` | Multiply local opacity with the inherited constructor opacity. Typed brush opacity multiplies shape opacity additionally. |
| `DrawLine`, `DrawPolyline` | Record analytic round-capped segment strokes. |
| `DrawRect`, `DrawRoundedRect`, `DrawCircle` | Record analytic filled/bordered GPU shapes using the current brush and pen. |
| `DrawPath(path)` | Stroke cached, adaptively flattened contours with round-capped segments. |
| `DrawText(layout,baseline)` | Draw a previously shaped `TextLayout` in the current pen color. |
| `DrawText(utf8,baseline,fontSize)` | Convenience overload that shapes on every call; use the layout overload for frequently changing frames. |

A standalone painter can also be created inside an existing widget's `Paint`
override with `Painter(renderer, font, clip, inheritedOpacity, origin)`. Its
fixed outer clip and origin are in the renderer's current coordinate system.
The painter destructor restores **all states it owns**, including local
transforms and unmatched `Save()` calls, even when drawing exits by exception.
Keep a painter within the current paint callback; do not retain it, nest direct
renderer state manipulation within it, or use it from another thread.

When using `SetBrush(GetStyle())`, remember that `PaintWidget` has already
included the widget's CSS opacity in inherited opacity. Set a copied brush's
`m_Opacity` to one when that opacity should only apply once.

## Reuse paths and text

`PainterPath` supports `MoveTo`, `LineTo`, quadratic `QuadTo`, cubic `CubicTo`,
circular `ArcTo`, and `Close`. Arc angles are radians, sweeps are signed and
limited to one turn. An arc connects the current endpoint to its start; without
a current contour it starts a new one. `Close()` ends the contour. Start another
with `MoveTo` or `ArcTo`.

The mutable flatten cache is keyed by source revision and local tolerance. Keep
paths as members of the custom control and only edit them when geometry changes.
`DrawPath` chooses a tolerance with a 0.35 logical-pixel target adjusted for its
own affine transforms; outer CSS transforms can further scale that error.
`Flatten(tolerance)` exposes contours for callers requiring an explicit local
tolerance. Each path caches one tolerance, so sharing a path between differently
scaled painters may rebuild that cache.

There are hard limits of 4096 source commands, 65536 flattened vertices, and 18
subdivision levels. Invalid coordinates and command overflow throw. Reaching a
flattening work budget sets `WasFlatteningLimited()`; the result can have reduced
accuracy or omit later segments. The cache's `GetFlattenBuildCount()` makes
repeated geometry work observable. The painter bounds nested `Save()` calls at
128; rounded/rotated clip nesting also follows the renderer's existing depth
limit of 32.

Cache `FontFace::Shape()` results by the text, font size, and font service used.
The existing `Label` class is a suitable choice whenever ordinary text layout
and CSS alignment are enough. The dial demo keeps its cached `TextLayout` until
its displayed value or CSS font size changes.

This API currently **strokes paths**. It does not implement arbitrary filled
paths, holes/fill rules, general polygon tessellation, images, dash patterns, or
miter/bevel stroke joins. Adjacent round-capped segments overlap; translucent
strokes can accumulate alpha at joins. Filled circles and rectangles use the
analytic shape pipeline. These limits are explicit so application code need not
assume complete QPainter compatibility.

## Verification

`EStudioUIPainterTests` checks contour topology, cache invalidation, sampled
Bezier/arc approximation, collinear overshoot, invalid input, and work budgets.
The demo's `VerifyPainter` framebuffer probe checks the actual GPU implementation
for custom CSS background/border/padding/rounded clipping, transforms, nested
clips, opacity, text, state isolation including exception unwinding, and retained
versus full RGBA frame equality at two rendering scales. Unchanged repeated
frames must submit zero draw instances and repaint zero pixels.

These checks validate correctness and retained rendering behavior. A software
OpenGL test does not establish native hardware GPU timing or utilization.
