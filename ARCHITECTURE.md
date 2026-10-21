# Architecture

The prototype separates the persistent UI model, event dispatch, and drawing
without exposing third-party types in its public widget headers.

| Layer | Responsibility |
| --- | --- |
| `Window`, `FramePacer` | GLFW lifetime/context, Unicode callbacks, clipboard, sleeping event loop, stable presentation deadlines |
| `UiContext` | Widget tree, CSS resolution, row/column layout, clipping, focus, capture |
| `Widget`, `Label`, `Button`, `TextBox` | Owned model, cached shaping, behavior, emitted signals |
| `SizeRule`, `Layout` | Fixed/content/fill contracts, bounded weighted distribution, overflow |
| `Animation`, `AnimationSystem`, `EasingCurve` | Synchronized typed tracks, elapsed-time scheduling, easing, cancellation and lifetime safety |
| `CssMotion`, `VisualStyle` | Compiled CSS timelines, decorative paint layers, gradients, affine transforms |
| `StyleProperty` | Typed computed-style access, valid-range clamping, paint-only invalidation classification |
| `TabWidget`, `ToolBar`, `ToolButton`, `ContextMenu` | Owned pages, commands/toggles, popup layer |
| `ScrollWidget`, `ListWidget`, `TreeWidget`, `TableWidget` | Cached viewport text, virtualized rows, selection/editing |
| `OpenGLWidget` | Separate shared scene context, depth FBO, synchronized texture composition |
| `TextBuffer` | Valid UTF-8, grapheme/word navigation, selection, bounded undo/redo |
| `FontFace` | HarfBuzz shaping, FreeType outline decoding, CPU geometry cache |
| `Renderer` | One ordered instance stream, analytic shape and Slug-derived text shaders |

## Frame and cache behavior

Input and model changes mark repaint. Changes affecting identity, state, text,
structure, CSS, or dimensions also invalidate style/layout. Caret and selection
changes keep style/layout caches where possible. Preparation resolves inherited
styles, measures intrinsic contents, assigns logical-pixel bounds, and computes
ancestor clips. Painting traverses parents before children in insertion order.

The event loop waits for GLFW events when nothing changes. A focused editable
text box wakes at its next half-second caret deadline. Visible continuous scene
surfaces request paced animation ticks; idle/hidden scenes do not. `FramePacer`
limits presentation starts to `WindowOptions::m_MaxFrameRate` (60 by default),
independently of vsync. Input wakeups retain the deadline; slow draws/swaps do
not accumulate catch-up frames. Minimized/invisible windows suspend presentation.
Unfocused windows suspend it by default; `m_RenderWhenUnfocused` opts in to
background rendering. Manual `RenderFrame` calls bypass the event-loop limit.

Property timelines share the context's nondecreasing clock and advance before
layout, external scene preparation, and painting. Running timelines request
paced frames; paused timelines do not. Each track weakly observes its widget,
and newer writers replace matching target/channel pairs. Dispatch keeps timeline
entries stable while callbacks start/cancel animations or delete widgets.
Stylesheet transitions snapshot computed endpoints and resolve incompatible
dimension units to measured pixels. Presentation overrides apply before explicit
code layout contracts. Paint-only overrides update cached presentation without
repeating style resolution or measurement; inherited/geometry changes use the
existing invalidation path. See [ANIMATIONS.md](ANIMATIONS.md).

CSS declarations compile into typed assignments once per stylesheet parse.
`CssMotion` caches underlying and presented styles separately for each widget and
its before/after paint layers. Automatic state transitions start from the current
presentation. Keyframes compile into per-property knot tracks, with missing
endpoints drawn from the underlying style. Ticks visit active property sets,
without selector evaluation or string/number parsing. Explicit application style
overrides and size contracts retain precedence. Stylesheet replacement resets
automatic tracks. Weak references protect deletion and address reuse. Hidden
subtrees do not sample automatic CSS tracks or request repaint on input clock
updates. Revealing them samples logical elapsed time, without rendering the
missing frames. Programmatically hidden pages also skip style traversal.

