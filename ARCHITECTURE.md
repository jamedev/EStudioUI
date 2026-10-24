# Architecture

The current implementation is described here. The source-audited
[foundation roadmap](FOUNDATIONS_ROADMAP.md) records implemented and planned hardening work and
acceptance gates; the [widget roadmap](WIDGET_ROADMAP.md) depends on those gates.
Proposed classes and APIs in either roadmap are not current capabilities.

The prototype separates the persistent UI model, event dispatch, and drawing
without exposing third-party types in its public widget headers.

| Layer | Responsibility |
| --- | --- |
| `Application`, `Window`, `FramePacer` | Shared native event loop, GLFW lifetime/context, Unicode callbacks, clipboard, stable presentation deadlines |
| `UiDispatcher`, `UiHandle`, `DispatchReceipt` | Bounded producer queues, owner delivery, target lifetime, coalescing, inspectable outcomes |
| `BackgroundTasks`, `Task`, `RequestScope` | Cooperative workers, UI continuations, progress, cancellation and stale-request retirement |
| `State`, `StateStore`, `Binding` | Typed snapshots, validated coherent revisions, savepoints, control synchronization and reconciliation |
| `Action`, `ActionState`, `Shortcut` | Transactional command presentation, lifetime-bound worker handles, scoped gesture precedence and ambiguity handling |
| `InputEvent`, `PopupSession` | Owning native-independent payloads, thread-local input boundaries, bounded asynchronous popup/modal results |
| `PlatformServices`, `ClipboardData`, `FileDialogSession`, `WindowMetrics` | Injectable native capabilities, owned transfers, asynchronous results, logical/physical coordinate mapping |
| `SemanticTree`, `SemanticInfo` | Lazy widget index, stable virtual item coordinates, logical popup hierarchy, bounded on-demand queries, canonical actions and Windows UI Automation |
| `TextInputClient`, `TextCompositionEvent` | Owned preedit/commit/cancel protocol, surrounding text, focus-session identity and prepared caret geometry |
| `UiContext` | Widget tree, CSS resolution, constrained measurement, strategy allocation, clipping, focus, capture |
| `Widget`, `Label`, `Button`, `TextBox` | Owned model, cached shaping, behavior, emitted signals |
| `SizeRule`, `Layout` | Fixed/content/fill contracts and shared bounded weighted distribution |
| `MeasureResult`, `LayoutStrategy`, `BoxLayout`, `GridLayout`, `FormLayout`, `FlowLayout` | Width-first measurement, baseline alignment, shared tracks, wrapping rows, owned allocation reports |
| `Animation`, `AnimationSystem`, `EasingCurve` | Synchronized typed tracks, elapsed-time scheduling, easing, cancellation and lifetime safety |
| `CssMotion`, `VisualStyle` | Compiled CSS timelines, decorative paint layers, gradients, affine transforms |
| `StyleProperty` | Typed computed-style access, valid-range clamping, paint-only invalidation classification |
| `TabWidget`, `ToolBar`, `ToolButton`, `ContextMenu` | Owned pages, commands/toggles, popup layer |
| `ScrollWidget`, `ListWidget`, `TreeWidget`, `TableWidget` | Cached viewport text, shared external models/selection, typed editing and GPU delegates |
| `ItemModel`, `ModelSnapshot`, `SelectionModel` | Coherent revisioned collections, paged immutable records, stable-ID shared selection |
| `SortFilterModel`, `TreeRowMap` | Shared identity projections and expanded-branch viewport indexing |
| `OpenGLWidget` | Separate shared scene context, depth FBO, synchronized texture composition |
| `TextBuffer` | Valid UTF-8, grapheme/word navigation, selection, bounded undo/redo |
| `FontFace` | HarfBuzz shaping, FreeType outline decoding, CPU geometry cache |
| `Renderer` | One ordered instance stream, analytic shape and Slug-derived text shaders |

## Native-service and semantic lifetime

Platform services share the context's bounded dispatcher. Windows picker workers
own copied options/results and one COM STA; a reserved completion slot survives
queue overload. Cancellation settles the logical session before native unwind.
Window/adaptor retirement retains HWND/GLFW resources while native work remains,
without joining an active picker on an ordinary owner turn. Manual hosts retain
their native owner/context until HasPendingPlatformWork becomes false.

