# Styling the existing widgets

All widgets share the compiled CSS cascade and GPU renderer. Outer boxes support
gradients, borders, rounded corners, shadows, opacity, transforms, `::before` and
`::after`, transitions, and keyframes. The **Widget styles** tab contains working
controls in Aurora, Paper, and Ember presets. Switching a preset changes a parent
class without reparsing CSS.

## Selector reference

| Widget | Styling hooks | Relevant states |
| --- | --- | --- |
| Button, ToolButton | `button`, `toolbutton`, `::before`, `::after` | hover, active, focus, disabled; checked/selected for toggle commands |
| Label, panels | `label`, declared type, ID, class, child/descendant selectors | inherited disabled; hover/focus-within on containers |
| TextBox | `textbox`, `::selection`, `::caret`, `::placeholder` | focus, read-only, disabled, valid, invalid, intermediate, placeholder-shown |
| NumericInput | `numericinput`, `.numeric-editor`, `.numeric-increment`, `.numeric-decrement` | focus-within, valid, invalid, intermediate, disabled |
| Slider | `slider`, `::groove`, `::fill`, `::handle` | horizontal/vertical, hover, active, focus, disabled |
| TabWidget | `tabwidget`, `tabbar`, `.tab-button`, `tabpages`, `tabpage` | selected on the tab command |
| ToolBar | `toolbar`, `.toolbar-button`, `.toolbar-separator` | ordinary child command states |
| SplitToolButton | `splittoolbutton`, `.split-primary`, `.split-arrow`, `.split-menu` | focus-within, hover, disabled; ordinary popup action states |
| ContextMenu | `contextmenu`, `.menu-action`, `.menu-separator` | focus/hover/disabled on actions |
| ListWidget | `listwidget::item` | hover, selected, alternate; empty on owner |
| TreeWidget | `treewidget::item`, `treewidget::branch` | hover, selected, alternate, expanded; empty on owner |
| TableWidget | `tablewidget::cell`, `tablewidget::header`, `textbox.cell-editor` | hover, selected, alternate on cells; hover/sorted on headers; empty on owner |
| All collections | `::scroll-track`, `::scroll-thumb` | horizontal/vertical, hover, active; disabled from owner |
| CanvasWidget, SketchWidget | `canvas`, IDs/classes, hosted widgets | ordinary interaction states; dense-item palette has its own API |
| OpenGLWidget | `openglwidget`, ordinary outer decorations | focus, hover, disabled; scene content is application-rendered |
| PaintWidget and derived controls | declared type, IDs/classes, registered custom parts | ordinary states and states maintained by the custom implementation |

`:active` and `:pressed` are aliases. Active tabs retain their older `.selected`
class. `:valid` means acceptable current text; `:intermediate` means incomplete
but editable text. Rejected keystrokes do not store an invalid value. Installing
a stricter validator on existing text can expose `:invalid`.

## Examples

```css
slider::groove:horizontal {
    height: 8px; border-radius: 4px;
    background: #253149;
    box-shadow: inset 0 2px 4px #00000066;
}
slider::fill:horizontal {
    height: 8px; border-radius: 4px;
    background: linear-gradient(90deg, #47d8b0, #6ba7ff);
}
slider::handle {
    width: 22px; height: 22px; border-radius: 11px;
    border: 2px solid #c8eaff;
    background: linear-gradient(140deg, #ffffff, #9cc9ff);
    transition: background 150ms ease, box-shadow 150ms ease;
}
slider::handle:hover { box-shadow: 0 0 8px #7dbbff99; }
numericinput:focus-within { border: 1px solid #7cb8ff; }
textbox:invalid { border-color: #f4768b; }
textbox:intermediate { border-color: #e7ba62; }
textbox::placeholder { color: #91a1bc; }
textbox::selection {
    background: linear-gradient(90deg, #586ccc, #8c64bf);
    color: white; border-radius: 3px;
}
textbox::caret { width: 2px; background: #90e8d0; }
listwidget::item { padding: 4px 12px; border-radius: 5px; }
listwidget::item:alternate { background: #243249; }
listwidget::item:hover { background: #364d70; }
listwidget::item:selected {
    background: linear-gradient(100deg, #4466b4, #7154a5);
    color: white; border: 1px solid #9ebaff;
}
treewidget::branch:expanded { color: #60e0b8; }
tablewidget::header:sorted { background: #355c82; color: white; }
tablewidget::cell:selected { border: 2px solid #8edfc5; }
tablewidget::scroll-track:vertical { width: 14px; background: #182239; }
tablewidget::scroll-thumb:vertical {
    width: 8px; min-height: 28px; border-radius: 4px;
    background: linear-gradient(180deg, #76cdbb, #5779ca);
}
```

