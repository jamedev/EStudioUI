# EStudioUI foundation audit and hardening roadmap

Audit date: 2026-10-22. Audited baseline: 0.8.0, commit `dc14170`.
The original audit below describes the 0.8.0 baseline. The implementation status
section records subsequent work; historical gap descriptions are not statements
about the current source.

The goal is a complete C++ application framework, with a GPU-drawn UI and shared
application services that can eventually also serve applications without a UI.
[WIDGET_ROADMAP.md](WIDGET_ROADMAP.md) describes the immediate control and workspace
coverage. Settle common execution, lifetime, state, input, layout, text, and
resource contracts before many controls depend on different local solutions.
Retain the working GPU renderer and existing tests.

## Agreed scope and sequencing: 2026-10-22

Prioritize the foundations needed for a useful, broad widget catalog and complete
application workspaces. Then use those real consumers to identify and extract
reusable modules, including services for applications without windows or a GPU.
F14 module extraction and SDK packaging are deferred and no longer block widget
development. This changes the plan, not the delivered 0.13 API or build targets.

F09 input/actions/focus is now delivered. Continue F11 platform/accessibility
services and F10a essential control text/input. Complete the relevant Gate B integration
evidence, then expand common controls, dialogs, data views, and workspaces. F10b
document editing is a later dedicated track; it does not block those widgets.
F12 resource limits, F13 diagnostics, and F15 evidence accompany each feature
that needs them rather than becoming an unrelated final cleanup phase.

Preserve clear responsibilities and injectable boundaries now. Keep generic
algorithms/data independent of widget, GLFW, and GL types where practical, but
defer new package names, target splits, and compatibility commitments until the
module extraction stage. Candidates include tasks/signals, state/models,
command history, geometry, and persistence; these are not promised modules or
current standalone SDKs. Public API usability and supported extension points
remain acceptance criteria throughout.

## Implementation status: 0.20, image resources on verified foundations

The 0.14.1 maintenance increment fixes retained native window closure, stable
GPU capture after presentation, and MSVC conversion warnings before the initial F11 increment.
Its regression evidence is in
[verification/WINDOW_CAPTURE_0_14_1.md](verification/WINDOW_CAPTURE_0_14_1.md).

**Gate A is implemented.** F01-F04 now have public APIs, lifetime/phase/error
contracts, concurrency tests, and actual multiwindow/framebuffer probes.
[THREADING.md](THREADING.md) documents the shipped signatures and policies;
[verification/FOUNDATIONS_0_9.md](verification/FOUNDATIONS_0_9.md) records the
measured evidence and platform limits. The original illustrative `Task::ThenOnUi`
proposal became receiver-bound `BackgroundTasks::Run` / `RunLatest`.

**The first F05/F07 slice is implemented and now has the later Gate B integration evidence.** Typed state,
coherent transactions, built-in/custom bindings, and targeted invalidation now
have public APIs. [STATE.md](STATE.md) documents their delivered contracts and
examples. [verification/STATE_0_10.md](verification/STATE_0_10.md) records the
Release, sanitizer, work-counter, and actual framebuffer evidence for this slice
and its remaining platform limits.

| Topics | Current status |
| --- | --- |
| F01-F04 | Implemented: shared application loop, bounded dispatch, typed handles, receiver-bound signals, cooperative tasks, progress, stale-request suppression |
| F05 | First slice implemented: typed owner-side state, final-candidate validation, coherent revisions, nested savepoints, bounded reentrant notification, lifetime-bound control/custom bindings; verified by CPU and actual framebuffer probes |
| F07 | First slice implemented: explicit style/measure/arrange/hit-test/paint impacts, intrinsic/aggregate measurement caches, dependency-aware arrangement, and work counters; verified by the 10,000-widget work fixture and actual framebuffer checks |
| F13 | Gate A baseline implemented: owner checks, traversal phase guards, deferred mutation, receipt/task failures, queue counters; visual inspector remains planned |
| F15 | Gate A concurrency/lifetime/framebuffer coverage implemented; native hardware and broader platform coverage remain open |
| F06 | Core implemented: typed external snapshot models, stable IDs, batched edits/structural changes, shared selection, prevalidated sort/filter chains, delegates, worker publication, bounded content sizing, and expanded-branch row maps; integration evidence recorded with 0.11 |
| F08 | Implemented in 0.12: width-first constrained measurement, baselines and composition sources, shared box/grid/form/flow strategies, bounded caches, percentage-cycle fallback, owned child/track diagnostics; verification recorded below |
| F09 | Implemented in 0.14: owning input payloads, rich native metadata, transactional shared actions and worker handles, scoped/ambiguity-safe shortcuts, local focus scopes, nested popup/modal sessions, menu nesting and phase/lifetime guards |
| F11 | 0.16 implementation extends 0.15 native services with shared composition, Windows IMM, GPU preedit, stable virtual list/tree/table/canvas semantics and native collection/text/event patterns; automated Windows Release/Debug/native repetitions pass; interactive IME/AT/mixed-DPI acceptance remains open |
| F10a | Implemented in 0.17: actual font identity/selection/fallback, script and bidi shaping, visual caret affinity and disjoint selection, Unicode wrapping, shared composition and bounded CPU/GPU text caches; see TEXT.md and verification/TEXT_0_17.md |
| F12 image slice | 0.20: immutable worker-friendly images, bounded decoding, DPI icon sources and byte/count-bounded per-context texture residency with safe retirement; broader resource services remain feature-driven |
| F10b, F12, F16 | Planned: later document editing, feature-driven resource hardening, and reusable history/workspace services |
| F14 | Deferred until broad widget/workspace use informs reusable module extraction and SDK packaging; not a Gate B prerequisite |