Semantics index actual widgets lazily and hold weak references. A virtual identity
combines that widget's lifetime identity with stable record/sub-item/dataset
coordinates; queries do not allocate a permanent node for every row or scene
item. Metadata and last-prepared transformed bounds are inspected without
preparing a frame. Custom hooks run in the inspection phase. Canonical actions
use the existing input/focus/validator path; native COM-thread requests cross
typed owner handles with bounded waits. UI Automation change reporting keeps a
bounded observation set and skips semantic scans without native listeners.

Text input uses the same owner boundary. Native composition owns its payload and
targets a widget lifetime plus focus-session generation. TextBox keeps preedit
separate from TextBuffer, paints its temporary run and underline through the GPU,
and commits one validated undoable edit. Focus/owner loss expires pending input.
The Windows adapter updates candidate geometry from the prepared caret and current
window metrics rather than asking widgets to handle native messages. Custom
editors implement TextInputClient. F10a now shares actual-font resolution,
script/bidi runs, visual caret affinity and disjoint range geometry across all
control text; [TEXT.md](TEXT.md) documents resource ownership and budgets. Native input-method, assistive-technology and
mixed-monitor observations remain separate acceptance evidence; see [PLATFORM.md](PLATFORM.md).

## Frame and cache behavior

Input dispatch has one thread-local event boundary across contexts. Owning
InputEvent payloads preserve logical/physical keys and pointer metadata; legacy
widget hooks remain adapters, while embedded scenes receive complete keyboard
events. Preview paths and command associations observe unique widget lifetimes.
Shortcut lookup visits associated commands rather than scanning the widget tree.
Shared ActionState values use the existing transaction/notification engine.

Native callbacks queue reentrant input through the bounded dispatcher. Focus loss
invalidates deferred input from the old focus session. Input prevents nested
native pumping, frame advancement, surface/paint/submission traversals, and queue
draining; explicit geometry-only Prepare remains available to controls and CSS
animation endpoint capture. Native resize continues through the ordinary scheduler
after the current callback. This introduces no render thread or runtime dependency.

Popup entries hold weak widget/parent/return-focus observations and one owned
PopupSession result. Physical root ownership is separate from logical selector
ancestry and popup-stack order. Closing first detaches/hides the branch, restores
eligible focus, then settles all results. Completion callbacks cannot overwrite a
newer presentation or reopen during teardown/deactivation. See [INPUT.md](INPUT.md).

Input and model changes declare their `Invalidation` impacts. Paint-only changes
retain styles and geometry; content changes discard the source intrinsic result
and its ancestor aggregate/allocation dependency path. Per-widget aggregate
measurements retain at most three variants of allocated dimensions and independent
percentage references, separately from intrinsic hooks. Arrangement skips unchanged
bounds/clips/reference semantics and clean subtrees.
Selector and structural changes conservatively match the tree, compare final
computed properties, and retain unrelated intrinsic measurements. The legacy
no-argument invalidation remains a full fallback for custom widgets.

Preparation resolves inherited styles, allocates widths before measuring dependent
heights, assigns logical-pixel bounds, and computes ancestor clips. Pure container
strategies share a temporary child measurement callback; extension output is
validated before child arrangement. Grid/flow/form layouts do not create track or
line widgets. Unresolved percentage/content cycles use a diagnosed Content fallback
rather than iteration. Compositions can select an immediate child baseline by
lifetime-unique identity; NumericInput selects its editor. Painting still records the visible tree in
order; incremental layout does not imply cached paint subtrees. `UiStats`
distinguishes actual intrinsic hooks, aggregate calculations/cache hits, and
arrangements, constrained hooks and strategy computations from stylesheet resolutions
and layout passes. Owned reports expose actual child/track decisions. See
[STATE.md](STATE.md) and [LAYOUT.md](LAYOUT.md).

