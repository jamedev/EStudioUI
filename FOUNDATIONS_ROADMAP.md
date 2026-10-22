# EStudioUI foundation audit and hardening roadmap

Audit date: 2026-10-22. Audited baseline: 0.8.0, commit `dc14170`.
The original audit below describes the 0.8.0 baseline. The implementation status
section records subsequent work; historical gap descriptions are not statements
about the current source.

The goal is a dependable C++ GUI foundation for the applications described in
[WIDGET_ROADMAP.md](WIDGET_ROADMAP.md). Settle common execution, lifetime, state,
input, layout, text, and resource contracts before many controls depend on
different local solutions. Retain the working GPU renderer and existing tests.

## Implementation status: 0.9

**Gate A is implemented.** F01-F04 now have public APIs, lifetime/phase/error
contracts, concurrency tests, and actual multiwindow/framebuffer probes.
[THREADING.md](THREADING.md) documents the shipped signatures and policies;
[verification/FOUNDATIONS_0_9.md](verification/FOUNDATIONS_0_9.md) records the
measured evidence and platform limits. The original illustrative `Task::ThenOnUi`
proposal became receiver-bound `BackgroundTasks::Run` / `RunLatest`.

| Topics | Current status |
| --- | --- |
| F01-F04 | Implemented: shared application loop, bounded dispatch, typed handles, receiver-bound signals, cooperative tasks, progress, stale-request suppression |
| F13 | Gate A baseline implemented: owner checks, traversal phase guards, deferred mutation, receipt/task failures, queue counters; visual inspector remains planned |
| F15 | Gate A concurrency/lifetime/framebuffer coverage implemented; native hardware and broader platform coverage remain open |
| F05-F12, F14, F16 | Planned, with the existing baseline capabilities described below |

Ease of use is a standing acceptance criterion, not a separate polish phase.
[DEVELOPER_EXPERIENCE.md](DEVELOPER_EXPERIENCE.md) defines the review rules:
concise common paths, safe lifetime defaults, clear errors, and explicit advanced
control. No runtime dependency was added for Gate A.

**Next: Gate B, beginning with F05/F07.** Define typed observable state and batched
transactions alongside precise invalidation impacts. Prove coherent updates and
unrelated-cache reuse before external models, layout strategies, input/semantic
contracts, and package integration. Broad widget expansion remains gated.

## Original assessment of the 0.8 implementation

| Area | Existing foundation | Source-visible gap |
| --- | --- | --- |
| Widget ownership | Unique child ownership; weak observers; deletion-safe input/signal cases | No enforced owner thread or general deferred structural mutation contract |
| Signals | Typed, ordered synchronous delivery; nested emission and connection mutation; scoped disconnection | No synchronized cross-thread delivery or receiver-bound queued lifetime policy |
| Application execution | Efficient GLFW wait loop, frame pacing, deterministic animation clock | Window::Run schedules one window; no central Application, dispatcher, task continuation layer |
| Layout | Fixed/Content/Fill, hard limits, capped redistribution, explicit overflow | Row/column layout; no width-constrained measurement, baseline/grid/form/flow strategy |
| Change propagation | Paint-only style paths and compiled CSS/part palettes | One global dirty flag usually triggers root restyle and arrangement |
| Rendering | GPU shapes/glyphs, retained damage, stable paint identities, full-redraw reference | Visible tree still re-recorded; GPU glyph data has no eviction policy; multi-font keys not present |
| Collections/canvas | Viewport rows, bounded caption/curve caches, BVHs, stable canvas IDs | Collections own their data; no external/lazy model, shared selection/delegate contract or async publication API |
| Text | UTF-8 sanitation, grapheme editing, shaping, history, validators | Single-line storage; single font; explicit LTR shaping; no IME composition or paragraph engine |
| Input/platform | Focus/capture, transformed input, logical/framebuffer scaling, text clipboard | Single popup, limited key/event envelope, no native accessibility or general MIME/drag/drop service |
| Delivery/verification | Pinned dependencies, Windows one-shot build, Linux real software GL, sanitizer evidence | No install/export SDK consumer test; native platform/hardware matrix remains incomplete |