Ease of use is a standing acceptance criterion, not a separate polish phase.
[DEVELOPER_EXPERIENCE.md](DEVELOPER_EXPERIENCE.md) defines the review rules:
concise common paths, safe lifetime defaults, clear errors, and explicit advanced
control. No runtime dependency was added for Gate A or the first F05/F07 slice.

**The F06 core is implemented in 0.11.** [MODELS.md](MODELS.md) documents the
actual model/view, selection, delegate, transaction, and provider contracts.
[verification/MODELS_0_11.md](verification/MODELS_0_11.md) records verification
and scope limits, including future asynchronous provider paging/cache services.
No runtime dependency was added.

**F08 is implemented in 0.12.** [LAYOUT.md](LAYOUT.md) documents the public sizing,
strategy, baseline, cycle, cache and diagnostic contracts.
[verification/LAYOUT_0_12.md](verification/LAYOUT_0_12.md) records actual resize,
framebuffer, work-counter and sanitizer evidence and platform limits.

**The optional linear constraint-layout increment is implemented in 0.13.**
[CONSTRAINT_LAYOUT.md](CONSTRAINT_LAYOUT.md) documents stable anchors, linear
relationships, weighted priorities, deterministic infeasibility recovery,
width-first measurement and bounded incremental caches. Native refresh/resize
callbacks and a Win32 system-modal-loop timer present safe paced frames while
resizing. [verification/CONSTRAINTS_0_13.md](verification/CONSTRAINTS_0_13.md)
records CPU, framebuffer, sanitizer and platform evidence with explicit limits.
This adds private header-only Kiwi under permissive licenses; Box/Grid/Form/Flow
remain independent of the strategy. Continue F11 platform/accessibility services
and F10a essential control text/input.
F10a and automated Gate B integration are verified. The user-authorized first
common-control batch is delivered in 0.18; human platform acceptance remains
separate F15 work.

**F09 is implemented in 0.14.** [INPUT.md](INPUT.md) documents the delivered APIs
and migration contracts; [examples/InputActions.cpp](examples/InputActions.cpp)
is compiled by the ordinary demo build. Shared commands reuse State/StateStore,
including validated coherent updates, bounded observer feedback, and owner-side
worker delivery through typed action handles. Buttons, toolbars, menus, and split
tool choosers share command presentation and execution. No runtime dependency
was added. [verification/INPUT_0_14.md](verification/INPUT_0_14.md) records CPU,
concurrency, framebuffer, and native-adapter evidence and remaining platform limits.
Full font fallback/IME, semantic accessibility/native services, document editing,
and deferred module packaging retain their F10a/F11/F10b/F14 scopes.

