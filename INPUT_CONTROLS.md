# Scalar inputs and tool choosers / EStudioUI 0.7

The Sketcher and Custom widgets pages use the same public controls available to
applications. They add no dependencies. All input, validation, and signals stay
on the GUI thread; painting uses the existing GPU renderer.

## Numeric input

```cpp
#include <EStudioUI/NumericInput.h>

auto& radius = panel.Add<EStudioUI::NumericInput>(25.0);
radius.SetRange(0.001, 1000000.0);
radius.SetStep(0.5);
radius.SetDecimals(3);
radius.ValueChanged().Connect([](double value) { /* update the model */ });
```

`NumericInput` composes an ordinary `TextBox` with two `ToolButton` children.
`GetEditor()` exposes the text box for placeholders, focus, and application
composition. Enter or focus loss commits acceptable input; an incomplete or
out-of-range value restores the last committed value. Escape restores the value.
Up/Down and the increment/decrement buttons step it; wheel input steps only while
the editor has focus. Values clamp to the configured inclusive range.

`GetValue()` returns the committed double, independent of intermediate editor
text. `SetDecimals()` changes formatting without reducing stored precision.
`ValueChanged(double)` fires only when the committed value changes, including
programmatic updates. `EditingFinished(double)` reports successful commits.
Call `Commit()` before reading typed input from an explicit Apply command.

## Validation

```cpp
#include <EStudioUI/InputValidator.h>

editor.SetValidator(std::make_shared<EStudioUI::IntegerValidator>(1, 100));
```

Validators classify the entire candidate string as `Invalid`, `Intermediate`,
or `Acceptable`. An invalid edit or paste is rejected atomically, preserving
text, selection, and undo history. Intermediate strings remain editable but do
not emit `Submitted()`. Empty input, signs, and out-of-range numeric values can
be intermediate; malformed numbers are invalid. `DoubleValidator` supports
finite decimal and scientific notation using a dot separator, independent of
the process locale. `IntegerValidator` accepts signed decimal integers.

Derive from `InputValidator` and override the const `Validate(std::string_view)`
method for domain-specific policies. Policies must be pure and cannot modify
the editor during validation. Shared policies are immutable. Replacing a policy
does not rewrite existing text; `GetValidationState()` reports its classification.
`SetText()` rejects invalid candidates too. Removing the validator restores
ordinary unrestricted text editing.

## Slider

```cpp
#include <EStudioUI/Slider.h>

auto& zoom = panel.Add<EStudioUI::Slider>();
zoom.SetRange(10, 400);
zoom.SetStep(5);
zoom.SetValue(100);
zoom.ValueChanged().Connect([](double percent) { /* update view */ });
```

Sliders support horizontal/vertical orientation, pointer capture, arrow keys,
PageUp/PageDown, Home/End, and focused wheel input. Vertical values increase
upward. Pointer input snaps to the configured step; programmatic values retain
their supplied precision. Equal endpoints produce a stationary range.
`SliderReleased()` reports a completed pointer gesture. Callback delivery allows
deletion of the control, as with the existing buttons.

## Split tool button

```cpp
auto& tools = toolbar.AddSplitButton("Curves");
tools.AddAction("Arc");
tools.AddAction("Spline");
tools.Triggered().Connect([](std::size_t index) { /* select tool */ });
```

The primary button invokes the remembered action; the arrow opens an ordinary
`ContextMenu`. Choosing an action changes the primary caption and emits
`Triggered(index)` once. `GetAction(index)` exposes each ordinary tool button
for enablement or additional callbacks. `SetDefaultAction(index)` changes the
default without invoking it. Down/Alt+Down/Menu opens the chooser; popup arrow
keys and Enter choose an action. Toolbar Left/Right navigation includes both
halves of the split control.

`UiContext::AddOwnedPopup()` supports this composition by owning the root popup
while observing its logical owner. Hidden/disabled owners dismiss their popup;
deleted owners have their popup removed during preparation. Root cleanup avoids
reentrant sibling deletion. Applications can use the same API for custom controls.

## Styling

The normal theme supplies `numericinput`, `slider`, and `splittoolbutton` rules.
Numeric children expose `numeric-editor`, `numeric-increment`, and
`numeric-decrement` classes; the chooser uses `split-primary` and `split-arrow`.
Slider `grid-color` paints its groove, `selection-background-color` its active
track/focus ring, `color` its handle, and `border-color` the idle handle outline.
These are existing compiled CSS properties; there is no parsing in input or paint.

For a control built through inheritance or composition, see
[CUSTOM_WIDGETS.md](CUSTOM_WIDGETS.md).
