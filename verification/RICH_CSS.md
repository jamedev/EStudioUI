# Rich CSS validation / EStudioUI 0.5

These captures are framebuffer output from the C++ library, not browser mockups.
The PNGs are lossless conversions of the self-test PPM files.

| Configuration | Result |
| --- | --- |
| Linux GCC 13 Debug | 5/5 CTest suites pass |
| Linux GCC 13 Release | 5/5 CTest suites pass |
| Linux AddressSanitizer + UndefinedBehaviorSanitizer | 5/5 CTest suites pass; halt-on-error enabled |

Leak detection was disabled because the host tracing environment does not support
it. These checks do not establish leak-free operation.

Core: 78 checks. Widgets/layout: 3,674 checks. Animation: 2,319 checks.
Rich CSS: 288 checks, including 240 cache assertions for 500 animated widgets.
The latter Release run measured 0.164 ms per clock/prepare tick, with zero style
resolutions or layout passes throughout. This measures cached CPU animation work,
not GPU frame time or a portable performance guarantee.

The real GL test verifies linear/radial gradient samples, inset and ordered outer
shadows, oversized pseudo-element clipping, an interpolated hover shine, transformed
button activation, keyframe colors/positions, endpoint frame demand, and the full
native gallery tab. Existing scene-context and widget regressions still pass.

- [Rest](rich-buttons-rest.png) / [hover shine](rich-buttons-hover.png)
- [Full application tab](rich-css-tab.png)
- [Isolated pixel probes](rich-css-pixels.png)
- [Measured render timing and uploads](rich-css-performance.txt)

The render probe uses 160 visible buttons with warmed text/outline caches,
800x600 pixels, four warmup and 24 measured frames per case. A framebuffer read
forces completion before each timing sample ends. Both the simple baseline and
rich case use one ordered draw; rich mode adds two gradients, two shadows, and a
paint-only animation per button. The rich case submits 641 shapes and 480 glyphs,
143,488 instance bytes and 74,272 effect bytes per frame, including the root.
Timings include CPU submission and software GPU completion. They are not isolated
hardware GPU timings, and the llvmpipe results do not predict discrete-GPU FPS.

The existing Windows CI workflow uses Build.ps1 for Release and Debug with the
x64-windows-estudioui triplet. Hosted runners explicitly skip native OpenGL tests;
run `.\Build.ps1 -Configuration Release -Run` on a desktop OpenGL 3.3 driver for
native GPU validation. No runtime dependencies or dependency versions changed.

See [CSS.md](../CSS.md) for exact syntax and boundaries, including bounded effect
counts, compatible interpolation structures, and analytic shadow approximations.
