# Measurement and layout / EStudioUI 0.12

F08 adds width-aware measurement, shared layout strategies, baselines, and owned
allocation reports. Existing Fixed/Content/Fill APIs and CSS precedence remain.
No dependency or constraint solver was added. The **Layout lab** is the default
demo page; its form fields and wrapping tool buttons are working controls.

## A small working form

Run from the demo executable directory, where assets/NotoSans.ttf is copied.

```cpp
#include <EStudioUI/Application.h>
#include <EStudioUI/FormLayout.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Window.h>

int main()
{
    using namespace EStudioUI;
    Application app;
    Window window(WindowOptions{}, "assets/NotoSans.ttf");
    auto& form = window.Ui().Root();
    form.SetLayout(FormLayout{});
    form.SetGap(12);
    form.SetStyleProperty(StyleProperty::Padding, Insets{20, 20, 20, 20});

    form.Add<Label>("Name");
    form.Add<TextBox>("Bracket").SetHeight(SizeRule::Fixed(36));
    form.Add<Label>("Thickness / mm");
    auto& thickness = form.Add<NumericInput>();
    thickness.SetValue(2.5);
    thickness.SetHeight(SizeRule::Fixed(36));
    form.Add<Label>("Notes");
    auto& notes = form.Add<Label>(
        "This field wraps at its allocated width. The form row grows automatically.");
    notes.SetWordWrap(true);
    return app.Run();
}
```

Children retain ordinary unique ownership. SetLayout accepts a strategy value
or unique_ptr<LayoutStrategy>; nullptr restores the default CSS/code row or
column. An installed strategy is immutable through GetLayout. Replace it at an
idle owner-thread boundary to change its configuration. Existing
SetLayoutDirection continues to configure the default box strategy.

## Sizing rules

| Rule | Allocation behavior |
| --- | --- |
| Fixed(px) | Hard outer size; never stretched or squeezed, including after theme reloads |
| Content(min,max) | Preferred content size, bounded by limits; cross-axis stretch may assign available space within those limits |
| Fill(weight,min,max) | Starts at its effective minimum and shares remaining space by weight; capped shares are redistributed |
| CSS pixels/percent/fr | Used beneath explicit C++ rules; legacy flex-grow remains supported |

The window root occupies the viewport. Sizing rules govern in-flow children.
Border and padding are inside the outer allocation. Margins and gaps consume
space outside it. Content minima propagate through containers, bounded by explicit
limits; a hard maximum can therefore clip larger descendant requirements rather
than silently violate the maximum. Content/fixed bases do not shrink to absorb a
main-axis deficit. Overflow remains inspectable. Explicit floating bounds stay
out of normal flow; their caller owns geometry and the ancestor clip still applies.

Measurement resolves width before height. A row allocates fixed/content/fill
widths, measures width-dependent children at those widths, then determines the
required height. Columns, grid rows, forms, and flow rows use that same boundary.
A local content edit invalidates its own content measurement and ancestor
aggregate requirements; unrelated intrinsic results remain reusable.

A percentage needs an independent parent dimension: a viewport, fixed size,
resolved percentage, Fill allocation, or stretched cross-axis allocation. A
content-derived dimension does not become independent just because its final
numeric size is known. An unresolved percentage uses Content sizing and emits
IndefinitePercentage. No fixed-point iteration, previous-frame feedback, or
implicit nonlinear solve is performed. Width-dependent custom content must not
introduce height-to-width feedback.

## Strategies and alignment

| Strategy | Behavior |
| --- | --- |
| Default box / BoxLayout(axis) | Row or column using the existing bounded allocator |
| GridLayout(columns, rows) | Displayed in-flow children placed consecutively in row-major order; shared Content/Fixed/Fill columns; unspecified rows are Content |
| FormLayout(labels, fields) | Pairs consecutive label/field children; shared columns and baseline alignment |
| FlowLayout | Consecutive children wrap into rows at the allocated width; each row reuses box allocation and justification |

Grid is deliberately row-major in this milestone; explicit cell spans and
placement are not part of its API. Nest a container when children need their own
layout. Grid column and row rules are outer track sizes including the child
margin budget; gaps separate tracks. Explicit empty row tracks are supported.
Fill tracks use child minima, while Content tracks use measured preferred sizes.
A fixed or capped track may be smaller than a fixed child; the child's hard rule
wins and the overflow is reported.

SetAlignment controls Stretch/Start/Center/End/Baseline. SetJustification controls
Start/Center/End/SpaceBetween. Default boxes also read CSS align-items and
justify-content. Baseline alignment uses the actual painted baseline after
padding, border, and vertical text placement. Labels/buttons and text/numeric
editors supply baselines; a child without one uses start alignment. Containers
propagate their first participating child's baseline. SetBaselineChild selects an
immediate child for composed controls; NumericInput chooses its editor. A hidden
selected child supplies no baseline, and deletion restores the default selection.
Form uses baseline alignment as its strategy policy; use a two-column GridLayout for other cell
alignment policies. Flow justification applies separately to each
row. In a flow, an item wider than the container retains its hard size and
overflows; width-dependent content can wrap inside one bounded row.

```cpp
#include <EStudioUI/FlowLayout.h>
#include <EStudioUI/GridLayout.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/Label.h>

void BuildTools(EStudioUI::Widget& parent)
{
    using namespace EStudioUI;
    auto& grid = parent.Add<Widget>();
    grid.SetLayout(GridLayout({SizeRule::Content(0, 140), SizeRule::Fill(1, 120)}));
    grid.SetGap(10);
    grid.Add<Label>("Tools");
    auto& tools = grid.Add<Widget>();
    tools.SetLayout(FlowLayout{});
    tools.SetGap(8);
    for (const auto* title : {"Select", "Line", "Arc", "Circle", "Trim"})
        tools.Add<Button>(title).SetHeight(SizeRule::Fixed(32));
}
```

