# CSS supported by EStudioUI 0.8

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
| Interaction states | `:hover`, `:active`, `:pressed`, `:focus`, `:focus-within` |
| Selection/toggle | `:selected`, `:checked` (aliases on toggle buttons) |
| Decorative paint layers | `button::before`, `button:hover::after` |
| Availability | `:enabled`, `:disabled`, `:read-only` |
| Editor states | `:valid`, `:invalid`, `:intermediate`, `:placeholder-shown` |
| Orientation | `:horizontal`, `:vertical` |
| Collection states | `:empty`, part states `:alternate`, `:expanded`, `:sorted` |
| Named control parts | `slider::handle:hover`, `tablewidget::cell:selected` |

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
| `background`, `background-color`, `color` | A supported color; `background` also accepts a gradient and resets the other background component |
| `background-image` | `none`, `linear-gradient(...)`, `radial-gradient(...)` |
| `transform` | Ordered `translate`, `translateX/Y`, `scale`, `scaleX/Y`, `rotate`, `skewX/Y`, `matrix`, or `none` |
| `transform-origin` | One/two percentages or edge/center keywords; defaults to `50% 50%` |
| `overflow` | `hidden` clips content and decorative layers to the rounded border box; `visible` retains ordinary ancestor content clips |
| `content` | Empty quoted string enables a decorative layer; `none` disables it |
| `position` | `absolute` for decorative layers; does not change widget layout |
| `inset`, `top/right/bottom/left` | Decorative offsets: signed pixels, percentages, or `auto` |
| `border` | `2px solid #abcdef`, `none`, or `0` |
| `border-color`, `border-width`, `border-radius` | A color or nonnegative pixel length |
| `font-size` | Pixel size clamped to 0..512; zero hides text |
| `text-align` | `left`, `center`, `right` |
| `opacity` | Nonnegative number, clamped to 0..1 |
| `translate-x`, `translate-y` | Signed logical-pixel offsets after layout; descendants in flow and input geometry follow |
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
| `box-shadow` | Up to eight comma-separated `[inset] x y [blur [spread]] color` layers, or `none` |
| `selection-background-color`, `selection-color` | Editor and collection selection colors |
| `row-height` | List/tree/table row height, at least 12px |
| `indent-size` | Nonnegative tree indentation per depth |
| `header-background` | Table header background color |
| `alternate-background`, `hover-background` | Alternating and hovered collection row colors |
| `grid-color` | Table grid color |
| `placeholder-color`, `caret-color` | Text-box colors |

Padding and margin shorthand order is top, right, bottom, left. Two values mean
vertical/horizontal; three mean top/horizontal/bottom. Margin side longhands are
not implemented. Color forms:
`#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb(0, 0, 0)`, and `rgba(0, 0, 0, 0.5)`.
RGB channels are 0..255 and alpha is 0..1. Named colors are `transparent`, `black`,
`white`, `red`, `green`, `blue`, `gray`, and `grey`. Percentage RGB/alpha channels are also accepted.

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

Tab strips use `tabbar .tab-button:selected`; the older `.selected` class remains available.
ToolButton's `:checked` / `:selected` state is persistent; `:active` remains the
transient press state. Toolbars and menus accept ordinary child selectors.

Collection rows/cells are model entries, not child widgets. Named parts expose
their backgrounds, gradients, borders, shadows, typography, padding, alignment,
and state styling through a shared compiled palette. Examples include
`listwidget::item:hover`, `treewidget::branch:expanded`,
`tablewidget::header:sorted`, and `tablewidget::cell:selected`.
Keep `row-height`, `indent-size`, and `grid-color` on the owning collection.
The reusable table editor is `tablewidget textbox.cell-editor`.
See [WIDGET_STYLING.md](WIDGET_STYLING.md) for the complete part/state table and
metric, animation, and custom-control contracts.

Code-level size/direction/gap contracts take precedence over CSS for the explicitly
set values; other computed properties still follow the cascade. Popup placement
and table editor bounds are controlled by their owners, outside normal flow.
Built-in cosmetic gaps, stepper/arrow widths, and separator sizes are CSS defaults,
so themes can override them until application code sets an explicit contract.
Root-owned composed popups match selectors through their logical owning control;
changing the owner's theme class also restyles an open popup.

## GPU paint and decorative layers

Gradients are evaluated analytically in the fragment shader, without allocating
images or framebuffer passes. Linear gradients accept a CSS angle (`deg`, `rad`,
`turn`) or `to right/left/top/bottom` and corner directions. Each of two to eight
stops is `color [position%]`; missing offsets distribute between their neighboring
explicit stops, and coincident offsets produce hard color changes. Color
interpolation uses premultiplied alpha in the existing component color space.

