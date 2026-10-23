# Rendering performance

## Paced interaction capture (0.19)

Run on the target GPU in Release to investigate Rich CSS peaks, repeated slider
input and text-editing workloads:

```powershell
.\scripts\Profile-Rendering.ps1
```

The runner builds/tests the current source and then runs the native profiler.
The direct executable entry point, useful for a narrower repeated capture, is:

```powershell
.\build\windows-vcpkg\Release\EStudioUIDemo.exe --profile-rendering --profile-case rich-css --profile-frames 180 --profile-warmup 30 --profile-rate 60 --output benchmark-local\rich-css
```

Case filters are `all`, `rich-css`, `widget-styles`, `isolated-slider`, `text` and
`baseline`. Each runs full and retained redraw using the same workload. The default
is 30 warmup and 180 measured frames at a scripted 60 Hz. Rate zero explicitly
selects unpaced throughput. The native swap interval is disabled for this scripted
pacing and restored afterward; the actual achieved rate is recorded.

| Workload | What it isolates |
| --- | --- |
| Rich CSS animations | The actual twelve-button gallery with its normal persistent CSS motion |
| Widget styles slider | Actual pointer dragging, including NumericInput and distant status-text synchronization |
| Isolated slider, effects/flat | The same standalone slider geometry and input, with gradient/shadow effects enabled and disabled |
| Dense text edit/selection | Existing mixed-script TextBox controls, input/backspace and captured selection among unchanged text |
| Forced static presentation | Full-copy/submission baseline; deliberately requests frames and does not measure idle scheduler power |

The dense-text cases exercise the current text primitives. They are not a
rich-document editor implementation or a document-layout benchmark.

The output directory contains:

- `render-profile-metadata.json`: driver/build, software/headless classification,
  logical/physical dimensions, pixel scale, pacing, warmup and timestamp-counter width.
- `render-profile-frames.csv`: entry, warmup, measured and drain frames, exact frame
  serials, matched asynchronous GPU timings, CPU/input/swap wall time, repaint
  reasons, changed tile area, replayed glyphs, draw calls, transfers and UI work.
- `render-profile-summary.csv` and `render-profile.txt`: steady-state median/p95
  timings, sample coverage, achieved rate and damage-area/fragmentation fallbacks.

There is no `ReadPixel`, `glReadPixels` or `glFinish` in the measured path. GPU
queries are matched back to their originating frame, not assigned to whichever
frame happens to poll them. Missing samples stay empty in frame CSV and use -1
for unavailable aggregates; they never become zero-duration samples. Warmup and
drain frames are excluded from measured percentiles. GPU glyph caches are cleared
before each run; CPU shaping/layout caches may persist between runs. Entry rows
therefore expose first-frame work, while the steady-state summary describes warm
interaction.

GPU scopes separate upload commands, retained drawing and the full presentation
copy. They measure elapsed GPU timeline intervals, which can include waits between
commands. They exclude native swap and desktop composition and are not a conversion
to Task Manager utilization. Input and CPU rendering wall time are separate; the
latter includes preparation, recording and GL submission, which can overlap GPU work.

Linux/CI may use the same entry point with `--headless`. Software GL captures
verify scenarios, accounting and profiling output; their elapsed times describe a
CPU renderer and do not predict hardware GPU utilization. Keep build, physical
resolution, DPI, workload and requested rate equal for before/after comparisons.

## Retained rendering foundation (0.5.1)

The renderer retains unchanged pixels instead of shading the entire window
whenever a control or animation changes. This targets continuous Rich CSS load
and brief spikes while using other tabs.

- One window-sized RGBA8 framebuffer retains the composed UI. Changed paint
  commands invalidate their old/new bounds; only damaged regions are redrawn.
- Overlapping primitives replay in painter order, preserving transparency,
  shadows, rounded clips, and analytic text. A few scissored draws replace full
  window shading. Dense/fragmented changes automatically use a full redraw.
- Hidden CSS timelines do not sample or request repaint when input advances the
  clock. Revealing a widget catches up to elapsed logical time. Inactive pages
  also skip style traversal.
- Transparent collection backgrounds reserve CPU paint slots without submitting
  invisible primitives. Selection changes retain adjacent grid/text pixels.
- A fixed presentation deadline prevents frequent pointer events from bypassing
  the default 60 Hz limit. Rendering pauses while minimized, invisible, or
  unfocused. Set `WindowOptions::m_MaxFrameRate` or `m_RenderWhenUnfocused` before
  constructing the window to change these defaults.