Affine paint transforms compose through the tree without changing allocated
bounds. Input uses inverse composed matrices, with a separate ancestor-clip flag
for capture events. Rounded/transformed clip chains share the same geometry on
CPU and GPU. Decorations draw within the owner paint order, without interactive
children. `UiStats` reports cumulative style/layout work; `RenderStats` reports
draws, shapes, glyphs, instance/effect upload bytes, gradients, and shadows, plus
actual repainted pixels, submitted instances, and replayed glyphs. Recorded
glyph/shape counts describe the paint stream; submitted counts describe GPU work.

Each label/editor caches shaping until text or CSS font size changes. FreeType
loads unhinted outlines only once per glyph ID. Curves use em-space coordinates;
TrueType quadratics remain exact. CFF cubic curves are recursively approximated
with quadratic segments to a 0.00005 em control-point tolerance, bounded at depth
12. HarfBuzz uses the font's OpenType tables without optional external integrations.
Editable runs disable `liga`/`clig` to simplify grapheme caret placement. Cluster
caret positions interpolate when a shaped cluster spans multiple graphemes.
Only left-to-right runs are currently supported.

## GPU representation

Each instance holds a logical rectangle, auxiliary parameters, colors, clip,
band transform, and integer kind/address data. Six vertices are generated from
`gl_VertexID`, with no individual widget meshes. Shape instances use a rounded-box
distance function for fills, borders, antialiasing, and soft shadows. Glyph
instances sample quadratic segments from an RGBA32F buffer texture, with band
headers and sorted curve lists in an RG32UI buffer texture. Horizontal/vertical
bands reduce curve candidates. Coverage combines both ray directions using the
Slug reference algorithm. Glyph quads expand by a physical-pixel margin so the
coverage filter survives at outline edges. No bitmap/SDF atlas is generated.

Shapes, text, selection highlights, and carets share one shader program and one
instance buffer. A flat instance kind selects shape or glyph processing. Ordered
instanced draws preserve painter order and alpha blending, including translucent
primitives interleaved with glyphs. The fixed-width stream costs 128 bytes per
recorded instance, including a 16-byte field indexing an RGBA32F effect buffer for affine
matrices, clip chains, and analytic gradient/shadow parameters. The ordinary
identity-transform path avoids matrix texture fetches; axis-aligned clips remain
in the instance. Effect values are bounded (eight gradient stops/shadows/transform
operations, 32 complex clip levels). Gradient directions, extents, radial centers,
inverse radii, and premultiplied stop colors are prepared on the CPU rather than
recomputed for every fragment. No effect creates a widget or its own offscreen
render pass. Cached font buffers upload only after new glyphs are encountered; per-frame
instance data uses GL buffer orphaning/upload. Existing vector capacities are
reused. Texture-buffer limits are checked and diagnosed; eviction is future work.

The renderer retains one RGBA8 framebuffer for the entire window (four bytes per
physical pixel). It compares exact paint values, transforms, clip chains, effects,
and external texture revisions with the preceding frame. CPU-only owner/layer/kind
keys keep unrelated widgets matched when a decoration appears/disappears; painter
order inversions invalidate every affected owner. Keys do not dereference old
widget addresses. Old and new conservative bounds mark 64-pixel damage tiles,
merged into nonoverlapping rectangles. Bounds include shadow/glyph padding,
transforms, and complex clip antialiasing margins.

Each damaged region is cleared and all overlapping primitives are replayed in
their original order, including unchanged translucent backgrounds and text.
Only this stream is uploaded. More than 16 regions or 60% damaged area falls back
to a full redraw; resize, scale changes, and clear-color changes also redraw fully.
Unchanged frames upload no instances/effects and submit no analytic draws. Each
presented frame still blits the full retained image to the native back buffer,
whose pixels are not assumed to survive swaps. The event loop avoids presenting
idle frames entirely. The design trades a few scissored draws for much less
fragment shading rather than treating one draw call as the performance goal.