Radial syntax is `radial-gradient([ellipse [rx% ry%] at x% y%,] stops...)`.
The default radii are 70.7107% of each box dimension. Explicit percentage radii
also work with the `circle` spelling, but remain box-relative ellipse radii on a
non-square box; browser circle/farthest-corner sizing keywords are not implemented.

Each widget can have two noninteractive paint layers. `::before` is drawn after
the widget's background and before its caption/children; `::after` is drawn after
its children. They allocate no widgets, focus entries, signals, or GL contexts.
Enable them with `content: ""`; textual generated content is not supported.
Offsets, dimensions, and transforms refer to the owner's border box. Auto width
and height fill the space between specified edges. Width/height, shadows, radius,
opacity, gradients, and CSS motion also work on these layers.

`transform` accepts up to eight ordered operations and moves every drawn primitive
and descendant. Translation percentages resolve against the transformed box;
scale is unitless; rotations/skews use angle units. Transform origin accepts
percentages/keywords. Matrix syntax is `matrix(a,b,c,d,tx,ty)`.
Visual transforms preserve layout allocation and text shaping. Pointer routing
uses inverse composed matrices; capture retains world-space ancestor clipping.
Singular transforms paint no visible area and cannot capture input. Extremely
large composed matrices are suppressed before they can overflow GPU coordinates.
The older `translate-x/y` APIs retain their layout-bound translation semantics.

Outer shadows paint behind the box, inset shadows inside it, and the first shadow
of each kind paints on top. Spread can be negative. Shadows use an analytic soft
edge approximation rather than a browser's Gaussian blur. `overflow: hidden`
adds a rounded clip for text, decorations, and children; the owner's outer shadow
remains visible. Axis-aligned rectangle clips collapse into the ordinary
per-instance rectangle. Rounded/transformed clip chains are bounded to 32 levels.
Opacity remains per primitive rather than an isolated composited group.

## CSS transitions and keyframes

```css
button.launch {
    background: linear-gradient(120deg, #2877ff, #9d43d5);
    transform: translateY(0px) scale(1);
    transition: transform 220ms ease, box-shadow 220ms ease;
}
button.launch:hover {
    transform: translateY(-4px) scale(1.04);
    box-shadow: 0 8px 20px #7555ff88;
}
button.launch:active { transform: translateY(0px) scale(.97); }
button.busy::after {
    content: "";
    left: 14px; right: 14px; bottom: 6px; height: 2px;
    background: linear-gradient(90deg, #32d9cc, #73a7ff);
    transform-origin: left center;
    animation: progress 1.8s ease-in-out infinite alternate;
}
@keyframes progress {
    from { transform: scaleX(.1); opacity: .5; }
    50% { opacity: 1; }
    to { transform: scaleX(1); }
}
```

`transition` lists property/shorthand/`all`, duration, optional easing and delay.
State/class changes retarget from the current presentation, preserving continuity
on interruption. Initial attachment and stylesheet replacement do not start state
transitions. Times accept `s` and `ms`, with nonnegative durations/delays. The last
matching transition definition selects a property's timing.

`animation` accepts a name, duration, easing, optional delay, positive integer
iteration count or `infinite`, `normal/reverse/alternate/alternate-reverse`,
`none/forwards/backwards/both` fill, and `running/paused`. Up to eight timelines
can run on one widget/layer; later animations win overlapping properties.
Pause/resume preserves elapsed time; removing an animation restores underlying CSS.
Undefined names have no active tracks. Zero-duration infinite repetition is
rejected in shorthand and suppressed if assembled through longhands.

Transition longhands: `transition-property/duration/delay/timing-function`.
Animation longhands: `animation-name/duration/delay/timing-function/iteration-count/
direction/fill-mode/play-state`. Comma-separated timing values cycle across the
current property/name list. Declare that list before timing lists when their
lengths differ; this bounded parser does not retain independent browser longhand
list lengths for later expansion.

Keyframes support `from`, `to`, percentage and grouped offsets, duplicate offsets
in source order, missing endpoints from the underlying style, and up to 256 blocks
per name. Easing applies to each property segment; keyframe blocks can set
`animation-timing-function`. Curves are `linear`, `ease`, `ease-in/out/in-out`,
`cubic-bezier(x1,y1,x2,y2)`, `step-start/end`, and `steps(n,start/end)` (also
`jump-start/end`). `!important` is rejected inside keyframes.