## Width-dependent custom widgets

The original protected Measure(FontFace&) hook remains supported and cached
independently of allocation. New code overrides MeasureConstrained for
minimum/preferred content sizes, a first baseline, and height-for-width behavior.
An absent constraint requests natural content size; a present constraint is the
allocated content dimension after padding/border. Return content requirements,
not another outer size rule. Flag m_HeightForWidth consistently, including the
unconstrained query, whenever width can change height or baseline/child geometry.

```cpp
#include <EStudioUI/Widget.h>
#include <algorithm>
#include <cmath>

class DiagramPreview final : public EStudioUI::Widget
{
  protected:
    EStudioUI::Point Measure(EStudioUI::FontFace&) const override
    {
        return {400, 20};
    }

    EStudioUI::MeasureResult MeasureConstrained(
        EStudioUI::FontFace&, const EStudioUI::MeasureConstraints& constraints) const override
    {
        const float width = std::max(1.0f, constraints.m_Width.value_or(400));
        return {{40, 20}, {400, std::ceil(400 / width) * 20}, {}, true};
    }
};
```

Baseline offsets are from the content top and include vertical alignment at a
supplied height. A clipped container may export a finite baseline outside its
allocated bounds, including a negative offset. Preferred dimensions must be finite/nonnegative and at least
the minimum. Geometry and explicit size contracts retain precedence. Invalid
results throw with the traversal phase restored so corrected code can retry.
GetIntrinsicSize returns the legacy unconstrained result; GetContentMeasurement
returns the last prepared content requirements for custom painting. A custom
Paint override continues to use the GPU Painter/Renderer interfaces.

Measurement/strategy hooks are pure owner-thread queries. They may read state and
model snapshots. State publication, tree changes, layout replacement, and public
sizing/alignment/gap changes during traversal are rejected before mutation.
Use UiContext::Defer or a receiver-bound UiHandle to request a later change.
OnLayout may still position existing floating children. It must not replace the
active strategy or modify sizing rules during traversal.

## Application-supplied strategies

Derive from LayoutStrategy, not Widget. Compute receives descriptors of displayed
in-flow children and a temporary measurement callback; it owns no widgets or
font/platform services. It must return one unique finite slot per child,
preserving hard minima/maxima, and finite aggregate requirements. Slots are
relative to the content origin. Optional assigned dimensions in the callback are
outer child dimensions. Definite-width/height flags state whether an allocation
is independent of that child's content and supplies a descendant percentage
reference. Never retain the callback or references to LayoutContext.

The engine validates extension output. Existing BoxLayout can also be composed
inside a custom strategy. A future linear constraint layout can implement this
same boundary without changing ownership, measurement, or rendering APIs.

## Explain an allocation

After Prepare/RenderFrame, Widget::GetLayoutReport returns an owned snapshot:
strategy, content allocation, content and outer measurements, child identities,
effective sizing/limits, baselines, shared track rules/allocations, and diagnostics.
Explain formats these values as readable text. Container/child IDs are CSS IDs
copied at allocation time, with lifetime-unique numeric widget identities as a
fallback; reports contain no borrowed strings or raw widget pointers.

UiContext::GetLayoutDiagnostics explicitly visits the visible widget tree and
copies its last prepared issues. Overflow identifies a child whose rectangle
exceeds the available space. ContentClipped identifies minimum or wrapped-height
requirements outside a hard allocation. IndefinitePercentage explains the cycle
fallback. GetOverflowingContainers remains available and excludes percentage
warnings. Unwrapped text and scrollable viewports may intentionally clip preferred
content; the content-hook dimensions in their reports remain inspectable.

Reports describe the last completed allocation, not pending setter calls. An owned
report remains safe after its source widget is deleted. Query again after Prepare
to inspect updated/hidden/removed children. Formatting/copying reports is explicit
application work; it does not run during a settled frame.

## Work and scope

Each widget retains at most three aggregate constraint variants. Wrapped labels
retain natural paragraph shaping plus at most three width-specific layouts, reset
on text/font changes. Default wrapping preserves explicit newlines, breaks at
ASCII spaces/hyphens, and falls back to extended grapheme boundaries for long
runs. Actual line fragments are reshaped so line-boundary kerning/ligatures are
correct. This is the current LTR label service, not the F10 paragraph/bidi/IME or
Unicode line-breaking implementation. A single grapheme wider than its allocation
can still overflow and is clipped.

Box/flow/grid passes visit participating children; weighted distribution uses the
existing bounded allocator. A content measurement may require a natural pass and
a width-resolved pass. Grid measures column demand before row height; flow visits
items in order and lays out each row. There is no widget per text line or track.
Diagnostic snapshots add storage proportional to actual widget children/tracks,
not collection model records or canvas primitives. No paint-time CSS parsing or
solver loop was added. UiStats reports constrained content hooks and strategy
computations separately from intrinsic hooks, cache hits, and arrangements.

The [verification record](verification/LAYOUT_0_12.md) contains actual CPU,
framebuffer, sanitizer, and platform evidence. The constraint solver is a later
milestone: linear equalities/inequalities, hard/soft priorities, conflict reporting,
and explicit interaction with these sizing rules must be designed and verified
before adding a solver dependency.
