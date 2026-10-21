# Architecture

The prototype separates the persistent UI model, event dispatch, and drawing
without exposing third-party types in its public widget headers.

| Layer | Responsibility |
| --- | --- |
| `Window` | GLFW lifetime/context, Unicode callbacks, clipboard, sleeping event loop |
| `UiContext` | Widget tree, CSS resolution, row/column layout, clipping, focus, capture |
| `Widget`, `Label`, `Button`, `TextBox` | Owned model, cached shaping, behavior, emitted signals |
| `SizeRule`, `Layout` | Fixed/content/fill contracts, bounded weighted distribution, overflow |
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
text box wakes at its next half-second caret deadline. Visible continuous scene surfaces request paced animation ticks; idle/hidden
scenes do not. Draw/swap time is counted in the animation deadline. Rendering is currently whole-window for each dirty frame.

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
instance buffer. A flat instance kind selects shape or glyph processing. Without scene textures, a single
instanced draw preserves painter order and alpha blending, including translucent
primitives interleaved with glyphs. Per-instance clip rectangles avoid scissor
state changes. The fixed-width stream costs 112 bytes per visible instance.
Cached font buffers upload only after new glyphs are encountered; per-frame
instance data uses GL buffer orphaning/upload. Existing vector capacities are
reused. Texture-buffer limits are checked and diagnosed; eviction is future work.

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

Static scenes keep their textures until invalidated/resized. Animated scenes
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

A practical next sequence is Windows hardware validation, grid/wrapping/splitters,
IME/bidirectional text and accessibility, then incremental style/layout and GPU
profiling at larger widget counts. Collection selection is single-item/cell;
menus are single-level and large menus are clipped rather than scrollable. Performance measurements must
separate shaping, style/layout, instance submission, and GPU time rather than
inferring throughput from draw-call count alone.