`SetPartialRedrawEnabled(false)` selects the full redraw reference path, submitting
every recorded primitive without damage culling. `SetGpuTimingEnabled(true)`
enables an eight-slot `GL_TIME_ELAPSED` query ring; only available results are
read, and busy slots never stall submission. Timing is disabled by default.
See [PERFORMANCE.md](PERFORMANCE.md) for reproducible native profiling.

The first backend is GL 3.3 core to keep setup and platform dependencies small.
It uses shaders, instancing, and buffer textures, with no fixed-function drawing.
A later Vulkan/D3D12/Metal backend would replace the renderer and window context
integration, while preserving model and input semantics. That work has not been
implemented or benchmarked.

## Layout, collections, and popups

Measurements are cached per prepare pass. Code-level size rules are applied
following CSS resolution, so a theme cannot override an explicit fixed/limited
contract. Nested rows/columns and table columns share the bounded weighted
allocator. Capped children return unused shares; over-constrained containers are
reported through weak widget observers instead of silently violating minima.
See [LAYOUTS.md](LAYOUTS.md) for exact semantics and remaining layout features.

Tab pages keep their model/editor state while inactive pages are removed from
measurement, painting, and input. Collections store data rather than one widget
per row/cell. Lists/tables visit viewport rows during painting; trees cache a
flattened expanded traversal, rebuilding only after structural/expansion changes.
The text cache is bounded to 512 runs per scrollable control. Table content-sized
columns deliberately inspect all cell values when their size cache is stale;
prefer fixed/fill columns for large data. Sorting is stable and lexicographic.

Right-click/Menu-key requests bubble through weakly observed ancestors. One root
popup is laid out/clamped separately, painted last, and receives pointer and
keyboard routing. Outside clicks dismiss and are consumed; dismissal restores a
live visible focus target. Table cell editors are reusable floating children,
clipped to the body viewport. Focus-loss commit handles callback-driven deletion
without retaining destroyed focus targets.

## Embedded scene contexts

Each visible OpenGLWidget lazily creates a hidden GLFW context sharing objects
with the containing UI context. Color textures are shared; depth attachments,
framebuffer objects, and vertex arrays are managed in their scene context.
Producer and consumer fences order scene rendering, UI sampling, texture reuse,
resize, and destruction in both directions. Context switching is scoped so
initialization/render exceptions restore the containing context. Rendering is
GUI-thread work; separate-thread rendering has not been implemented.

Static scenes keep their textures until invalidated/resized. Each successful scene
render advances a texture content revision; unchanged revisions retain composed
pixels. `DrawTexture` revision zero conservatively invalidates its bounds on every
frame for callers that cannot supply content revisions. The two-way fences remain
in place. Animated scenes
request timed frames only while visible. The ordered UI instance stream adds
textured quads and splits draws at texture changes; it preserves painter order
around normal shapes/text. This does not imply one draw for an entire 3D scene.
See [OPENGL_WIDGET.md](OPENGL_WIDGET.md) for lifecycle and application contracts.

## Ownership and event contracts

Children have one owner. Widget observers pair an address with a weak lifetime
token so focus, hover, and capture become empty after deletion. Mouse capture
continues outside a widget, and activation checks the release position against
both bounds and ancestor clips. Disabling/hiding a focused or captured subtree
cancels its interaction on preparation. Tab visits enabled displayed focusable
nodes in tree order. Built-in controls finalize internal state before emitting,
so callbacks can delete the sender. A signal emission retains its own state and
slot objects until all nested deliveries finish. New callbacks wait until the
next emission; a nested emission counts as another emission.

