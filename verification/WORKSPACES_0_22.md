# Application workspaces 0.22 verification

This increment implements W29 file/path controls, W20/W30 application shell and
status, and an initial W21 docking topology. [WORKSPACES.md](../WORKSPACES.md)
records the exact API, ownership, bounds and platform limits. No new runtime
dependency is added. Final implementation source: `d2aa847ff453c2de3768cde3b6c7031afee79587`.
[Build and test run 39980739125](https://github.com/jamedev/EStudioUI/actions/runs/39980739125)
passed for that exact source, including native Windows and Linux checks.

## Behavioral checks

WorkspaceCore runs **64 checks** on actual UI/context/state implementations:
exact UTF-8 paths and budgets, two-way canonical bindings and rollback, recent
path bounds, transformed/repositioned recent-menu anchors, validator-disabled
choices, asynchronous/coalesced browsing, stale result suppression, logical
owner cancellation, callback deletion, owned reentrant snapshots, source/destination
menu and nested-popup migration, shared bindings across contexts, permanent expiry
of old dispatcher handles, fresh handle delivery, root/cycle/thread rejection,
capture-callback deletion and nested transfer guards, toolbar clipping/focus,
stable tab reordering, hidden-selector semantics and persistent status children.

Existing input/actions, text, semantic, State/Bindings, layout/constraints,
collections, popup and GPU suites remain part of the complete run. Existing
toolbar APIs route custom commands into Content; integration probes retain
command identities instead of depending on internal child indices. The new
ApplicationWorkspace example compiles the ordinary public API.

## Actual GPU coverage

WorkspaceGallery::Verify currently runs **47 checks** using production Window,
UiContext and Renderer, with complete rich CSS loaded independently of prior
gallery fixtures. It exercises physical grip dragging/drop previews, tabifying,
renaming, hiding/reopening, native owned floating windows, State edits across
transfers, incompatible GPU-group rejection, stylesheet propagation without
unchanged-frame recopying, malformed/duplicate/version/payload rejection,
workspace restoration, retained/full pixel equality and zero settled repaint.

A real OpenGLWidget surface is initialized in its separate shared context,
floated and redocked. Context identity and green framebuffer samples must survive
both transfers. Floating-close settlement returns actual content to hidden
storage and keeps bindings; main closure closes owned floating surfaces.
Windows uses the production WM_CLOSE callback for the floating-close check.
Narrow/wide captures verify caption visibility and prepared close hit geometry.

The checks exposed and fixed a context-retirement defect: releasing a floating
Window implementation after a frame left its context current/destroyed instead
of restoring the containing context. A nonempty framebuffer and retained/full
comparison now guard that path. A further regression checks manager lookup from a floating
window retirement callback; panel metadata leaves the manager before those
callbacks run.

Generated captures are actual C++ renderer output. No hardware speed/FPS claim
is inferred from OSMesa or native software WGL.

## Native Windows checks

NativeChrome is an independent real Win32-window test, without requiring GL.
It verifies extended/simple NCCALCSIZE requests, interactive client versus caption
regions, minimize/maximize/close roles, the HTMAXBUTTON Snap Layout protocol,
corner resize regions, native window styles/system menu, native maximize/restore,
caption hover delivery, restored standard chrome and scheduler ticks during a
real SC_SIZE modal loop. The existing NativeResize and NativePlatform lifecycle/
STA/UI Automation probes continue to run separately. Native GPU checks additionally
verify that a focused floating inspector keeps its unfocused owner presenting
updates, and that the whole group pauses when all members lose focus.

These are protocol and lifecycle checks. They do not establish the Windows 11
Snap Layout flyout experience, real snap gestures, mixed-monitor dragging or
screen-reader usability.

## Validation status

- Final GNU 13 Release build: **42/42 passed**, 13.67 seconds, including actual
  Linux OSMesa GPU integration and rendering-profile smoke; zero C++ compiler
  warnings. The pre-existing libunibreak CMake developer-scope warnings remain.
- Final local AddressSanitizer/UndefinedBehaviorSanitizer Debug (`-O1 -g1`):
  **42/42 passed**, 52.67 seconds, including software-GL integration and the
  transformed recent-path checks. Leak detection is disabled for the host tracing
  environment; address and undefined-behavior instrumentation remain enabled.
- Final Linux CI: **42/42 passed**, including OSMesa framebuffer integration
  and the rendering-profile smoke; isolated ThreadSanitizer **6/6 passed**.
  Job `120507726319` in the linked final-source workflow.
- Final Windows/MSVC Release: **44/44 passed**. Debug: **44/44 passed**.
  Both include the actual NativeChrome, NativeResize, NativeTextInput and
  NativePlatform probes. Job `120507726227`; zero C++ compiler warnings/errors.
- Native Win32/WGL GPU integration: **PASS**, using pinned Mesa 26.2.4 llvmpipe
  (LLVM 23.1.2). This includes shared focus-group updates/pause and the production
  floating WM_CLOSE path. Windows PowerShell 5.1 rendering-profile smoke: **PASS**
  across all seven full/retained scenarios.
- Five additional native Debug lifecycle repetitions: **99 checks passed each**.
- Interactive Windows 11 flyouts/OS snap, physical mixed-DPI monitor transitions,
  real assistive technology and hardware performance: **not claimed**; remain
  human/platform acceptance work.

The local sanitizer host initially produced zero-byte cached object artifacts.
Those objects were recompiled with CMake's recorded compiler commands, and all
50 executables were relinked with Ninja's generated link commands before the
final successful run. This was a local artifact recovery; no product-source
workaround or skipped test was introduced.

## Actual renderer captures

- [Wide workspace](workspace-wide.png)
- [Narrow workspace](workspace-narrow.png)
- [Captured grip drag/drop preview](workspace-drag-preview.png)

These PNGs are direct conversions of the production renderer's PPM output.
They capture the client UI, not desktop-compositor or Windows 11 flyout pixels.

Initial docking uses four resizable edge tab groups. Arbitrary recursive dock
trees, document workspaces, main-window geometry persistence, F10b and F14 remain
separate work.