**The initial F11 increment is implemented in 0.15.** [PLATFORM.md](PLATFORM.md)
documents the service boundary, native capability matrix, asynchronous picker
lifetime/overload behavior, lazy semantic tree and initial Windows UI Automation
patterns. [examples/PlatformServices.cpp](examples/PlatformServices.cpp) is compiled
with the ordinary demo. No new third-party runtime dependency was added.
[verification/PLATFORM_0_15.md](verification/PLATFORM_0_15.md) records the evidence.
**The 0.16 implementation adds the remaining composition and virtual semantic
pipeline.** TextBox and custom TextInputClient editors share owned preedit/commit/
cancel events, surrounding text, selection and transformed caret geometry. Windows
IMM consumes native composition, while TextBox paints cached preedit separately
from canonical text and undo. Virtual list/tree/table/canvas nodes reuse stable
model/scene identities, direct paging and canonical selection/reveal/edit actions.
Windows UI Automation adds collection and single-line Text/Text2 patterns with
bounded change observation. No new third-party runtime dependency was added.
[0.16 verification](verification/PLATFORM_0_16.md) records tested results and
[interactive acceptance](verification/PLATFORM_ACCEPTANCE.md) records the remaining
input-method/assistive-technology/mixed-monitor checks. Windows Release and Debug
each pass 37/37 suites, followed by three 80-check native Debug repetitions.
Implementation and automated acceptance are complete. Interactive acceptance
remains open, so protocol tests alone do not establish application readiness. Gate B remains
verified for the F10a/common-control automated integration; human native
acceptance remains explicitly tracked.

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
| F10a | Essential text/font/input for controls | Font identity/fallback, short shaped runs, selection/caret mapping, composition client, wrapping display text | B |
| F10b | Document text and editing | Multiline storage, paragraph layout/invalidation, structured editing and rich documents | C/document track |
| F11 | Platform and accessibility services | Semantic nodes, injectable native adapters/capabilities, representative Windows integrations and real platform probes | B/C |
| F12 | Resource budgets and render lifecycle | Feature-required font/image/GPU limits and safe retirement first; extended budgets, diagnostics and recovery policy follow | B/C |
| F13 | Errors, phases, and developer diagnostics | Deterministic mutation/error rules, inspectable invalidation/queue/layout work | A/B |
| F14 | Reusable module extraction and SDK integration | Evidence-led module boundaries, headless consumers, relocatable packages and independent consumer tests | Deferred: after widget/workspace coverage |
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

The F09 portion is delivered in 0.14; the following pipeline requirements remain
the design contract. Actual signatures, costs, native-adapter limits, and examples
are in [INPUT.md](INPUT.md). The initial F11 platform/semantic increment is documented
in [PLATFORM.md](PLATFORM.md); it does not close the full F11 gate.

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

F11 delivery requires working representative services on the initial Windows
target as well as the contracts: composition connected to F10a, clipboard,
file/folder dialogs, drag/drop, scaling/owned-window behavior, and a native
accessibility bridge for existing control semantics. Record actual capabilities
and native evidence per adapter. Test doubles alone do not finish F11. Extend
semantic roles and adapter checks as each new widget family arrives; additional
platforms and broader hardware coverage remain explicit F15 work.

## F10a: essential text and input before common controls

Implemented in 0.17 with the existing 0.16 composition protocol. TextBuffer
continues to sanitize line breaks/tabs to spaces and retain full-string history
snapshots. FontFace resolves actual registered fonts and shapes script/direction
runs with visual caret mapping. See [TEXT.md](TEXT.md) and
[verification](verification/TEXT_0_17.md) for evidence, limits and outstanding
human native acceptance.

F10a supplies the common services required by labels, buttons, menus, text and
numeric fields, editable combos, and collection editors:

- Stable font identity and resolution, CSS family/size/style selection, fallback,
  DPI-aware metrics, and renderer glyph keys that include the actual font instance.
- Shaped control text with script/direction segmentation, grapheme-safe selection,
  hit testing, and logical-to-visual caret mapping. Define supported scripts and
  directions explicitly and prove representative mixed-direction control text.
- A shared TextInputClient/platform composition protocol: preedit, commit/cancel,
  surrounding text/selection, screen-space caret geometry, and focus/popup teardown.
- Consistent single-line editing, clipboard and validation behavior, plus reusable
  wrapping/measurement for display labels and help text. Preserve the existing
  TextBuffer contract rather than silently turning it into a document store.

Acceptance includes fallback and mixed-script captions, selection/navigation in
single-line editors, native IME candidate placement after DPI/scroll/transform
changes, AltGr/shortcut interaction with F09, and cancellation on focus/owner loss.
Verify real pixels and unaffected layout/paint caches. Multi-font cache identity
and the necessary F12 budget/retirement policy accompany this slice; document
features are not required to exercise it.

## F10b: later multiline and structured document editing