These are scope limits and scaling risks established by source inspection, not
newly reproduced crashes. The 0.8 evidence records 16/16 local Release and
ASan/UBSan suites, Linux CI 16/16 including software GL, and Windows CPU Release
and Debug 15/15 each. It does not establish arbitrary-thread safety, IME/bidi,
native accessibility, or Windows hardware throughput.

## Priority topics

At the audit baseline all entries started as **planned**. A contract is complete when it has a documented
public behavior, an implementation, and the relevant acceptance evidence.

| ID | Topic | Required result | Gate |
| --- | --- | --- | --- |
| F01 | Application and window lifecycle | One event loop schedules all windows, deadlines, closure, and shutdown | A |
| F02 | UI dispatch and thread affinity | Safe posting from any producer; owner-only widget access is enforced | A |
| F03 | Lifetime and signal delivery | Receiver-bound subscriptions, owned queued payloads, generation-safe delivery | A |
| F04 | Tasks, cancellation, and publication | Executor interface, UI continuations, progress, stale-result suppression, bounded queues | A |
| F05 | Typed state and update transactions | Consistent validation/notification/binding semantics and batched commits | B |
| F06 | External models and selection | Stable IDs, lazy/snapshot data, roles, batched deltas, delegates, shared selection | B |
| F07 | Incremental invalidation | Separate style/measure/arrange/hit-test/paint changes; retain unaffected caches | B |
| F08 | Layout measurement and diagnostics | Constraint-aware measure, height-for-width, baselines, layout strategy interface | B |
| F09 | Input, commands, popup/modal scopes | Shared event envelope, actions/shortcuts, focus and popup stack | B |
| F10 | Text/font/document service boundaries | Font identity/fallback/styled runs, composition client, paragraph/edit model | C |
| F11 | Platform and accessibility services | Semantic nodes and native adapter/capability contracts; real platform probes | B/C |
| F12 | Resource budgets and render lifecycle | Bounded font/image/GPU caches, safe retirement, diagnostics and recovery policy | C |
| F13 | Errors, phases, and developer diagnostics | Deterministic mutation/error rules, inspectable invalidation/queue/layout work | A/B |
| F14 | Public API and package integration | Relocatable CMake package, independent consumer, version and extension contracts | B |
| F15 | Verification and performance gates | Concurrency, lifetime, platform, work-count, memory, and actual framebuffer checks | A/B/C |
| F16 | Commands, undo, and persisted workspace state | Reusable history/change grouping, stable serialized identities, versioned restore | C/application track |

## F01-F04: make background work natural and safe

### The execution model

Keep one owner executor for the live widget tree, focus, layout, CSS presentation,
and initial GL submission. GLFW native event processing and window creation/
destruction, including hidden shared-context windows, have main-thread
restrictions. Context-current/rendering ownership is a separate rule. A
thread-safe posting API can hide routine marshaling without allowing concurrent
mutation of those structures.

Qt already has automatic/queued connections and worker facilities. Our intended
improvement is a small, consistent workflow with lifetime, cancellation,
coalescing, and model binding built into the common path.

Proposed execution domains:

| Domain | Owns/does | Cross-domain boundary |
| --- | --- | --- |
| Main/UI executor | Native pumping, live widgets, attached/presented UI models, input, style/layout, GL work initially | Receives owned commands or immutable results |
| Worker executors | I/O, parsing, geometry generation, heavy calculations, image decoding | Publish snapshots/deltas and task completion |
| Rendering resources | Existing owner-context allocation, fences, submission and retirement | Explicit GL lifecycle; CPU task dispatch does not make scene callbacks worker-safe |

Introduce an `Application` abstraction for window registration and scheduling.
Keep simple single-window usage as a convenience over that application owner.
Interactive dialogs and floating docks must share one event loop, rather than
depending on nested independent Window::Run calls.

### Dispatch and lifetime

