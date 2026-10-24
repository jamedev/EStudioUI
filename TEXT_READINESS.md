# Text readiness before F10b (0.26.1)

This milestone hardens the existing control-text services before introducing a
document engine. It adds shared validation, bounded automatic GPU glyph
retirement, deterministic stress fixtures and a focused native acceptance path.
It introduces no runtime dependencies or document-editor widget.

## Shared typography contract

`FontSelection` lives in its own public header and exposes `Validate()`,
`IsValidFamilyList()` and `IsValidLanguage()`. CSS property admission, font
resolution/shaping, FontEditor and FontPicker use the same boundary:

| Input | Contract |
| --- | --- |
| Family list | Valid UTF-8, 1–16 nonempty quoted/unquoted names, at most 1,024 bytes; no NUL or line/control characters except tab |
| Language hint | 1–63 ASCII letters, digits or hyphens; syntax hint rather than a registry-backed BCP-47 validator |
| Direction | `auto`, `ltr`, `rtl` |
| Weight / style | Finite 1–1,000; `normal`, `italic`, `oblique` |
| Service size | Finite 0–4,096 logical pixels; existing CSS limits remain in effect |
| FontEditor/FontPicker size | Explicit narrower user-control range 1–512 |

Malformed assignments fail before font loading, shaping or committed binding
changes. An incomplete family draft remains editable and does not replace the
committed value. The preview receives all six fields, including language and
direction. `FontEditor::GetPreview()` exposes the ordinary Label for application
customization. Bindings retain the existing atomic state-candidate contract.

## GPU residency and recovery

Glyph keys still contain actual font identity and glyph ID. The renderer checks
glyph bounds against the current transformed/clipped viewport before GPU cache
admission. Whitespace and fully offscreen glyphs consume no GPU glyph entries.
This does not eliminate whole-run CPU shaping or outline-bound lookup.

Each recording pins the unique visible glyphs referenced so far. When unused
residency prevents another admission, compaction retires unused glyphs, rebuilds
curve/band addresses and patches already recorded text instances before any GL
submission. All current-frame pins survive. Relocation invalidates retained
damage history; the next submission fully repaints instead of interpreting old
addresses as unchanged paint. The default hard budget remains 32 MiB and 65,536
actual-font/glyph pairs, with hardware texture-buffer limits also enforced.
CPU staging grows geometrically within bounded capacities. Compaction uses
temporary storage; staging and retained CPU layouts are separate from GPU bytes.

There is no automatic budget growth, visible-glyph eviction or partial text
truncation. If a single outline or the current visible set cannot fit,
`FontCacheCapacityError` provides a reason, required bytes, configured limit and
required glyph count. `EndFrame()` rejects a recording with failed admission.
Call `AbortFrame()` to discard the unsubmitted recording and retain access to
the last successfully completed framebuffer. Increase the budget at that frame
boundary or reduce visible content, then begin a new frame. This recovery
guarantee covers failures during recording, before GPU submission; it is not a
rollback guarantee for a driver/submission failure inside EndFrame.

```cpp
try
{
    renderer.BeginFrame(width, height, framebufferWidth, framebufferHeight, clear);
    // Record the visible UI using the ordinary renderer or Painter.
    renderer.EndFrame();
}
catch (const EStudioUI::FontCacheCapacityError& error)
{
    renderer.AbortFrame();
    // Inspect error.GetReason()/GetRequiredBytes()/GetByteLimit().
    // Change an application-owned budget or visible working set before retrying.
}
```

`RenderStats` adds `m_FontEvictions`, `m_FontCompactions`, `m_FontUploadBytes`
and `m_VisibleGlyphs`. Counters describe the last completed frame. An unchanged
frame uploads no fonts and performs no analytic drawing. A miss still uploads
the current font arena when drawing is required; incremental GPU suballocation
and multi-page streaming remain future measured optimizations.

## Repeatable verification and profiling