Build a separate document layer using F10a services. Do not grow a document editor
by removing the newline restriction alone. Changing one HarfBuzz direction flag
does not implement bidirectional documents.

Define separate multiline/document storage, positions, edit commands, byte-bounded
history, cross-line selection, search/replace, paragraph layout and incremental
invalidation. Add document-level direction/line breaking and scroll/caret reveal.
Share layout between editing and viewing, and reuse the F10a composition client.

Then add structured styles, lists, links, inline content, tables, and formatting
tools according to the document roadmap. PlainTextEditor and RichTextEditor/View
(W04/W05), code editing, and publishing depend on this later track. Basic wrapping
labels and ordinary form controls do not require its storage or editing engine.

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

F14 is deferred until broad widget/workspace use supplies evidence for reusable
module boundaries. Extract appropriate services from the framework, with headless
consumers that need no widget/native/GL initialization or graphics dependencies.
Keep UI owner-delivery and native/render adapters attached through explicit
interfaces. Tasks/signals, state/models, command history, geometry, and persistence
are candidates to evaluate, not a prescribed target graph.

At that stage ship relocatable CMake install/export/package files, public-header
checks, independent installed-SDK UI and headless consumer tests, asset/notices
rules, and a clear source/API compatibility policy. Until then retain readable
public APIs, extension contracts, and dependency direction on every milestone;
deferring packaging does not defer those everyday design requirements.

Generalize reusable command grouping/history and versioned workspace state at
the application track. Reuse lessons from SketchDocument; do not couple all
widgets to the sketcher's history. Restore layouts, dock states and settings by
stable IDs, with migration and malformed-state handling.

## Delivery gates and evidence

These are acceptance fixtures; the status/evidence above records Gate A results
and the verified first F05/F07 implementation slice. Shared widget contracts now
have automated Gate B integration evidence; human native acceptance continues.
Record observed work and named hardware; avoid arbitrary FPS claims.

| Gate | Work to close | Acceptance evidence | Widget work unlocked |
| --- | --- | --- | --- |
| A: execution and lifetime | F01-F04, phase/error baseline from F13, relevant F15 | Two changing windows under one loop; worker posting during sleep/render/resize/close; dead/reused receivers and stale revisions rejected; bounded overload; exactly-once settlement; shutdown has no UI-worker wait cycle | Prerequisite for adopting background/UI APIs; no broad widget expansion yet |
| B: shared widget contracts | F05-F09; F10a; F11 contracts and representative native services; relevant F12/F13/F15 | Shared models/selection and coherent transactions; unrelated caches retained; constrained wrapping/baselines; nested popup/focus; fallback/composition/caret cases; native service/accessibility evidence for existing controls | First common-control batch, with role/style/input contracts reused; F14 is not required |
| C: document text, resources, broader native readiness | F10b; remaining F12; extended F11/F15 coverage | Incremental paragraphs and document direction/selection; bounded font/resource churn; broader Windows clipboard/DPI/focus/accessibility scenarios; reference GPU measurements | Multiline/rich-text track and stronger application-readiness claims |
| Application track | F16 and completed window/input/platform contracts | Workspace versioned restore; close/save cancellation; docking/document lifecycle tests | MainWindow/docking/document workspace maturity |
| Later module/SDK extraction | Deferred F14 after broad widget/workspace use | Reusable boundaries justified by consumers; installed UI application and headless application build independently; headless consumer has no graphics/window dependency | Reusable framework modules for UI and non-UI applications |

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

## Delivery sequence

Start with **Gate A**: owner-thread contract/assertions; Application loop; bounded
dispatcher and typed handles; then task continuations, receiver-bound delivery,
shutdown, and concurrency tests. Specify F05/F07 transactions/phase boundaries
alongside this work so posting integrates cleanly with the later state engine.

Use the existing demo as an acceptance application: load a large model or create
geometry in a worker, bind progress, replace an earlier request, close the target,
and keep a second window interactive. This validates developer ergonomics and
the actual failure paths before they spread into new widgets.

Gate A, the F05/F07 first slice, F06 core, F08, the optional constraint-layout
increment, and F09 are now delivered as recorded above. F11 composition/virtual
semantics and automated native verification are complete; retain its interactive
acceptance checklist. F10a implementation is complete; its automated integration
evidence and remaining human native acceptance are recorded separately. The
user-authorized next increment is the first common-control batch, reusing these
contracts while retaining that acceptance checklist. Then expand application
workspaces. Keep the document-editor track F10b later and develop F16 with the
workspaces that need it. Schedule F14 module extraction/package integration once
real widget/workspace consumers establish useful boundaries, including non-UI
use. A dedicated render thread remains subject to measured need.

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