- Gradient parameters and premultiplied stops are prepared per instance on the
  CPU. Rounded-box distance avoids a square root outside corner calculations.
- Static OpenGLWidget textures carry content revisions. Animated scene updates
  invalidate their own rectangle, preserving the existing producer/consumer fences.

No runtime dependency, CSS effect, or animation duration was removed. Idle native
windows wait for events; they do not continually blit cached frames.

## Original completed-frame benchmark

```powershell
.\Build.ps1 -Configuration Release -Run
```

Compare Rich CSS, table selection, idle Inputs, and minimize/focus changes in the
normal demo. For repeatable full-redraw versus retained-rendering measurements:

```powershell
.\build\windows-vcpkg\Release\EStudioUIDemo.exe --profile-ui --output benchmark-local\ui
```

This profiles the actual gallery at 1180x820 logical pixels: four warmup frames
and 32 measured frames for each mode/case. Table selection clicks eight visible
rows and verifies the resulting selection. The idle case delivers pointer moves
without changing visuals. Rich CSS runs its normal animations. Full redraw mode
uses the same optimized shaders, font caches, and presentation copy, isolating
the effect of retaining pixels. It is not a binary timing comparison with 0.5.0.

`gpu-workload.csv` and `.txt` record completed-frame medians, asynchronous
GPU timestamp results, repainted area, replayed glyph instances, and draws.
The profiler deliberately drives frames without pacing, and reads one pixel to
complete each sample. Its timings measure per-frame cost; they do not represent
normal idle behavior or Windows Task Manager GPU utilization. Run it on the
target hardware/driver for hardware timings. The normal event loop and vsync
remain enabled when launching without profiling flags.

Linux/CI can use `--headless --profile-ui`; this requires OSMesa. Software OpenGL
timings do not predict discrete-GPU utilization. Saved local measurements and
validation are in [verification/GPU_WORKLOAD.md](verification/GPU_WORKLOAD.md).

## Diagnostics and memory

`Renderer::GetStats()` separates the recorded scene (`m_Glyphs`, `m_Shapes`) from
actual analytic work (`m_ReplayedGlyphs`, `m_SubmittedInstances`, `m_RepaintPixels`,
`m_DamageRectangles`, upload bytes, and `m_DrawCalls`). Optional GPU timing is off
by default; enable `SetGpuTimingEnabled(true)` while the owning context is current.
An eight-frame ring uses four `GL_TIMESTAMP` markers per frame and polls available
results without waiting. `m_FrameSerial` identifies the submitted frame;
`m_GpuTimingFrame` identifies the earlier frame whose timing completed.
`m_GpuMilliseconds` is -1 before a result arrives. Upload, retained drawing and
presentation-copy intervals sum to that total. `m_TimingSamples` counts valid
completed samples, but polling can consume several while publishing only the
newest duration; captures must count actual matched frames. A zero-bit timestamp
counter disables optional timing. Wrapping/nonmonotonic results are discarded.

`m_RedrawReason` distinguishes unchanged/partial frames, explicitly requested full
redraw, unavailable history, clear/viewport changes and area/rectangle fallback.
`m_DirtyTileCount` and `m_DirtyTilePixels` retain the complete changed tile map
before a budget fallback; mandatory/explicit full redraw instead reports the
whole target. `m_DamageCandidateRectangles` records the accumulated rectangles
at the decision, including the bounded seventeenth rectangle that triggers
fragmentation fallback. `m_RepaintPixels` reports the final plan, so a small
dirty-tile area followed by full repaint makes amplification visible.

The retained texture costs four bytes per physical pixel: about 3.7 MiB at
1180x820 or 31.6 MiB at 3840x2160, plus CPU copies of the paint stream and effects.
Every presented frame still copies the full retained image to the native back
buffer because swap preservation is not guaranteed. Zero analytic redraw does
not mean a forced presentation has zero GPU cost. The event loop avoids that
copy entirely when idle. More than 16 regions or 60% damaged area falls back to
a full repaint. Resize, logical scale, and clear-color changes also repaint fully.

`SetPartialRedrawEnabled(false)` provides a diagnostic full-redraw reference.
Custom `DrawTexture` users should supply a nonzero content revision and advance
it whenever texture contents change. The default revision zero always invalidates
its bounds. OpenGLWidget manages revisions automatically. Custom widgets continue
to request repaint after model changes; retaining pixels does not replace that
existing invalidation contract. Model traversal and paint recording still run on
requested frames; per-widget display-list caching is future work.
