# CSS supported by EStudioUI 0.3

Style is external UTF-8 text, with optional BOM and `/* comments */`. Parsing is
atomic: an unknown property, unsupported value, or malformed rule rejects the
new stylesheet and leaves the previous one active. Diagnostics identify a line.
CSS is loaded on request, not watched automatically. Press F5 in the demo to reload.

## Selectors and cascade

| Form | Example |
| --- | --- |
| Type, matched without case | `button`, `textbox`, `toolbutton`, `tabwidget`, `listwidget`, `treewidget`, `tablewidget`, `contextmenu`, `openglwidget`, `label`, `panel`, `window` |
| Universal | `*` |
| Case-sensitive ID | `#editor` |
| Case-sensitive class | `.primary` |
| Compound | `button.primary:focus` |
| Descendant | `.card textbox` |
| Immediate child | `.row > button` |
| Selector list | `button, textbox` |
| Interaction states | `:hover`, `:active`, `:pressed`, `:focus` |
| Selection/toggle | `:selected`, `:checked` (aliases on toggle buttons) |
| Availability | `:enabled`, `:disabled`, `:read-only` |

Type names are the strings passed to `Widget`'s constructor. IDs and classes are
set using `SetId()` and `AddClass()`. Hover includes the target's ancestors.
`:pressed` and `:active` are aliases. Disabled state includes disabled ancestors.

Declarations sort by `!important`, then ID/class/type specificity, then source
order. Specificity is encoded as ID=10000, class/pseudo-class=100, type=1, with a
32-part selector limit. It is suitable for this subset rather than an unbounded
browser implementation. Longhands and shorthands follow declaration order after
the cascade. Only `color`, `font-size`, and `text-align` inherit.

## Properties

Numeric lengths are logical pixels, written as `12px` or `12`. Width and height
also allow percentages relative to the parent's content size, `auto` / `content`,
and positive Fill weights such as `1fr` / `2fr`. All
sizes include padding and borders: the model is **border-box**.

| Property | Values |
| --- | --- |
| `background`, `background-color`, `color` | A supported color |
| `border` | `2px solid #abcdef`, `none`, or `0` |
| `border-color`, `border-width`, `border-radius` | A color or nonnegative pixel length |
| `font-size` | Pixel size clamped to 0..512; zero hides text |
| `text-align` | `left`, `center`, `right` |
| `opacity` | Nonnegative number, clamped to 0..1 |
| `padding`, `margin` | One to four nonnegative pixel lengths in CSS edge order |
| `padding-top/right/bottom/left` | One nonnegative pixel length |
| `width`, `height` | `auto`, `content`, nonnegative pixels, percentage, positive `fr` |
| `min-width/height`, `max-width/height` | Nonnegative pixels |
| `display` | `none`, `flex`, `block` |
| `flex-direction` | `row`, `column` |
| `flex-grow`, `flex` | Nonnegative number; `flex` is a numeric grow alias |
| `gap` | Nonnegative pixels |
| `align-items` | `stretch`, `start`, `center`, `end`, `flex-start`, `flex-end` |
| `justify-content` | `start`, `center`, `end`, `space-between`, `flex-start`, `flex-end` |
| `box-shadow` | `x y blur color` or `none`; offsets can be negative |
| `selection-background-color`, `selection-color` | Editor and collection selection colors |
| `row-height` | List/tree/table row height, at least 12px |
| `indent-size` | Nonnegative tree indentation per depth |
| `header-background` | Table header background color |
| `alternate-background`, `hover-background` | Alternating and hovered collection row colors |
| `grid-color` | Table grid color |
| `placeholder-color`, `caret-color` | Text-box colors |

Padding and margin shorthand order is top, right, bottom, left. Two values mean
vertical/horizontal; three mean top/horizontal/bottom. Margin side longhands are
not implemented. A shadow has no spread or inset variant. Color forms:
`#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb(0, 0, 0)`, and `rgba(0, 0, 0, 0.5)`.
RGB channels are 0..255 and alpha is 0..1. Named colors are `transparent`, `black`,
`white`, `red`, `green`, `blue`, `gray`, and `grey`. Percentage RGB is unsupported.

## Layout semantics

Every widget can contain children. `display: block` and `display: flex` both use
the same single row/column layout here; the default direction is column.
Auto sizes use intrinsic content, then padding and border. Auto cross-axis sizes
stretch when the parent uses `align-items: stretch`. Positive remaining main-axis
space is distributed by `flex-grow`; setting the growing dimension to `0` makes
it a useful equal-share basis. There is no flex shrink, flex basis property, or
wrapping. Main-axis alignment applies when there is no growth. Oversized children
are clipped to all ancestor content rectangles, and clipped areas cannot receive
pointer input. Very small windows can therefore clip controls.

`display: none` removes a subtree from layout and input, and clears focus/capture
within it. A minimum larger than a maximum raises the effective maximum to the
minimum. Growth is clamped to min/max and leftover space is redistributed to other
growing children. Fractional dimensions start at their hard minimum rather than
their measured content size. See [LAYOUTS.md](LAYOUTS.md) for size contracts,
API precedence, and explicit overflow diagnostics.
The root occupies the entire logical window; its width/height properties do not
resize the native window.

## Control styling

Tab strips use `tabbar toolbutton` and a `.selected` class for the active tab.
ToolButton's `:checked` / `:selected` state is persistent; `:active` remains the
transient press state. Toolbars and menus accept ordinary child selectors.

Collection rows/cells are model entries, not child widgets. Configure their
selection, hover, alternating backgrounds, row height, indentation, header, and
grid through the properties above on `listwidget`, `treewidget`, and
`tablewidget`. Arbitrary per-row selectors and subcontrol pseudo-elements are not
implemented. The reusable table editor is `tablewidget textbox.cell-editor`.

Code-level size/direction/gap contracts take precedence over CSS for the explicitly
set values; other computed properties still follow the cascade. Popup placement
and table editor bounds are controlled by their owners, outside normal flow.

## Explicit omissions

No `@` rules, variables, `calc`, sibling/attribute selectors, pseudo-elements,
`:not`, font family/weight/style, transitions, gradients, image backgrounds,
percentage padding, CSS grid, negative margins, browser box formatting, or Qt
`qproperty-*`/subcontrol syntax. Use the font passed to `Window` for this first
version. Text alignment affects labels/buttons; the editable line scrolls
horizontally around its caret. Radius rounds a widget's own background/border;
child clipping remains rectangular. Opacity is applied per primitive, not to an
offscreen composited group. These boundaries are intentional and visible through
parse errors rather than silently ignored properties.
