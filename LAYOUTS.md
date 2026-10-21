# Predictable sizing

EStudioUI 0.3 uses explicit outer-size contracts in logical pixels. The API is
small enough to read directly from application code; themes can change colors,
fonts, and spacing without changing an explicit code-level size contract.

| Contract | Meaning |
| --- | --- |
| `SizeRule::Fixed(180)` | Exactly 180 pixels, including padding and borders |
| `SizeRule::Content(min, max)` | Intrinsic content size within hard limits |
| `SizeRule::Fill(weight, min, max)` | Minimum size plus a weighted share of remaining space, capped at maximum |

`Fixed` never silently grows or shrinks. `Content` has no growth weight. `Fill`
starts from its minimum, independently of long captions or model item counts.
Weights divide the **space above all initial bases**, rather than total widths.
When one Fill item reaches its maximum, its unused share is redistributed to
other growing children. Hidden children and inactive tab pages consume no space
or gaps. Layout diagnostics identify containers that overflow.

## A workspace without size hints

```cpp
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/TreeWidget.h>

void BuildWorkspace(EStudioUI::Widget& parent)
{
    using namespace EStudioUI;
    parent.SetGap(10);

    auto& toolbar = parent.Add<ToolBar>();
    toolbar.SetHeight(SizeRule::Content());
    toolbar.AddButton("Open");
    toolbar.AddButton("Save");

    auto& row = parent.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetHeight(SizeRule::Fill());
    row.SetGap(12);

    auto& tree = row.Add<TreeWidget>();
    tree.SetWidth(SizeRule::Fixed(240));
    tree.AddItem(0, "Project");

    auto& table = row.Add<TableWidget>();
    table.SetWidth(SizeRule::Fill(1, 320));
    table.SetColumns({{"Name", SizeRule::Fill(2, 140)},
                      {"Value", SizeRule::Fill(1, 80)}});
    table.SetRows({{"Frame A", "120"}, {"Frame B", "240"}});
}
```

A row assigns horizontal sizes and stretches automatic/fill heights; a column
does the inverse. Nest rows and columns for forms, sidebars, command strips, and
editor panes. Table columns use the same allocator and contracts as widgets.
`SetColumnWidth` or a header drag establishes a fixed user override.

## The layout playground

The demo's Layout lab places these three siblings in one row:

```cpp
fixed.SetWidth(SizeRule::Fixed(180));
capped.SetWidth(SizeRule::Fill(1, 120, 240));
remaining.SetWidth(SizeRule::Fill(2, 160));
```

The first always measures 180. The second initially receives one third of free
space, reaches 240, and then returns its remaining share to the third. The
playground includes native window-size presets and an Inspect sizes command.
It is rendered by the C++ application, including the captures in `verification/`.

## CSS and API precedence

The equivalent theme-driven layout can use fractional dimensions:

```css
.workspace { flex-direction: row; gap: 12px; height: 1fr; }
.sidebar { width: 240px; }
.editor { width: 1fr; min-width: 320px; }
.capped { width: 1fr; min-width: 120px; max-width: 240px; }
.caption { width: content; }
```

`content` and `auto` are aliases. `1fr` is a positive fractional Fill weight, not
a full CSS grid implementation. Code `SetWidth` / `SetHeight` contracts override
CSS width, minimum, and maximum for that axis, including `!important`. Code
`SetLayoutDirection` / `SetGap` similarly override theme direction/gap. Other
presentation values still follow the normal cascade. This keeps a color-theme
reload from unexpectedly resizing a deliberately fixed sidebar.

Existing `flex-grow` CSS remains supported: it grows from the measured base,
whereas a fractional dimension starts from the minimum. Avoid combining both
forms on the same axis; the fractional dimension takes precedence.

## Too-small windows and current boundaries

If fixed/content bases, hard minima, margins, and gaps require more than the
available space, EStudioUI preserves them and clips overflow to ancestor content
rectangles. Clipped regions cannot receive pointer input. Inspect
`UiContext::GetOverflowingContainers()` to identify the affected containers; the
returned `WidgetRef` values do not keep widgets alive. There is no automatic
hiding, font scaling, or rewriting of constraints.

Lists, trees, and tables scroll their **own model contents**. This version does
not provide a general-purpose scrolling container for an oversized form. Grid,
wrapping rows, automatic label word wrapping/height-for-width measurement,
splitter handles, responsive breakpoints, and aspect-ratio constraints are future
work. Label content size currently uses its explicit text lines. Percentages are
relative to the arranged parent's content box; avoid cyclic percentage/content
requirements when you need an exact intrinsic size.

Measurements are cached for a preparation pass; final percentage dimensions are
resolved against arranged parent bounds. Style/layout preparation runs only when
invalidated, and inactive tab contents do not participate in measurement.