The optional `ConstraintLayout` expands stable child anchors into private Kiwi
linear rows. Widget limits and fixed sizes precede named required constraints;
soft rules minimize weighted error. Width-dependent content uses one horizontal
solve and a vertical solve with frozen widths, without feedback iteration.
Three bounded query variants retain incremental tableaux; owned reports contain
rule expressions/status/residuals. See [CONSTRAINT_LAYOUT.md](CONSTRAINT_LAYOUT.md).

The event loop waits for GLFW events when nothing changes. A focused editable
text box wakes at its next half-second caret deadline. Visible continuous scene
surfaces request paced animation ticks; idle/hidden scenes do not. `FramePacer`
limits presentation starts to `WindowOptions::m_MaxFrameRate` (60 by default),
independently of vsync. Input wakeups retain the deadline; slow draws/swaps do
not accumulate catch-up frames. Minimized/invisible windows suspend presentation.
Unfocused windows suspend it by default; `m_RenderWhenUnfocused` opts in to
background rendering. Manual `RenderFrame` calls bypass the event-loop limit.

Native size/refresh callbacks also service safe frames directly. A private Win32
HWND subclass runs the shared window scheduler during system move/size/menu modal
loops, with an exit-cancelled timer and the same frame deadlines. It does not
poll nested events. Callback-depth, rendering/service, phase, and state-update
guards defer unsafe work; context restoration and deferred native destruction
remain enforced. Viewport changes invalidate arrangement/hit testing/paint while
retaining CSS/intrinsic caches. See the 0.13 live-resize contract and verification;
OS timers and hardware throughput still bound observed frame rate.

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

Each label/editor caches natural shaping until text or CSS font size changes.
Wrapped labels additionally retain at most three width-specific line layouts,
using space/hyphen breaks and grapheme-safe fallback in the current LTR service. FreeType
loads unhinted outlines only once per glyph ID. Curves use em-space coordinates;
TrueType quadratics remain exact. CFF cubic curves are recursively approximated
with quadratic segments to a 0.00005 em control-point tolerance, bounded at depth
12. HarfBuzz uses the font's OpenType tables without optional external integrations.
Editable runs disable `liga`/`clig` to simplify grapheme caret placement. Cluster
caret positions interpolate when a shaped cluster spans multiple graphemes.
Only left-to-right runs are currently supported.

## GPU representation

Managed `Image` resources (0.20) are shared immutable top-down RGBA8 CPU values,
independent of UI/native/GL initialization. PNG/JPEG decoding has bounded input,
output, dimensions and per-thread temporary allocation. `Icon` holds at most
sixteen densities with matching logical dimensions. `ImageView` uses source
density for intrinsic sizing and resolves object-fit/position during painting.
The image semantic role maps to Windows UIA Image without an extra focus stop.

Image recording retains source pixels until EndFrame. Before texture creation,
the renderer validates the complete visible unique-resource working set against
its GPU dimension, byte and 1,024-image limits. Required sources are pinned while
unused textures retire by least recent frame. Cache entries observe CPU lifetime
weakly; retirement and final deletion run on the context owner. Residency is
independent per renderer/context. New pixels upload inside the GPU upload scope,
with caller unpack PBO/pixel-store state restored. Premultiplied texture storage
avoids linear-filtering fringes, and the shader converts to the existing blending
contract. Existing external OpenGLWidget textures retain their own representation.

Repeated sources reuse residency and stable source revisions; unchanged frames
upload/draw no image data. Cache retirement/reload remains visible to retained
damage through texture/source identities. Explicit cache clears invalidate history.
Stats expose recorded images, resident count/bytes and per-frame upload bytes.
`Painter::DrawImage/DrawIcon` and Button's direction-aware image-icon slot use the
same recording path. [IMAGE_RESOURCES.md](IMAGE_RESOURCES.md) records public APIs,
budgets, extension boundaries and loading examples.

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

`ReadPixel` and `SavePpm` read the last completed retained GPU framebuffer with
the owning GL context current. Native swaps and unrelated read-framebuffer,
read-selector, pixel-pack-buffer, or row-packing state cannot redirect captures;
all affected state is restored. Captures require a successful frame and preserve
its logical/physical dimensions while a new frame is being recorded. They add no
readback or CPU image copy to ordinary rendering/presentation.

