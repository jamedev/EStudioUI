# EStudioUI development

- This repository is the source of truth. Use EStudioUI for project names,
  CMake targets, include directories, and the public C++ namespace.
- Consult FOUNDATIONS_ROADMAP.md before expanding the widget catalog in
  WIDGET_ROADMAP.md. Preserve their gate dependencies and distinguish proposed
  APIs from implemented behavior; update completion status with verification evidence.
- Treat DEVELOPER_EXPERIENCE.md as a public API acceptance gate: keep the common
  path small, defaults safe, advanced behavior explicit, and diagnostics useful.
  Compile documented examples and review actual call sites before accepting new APIs.
- Use State/StateStore and Bindings for shared application values. Preserve
  final-candidate validation, coherent commit/rollback, owned snapshots, bounded
  reentrant notifications, and receiver-bound teardown. State reads are valid
  during painting; mutations during traversal and nested UI frame/event-loop/queue processing are diagnosed.
- Use ItemModel/ModelSnapshot and canonical SelectionModel for shared collections.
  Preserve stable IDs, final-candidate validation, typed values, immutable worker
  ownership, prevalidated adapter chains, and coherent deletion/selection updates.
  Keep viewport access bounded; document complete scans and selected-set copying.
- Use Action/ActionState for shared commands and typed action handles for worker
  requests. Preserve transaction validation, scoped shortcut precedence, ambiguity
  handling, and domain/receiver expiry. Keep platform key normalization out of widgets.
- Route owning InputEvent values through the shared input boundary. Preserve
  physical/logical/scancode/pointer metadata, captured pointer/button identity,
  transformed hit geometry, and deletion-safe preview paths. Explicit Prepare is
  geometry-only; input must not pump native events, drain queues, or advance frames.
- Reuse the popup/modal stack and PopupSession for asynchronous results. Preserve
  logical CSS ownership, parent lifetimes, top-layer dismissal, focus restoration,
  bounded depth, and callback-safe settlement. Native reentrant input is queued;
  deactivation invalidates stale focus-session input and prevents popup reopening.
- Declare built-in changes with the narrowest correct Invalidation impacts.
  Keep selector/inheritance dependencies correct and preserve unrelated intrinsic
  measurements. Verify work counters and actual pixels when changing cache behavior.
- Use C++20. Keep each public class in a .h/.cpp pair; templates may be defined
  in headers. Headers use brief Doxygen //! comments for every method/member.
  Put detailed explanations in .cpp files only when the behavior needs them.
- Use English identifiers/comments, CamelCase methods, camelCase arguments and
  locals, m_CamelCase members, and s_CamelCase static members. Use Allman braces,
  blank lines between methods, and readable logical blocks.
- Keep the UI model/input code independent of GLFW/GL headers. Own widget children
  explicitly and preserve safe callback/focus/capture behavior on deletion.
- Add runtime dependencies through the pinned vcpkg manifest. Preserve the
  permissive-license constraint and keep optional FreeType/HarfBuzz features off
  unless a deliberate reviewed change requires them. Do not add Qt.
- On Windows, Build.ps1 installs x64-windows-estudioui dependencies, builds, runs
  core and GPU checks, and optionally opens the demo. -SkipGpuTest is explicit
  for hosts without OpenGL 3.3. Keep the script compatible with Windows PowerShell
  5.1 and PowerShell 7; do not modify a shared vcpkg checkout's branch or installed tree.
- For engine changes, run the core tests and appropriate real framebuffer checks.
  For build-script changes, check PowerShell parsing, normal Debug/Release flows,
  and fail-fast behavior. Report native/CI verification separately from mocks.
- Update CSS.md / ARCHITECTURE.md when the supported behavior changes. Captures
  and measurements must come from the actual C++ renderer. Do not substitute
  hand-made screenshots or infer hardware speed from a software GL run.

- Preserve explicit Fixed/Content/Fill contracts across theme reloads. Reuse the
  bounded allocator for widgets/columns, redistribute capped shares, and report
  overflow without violating fixed sizes or hard minima. Keep hidden pages out
  of measurement/input.
