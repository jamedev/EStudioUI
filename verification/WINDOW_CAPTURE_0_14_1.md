# Windows warnings, secondary-window closure, and stable GPU capture / 0.14.1

Recorded 2026-10-22. This maintenance increment precedes F11.

## Reported failures and corrections

The supplied MSVC log passed all 32 core suites and then failed the native GPU
self-test at `first queued model update reaches actual GPU pixels`. It also
reported C4244 from `GridLayout` filling float ascents with an int and from the
layout probe constructing an optional float baseline from an int. Both literals
now use explicit float types; warning levels remain enabled.

The application probe sampled the default framebuffer after the scheduler had
swapped buffers. Its back-buffer contents are undefined after presentation on
Windows. The renderer already owns the completed image in its retained GPU
framebuffer. `ReadPixel` and `SavePpm` now capture that image with the owning
context current. Read framebuffer/selector, pixel-pack buffer, alignment, row
length, and skip state are restored. Capture before a successful frame is
diagnosed. This adds no readbacks to ordinary rendering.

The exact pixel assertion was reproduced on the pre-fix implementation by
overwriting the default back buffer after scheduled presentation. The permanent
probe keeps that overwrite and verifies the queued update's blue GPU pixels.
It also checks native presentation pixels before overwrite, independently of
retained capture. See [reproduction](window-capture-repro.txt).

Both the Async close button and the native close adapter previously set GLFW's
close flag while the gallery retained the C++ Window owner. The scheduler stopped
servicing a still-visible native surface. Closure now immediately hides that
surface and retires its delivery endpoint. Close is idempotent; native resources
remain owned safely until C++ destruction. The gallery reclaims closed owners
before reopening.

## Verification

| Check | Observed result |
| --- | --- |
| Linux GCC 13 Release | 32/32 suites, including actual renderer integration |
| AddressSanitizer + UndefinedBehaviorSanitizer | 32/32 suites, including the renderer and closure regressions |
| Actual Async gallery | Close-button input and production native-close adapter hide retained windows; reopening reclaims them; the main window remains open |
| Post-presentation capture | Queued blue pixels survive deliberate native back-buffer overwrite |
| External GL state | Pixel/PPM capture preserve framebuffer, read selector, pack PBO, and row-packing state |
| Screenshot contents | Actual framebuffer dimensions and complete PPM rows verified, including odd logical width |

Raw evidence: [Release suites](window-capture-release-tests.txt),
[ASan/UBSan suites](window-capture-sanitizer-tests.txt),
[application/framebuffer probe](window-capture-application.txt), and
[actual Async gallery probe](window-capture-async.txt).

The sanitizer run uses `detect_leaks=0` because of the host's process-tracing
restriction. It does not establish leak freedom or instrument Mesa/dependencies.

Local GL uses GLFW's null platform and OSMesa/llvmpipe. The native-close adapter is
invoked directly in the automated gallery probe; this is distinct from a person
pressing the title-bar control on Windows. MSVC CI uses the unchanged one-shot
PowerShell script for Release and Debug with `-SkipGpuTest`, because hosted
Windows runners do not guarantee OpenGL 3.3. Recheck the native GPU self-test on
the developer PC with `./Build.ps1 -Configuration Release -Run`.

F11 platform/accessibility services and F10a essential text/input remain pending.