`SetPartialRedrawEnabled(false)` selects the full redraw reference path, submitting
every recorded primitive without damage culling. `SetGpuTimingEnabled(true)`
enables an eight-frame ring with four `GL_TIMESTAMP` markers per frame; only
available results are read, and busy slots never stall submission. Completed
samples carry their submitted frame serial and separate upload, retained drawing
and presentation-copy scopes. Native swap/compositor work is outside those scopes.
Zero-bit timestamp counters disable timing; wrapping/nonmonotonic samples are
discarded. Timing is disabled by default. Damage diagnostics distinguish budget
fallback from mandatory full redraw and retain the original dirty-tile area.
See [PERFORMANCE.md](PERFORMANCE.md) for reproducible native profiling.

The first backend is GL 3.3 core to keep setup and platform dependencies small.
It uses shaders, instancing, and buffer textures, with no fixed-function drawing.
A later Vulkan/D3D12/Metal backend would replace the renderer and window context
integration, while preserving model and input semantics. That work has not been
implemented or benchmarked.

## Layout, collections, and popups

Intrinsic measurements persist across prepare calls until their content or style
dependencies change. Aggregate measurements also retain their actual constraint
keys and are recalculated after dependency invalidation or a constraint change.
Code-level size rules are applied following CSS resolution, so a theme cannot
override an explicit fixed/limited contract. Nested rows/columns and table
columns share the bounded weighted allocator. Capped children return unused
shares; over-constrained containers are
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

Widgets and UI services enforce owner-thread access before reading or mutating
their state. `WidgetRef` is an owner-side weak observation; `UiHandle<T>` is the
copyable producer endpoint. It resolves its target only during owner delivery,
using a process-unique target identity and a lifetime token. Structural mutation
during style, measurement, layout, paint, scene preparation, or submission is
rejected before ownership changes. `UiContext::Defer` schedules a safe later turn.

The signal registry supports concurrent connection, disconnection, and emission.
Direct callbacks run on the emitter thread and own their synchronization needs.
Receiver-bound connections copy owned arguments and always queue; disconnection,
sender deletion, or receiver shutdown suppresses pending listener invocation.
Ordinary connection handles do not disconnect by destruction; scoped handles do.
User exceptions propagate from direct signals; native GLFW adapters capture them
and rethrow in the C++ event loop. Queued exceptions settle a receipt instead.

`Application` fairly services every registered window through one GLFW loop,
including bounded delivery to hidden/inactive windows. Native wakeups coalesce.
Each window advances its UI clock before draining posted work, then prepares and
renders when needed under the existing frame pacer. Closing one window immediately
hides its native surface and retires its delivery endpoint; other windows continue.
Close is idempotent. A retained C++ Window still owns its UI/context resources until
destruction; callback-driven native destruction is deferred until active
dispatch/frame scopes release their resources. The Async gallery reclaims retained
closed windows before opening another.

`UiDispatcher` bounds pending records and per-turn work; it executes callbacks
outside queue locks. `PostLatest` replaces pending target/key publications with
an explicit Superseded receipt. `BackgroundTasks` runs owned computations on a
bounded pool or injected executor, coalesces progress, and returns results through
typed handles. Receiver and request lifetimes request cooperative stop and retire
stale delivery. `Application::Tasks()` lazily supplies a common pool. Shutdown
cancels delivery without waiting; destruction joins owned cooperative workers.
See [THREADING.md](THREADING.md) for ordering, backpressure, and error semantics,
and [DEVELOPER_EXPERIENCE.md](DEVELOPER_EXPERIENCE.md) for API acceptance criteria.

## Typed application state and bindings

`UiContext::States()` lazily supplies an owner-side `StateStore`. A `State<T>`
owns its value behind const snapshots; workers capture a snapshot or post through
its typed handle. Direct reads are valid during painting and measurement, while
mutation observes the containing context's phase rules. No widget pointer is
resolved on a producer thread.

`Batch` stages a final candidate set. Equality, validation, and allocation finish
before no-throw publication of one revision. Nested batches act as savepoints.
Every changed value is published before the first observer runs; reentrant writes
wait until the current notification round finishes. A finite round limit diagnoses
feedback loops. Observer errors after publication cannot undo a committed value.