Provide a thread-safe dispatcher and typed target handles. Names such as
`UiDispatcher`, `UiHandle<T>`, `Post`, and `Task::ThenOnUi` are illustrative API
proposals, not current classes or signatures.

- Create handles through the owning executor. Workers may copy/store handles,
  but only the UI executor resolves a handle into a live widget.
- Use dispatcher/context lifetime plus target generation. Reused addresses must
  never make an old command refer to a new widget.
- Execute application callbacks after releasing queue locks.
- Document whether work is accepted, delivered, cancelled, rejected, or failed.
  Define ordering for accepted commands from one producer and transaction.
- Make queued work consistently queued. An explicitly named dispatch-now path
  may execute inline on the owner; it must not silently change reentrancy based
  on which thread happened to call the method.
- Retain fast direct owner-thread signal delivery. Add a deliberate queued/
  receiver-bound path with owning values and safe connection teardown.
- Borrowed references, `string_view`, spans, or pointers in queued arguments
  require explicit lifetime ownership or conversion. Current reference-valued
  signals cannot simply be stored for later delivery.

`WidgetRef` currently checks a weak token and returns a raw address. It remains
useful inside serialized UI delivery; it must not be advertised as a worker-safe
dereference or a lifetime pin. Add owner-thread diagnostics to direct setters,
getters, tree access, signal operations, and GL APIs where appropriate.

Current `Window::RequestRepaint()` is not worker-safe: it changes ordinary
UiContext repaint state before calling `glfwPostEmptyEvent()`. The thread-safe
native wakeup does not synchronize preceding UI access or implement dispatch.

### Developer-facing convenience

The preferred application workflow is:

1. Start work through a configurable executor; capture owned input snapshots.
2. Publish progress or a result into an observable model/property, or use a
   continuation bound to a UI handle.
3. The framework schedules, batches, and applies the live UI updates, checking
   target lifetime and request/document revision.

Bindings should make a worker publishing progress sufficient to update a bound
progress control. Convenience handle operations can post common updates. The
application should rarely need to manually find a window/event-loop object.

Keep synchronous getters on the owner executor. Workers read immutable snapshots
or request an asynchronous owned result. Do not silently return stale cached
widget values or make every setter secretly asynchronous. Explicit asynchronous
semantics preserve understandable read-after-write behavior.

### Tasks and overload handling

- Support application-provided executors; C++20 primitives are enough for an
  initial implementation without adding a task runtime dependency.
- Cancellation is cooperative. Suppressing result delivery and stopping the
  underlying calculation are distinct operations.
- Use task/request generations so a slower old search or document calculation
  cannot overwrite a newer result on a still-live widget.
- Coalesce explicitly replaceable progress/preview updates by target/key.
  Define a Superseded outcome for any replaced publication with a receipt, so
  accepted work cannot leave an acknowledgement or task permanently pending.
  Ordered edits and model deltas retain their semantics; do not silently drop
  accepted commands.
- Bound queue memory and per-turn drain work. Define rejection/backpressure for
  nonreplaceable work; producer overload must not block the UI thread.
- Settle task errors/cancellation/completion exactly once. Avoid a default
  blocking invoke-and-wait API and UI waits on work that needs UI continuations.
- Expose queue high-water marks, latency, coalescing, rejection, and task errors.

Shutdown must stop accepting work, cancel scoped deliveries/subscriptions, retire
queued targets, and release native/GL resources on their owner. Distinguish owned
executors from application-provided ones: cancel UI delivery promptly, join only
owned workers according to policy, and define how injected executors detach.
Noncooperative computations cannot be promised prompt termination. No worker
completion may require a stopped UI loop. Closing one window must not stop other
live windows or invalidate application-wide services unexpectedly.

## F05-F07: common state, models, and change propagation

Extend the existing typed `StyleProperty` approach with shared change-impact
metadata and observable state contracts. Separate application values from their
computed CSS/animation presentation. A full reflection/code-generation system
is not a prerequisite.

Define validation, equality/no-op updates, notification ordering, reentrant edits,
binding loop handling, and transaction commit behavior. A batch applies one
coherent state and schedules necessary derived work after commit. Use these
contracts for async results, scalar controls, data models, and future documents.

