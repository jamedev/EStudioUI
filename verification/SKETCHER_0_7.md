# Sketcher, controls, and custom painting / 0.7.0

These checks run the actual C++ application and OpenGL renderer. PNG captures
are lossless conversions of framebuffer PPMs. Local rendering uses Mesa llvmpipe
software OpenGL; it establishes correctness, not Windows GPU timing/utilization.

## Coverage

- Reusable arcs, cubic Bézier chains, open/closed polylines, UTF-8 annotations,
  styled groups, immutable payloads, validation, and bounded curve approximation.
- Nested group BVHs: a scene of 200,000 grouped arcs submits 336 visible children
  and 1,872 analytic shapes in the tested viewport. All-visible scenes still
  scale with visible complexity; there is no LOD/overview performance claim.
- Cached geometry survives panning, translation, and repeated frames; zoom
  changes adaptive detail. Curve/text caches use at most 4,096 entries and an
  estimated 16 MiB combined. Idle probes require zero GPU instances/repaint.
- Twelve curve retained/full RGBA comparisons plus translucent grouping,
  nested grouping, and ungrouping comparisons. Group styles apply once per leaf.
- Numeric and control-point editing, grouping/ungrouping, duplication, direct
  horizontal/vertical and length/radius commands, bounded history, v2 save/load,
  backward v1 reading, atomic malformed-input rejection, and 200k edit tests.
- Numeric validation, atomic rejected paste/undo, intermediate edits,
  commit/rollback, range overflow, slider pointer/keyboard/wheel, and split menus.
  Callbacks deleting composed controls or popup trees have regressions.
- Eight custom-painter retained/full RGBA comparisons at 1x/2x scale, including
  transforms, clips, state restoration, exception unwinding, text, and opacity.
- Dark/light input framebuffer equality, CSS slider pixel checks, and real
  sketcher/dial gestures. The overview and tallest text inspector fit 1180x820.

## Captures and raw reports

- [Sketcher overview](canvas-sketcher.png), [curve editing](canvas-sketcher-curves.png),
  [annotation inspector](canvas-sketcher-text.png).
- [Custom widget inheritance/composition](custom-widgets.png).
- [Input controls](input-controls-dark.png), [tool popup](input-controls-popup.png).
- [Curve GPU report](canvas-curves.txt), [Painter GPU report](painter.txt),
  [controls GPU report](input-controls.txt), [sketcher report](canvas-sketcher.txt).

The existing Build.ps1 runs all new CPU suites and the native GPU self-test.
Hosted Windows CI explicitly uses -SkipGpuTest because it does not guarantee
OpenGL 3.3. Hardware measurements remain a separate local Windows check.

Local Release CTest: **12/12 passed**. AddressSanitizer + UndefinedBehaviorSanitizer:
**12/12 passed**, including framebuffer integration. LeakSanitizer was disabled
because this tracing host does not support it; this is not a leak check.
See [Release log](sketcher-release-tests.txt) and
[sanitizer log](sketcher-sanitizer-tests.txt).