Signal slots and widgets are GUI-thread objects, without mutexes or queued
execution. Ordinary connection handles are nonowning and do not disconnect by
destruction. Scoped handles provide receiver lifetime management. A sender can
be deleted during delivery, and remaining connected slots in that delivery still
run. Receivers captured by raw pointer/reference must outlive their connection
or use `WidgetRef`. User exceptions propagate from signals; native GLFW callback
adapters capture them and rethrow in the C++ event loop.

## Validation and future work

Core tests exercise observable outcomes: CSS cascade and transactional failure,
Unicode editing boundaries and history, signal mutation/lifetimes, shaping/caret
mapping, focus/capture, deletion during delivery, and clipping-aware hit testing.
Integration checks render actual GL pixels for both themes, hover, glyph holes,
rounded corners, clipping, blending, resizing, and a two-times physical scale.
Widget/layout tests also exercise bounded allocation with randomized inputs,
hidden-page sizing/focus, large list models, stable tree IDs, table sorting/editing,
command menus, and callback-driven deletion. GPU checks cover shared context
colors, state isolation, depth, resize/DPI, static caching, and cleanup.
The headless clipboard is an in-memory adapter; native OS clipboard and window
interaction still need validation on each target platform.

Animation tests exercise elapsed timing, curves, repeating/alternate playback,
pause/seek/resume, replacement, deleted targets, callback mutation/failures,
stylesheet retargeting, mixed units, focus/hover/hit testing, and paint-only
cache reuse. GPU probes compare animated color/opacity and geometry with actual
framebuffer pixels. Captures come from paused timelines sampled with Seek.

Rich CSS tests cover transactional nested parsing, compiled cascade assignments,
keyframe segments, timing longhands, interrupted state transitions, pause/resume,
fixed sizing, inverse transformed input, capture clipping, weak targets, and
500-widget cache reuse. Real framebuffer probes check linear/radial gradients,
inset/multiple shadows, rounded pseudo-element clipping, keyframe colors and
movement. A synchronous 160-button baseline/rich benchmark reports median/p95
frame time and batch/upload counts. See `verification/rich-css-performance.txt`
for the measured host and limitations. Retained-rendering regressions additionally
compare 48 complete RGBA frames with uncropped full redraws, covering subpixel
motion/shadows, translucent overlap and owner reordering, glyph changes,
decorative insertion/removal, transparent selection slots, visibility, rotated
rounded clips, and 1x/2x scale.
Hidden-tab clock/input tests ensure inactive CSS cannot request frames. Frame
pacing tests simulate 1,000 input wakeups per second.

A practical next sequence is Windows hardware validation, grid/wrapping/splitters,
IME/bidirectional text and accessibility, then incremental style/layout and GPU
profiling at larger widget counts. Collection selection is single-item/cell;
menus are single-level and large menus are clipped rather than scrollable. Performance measurements must
separate shaping, style/layout, instance submission, and GPU time rather than
inferring throughput from draw-call count alone.

## Canvas scene and CAD editing

`CanvasScene` owns dense analytic geometry and stable generational slots, with a
balanced dynamic AABB BVH for insertion, movement, deletion, and spatial queries.
`CanvasWidget` owns a double-precision camera, a cached viewport query, and a small
shared normal/selected CSS palette. Only candidates reach the GPU paint stream;
item identities are separate from widget paint identities so selection and
visibility changes do not renumber unrelated paint records. `DrawLine` uses one
oriented analytic capsule instance. Coordinates become floats after camera
subtraction; view-spanning lines are clipped before conversion.

`SketchDocument` layers analytic picking, snapping, selection, and bounded edit
commands over that model. Logical command records survive deleted/recreated scene
IDs so undo can safely restore geometry and painter order. `SketchWidget` keeps
in-progress gestures as transient previews and commits one command at release.
Sparse hosted widget trees remain ordinary owned children with normal input and
CSS; they are an overlay, not the representation of dense geometric objects.
See [CANVAS.md](CANVAS.md) for API contracts, format, and scaling boundaries.