Part selectors are terminal: owner selector, `::name`, then optional part states.
`listwidget:hover::item` addresses every item while its owner is hovered;
`listwidget::item:hover` addresses the hovered item. Owner disabled, focus,
focus-within, read-only, validation, and placeholder states participate in part
matching. Fixed animated parts also use owner hover/pressed state. Parts are
paint records with no IDs, classes, children, generated content, or focus targets.

## Geometry and performance contracts

Slider thickness and handle size affect drawing and pointer mapping together.
Scroll-track thickness reserves viewport space; thumb thickness and minimum
length affect painting and dragging together. Tree-branch width affects its
expander hit region. Explicit application size contracts still win over CSS.

Collection `row-height`, tree `indent-size`, and table column `SizeRule`s define
allocated geometry. Part padding, fonts, and alignment affect content inside it;
they do not independently resize each row. Decorative part transforms retain
logical row selection and slider drag targets. Use an outer-widget transform to
move the whole interactive control.
Virtualization uses those logical allocations: a wholly offscreen row or column
is not submitted merely because an extreme decorative transform or shadow could
reach into the viewport.

Slider parts support automatic transitions/keyframes through the CSS clock.
Collection, scrollbar, and editor parts use shared static variants: automatic
part transitions/keyframes are not scheduled for them. Hover/selection switches
styles immediately. Outer-widget and ordinary child-widget motion remain
available. Model entries do not allocate widgets or animation timelines; only
visible rows are submitted for painting. Hidden animated owners request no frames.

The explicit `UiContext::AnimateStyleSheet` API also interpolates registered
parts, including shared collection-state palettes. It allocates at most two
tracks per registered part, independently of the number of model rows, and
supports the existing pause, cancellation, and exact-endpoint contracts.

Legacy palette properties remain fallbacks: `selection-background-color`,
`hover-background`, `alternate-background`, `header-background`, `grid-color`,
`caret-color`, and `placeholder-color` still work without explicit part rules.

## Composition and custom controls

Built-in cosmetic gaps, arrow/stepper widths, and separator sizes are defaults
applied before CSS. Application `SetWidth`, `SetHeight`, and `SetGap` contracts
remain explicit overrides. Root-owned popups follow their logical owner's CSS
ancestry, so `.ember .split-menu .menu-action` matches an overlay menu as well.

A derived widget can register a small paint part in its constructor:

```cpp
RegisterStylePart("indicator", Hover | Selected,
                  [](const Style& owner, unsigned state)
                  {
                      Style part;
                      part.m_Background = state & Selected
                                              ? owner.m_SelectionBackground
                                              : owner.m_Color;
                      part.m_Radius = 4;
                      return part;
                  });
```

Read `GetPartStyle("indicator", state)` during paint and supply that typed style
to the renderer or `Painter`. The fallback factory must be pure. For one fixed
animated part, pass `animated=true` at registration and use `SetPartState`.
Registration is bounded to 32 names and six varying state bits per part. Never
register one part per model item. CSS parsing and selector matching happen during
style preparation, not painting or paint-only animation ticks.

`OpenGLWidget` styles its containing box; its scene callback owns 3D materials.
`PaintWidget` supplies its styled box and lets custom painting consume computed
properties. Dense canvas items retain their shared item palette; see
[CANVAS.md](CANVAS.md). See [CSS.md](CSS.md) for syntax limits and
[CUSTOM_WIDGETS.md](CUSTOM_WIDGETS.md) for painter and ownership examples.