Introduce external/lazy model interfaces with stable IDs, typed roles, revisioned
snapshots or deltas, selection models, editor/delegate hooks, and proxy
sort/filter mappings. Preserve convenience in-memory list/tree/table adapters.
Worker code prepares isolated data; attached model publication follows the UI
commit contract. One model should serve several views without duplicating data.

Replace the single global dirty category with explicit impacts: selector matching,
inherited style, measurement, arrangement, transformed hit geometry, painting,
and structure. Propagate through actual dependencies. A child caption may affect
ancestors and sibling allocation; that does not justify discarding unrelated
styles or intrinsic measurements.

Make expensive sizing explicit. Table Content columns currently shape all rows
on a cache miss. Add model hints, visible/sample policies, and incremental width
caches. Exact full-content fitting can remain an explicit measurable operation.
Likewise, optimize expanded tree row maps when measured model workloads require it.

Keep ordinary widgets as owned objects. Keep dense canvas geometry contiguous,
with generational IDs and BVHs. ECS for every control is not required to achieve
these changes. Add revisioned/batched scene publication so picking and painting
observe a coherent scene; direct scene changes currently require SceneChanged.

## F08: predictable layout before more containers

Preserve Fixed/Content/Fill, hard minima, cap redistribution, explicit API
precedence, and overflow reporting. Add an available-size/constraint-aware
measurement interface with preferred/minimum sizes, height-for-width, baseline,
and a clear policy for percentage/content cycles.

Separate container layout strategies from Widget so row/column, grid, form,
wrapping flow, and future splitters share allocation/diagnostic machinery. Report
which rule or child caused overflow and why the chosen size was allocated.

An optional constraint layout can follow these contracts. Begin with linear
equalities/inequalities and hard/soft priorities for anchors, alignment, equal
sizes, spacing, and aspect ratios. A nonlinear CAD sketch solver is unnecessary
for ordinary layout relationships. Define how explicit SizeRule contracts interact
with constraints and explain infeasible hard constraints deterministically.
Choose any solver dependency only after requirements and license review.

## F09/F11: one input, focus, command, and semantic pipeline

Define a stable event envelope separating committed text, logical shortcut keys,
physical keys/scancodes, repeat, modifiers, timestamps, pointer identity/button,
and future pen/touch data. Handle platform-primary shortcuts and AltGr without
per-widget native workarounds.

Add Action/Command state shared by menus, toolbars, shortcuts, palettes, and undo
commands. Add focus scopes and a popup/modal stack supporting nested menus,
combos inside dialogs, owner lifetime, logical CSS ancestry, outside-click/Escape
dismissal, and reliable focus restoration. Prefer asynchronous dialog results;
any nested-loop convenience needs an explicit reentrancy contract.

Create accessibility semantic nodes independent of GPU primitives: role, name,
value, state, actions, focus, logical hierarchy, virtualized children, and
transformed/DPI-aware bounds. Establish hooks before adding new control families;
verify native adapters with real assistive technology before claiming application
readiness.

Provide injectable platform services and capability reporting for text input,
clipboard formats, drag/drop, cursors/icons, native dialogs, monitor scaling,
owned windows, and accessibility. Keep headless adapters deterministic and make
unsupported features visible. GLFW remains useful for windows/GL; native text
composition and accessibility need additional platform integration.

## F10: text foundations before document controls

The current TextBuffer intentionally sanitizes line breaks/tabs to spaces and
keeps full-string history snapshots. FontFace shapes one LTR run from one face.
Do not grow a document editor by removing the newline restriction alone.

Define font identity/resolution/fallback, styled runs, script/direction
segmentation, logical-to-visual caret mapping, paragraph layout, and a
TextInputClient/platform composition protocol. Composition needs preedit,
commit/cancel, surrounding text/selection, and screen-space caret geometry.
Changing one HarfBuzz direction flag does not implement bidirectional documents.

