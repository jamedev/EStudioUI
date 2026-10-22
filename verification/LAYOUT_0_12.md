# Constrained measurement and layout / EStudioUI 0.12

Recorded 2026-10-22. F08 is delivered on the existing F05/F07 cache and F06 model
foundations. [LAYOUT.md](../LAYOUT.md) documents the public API, sizing precedence,
cycle handling, custom measurement/strategy contracts, diagnostics and costs.
No runtime dependency or constraint solver was added.

## Verification

- Release: **29/29** CTest suites, including actual software OpenGL integration.
- ASan/UBSan: **29/29** suites, including actual software OpenGL integration.
- New layout suite: **83 checks**. Existing widget/allocator, invalidation,
  model/view, style, input, canvas, lifetime and renderer suites remain passing.
- Three complete LAYOUT.md C++20 examples compile with GCC 13 and warning flags.

Raw evidence: [layout-release-tests.txt](layout-release-tests.txt),
[layout-sanitizer-tests.txt](layout-sanitizer-tests.txt),
[layout-work-counters.txt](layout-work-counters.txt),
[layout-examples.txt](layout-examples.txt), and the actual gallery output
[layout-foundation.txt](layout-foundation.txt).

Tests cover width allocation before dependent height, padding/border, row/column
and grid track caps/minima, hidden children, flow wrapping and oversized items,
independent percentage references and cycle fallback, mixed-font painted
baselines, chosen composition sources and deletion, clipped negative baselines,
owned reports after deletion, bounded cache reuse, invalid extension outputs,
owner/phase rejection, failure recovery, inherited CSS wrap and font changes,
UTF-8 grapheme fallback, and preservation of existing size-rule precedence.

## Actual work and pixels

| Fixture | Observed result |
| --- | --- |
| Paint edit in 10,000-widget fixture | 0 style resolutions, 0 intrinsic measurements, 0 arrangements |
| Content edit in that fixture | 0 style resolutions, 1 intrinsic measurement, 3 aggregate calculations, 102 arrangements; unrelated row retained |
| Alternating custom-content allocated widths | Natural and two width variants reused by the bounded three-entry cache |
| Form notes at native widths 880 / 1180 / 1520 | Heights 70.824 / 53.118 / 35.412 logical pixels; following geometry follows measured height |
| Actual form/editor baseline | Label and editor baseline alignment verified after each resize; numeric composition selects its editor |
| Actual paint-only color change | 0 strategy computations, 0 constrained hooks, 0 arrangements; sampled framebuffer color changes correctly |
| Retained versus full redraw | Actual Renderer::SavePpm captures match byte-for-byte |
| Settled layout gallery | 0 GPU draw calls |

The Layout lab contains working text/numeric inputs, tool buttons, baseline-aligned
form columns, wrapping tool rows, and shared grid tracks. Its captures are lossless
conversions of actual renderer output, not reconstructed UI images.

![Layout lab at 1280 logical pixels](layout-foundation.png)

[Narrow capture](layout-foundation-880.png) and [wide capture](layout-foundation-1520.png)
show the same widgets after native window resizing. The explicit size inspection
readout is refreshed before each capture.

The local compiler is GCC 13.3.0. Framebuffer tests use llvmpipe, LLVM 17.0.6,
OpenGL 4.5 core, Mesa 24.0.5-1ubuntu1. These are correctness and operation-count
results, not hardware GPU throughput measurements.

ASan uses detect_leaks=0:halt_on_error=1 because host process-tracing restrictions
prevent LeakSanitizer; UBSan uses halt_on_error=1. Framework/tests/demo C++ are
instrumented; third-party libraries and Mesa are not. No leak-free claim is made.
A host-only CTest launcher restores executable permission before running the
same compiled test binaries, accommodating this workspace's artifact permission
changes. Repository scripts and hosted CI do not require that workaround.

## Reproduce and boundaries

```powershell
.\Build.ps1 -Configuration Release -Run
```

The existing Windows script installs x64-windows-estudioui dependencies, builds,
tests, and opens Layout lab. Hosted Windows CI runs CPU Release and Debug and
explicitly skips native GPU checks; Linux CI includes actual software OpenGL and
isolated concurrency/ThreadSanitizer checks. Native Windows input/DPI, accessibility,
and hardware GPU performance remain target-machine verification.

Grid is row-major in this milestone; cell spans/explicit placement remain future
extensions. Form's baseline policy can be replaced with a two-column GridLayout
when other cell alignment is needed. Label wrapping uses the current LTR shaping
path, ASCII space/hyphen opportunities and grapheme fallback, not the F10 full
Unicode paragraph/bidi/IME service. Root geometry follows the window viewport;
explicit floating geometry remains an application-owned out-of-flow escape hatch.

The next requested experiment is an optional linear constraint layout built on
LayoutStrategy and these measurement contracts. Hard/soft priorities,
infeasibility reporting, and size-rule precedence must be settled before choosing
any solver dependency. Gate B still requires F09, F11 interfaces and F14 packaging;
see [FOUNDATIONS_ROADMAP.md](../FOUNDATIONS_ROADMAP.md).