Matching gradient types/stop counts interpolate their angles, centers, radii,
stops, and colors. `none` fades to/from a gradient. Matching transform operation
lists interpolate components, retaining full turns; `none` supplies neutral
operations. Incompatible lists/units/image structures switch discretely at the
endpoint. Length interpolation in automatic CSS motion requires matching units;
use the C++ whole-stylesheet API for measured content/Fill/mixed-unit endpoints.
Missing shadow layers pad with transparent shadows. Visible inset/outer mismatches
switch discretely. Booleans/strings also switch discretely.

Explicit C++ style overrides take precedence over automatic CSS presentation.
Existing Fixed/Content/Fill code contracts retain precedence over all CSS motion.
Transitions apply over named animation presentation. Moving geometry can refresh
hover beneath a stationary pointer. Hidden widgets do not sample automatic CSS
tracks or request animation frames, including when native input advances the
clock on another tab. On reveal, presentation catches up to logical elapsed time.
Paint-only tracks, including transforms and decoration geometry, retain computed
style, layout, and text shaping caches; widget dimensions, typography and inherited
color changes still require the relevant style/layout work.

`UiContext::AnimateStyleSheet` continues to interpolate existing widgets between
complete stylesheets. See [ANIMATIONS.md](ANIMATIONS.md) for that C++ API, explicit
property groups, endpoint measurement, and lifetime/cancellation contracts.
The **Rich CSS** demo tab uses [assets/rich-buttons.css](assets/rich-buttons.css).
The **Widget styles** tab extends this to the existing controls using
[assets/rich-widgets.css](assets/rich-widgets.css), with three switchable presets.

## Explicit omissions

This is a documented CSS subset, not a browser. No arbitrary at-rules, custom
properties/`var`, `calc`, sibling/attribute selectors, `:not`, font family/weight/
style selection, raster/SVG background images, conic/repeating gradients, filters/
backdrop blur, 3D/projective transforms, textual generated content, z-index,
percentage padding, CSS grid, negative margins, browser box formatting, or Qt
`qproperty-*` or browser pseudo-element compatibility beyond the named parts
listed here. Use the font passed to `Window`. Text alignment affects captions,
collection parts, and editable lines; overflowing editors scroll around the caret.
Widget `position`/edge offsets do not replace the layout API; those declarations
are intended for decorative layers. Unsupported declarations fail during parsing
rather than being silently interpreted as full browser CSS.

## Canvas items

The `canvas` widget itself uses the ordinary window stylesheet. Dense scene
items use `CanvasWidget::SetItemStyleSheet` and integer handles returned by
`RegisterItemStyle`; identities support type, ID, class, and `:selected` selectors.
Normal/selected presentations are compiled into a small shared palette at reload,
not resolved for every entity during paint. A synthetic `canvas` parent allows
selectors such as `canvas > canvas-item` within this palette.

Lines use `color`, `border-width`, and opacity. Rectangles/circles support static
backgrounds/gradients, borders, shadows, and opacity; rectangle radius stays in
logical pixels. Dense-item transforms, layout declarations, pseudo-elements,
and automatic transitions/keyframes are not applied. Hosted ordinary widgets
retain the existing CSS behavior. See [CANVAS.md](CANVAS.md) for examples and the
screen-space stroke versus world-space geometry contract.


## Precision controls and custom painting (0.7)

`numericinput`, `slider`, and `splittoolbutton` are ordinary selector identities.
Numeric inputs compose `.numeric-editor`, `.numeric-increment`, and
`.numeric-decrement` children; split controls compose `.split-primary` and
`.split-arrow` tool buttons. They support the ordinary disabled/focus/hover states.
Slider `grid-color` supplies the default groove, `selection-background-color` supplies the
active track and focused handle border, `color` paints the handle, and
`border-color` supplies its idle outline. Named `::groove`, `::fill`, and `::handle`
parts override these defaults. Their transitions and keyframes share the widget's
existing animation scheduler. Text boxes expose `::selection`, `::caret`, and
`::placeholder` without creating children.

Dense arcs, splines, and paths use the same shared item CSS palette as lines;
annotation text uses `color` and opacity while its font size is stored in world
geometry. Groups can retain a style index per child and propagate selection.
These remain dense model values, not full selector-tree widget instances.
`PaintWidget` paints its ordinary CSS box before calling the application's
content paint override. `Painter::SetBrush(const Style&)` can reuse typed computed
fills/gradients/shadows; applications choose which style values affect their own
painted shapes. See [CUSTOM_WIDGETS.md](CUSTOM_WIDGETS.md).
