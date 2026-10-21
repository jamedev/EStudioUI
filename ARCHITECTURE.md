# Architecture

The prototype separates the persistent UI model, event dispatch, and drawing
without exposing third-party types in its public widget headers.

| Layer | Responsibility |
| --- | --- |
| `Window` | GLFW lifetime/context, Unicode callbacks, clipboard, sleeping event loop |
| `UiContext` | Widget tree, CSS resolution, row/column layout, clipping, focus, capture |
| `Widget`, `Label`, `Button`, `TextBox` | Owned model, cached shaping, behavior, emitted signals |
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
text box wakes at its next half-second caret deadline. There is no fixed 60 Hz
polling loop. Rendering is currently whole-window for each dirty frame.

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
instance buffer. A flat instance kind selects shape or glyph processing. A single
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
The headless clipboard is an in-memory adapter; native OS clipboard and window
interaction still need validation on each target platform.

A practical next sequence is Windows hardware validation, IME/bidirectional text
and accessibility, scrolling/layout refinement, then incremental style/layout and
GPU profiling at larger widget counts. Add widgets after the ownership and input
contracts meet the target application's needs. Performance measurements must
separate shaping, style/layout, instance submission, and GPU time rather than
inferring throughput from draw-call count alone.