## First common-control integration (0.18)

F10a source and native fixture are published at cb60e5d / a1ab5bd. Windows Release
and Debug each pass 38 tests, plus three native Debug repetitions of 83 checks
with zero compiler warnings. Linux Release/software GL, local ASan/UBSan and
isolated TSan evidence are recorded in [TEXT_0_17](verification/TEXT_0_17.md).

The first catalog batch now reuses those contracts: checkbox/radio state,
virtual/editable combos, arbitrary scrolling content and focus reveal, bounded
splitter layouts, group/frame/separator structure, progress/idle policy and
nonactivating deadline-based help. [COMMON_CONTROLS.md](COMMON_CONTROLS.md) and
[0.18 evidence](verification/WIDGETS_0_18.md) define delivered scope and bounds.
No document editor or module extraction is implied; F10b and F14 remain deferred.


## Command/dialog integration (0.19)

W24/W26/W27/W28 reuse the delivered action/input, popup/session, typography,
layout and semantic contracts. New in-window dialogs are asynchronous;
modeless surfaces preserve outside interaction without a nested event loop.
Placement observes weak anchor identities and tracks resize/transform changes
without invalidating unrelated intrinsic measurements. Accepted input results own
their bytes before callbacks can remove editors. Menu commands remain available
before their presentations are first opened. Relevant F12/F13/F15 work includes
64 popup layers, 128 top-level menu entries, bounded command menus, validated
button candidates, owner/phase checks and actual retained/full framebuffer gates.
See [COMMAND_DIALOGS.md](COMMAND_DIALOGS.md) and
[verification/COMMAND_DIALOGS_0_19.md](verification/COMMAND_DIALOGS_0_19.md).
Human native acceptance, F10b and deferred F14 retain their separate scopes.


The 0.19 integration is verified at `77ae3ac`: Windows Release/Debug 40/40 each,
five extra native Debug runs of 95 checks each, Linux/software GL 38/38 and TSan
6/6; local ASan/UBSan 38/38. No compiler warnings. Native picker cancellation now
queues native close messages outside Shell callbacks, correcting successful COM
Close acknowledgements that could leave Show running. Human platform acceptance
and hard preemption of unresponsive Shell extensions retain their separate scope.

The subsequent Windows interaction report exposed missing production ScrollArea
press coverage, ambiguous bidi pointer endpoints and clipped gallery tabs.
[Interaction follow-up](verification/INTERACTION_REGRESSIONS_0_19.md) records their
regressions and corrections. Windows CI now also executes native Win32/WGL
framebuffer integration with an isolated pinned software driver; the prior
Windows core/native CPU results did not cover that rendering path. Hardware and
human native acceptance remain separate gates.

The rendering-profile follow-up adds readback-free, paced actual-gallery and
text-control workloads with exact asynchronous frame/timing association, damage
fallback diagnostics and a one-shot native Windows runner. It also corrects tab
focus reveal, adds independent arrow/wheel strip navigation and isolates hover
help from unrelated keyboard focus. [Verification](verification/RENDERING_PROFILE_0_19.md)
records measured work and platform checks. Hardware capture is the next input for
choosing rendering optimizations; this increment does not introduce a new backend
or claim rich-document editor performance. F10b and F14 remain deferred.

## Image/icon resource integration (0.20)

W06 supplies shared immutable Image values, PNG/JPEG decoding and RGBA creation,
ImageView fitting/alignment/clipping, multi-density Icon values, command images
and custom Painter image drawing. It reuses owner/phase guards, text/caption
layout, computed CSS/animation, lazy semantics and retained GPU composition.
Per-feature F12 work bounds encoded/decoded/temporary memory, source dimensions,
icon densities and per-context texture bytes/count; current-frame sources remain
pinned during eviction. CPU image teardown performs no OpenGL work.
See [IMAGE_RESOURCES.md](IMAGE_RESOURCES.md) and
[verification](verification/IMAGES_0_20.md). Continue W15/W17 scrolling/stacked
containers, then native-service widgets and application workspaces. F10b document
editing and F14 extraction remain deferred. GPU optimization is paused at the
user's request; the Windows profiler's unachieved 60 Hz pacing remains a recorded
diagnostic limitation rather than evidence of an interactive renderer limit.