- Collections store model data and paint viewport rows; do not create a child
  widget for every row or cell. Update the widget/layout tests for behavioral changes.
- OpenGLWidget scene resources belong to their separate shared context. Preserve
  producer AND consumer synchronization around shared textures, resize, and cleanup.
  Restore the containing context after callback errors. Keep scene lifecycle
  mutations outside scene-render callbacks; do not claim worker-thread rendering.
- Property animations run on the GUI clock before layout and scene preparation.
  Preserve weak targets, per-property replacement, callback-safe dispatch, and
  exact endpoints. Keep CSS parsing out of animation ticks. Extend StyleProperty
  metadata when adding computed properties, and classify paint-only changes carefully.
- Verify animation changes with deterministic timeline/input tests and actual
  framebuffer samples. Explicit layout contracts must retain precedence over CSS
  animation overrides; absolute geometry remains an intentional out-of-flow API.

- Rich CSS values compile once; keep parsing and selector evaluation out of paint-only
  ticks. Keep automatic CSS presentation below explicit C++ overrides and code size
  contracts. Decorative layers are paint records, not widgets. Preserve transformed
  hit testing AND world-space capture clipping. Extend the RichCss and framebuffer
  probes when changing gradients, shadow ordering, clipping, or timeline semantics.

- Use the shared LayoutStrategy/MeasureResult boundary for container strategies.
  Resolve width before dependent height; preserve explicit size precedence and
  independent percentage-reference semantics. Do not add iterative content/percent
  feedback. Keep three-entry constraint caches and unrelated intrinsic reuse.
- Preserve actual painted baselines and composition baseline-child identity/lifetime
  rules. Validate finite extension plans, unique child slots, hard limits and track
  reports. Keep measurement/strategy hooks pure and restore phases on exceptions.
- Record real narrow/wide and retained/full framebuffer evidence for layout changes.
  Expose child/track allocation causes through owned reports; keep the optional
  linear constraint solver separate from the delivered F08 foundation.

- Constraint anchors use lifetime-unique child identities, not ownership indices or
  retained widget pointers. Preserve hard size precedence, named inactive/relaxed/
  conflicting rule reports, deterministic required-prefix recovery, width-first
  measurement, bounded incremental tableaux, and finite numeric validation.
- Native resize/damage frames share the ordinary scheduler and presentation cap.
  Keep Win32 modal-loop timer/subclass lifetime safe, avoid nested event polling or
  queue/frame traversal, and preserve CSS/intrinsic caches on viewport changes.
  Verify actual intermediate framebuffer pixels and the separate Windows SC_SIZE
  modal-loop probe; neither software GL nor CPU probes establish hardware 60 FPS.

- Use PlatformServices and owned clipboard/picker/drop values for native integration.
  Preserve explicit Native/Emulated/Unsupported capabilities, reserved completion
  delivery under overload, owner-side sessions, and nonblocking native retirement.
  Keep a native picker owner alive until its STA has unwound; no nested framework loop.
- Accessibility uses lazy SemanticTree identities, logical popup ancestry, canonical
  control actions and transformed/DPI bounds. Keep custom metadata hooks pure,
  direct access owner-only, native provider requests dispatcher-bound, and query/event
  work bounded. No per-frame full-tree rebuild or per-collection-row widget expansion.
  Run actual Windows probes separately from injected services; record interactive
  input-method, real AT and mixed-monitor observations honestly. Virtual identities
  include widget lifetime, record, sub-item and dataset coordinates. Canonical
  paint-only data/selection/scene changes must publish a narrow semantic revision;
  ordinary hover and repaint requests must not become semantic scans.
- TextInputClient preedit stays separate from canonical text, validation and undo.
  Commit uses one validated edit; focus/owner changes expire native sessions. Cancel
  hooks may delete their editor or redirect focus, so retain owner lifetime, recheck
  weak targets and guard cancellation reentrancy. Keep unchanged-turn native
  snapshots and preedit shaping cached; verify real framebuffer pixels.
