# GPU workload / EStudioUI 0.5.1

The renderer now retains unchanged pixels instead of shading the entire window
whenever a control or animation changes. This targets both continuous Rich CSS
load and brief spikes while using other tabs.

## Changes

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

## Reproduce on Windows

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
`GL_TIME_ELAPSED` results, repainted area, replayed glyph instances, and draws.
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
An eight-query ring polls available results without waiting. `m_GpuMilliseconds`
is -1 before a result arrives; `m_TimingSamples` identifies completed samples.

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
