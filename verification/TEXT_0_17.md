# F10a control text verification (0.17)

Actual implementation: font registry/fallback, script and bidi runs, visual caret
and disjoint selection, Unicode wrapping, inherited typography, composition and
native text-range geometry, bounded CPU/GPU caches. See [TEXT.md](../TEXT.md).

Linux Release: 36/36 CTest suites pass, including FontText and actual GPU integration.
The public ControlText example is compiled alongside the existing examples.
ASan/UBSan and Windows Release/Debug/native results are recorded after their runs.

FontText checks fallback identities/coverage, real variation/italic selection,
Arabic glyph IDs against an independently configured HarfBuzz reference,
mirroring/isolation, grapheme hit/navigation and affinity, word/line breaking,
resource admission/retirement, wrong-thread access and typography invalidation
without unrelated CSS work. TextBuffer tests include a 1 MiB boundary crossed by
one combining grapheme. NativePlatform extends the existing HWND/UIA fixture with
mixed-direction logical selection, disjoint rectangles and visible intervals.

The text gallery's real renderer probe verifies painted split selections,
retained/full framebuffer equality, mixed-script preedit cancellation restoring
exact pixels, actual-font identity despite an identical numeric glyph ID, bounded
GPU admission and cache-clear redraw equality. Capture:
[text-foundation-0-17.png](text-foundation-0-17.png). Driver: Mesa llvmpipe 4.5,
LLVM 17 software GL; this proves rendering/caching behavior, not hardware speed.

The separate [platform acceptance checklist](PLATFORM_ACCEPTANCE.md) continues to
track human installed-language IME, Narrator and mixed-monitor DPI sessions.
Those observations remain unperformed in this environment; no automated result
is presented as completing them. F10b document editing remains deferred.