Define separate multiline/document storage, positions, edit commands, byte-bounded
history and paragraph invalidation. Share layout between editing and viewing.
Full paragraph implementation belongs to the document track; settle the service
boundaries and prove a representative text/input slice first. Evolve CSS font
selection and GPU glyph keys together so font-local glyph IDs do not collide.

## F12-F16: bounded resources and a consumable framework

Preserve producer/consumer fences, owner-context cleanup, exception restoration,
static scene reuse, stable paint identity, and retained/full comparison tests.
Background CPU work and a dedicated render thread are separate design decisions.
Add a render thread only if measured workloads justify the synchronization cost.

Introduce explicit CPU/GPU font/image/cache budgets, live-use accounting,
eviction/rebuild policy, safe resource retirement, and diagnostics. Existing
collection and canvas caches are bounded, while GPU glyph data accumulates until
the texture-buffer limit. Multi-font and long-running applications need a policy
before resource demand expands.

Zero submitted GPU instances does not mean zero CPU work: PaintTree still records
visible widgets and the renderer compares paint records. Measure recording,
damage construction, upload, and GPU execution separately. Cache paint subtrees
only against a demonstrated workload and retain the full-redraw correctness oracle.

Document legal mutation phases around input/commands, posted commits, animation,
style/layout, scene preparation, paint, and present. Preserve deletion-safe input
callbacks. Defer or diagnose unsafe structural changes during traversal. Define
exception behavior at user callback, native callback, task, and frame boundaries.

Add a debug inspector for ownership/focus/capture, computed style and precedence,
layout constraints/overflow, invalidation causes, queue/task state, and cache
work. Keep structured diagnostics usable without the visual inspector.

Ship relocatable CMake install/export/package files, public-header checks, a
separate installed-SDK consumer test, asset/notices rules, and a clear source/API
compatibility policy. Consider separating CPU model/core from native/render
targets so consumers/tests need only their actual capabilities.

Generalize reusable command grouping/history and versioned workspace state at
the application track. Reuse lessons from SketchDocument; do not couple all
widgets to the sketcher's history. Restore layouts, dock states and settings by
stable IDs, with migration and malformed-state handling.

## Delivery gates and evidence

These are acceptance fixtures; the 0.9 status/evidence above records Gate A results.
Record observed work and named hardware; avoid arbitrary FPS claims.

| Gate | Work to close | Acceptance evidence | Widget work unlocked |
| --- | --- | --- | --- |
| A: execution and lifetime | F01-F04, phase/error baseline from F13, relevant F15 | Two changing windows under one loop; worker posting during sleep/render/resize/close; dead/reused receivers and stale revisions rejected; bounded overload; exactly-once settlement; shutdown has no UI-worker wait cycle | Prerequisite for adopting background/UI APIs; no broad widget expansion yet |
| B: shared widget contracts | F05-F09; semantic/platform interfaces in F11; F14; relevant F13/F15 | Model shared by multiple views; sort/filter preserves identity; transaction yields coherent state; isolated edits retain unrelated caches; constrained wrapping/baseline tests; nested popup/focus cases; installed consumer builds | First common-control batch, with role/style/input contracts reused |
| C: text, resources, native readiness | F10/F12; concrete F11 adapters; remaining F15 | Mixed scripts/fallback and IME reference cases; bounded font/resource churn; real Windows clipboard/DPI/focus/accessibility scenarios; reference GPU measurements | Multiline/rich-text track and stronger application-readiness claims |
| Application track | F16 and completed window/input/platform contracts | Workspace versioned restore; close/save cancellation; docking/document lifecycle tests | MainWindow/docking/document workspace maturity |

Additional fixture details:

- Flood replaceable progress with 100,000 publications and show bounded queue
  memory plus continued input service. Separately verify order/delivery for
  accepted nonreplaceable commands under backpressure.
- In a 10,000-widget fixture, an isolated non-inherited paint change causes no
  measurement/layout work. A caption edit follows its layout dependency closure;
  unrelated style/measure caches remain reusable.
- Publish 100,000 model entries as a batch and verify one coherent revision,
  viewport-only access, and bounded preparation. Exact fitting must be explicit.
