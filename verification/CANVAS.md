# Canvas verification / 0.6.0

All measurements use the actual C++ scene/view and OpenGL renderer. Captures are
lossless PNG conversions of its PPM framebuffer output.

## Local measurement

Release build, GCC 13, 800x600, llvmpipe (LLVM 20.1.2, 256 bits), Mesa
25.1.7. This is software OpenGL; it does not predict Windows GPU utilization.
Four warmup frames precede 32 completed pan-frame samples. CPU query medians use
100 isolated BVH queries. Same local geometry density in both scenes:

| Items | Build/BVH ms | Query median µs | Visited BVH nodes | Visible items | Completed frame median ms | Shapes / draws |
|---:|---:|---:|---:|---:|---:|---:|
| 20,000 | 7.59 | 2.50 | 739 | 315 | 4.79 | 317 / 1 |
| 200,000 | 76.08 | 2.72 | 805 | 315 | 3.90 | 317 / 1 |

The lower measured 200k frame time is run-to-run noise, not a claim that larger
scenes render faster. Both scenes submit the same visible workload. During pan,
all 480,000 framebuffer pixels changed; unchanged/offscreen-edit probes repaint
zero pixels. No overview/LOD benchmark claim is made: all-visible/overlapping
geometry scales with visible count.

Raw results: [CSV](canvas-profile.csv), [driver/method](canvas-profile.txt).
Reproduce with `EStudioUIDemo --profile-canvas --output benchmark-local/canvas`
on Windows, or add `--headless` on a Linux OSMesa host.

## Coverage

- 11,900 scene checks: randomized brute-force query oracle, edits, balanced-tree
  behavior, 200k objects, nested/overlapping shapes, stale handles, replacement,
  large coordinates, invalid inputs and pointer-free identity lookup.
- 265 document checks: primitive creation, analytic picking, selection, snapping,
  history, 200k group move/delete/undo, precise file round-trip, malformed file
  rejection, atomic load, stable scene references, and handle invalidation.
- View/input checks: camera precision, Alt pan, pointer-centered wheel events
  including hosted children, native button hosting, preview cancellation, edit
  gestures, keyboard commands, clipped cache restoration, and sender deletion
  during callbacks.
- 32 complete retained/full RGBA comparisons of new GPU primitives at 1x/2x,
  plus 10 canvas scene comparisons. Tests cover caps, degenerate/diagonal lines,
  CSS circle paint, clipping/transforms, stable IDs, removal/reinsertion,
  painter order, selection, camera changes and idle reuse.
- View-spanning lines from ±1e12 and ±1e14 render to both viewport edges at
  1e6 zoom. A 200k scene query returns 336 candidates / 805 visited nodes /
  338 recorded shapes. An offscreen selection change causes zero repaint.
- Actual gallery gestures and document reload are exercised before capture.

[GPU canvas results](canvas-verification.txt),
[primitive results](canvas-primitives.txt),
[gallery capture](canvas-sketcher.png).

The regular Windows `Build.ps1` includes all new CPU suites and the native GPU
self-test. Hosted Windows CI uses its existing explicit `-SkipGpuTest`; native
hardware testing remains separate from Linux OSMesa validation.

Local Release CTest: **8/8 passed**. AddressSanitizer + UndefinedBehaviorSanitizer:
**8/8 passed**, including the real framebuffer integration test. LeakSanitizer was
disabled because the tracing host does not support it; this is not a leak check.
See [Release log](canvas-release-tests.txt) and
[sanitizer log](canvas-sanitizer-tests.txt).
