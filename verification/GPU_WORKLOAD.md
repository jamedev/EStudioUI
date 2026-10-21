# GPU workload validation / EStudioUI 0.5.1

Measurements come from the actual C++ gallery and renderer, using Linux GCC 13
Release and Mesa llvmpipe 25.1.7 (LLVM 20.1.2). This host uses **software OpenGL**;
these timings do not predict the user's Windows GPU utilization or hardware FPS.
The reported approximately 45% hardware utilization cannot be reproduced here.

## Actual gallery comparison

1180x820 pixels, four warmup and 32 measured frames per case/mode. Full redraw
uses the same shaders, caches, and presentation copy as retained rendering;
the comparison isolates retaining pixels. Each sample includes synchronous pixel
readback for completion. Optional GL_TIME_ELAPSED queries are collected without
blocking for unavailable results. The profiler drives frames without pacing.

| Gallery case | Analytic repaint, full → retained | Glyph instances, full → retained | Completed-frame median, full → retained | GPU query median, full → retained |
| --- | --- | --- | --- | --- |
| Inputs, unchanged pointer moves | 100% → 0% | 806 → 0 | 11.36 → 1.24 ms | 10.97 → 0.94 ms |
| Data & actions, table selection clicks | 100% → 8.25% | 802 → 80.5 | 9.32 → 3.18 ms | 8.76 → 2.76 ms |
| Rich CSS, normal animations | 100% → 12.40% | 909 → 133.1 | 8.17 → 2.63 ms | 7.81 → 2.33 ms |

Rich CSS reduces analytically repainted area by **87.6%**, and replayed glyphs by
**85.4%**, with three ordered draws rather than one full-window draw. Table
selection uses about 2.1 draws on average. Idle forced frames submit no analytic
draws or instance/effect uploads. They still copy the retained image to the
native back buffer. The normal event loop does not present those idle frames.

Raw measurements: [gpu-workload.csv](gpu-workload.csv) and
[gpu-workload.txt](gpu-workload.txt). Reproduce on the target GPU using the Release
`--profile-ui` command in [PERFORMANCE.md](../PERFORMANCE.md). No effects were
removed and animation durations remain unchanged; normal presentation is limited
to 60 Hz by default, independently of frequent input events and vsync overrides.

## Regression checks

- Linux Release, Debug, and AddressSanitizer/UndefinedBehaviorSanitizer: all five
  CTest suites pass. Dependencies/Mesa are not instrumented. Leak detection is
  disabled because this host cannot run LeakSanitizer; no leak-free claim is made.
- Core: 78 checks. Widgets/layout: 3,674 checks. Animation: 2,319 checks.
  Rich CSS: 292 checks, including 1,000 input wakeups against a 60 Hz deadline,
  slow-frame behavior, and 500-widget paint-only style/layout cache reuse.
- 48 complete RGBA frame comparisons against full redraw, permitting at most
  1/255 channel difference: moving/subpixel geometry and shadows, opacity,
  overlapping/reordered translucent owners, changing glyphs, decorative layer
  insertion/removal, visibility changes, rounded/rotated clips, transparent
  selection slots, and 1x/2x logical-to-physical scale.
- Hidden-tab tests advance the clock during 60 pointer events and assert no frame
  demand or style/layout work. Revealed animations catch up to logical time.
- Existing real-framebuffer tests for gradients, shadows, transformed input,
  shared scene contexts/fences, static scenes, resizing, and lifecycle errors pass.

[retained-pixels.txt](retained-pixels.txt) records pixel comparison coverage.
[retained-redraw.png](retained-redraw.png) is a lossless conversion of actual
C++ framebuffer output. The full redraw reference submits every recorded
primitive without damage-bound culling.

CTest records are `gpu-workload-release-tests.txt`, `gpu-workload-debug-tests.txt`,
and `gpu-workload-sanitizer-tests.txt`. The intentional invalid-theme test prints
a read diagnostic before the GPU integration pass. The workflow separately uses
the unchanged Build.ps1 to compile/test Windows MSVC Release and Debug with
`x64-windows-estudioui`; hosted Windows runners explicitly skip native OpenGL.
Hardware load, native input, focus/minimize behavior, and driver-specific timing
still need measurement on a desktop driver.
