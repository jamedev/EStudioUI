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
