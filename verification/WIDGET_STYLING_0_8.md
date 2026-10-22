# Widget styling / 0.8.0

These records come from the C++ application and its OpenGL renderer. Captures are
lossless conversions of framebuffer PPMs, with no mock UI. The local driver is
Mesa llvmpipe software OpenGL; these checks establish correctness and work
counts, not Windows hardware GPU utilization.

## Coverage

- CSS part cascade and states, bounded registered palettes, fixed-part automatic
  motion, shared whole-stylesheet animation, cancellation/replacement, hidden
  animation suspension, and popup selector ancestry/lifetime.
- Text validation, placeholder/selection/caret, composed numeric focus, slider
  dimensions and pointer endpoints, semantic tab selection, and CSS-controlled
  composite gaps/arrow/separator sizes with explicit C++ contracts preserved.
- Rich collection row/cell/header/branch states, content sizing, tree expander
  hit regions, transformed scrollbars, scrollbar drag/clamp and stationary-pointer
  hover after scrolling, hiding, or moving a collection.
- **28 complete retained/full RGBA comparisons at 1x and 2x**. Real pixels verify
  gradients, borders, selection, caret, headers, tree expansion, and scrollbars.
- **10,000 list items and table rows**: peak 248 visible glyphs in the probe.
  Models allocate no row widgets or per-row animation objects.
- **500 animated fixed parts over 240 ticks**: zero selector resolutions and
  zero layout passes. Idle and hidden animated frames submit zero analytic work.
- Eight native gallery captures across three presets, including actual popup,
  editor/table interaction, custom painting, canvas zoom, and the separately
  rendered static 3D surface. Settled gallery frames make zero GPU draw calls.

See [raw GPU counters](widget-styles.txt),
[gallery checks](widget-styles-verification.txt),
[Release log](widget-styles-release-tests.txt), and
[sanitizer log](widget-styles-sanitizer-tests.txt).

Local Release CTest: **16/16 passed**. AddressSanitizer plus
UndefinedBehaviorSanitizer: **16/16 passed**, including the framebuffer suite.

## Captures

| Controls | Aurora | Paper | Ember |
| --- | --- | --- | --- |
| Inputs and commands | [Image](widget-styles-inputs-aurora.png) | [Image](widget-styles-inputs-paper.png) | [Image](widget-styles-inputs-ember.png) |
| Virtualized collections | [Image](widget-styles-collections-aurora.png) | [Image](widget-styles-collections-paper.png) | [Image](widget-styles-collections-ember.png) |

[Context popup](widget-styles-context-menu.png) and
[canvas, custom painting, and 3D](widget-styles-surfaces.png) use real application
content and input paths. See [WIDGET_STYLING.md](../WIDGET_STYLING.md) for supported
parts, state selectors, geometry contracts, and animation boundaries.

The existing one-shot `Build.ps1` discovers the new CTest suites and runs the GPU
self-test by default. Hosted Windows CI uses `-SkipGpuTest`, since OpenGL 3.3 is
not guaranteed there. The sanitizer run instruments the toolkit and demo with
AddressSanitizer and UndefinedBehaviorSanitizer; dependencies and Mesa are not
instrumented. Leak detection is disabled on this tracing host.