The registered `TextReadiness` CPU suite exercises malformed Unicode/family
limits, language/direction consistency, atomic bound rejection, preview fields,
and bounded exact shaping of requested paragraphs. `benchmarks/TextCorpus.*`
contains no widgets or GL dependencies. Its owned deterministic fixtures include
100,000 short paragraphs, one 64 KiB paragraph, mixed-script paragraphs and
paragraphs intended for wrapping. This fixture inventory is not a document
storage implementation.

The normal demo `--self-test` also runs real framebuffer checks for:

- Forced small-budget churn, retirement and relocation after an earlier glyph
  has already been recorded, including transformed clips and translucent paint.
- Exact comparison of every churn frame with an independent full-redraw renderer.
- Zero analytic drawing/font upload on the identical following frame.
- Oversized visible-set and single-outline diagnostics; failed EndFrame,
  abort during a prospective resize, last-frame capture and recovery.
- Fully clipped text/whitespace admission and the ordinary native acceptance UI.

The test writes `text-cache-churn.csv`, `text-readiness-gpu.txt` and actual
renderer PPM captures under the requested output directory.

```powershell
.\build\windows-vcpkg\Release\EStudioUIDemo.exe --text-profile --output benchmark-local\text
```

The profile records 24 unpaced frames for each fixture in cold/warm, scrolling,
editing, font-churn and wrapping phases: 480 samples. It draws directly through
the production renderer at a 960×720 logical/physical viewport, with no
synchronous pixel readback or artificial frame pacing. CSV records corpus sizes,
requested paragraph count, CPU elapsed time, shaping/cache work, CPU/GPU storage,
font uploads/churn, damage/draw counters and delayed GPU query/frame identity.
The companion text file records the GL driver and interpretation limits. CPU
time includes shaping, wrapping, recording and submission. GPU query intervals
are not utilization percentages or native presentation/compositor timings.

The existing whole-paragraph control API deliberately does not cache a shaped
result larger than its 4 MiB shape budget. The 64 KiB fixture exposes this:
repeated requests reshape the run. `Wrap()` also computes the full requested
paragraph rather than virtualizing it. These are explicit boundaries to address
in F10b; raising cache budgets is not a document-layout architecture. Use
`Profile-Rendering.ps1` for paced application text/effect comparisons and target
hardware measurements.

## Focused Windows acceptance

```powershell
.\scripts\Test-TextReadiness.ps1 -SkipBuild
```

Without `-SkipBuild`, the script first runs Build.ps1's pinned Release build and
automated checks. It then runs the text profile and opens `--text-readiness`:
mixed-direction and RTL fields, a composition field and registered-font preview.
`-SkipProfile` skips profiling; `-PrepareOnly` only creates the report scaffold.
An existing `acceptance.json` is never overwritten.

The JSON records source revision/dirty state, executable SHA-256, OS, PowerShell,
discovered video adapters/driver versions and explicit human-check instructions.
Fill in keyboard/IME and physical monitor/scaling details. Every human result
starts as **NotTested**. Closing the window or a successful automated run never
marks those observations passed. Japanese/Chinese/Korean IME, dead keys/AltGr,
mixed selection, native clipboard/undo, focus transitions, mixed-monitor
candidate positioning, real Narrator and physical GPU performance require
actual observations. See also [platform acceptance](verification/PLATFORM_ACCEPTANCE.md).

## F10b entry contract

Start with a widget-independent multiline document model and edit commands, then
an incremental paragraph/line layout service, then the multiline editor/view.
Use delta undo through the reusable UndoHistory service, explicit byte/grapheme
positions, paragraph-local invalidation, visible-line traversal and retained
layout ownership independent of the short-run shape LRU. Define long-paragraph
work limits and viewport behavior before claiming fluid large-document editing.
Reuse the current shaping, GPU, focus and TextInputClient boundaries. Extend
multiline accessibility/text-range semantics with the editor, rather than
pretending the existing single-line provider already supplies them. Rich spans,
embedded images/tables, pagination and document tooling follow that plain-text
layer. Human acceptance stays open until the recorded target sessions occur.