Built-in bindings connect labels, text/numeric editors, sliders, checked state,
visibility, and enabled state; generic `Bind` accepts a custom setter. Owner-side
propagation avoids an extra queued hop. Weak receiver identities and stop tokens
retire both directions. Bound-control representability participates in precommit
validation. Cancelled control edits restore the canonical value and expose an
inspectable error; discarded transaction candidates reconcile bound views without
false change notifications. Window resources remain leased through active calls.
Nested UI frame/event-loop processing and context-owned dispatcher drains are
rejected during state updates. See [STATE.md](STATE.md) for the exact read,
reentrancy, error, and teardown contracts.

## Shared collection models (0.11)

Canonical ItemModel data and SelectionModel values use StateStore transactions.
ModelSnapshot owns immutable ModelData; RecordData adapters retain C++ records in
256-record copy-on-write pages with shared identity/hierarchy indexes. Field edits
copy the page directory and touched pages; large Apply batches prepare pages once.
Structural builders perform explicit linear rebuilding while retaining typed
records and semantic-role adapters. Selection is an owned ordered set; individual
edits cost proportional to selected count, and bulk replacement is explicit.

SortFilterModel owns mappings and shared source snapshots. Typed sort/filter
preparation can scan full sources; declared filter field dependencies retain
unaffected mappings. Pure derived candidates are prepared before changes to any
upstream model or rule publish. Adapter changes notify in subsequent coherent
state rounds, with rendering excluded through the complete notification drain.
Selection remains canonical; scroll and expansion are view-local.

CollectionView composes weak widget subscriptions and precommit target phase
guards over canonical data, projections, and selection. ItemDelegate supplies
semantic style, cached-text customization, typed editing, and optional direct GPU
paint hooks without child widgets per record. TreeRowMap stores expanded-branch
prefixes rather than flattening a complete external hierarchy. Shared tables
sample content widths by default, with explicit hints/visible/exact policies.
External ModelData queries are nonblocking immutable provider contracts; built-in
network/database page scheduling remains future work. See [MODELS.md](MODELS.md).

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


## Reusable geometry, inputs, and custom drawing (0.7)

`CanvasGeometry` keeps simple primitives inline and complex payloads behind
immutable shared values. Curve controls and nested group children use local
coordinates. Group factories build private BVHs; nested queries preserve child
painter order without traversing every child. Adaptive curve and shaped-text
caches use bounded memory, stable immutable payload identities, and local
coordinates. Cache counters expose reuse and tessellation quality limits.
See [CANVAS.md](CANVAS.md) for model and persistence contracts.

`InputValidator` classifies complete candidate strings before editor mutation.
Invalid edits cannot damage selection or undo history; intermediate text remains
editable. `NumericInput` composes a text box and two commands, committing finite
values on Enter/focus loss. `Slider` maps captured pointer input and keyboard
commands into bounded scalar values. `SplitToolButton` composes commands with a
context-owned root popup. Popup ownership observes the logical control and prunes
expired owners during preparation, avoiding sibling deletion inside destructors.

`PaintWidget` provides a content-local `PaintContent(Painter&)` override.
`Painter` is a scoped facade over the existing ordered GPU command stream. Its
state stack restores transforms and clips even during exception unwinding.
`PainterPath` caches bounded adaptive flattening for stroked lines/arcs/Béziers.
Custom widgets use normal ownership, styles, focus, invalidation, retained damage,
and input dispatch; there is no separate GL context or renderer per control.
See [CUSTOM_WIDGETS.md](CUSTOM_WIDGETS.md) and [INPUT_CONTROLS.md](INPUT_CONTROLS.md).

## Named widget style parts (0.8)

Controls register a bounded set of named paint parts with a fallback-style factory
and supported state bits. The style engine compiles matching assignments during
restyle and caches the variants. Paint reads typed `Style` values; it does not
parse CSS or match selectors. Cached assignments can refresh owner-derived
fallback colors during paint-only animation without resolving selectors again.
Parts are not children, layout nodes, or focus targets.