- Compare retained/full actual pixels after dirty-region, transform, text,
  resource-eviction, selection, and lifetime changes at multiple scales.
- Keep ASan/UBSan and add a separate ThreadSanitizer job for the CPU dispatcher,
  tasks, and queued connection layer. Do not assume a software-GL/sanitizer host
  validates native driver behavior or every third-party library.
- Record native keyboard-layout/IME, mixed-monitor fractional DPI,
  resize/minimize/restore, clipboard/drag/drop, multiple-window focus, and screen
  reader checks in a platform capability matrix. Mark untested platforms clearly.

## Original sequencing recommendation

Start with **Gate A**: owner-thread contract/assertions; Application loop; bounded
dispatcher and typed handles; then task continuations, receiver-bound delivery,
shutdown, and concurrency tests. Specify F05/F07 transactions/phase boundaries
alongside this work so posting integrates cleanly with the later state engine.

Use the existing demo as an acceptance application: load a large model or create
geometry in a worker, bind progress, replace an earlier request, close the target,
and keep a second window interactive. This validates developer ergonomics and
the actual failure paths before they spread into new widgets.

Then complete Gate B, followed by the first widget batch. Develop Gate C with the
text/application track. Set the text, accessibility, platform, and resource
interfaces early, and implement their representative vertical slices before many
controls depend on them. Full rich-document/publishing features remain on their
own roadmap; an optional constraint solver and render thread follow measured need.

## Evidence map and external references

| Observation | Source |
| --- | --- |
| Direct signal execution and unsynchronized activation | [Signal.h](include/EStudioUI/Signal.h): Signal::Emit/Connect, Connection::Disconnect |
| Weak raw widget observation | [Widget.cpp](src/Widget.cpp): WidgetRef::Get; [Widget.h](include/EStudioUI/Widget.h) |
| Ordinary dirty/repaint flags and root preparation | [UiContext.cpp](src/UiContext.cpp): Invalidate, RequestRepaint, Prepare, Arrange |
| Window-local scheduling and wakeup | [Window.cpp](src/Window.cpp): Run, RequestRepaint, native callbacks |
| Context ownership/fences | [OpenGLWidget.cpp](src/OpenGLWidget.cpp) |
| Typed CSS metadata and explicit size contracts | [StyleProperty.h](include/EStudioUI/StyleProperty.h), [SizeRule.h](include/EStudioUI/SizeRule.h), [Layout.cpp](src/Layout.cpp), [LAYOUTS.md](LAYOUTS.md) |
| Owned collection data/full Content fitting | [ListWidget.h](include/EStudioUI/ListWidget.h), [TreeWidget.cpp](src/TreeWidget.cpp), [TableWidget.cpp](src/TableWidget.cpp): GetColumnWidths |
| Scene publication/caches | [CanvasScene.h](include/EStudioUI/CanvasScene.h), [CanvasWidget.cpp](src/CanvasWidget.cpp), [CANVAS.md](CANVAS.md) |
| Single-line/history/LTR/single-font | [TextBuffer.cpp](src/TextBuffer.cpp), [FontFace.cpp](src/FontFace.cpp): Shape |
| Single popup and focus traversal | [UiContext.cpp](src/UiContext.cpp): ShowPopup, FocusNext |
| Glyph accumulation and renderer work | [Renderer.cpp](src/Renderer.cpp): Glyph, BuildDamage, BuildBatches, EndFrame |
| Packaging/verification scope | [CMakeLists.txt](CMakeLists.txt), [CI workflow](.github/workflows/build.yml), [0.8 records](verification/WIDGET_STYLING_0_8.md) |

- [GLFW thread-safety and reentrancy guarantees](https://www.glfw.org/docs/latest/intro_guide.html#thread_safety).
- [Qt object threading and queued connections](https://doc.qt.io/qt-6/threads-qobject.html).
- [Qt threading technologies](https://doc.qt.io/qt-6/threads-technologies.html).
- [Baseline CI result](https://github.com/jamedev/EStudioUI/actions/runs/39721394003).
