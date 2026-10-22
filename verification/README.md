# Local verification / EStudioUI 0.4.0

For subsequent work, see [RICH_CSS.md](RICH_CSS.md) (0.5.0) and
[GPU_WORKLOAD.md](GPU_WORKLOAD.md) (0.5.1 retained rendering, hidden animations,
frame pacing, and actual gallery profiling). The records below describe 0.4.0.

Current widget-styling coverage and captures are in
[WIDGET_STYLING_0_8.md](WIDGET_STYLING_0_8.md); the earlier sketcher/custom-control
checks are in [SKETCHER_0_7.md](SKETCHER_0_7.md).

These records and PNGs come from the actual C++ project and renderer. Runtime
packages were GLFW 3.4#1, FreeType 2.13.3, HarfBuzz 10.2.0#3, and utf8proc 2.10.0,
without default features. Build-only vcpkg helper/Meson ports were also installed.
No new runtime dependency was added for controls, scene contexts, or animations.

Debug and Release CTest passed all four suites: Core (78 assertions),
WidgetsAndLayout (3,674 assertions, including 300 randomized allocations),
Animation (2,313 checks), and GpuIntegration (actual framebuffer/context/interaction checks). The recorded
CTest logs include command lines and program output.

`sanitizer-tests.txt` records the same four suites with AddressSanitizer and
UndefinedBehaviorSanitizer on EStudioUI and its test/demo C++ code. Dependencies
and Mesa were not instrumented. Leak detection was disabled because the host's
process tracing prevents LeakSanitizer; no leak-free claim is made.

`powershell-tests.txt` retains the 27 orchestration checks for the unchanged
Build.ps1 on PowerShell 7.6.7 with mock native tools. Those mocks cover flow,
triplets/CRT arguments, paths, bootstrap, shared-checkout preservation, and failure
propagation. The GitHub Actions workflow separately runs the real Build.ps1
under Windows PowerShell 5.1 for MSVC Debug/Release. Hosted Windows CI explicitly
uses -SkipGpuTest; the developer script runs native GPU tests by default.

## Actual framebuffer captures

| Capture | What it exercises |
| --- | --- |
| `widgets-dark.png`, `widgets-light.png` | Tabs, toolbar, hierarchy, 2,006-item list, 10,000-row editable table |
| `table-editing.png` | Reusable floating cell editor and selection |
| `context-menu.png` | Popup layer above the table |
| `opengl-scenes.png` | Two depth-tested cube/grid scenes with separate contexts |
| `context-isolation.png` | Distinct red/green scene textures with a translucent UI overlay |
| `layout-880.png`, `layout-1180.png`, `layout-1520.png` | Fixed/capped/remaining Fill sizing at three native window sizes |
| `dark.png`, `selection.png`, `light.png`, `resized.png` | Original button/editor regression gallery |
| `primitives-2x.png` | Clipping, blending, rounded corners, analytic glyphs, two-times pixel scale |
| `benchmark.png` | 640 boxes and 3,840 glyphs in one ordered draw |
| `animation-css-start.png`, `animation-css-middle.png`, `animation-css-end.png` | Stylesheet interpolation sampled at exact normalized times |
| `animation-properties-middle.png` | Synchronized typed setters, absolute geometry, colors, and translated button activation |
| `animation-blending.png` | Actual midpoint pixel blend of animated color/opacity and composed bounds/translation |

PNG conversion is lossless from the PPM files written by `Renderer::SavePpm`.
The scene-context checks verify sharing, context-local state, two-way fences,
physical-size/depth attachments, resize, static-scene caching, input invalidation,
initialization failure recovery, and cleanup. `widgets-report.txt` records the
additional GPU checks.
`animation-report.txt` records animation pixel and input checks. Animation CTest
also covers monotonic timing, delay, repeats/alternate, pause/seek/resume, target
deletion, callback mutation/errors, CSS failure/retargeting, mixed dimension
units, exact endpoints, inherited colors, stationary-pointer hover, focus
preservation, and paint-only layout-cache reuse. The demo's intentional invalid
theme reload test prints a read diagnostic before reporting its overall pass.

The driver was Mesa llvmpipe software OpenGL. Benchmark samples include CPU
submission and synchronous readback and exclude widget layout/shaping; hardware
throughput is unmeasured. See benchmark.txt / benchmark.csv for current samples.

The headless clipboard is an in-memory adapter. Native OS input, clipboard,
per-monitor DPI changes, and hardware drivers require target-machine validation.