Fixed animated parts use stable additional layers in `CssMotion`, sharing the
owner's lifetime, visibility, and clock. Collection rows/cells and scrollbar
parts use shared static variants; model size cannot create per-item timelines.
Collections continue to submit viewport entries only. Inner paint identities
include stable item/part coordinates so shadows and selection changes do not
renumber unrelated retained records. Text shaping is cached by text and font
size under bounded entry and byte limits.

Owner and part states are distinct: `listwidget:hover::item` addresses every item
while `listwidget::item:hover` addresses the hovered item. Common owner states
such as disabled, focus, validation, and read-only participate in part matching.
Fixed control dimensions used by pointer mapping come from the same styles as
their drawing. Collection row height and table column allocation retain their
existing geometry contracts; decorative part transforms do not change those
allocations.

Composite controls expose real child selectors and semantic state. Built-in
cosmetic sizes and gaps are pre-cascade defaults, while explicit application
contracts still win. Owned popup selector ancestry follows the logical owner,
independently of root overlay layout, clipping, focus routing, and lifetime
pruning. See [WIDGET_STYLING.md](WIDGET_STYLING.md).

## Common-control composition (0.18)

TabWidget reserves a navigation row containing backward/forward arrow buttons,
the actual `tabbar` and a persistent tab chooser. A pure row strategy preserves
caption widths and an explicit viewport offset. Selection/focus uses minimal
reveal; it does not realign a visible selector between pointer press and release.
Arrows and horizontal/vertical wheel motion scroll the viewport independently of
page selection. The chooser reuses the owned popup stack and a virtual
ListWidget; captions and weak page identities are copied on opening, never during
paint. Choosing a snapshot row resolves its persistent page again after popup
closure, so removal of preceding tabs cannot select an unrelated page.

ComboBox composes the existing TextBox and a nonfocusable analytic arrow button.
Its ListWidget popup shares canonical model storage with independent preview
selection; a bounded Unicode prefix index and numeric enabled-row index avoid
model scans per key or frame. Candidate validation precedes model publication.
The popup uses existing owner lifetime, logical styling and asynchronous sessions.

ScrollArea composes a clipping viewport and ordinary content widget. A scroll
change explicitly invalidates the child strategy input through InvalidateChild;
this preserves intrinsic caches while rearranging translated descendants. Focus
reveal uses the common ancestor hook and transformed coordinate conversion.
The base ScrollWidget starts thumb/track capture on pointer-down, so ScrollArea
uses the same production drag path as collections without a test-only adapter.
Splitter assigns lifetime-keyed stretch weights through the shared allocator;
hard fixed/min/max rules remain authoritative. Prepared reports are borrowed in
paint and copied only through the public owned diagnostics API.

Choice controls commit persistent state before notification; radio groups update
all members coherently and observe widgets weakly. Progress reuses the paint-only
invalidation path. Tooltip deadlines join the window scheduler, rather than
requesting animation frames. Nonactivating/input-transparent popup options retain
normal defaults, modal ancestry and shared input reentrancy/focus guards. See
[COMMON_CONTROLS.md](COMMON_CONTROLS.md) for limits and public usage.


## Command and dialog surfaces (0.19)

MenuBar owns bounded persistent triggers and observes root-owned ContextMenus.
The bar retains canonical action associations independently of popup visibility;
shortcut resolution continues to deduplicate shared actions. MenuItem extends
ToolButton with cached caption/icon/hint layouts and analytic check/arrow parts.
There is no OS menu dependency or per-paint CSS parsing.

Popup exposes the existing PopupSession stack as a composable control. Directional
anchors are weak identities, not retained widget pointers. A bounded overlay
arrangement pass follows viewport/layout/transform changes without invalidating
unrelated intrinsic caches. Center placement stays current throughout live resize.
The nonmodal owner-input exception enables menu switching; modeless mode permits
outside interaction without relaxing modal barriers. Ordinary defaults preserve
existing popup dismissal and focus behavior.

Dialog composes a header, content widget and DialogButtonBox. Standard response
roles choose acceptance/rejection or remain open for Apply/Reset/Help handlers.
MessageBox adds wrapping text; InputDialog reuses TextBox validation/composition
and returns a typed session. Accepted text is snapshotted once before completion
callbacks; no result owns an editor, context or native window. Modeless closure
preserves focus already moved outside its dialog. All surfaces reuse the ordinary
scheduler, input transforms, semantics and retained GPU renderer. See
[COMMAND_DIALOGS.md](COMMAND_DIALOGS.md) for the public ownership/cost contract.


