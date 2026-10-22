# Constraint layouts and live resizing / 0.13 verification

Recorded 2026-10-22. Public contracts and compiled application:
[CONSTRAINT_LAYOUT.md](../CONSTRAINT_LAYOUT.md),
[examples/ConstraintLayout.cpp](../examples/ConstraintLayout.cpp).

## Local checks

| Check | Result |
| --- | --- |
| Linux GCC 13 Release | 30/30 CTest suites pass, including actual GPU integration |
| AddressSanitizer + UndefinedBehaviorSanitizer | 30/30 pass; final demo navigation/framebuffer regression also checked |
| Constraint-specific checks | 278 checks: anchors, 120 intermediate sizes, priorities, conflicts, exact recovery, lifetime/visibility, wrapping, ratios, scaling and stress cases |
| Compiled example | `EStudioUIConstraintExample` builds through the normal project |
| Actual renderer | GLFW null platform + OSMesa, llvmpipe LLVM 17 / Mesa 24.0.5 |

LeakSanitizer is disabled on this restricted host (`detect_leaks=0`); these runs
do not establish leak freedom or instrument third-party Mesa libraries. The
separate Windows CTest below does not require OpenGL. CI runs the full Linux
suite plus the existing five isolated CPU TSan checks and MSVC Release/Debug
through the one-shot PowerShell script. Inspect the Actions checks on the
published revision for platform results; local software GL is not Windows
driver validation.

Raw evidence: [Release](constraint-release-tests.txt),
[sanitizers](constraint-sanitizer-tests.txt),
[work counters](constraint-work-counters.txt),
[framebuffer and callback report](constraint-layout.txt).

## CPU resize work

Two synthetic fixtures arrange 100 widgets connected by 300 user rules. After
initial graph construction, each prepares 64 different viewport widths. The
height-dependent fixture uses synthetic measurement hooks, not a claim about
full paragraph shaping throughput. Timings include CPU measurement, solving and
arrangement, excluding GPU drawing/readback, graph construction and I/O.

| 100-widget fixture | Median | p95 | Maximum | Tableau rebuilds during 64 resizes |
| --- | ---: | ---: | ---: | ---: |
| Fixed height | 0.375 ms | 0.528 ms | 0.726 ms | 0 |
| Height for width | 0.920 ms | 1.040 ms | 2.984 ms | 0 |

The earlier row-replacement implementation measured about 33 ms median for the
fixed-height fixture on this host. Verified edit-variable updates remove that
cost from this feasible resize path. A separate 120-size fixture records one
initial tableau build, 119 incremental updates, and 120 identical-row cache
hits. Style and intrinsic-measurement recalculation deltas remain zero.

These observations are host-specific, with other compilation work running.
They are not frame-rate guarantees. Cold construction, dense/coupled systems,
infeasibility recovery, application hooks, font work and GPU throughput have
different costs. The 10,000-rule validation ceiling is not a measured scale claim.

## Real framebuffer checks

The Constraints page renders CSS-styled panels, working numeric inputs/editor,
wrapped text, a bottom-anchored footer and a 2:1 badge. At 880/1180/1520 logical
pixels, sidebar width remains 220, document width becomes 592/892/1232, and
horizontal spacing remains 12. Native programmatic resizes are followed by real
renderer captures. PNGs are lossless transcodes of its PPM output:

- [880 px](constraints-880.png)
- [1180 px](constraints-1180.png)
- [1520 px](constraints-1520.png)

Retained and forced-full redraws match byte for byte. A paint-only badge color
change has zero layout/arrangement work. Settled frames submit zero GPU draws.
The visible **Layout lab → Constraint layouts** command is exercised by pointer
input, so the page is reachable when its tab lies beyond a narrow strip.

The production GLFW callback probe changes six intermediate sizes without
returning to `Application::ProcessEvents` or calling `RenderFrame` between them.
Each native callback cycle presents updated geometry and actual pixels in the
newly exposed area. Bursts of refresh callbacks respect the shared 60 Hz cap;
style/intrinsic recalculation deltas are 0/0. Batched state changes and a resize
issued during Paint defer traversal until safe. Native service also delivers
queued UI work without polling another event loop.

## Windows modal-loop regression

`NativeResize` builds `src/NativeResizePump.cpp` against real Win32/comctl32 APIs,
without GLFW or GPU dependencies. It creates an HWND and sends
`WM_SYSCOMMAND / SC_SIZE` to enter DefWindowProc's actual system sizing loop.
The production subclass/timer must tick at least eight times and apply at least
six intermediate sizes **before SendMessage returns**. A watchdog cancels a
regression after three seconds. After exit, message pumping must produce no
remaining timer ticks. Both MSVC configurations run this CTest.

This tests scheduling inside the native modal loop. Combined with the separate
actual renderer callback tests, it covers the two mechanisms that previously
waited for release. It does not simulate a human dragging a physical border,
exercise Windows OpenGL hardware, establish compositor/vsync cadence, or test
cross-monitor DPI transitions. Those remain manual platform checks. The normal
`Build.ps1` runs GPU checks on developer machines; hosted Windows CI explicitly
uses `-SkipGpuTest` because OpenGL 3.3 is not guaranteed.