Native picker cancellation uses queued WM_CLOSE on its STA-owned dialog HWND,
with a 100 ms native timer and a helper-window channel after folder readiness.
Only an observed same-thread owned confirmation may be dismissed first. No
Shell window is destroyed and no native thread is interrupted. Timers/property
cleanup and COM retirement preserve the native owner lease; diagnostic phases
are enabled by the explicit native-test environment flag. This replaces
IFileDialog::Close calls from timer/helper callbacks, which could acknowledge
success without unwinding Show. Arbitrary unresponsive extensions still require
a future isolation policy rather than unsafe in-process preemption.

## Standalone scrolling and persistent pages (0.21)

ScrollBar owns a finite canonical scalar range and visible extent. ScrollGeometry
is private, shared by standalone and collection scrollbar inset/proportion/drag
paths; no content widget or per-row presentation is introduced. Value bindings
reuse State validation and rollback; semantics use the existing numeric action
and Windows RangeValue adapter with the ScrollBar control type. Value/page changes
retain CSS and measurement/layout caches. No new timer or runtime dependency is added.

StackedWidget owns ordinary pages through Widget and observes lifetime-safe page
identities. It applies visibility before bounded CurrentChanged notifications.
Only the active page reaches the existing width-first allocator, input/paint and
animation traversal. Hidden focus/capture/popup/composition retirement stays in
UiContext; the container creates no event loop or duplicate input system. Custom
page adoption preserves explicit sizing, composition and paint overrides.
[SCROLL_STACK.md](SCROLL_STACK.md) records APIs, costs and ownership boundaries.


## Application shell and workspace ownership (0.22)

MainWindow composes WindowTitleBar, ordinary overflow ToolBar rows, DockManager
and StatusBar. NativeWindowChrome contains Win32 subclass/DWM details, delegates
caption/resize hit regions, and chains with NativeResizePump. Prepared hit queries
never pump, lay out or render. Other native platforms retain their standard frame.
Small caption/panel symbols use analytic GPU lines independent of font coverage.
Custom Windows caption controls own their pointer/capture path instead of
entering User32's native button tracker. Frame calculation and activation keep
the client/GPU caption separate from native caption painting; native resize,
move, system-menu and Snap hit roles remain available. Client size is preserved
across frame changes and computed from actual frame insets for explicit sizing.
Native caption presses carry an explicit preserve-focus pointer policy through
the shared input boundary, leaving client focus and preedit intact; native
caption controls do not become client Tab stops.
Maximize/restore presentation synchronizes only at ordinary frame boundaries.
Explicit `RenderFrame()` calls report captured native callback errors before
and after processing, matching the ordinary event loop's error boundary.
See [native title-bar verification](verification/TITLEBAR_FIX.md).

FileDialog is an owner-lifetime facade over PlatformServices. FilePathEditor and
FolderPicker own an ordinary editor, bounded recent choices and a session/revision
guard; BindPath uses the existing canonical binding machinery. No filesystem
probe, UI wait, theme parse during painting or continuous workspace timer is added.

DockManager composes two Splitters, four TabWidgets, a central container and hidden
panel storage. Tab rows appear only for multiple panels. DockWidget subtrees move
intact through Widget::ReparentTo; focus/preedit/capture and active popups retire
before transfer, and owned popup records/widgets migrate together. Font caches
retire on context changes. Context generations prevent stale UiHandles from
reactivating after a return; shared bindings follow WidgetRef instead.

CreateOwnedWindow shares the GPU group/fonts/compiled CSS and a weak presentation
focus group. Any focused member keeps visible group windows eligible for normal
capped updates; a wholly unfocused group retains the existing pause policy. Floating GL surfaces
keep separate scene contexts and producer/consumer synchronization. Incompatible
GPU groups fail before ownership changes. Retired Window implementations restore
the prior GL context after cleanup, including retirement at a frame boundary.
A bounded versioned snapshot stores panel placement, selection, visibility,
floating bounds and splitter sizes; parsing rejects the complete invalid payload
before touching live state. [WORKSPACES.md](WORKSPACES.md) documents costs and limits.

## Property values and UI documents (0.23)

`PropertyDefinition` describes immutable typed editor metadata. `PropertyModel`
uses canonical State values, final-candidate validation and owned notifications;
`PropertyGrid` caches category/row positions, paints only visible rows, keeps one
live editor and exposes lazy dataset-qualified semantic cells. Independent color,
unit and vector controls reuse ordinary widget composition, shared popup sessions,
State bindings, narrow invalidation and bounded reentrant notifications.

`WidgetRegistry` supplies explicit trusted factories/content slots/property adapters
for both `UiDocument` runtime construction and direct C++ generation. Documents
contain owned stable design IDs, typed overrides, layout/constraint values and
optional CSS; unknown types or incomplete references reject before attachment.
Factories construct off-tree, and a failure preserves the live host. The canvas
loader uses existing sparse `HostWidget` subtrees. JSON parsing is private,
header-only nlohmann/json at the pinned permissive baseline. Generated C++ calls
ordinary constructors/setters/layout APIs without a runtime document dependency.

The Creator is a separate application target consuming those public APIs. It
coalesces preview work at frame preparation, preserves actual widget identity for
property-only updates and commits one document snapshot per completed geometry
gesture. Structural changes rebuild a staged preview. F14 packaging/module
extraction remains deferred; this boundary is evidence from a real consumer,
not a new SDK compatibility commitment. See [CREATOR.md](CREATOR.md).


`Window` owns its `ThemeController` before the UI is released. Theme values and
semantic palette resolution are independent of native/GL handles. Only changed
CSS is compiled; existing widgets and normal caches remain shared. Platform
appearance notifications carry owned snapshots and use the existing owner-side
service pump. Windows messages mark dirty state without executing application
callbacks; recursive service pumping is diagnosed. Other platform adapters report
support explicitly. The generated cascade preserves a direct stylesheet escape
hatch and normal CSS specificity; no recurring appearance timer is introduced.

Creator palette icons are bounded visible-row paint records inside one ListWidget,
not one child per entry. Captured pointer gestures preview registered content-slot
targets and defer document insertion until after input delivery. Geometry gestures
cache sibling references once, preview at most two snap guides and commit one
canonical snapshot. Optional relationships use the existing constraint solver.
Reusable compositions are flattened owned document subtrees with remapped IDs;
source class generation reuses the direct C++ node/property/layout emitters and
is compiled in the catalog test, without a runtime registry/JSON requirement.

## Document lifecycle and transactional history (0.24)

`UndoHistory` stores its timeline as State in the application's existing StateStore.
Command effects, grouped commands and cursor/clean-position metadata share the
same transaction, rollback and final-candidate validation boundary. Immutable
command snapshots are pruned under count/estimated-byte budgets; lifetime-unique
positions distinguish older saves from newer edits even after branching/pruning.
The public service requires no widgets or GL initialization; F14 packaging remains
deferred. Creator bridges its existing canonical design edits into this service.

`DocumentWorkspace` owns stable IDs and persistent view containers, reparenting
actual views to owned native Window hosts on detach. It uses shared Action
presentations, opt-in TabWidget close/reorder/drag-out hooks and PopupSession-based
MessageBox approval. Close All preflights all documents, records exact checkpoint/
external-edit stamps and rechecks focused-editor commits before any removal.
Native close requests use a synchronous veto and retry after asynchronous approval;
forced lifetime teardown remains explicit.

Each pending application save reserves one independently synchronized completion
slot. Workers own immutable application snapshots and complete tickets exactly
once; they never read widget data. Completion sets a ready bit and wakes the
existing dispatcher without consuming ordinary queue capacity. UiContext's idle
service callback consumes results, advances decisions and retires closed hosts.
Unchanged turns do no document scan and introduce no new timer. Recursive service
pumping is diagnosed; owners can disappear during a callback without leaving raw
service targets alive. See [contracts and limits](DOCUMENT_WORKSPACE.md).
