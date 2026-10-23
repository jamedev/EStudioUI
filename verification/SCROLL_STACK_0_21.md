# ScrollBar / StackedWidget 0.21 verification

The implementation adds W15 standalone ScrollBar and W17 StackedWidget on the
existing foundation contracts. No dependency or new continuous timer is added.
Source is published in `e057186` plus the local-hover follow-up `795f638`.
CI for the final source: [Build and test run 39954728552](https://github.com/jamedev/EStudioUI/actions/runs/39954728552).
Automated integration passed for that exact final source. Human platform
acceptance and hardware performance limits remain separate below.

## Behavioral coverage

`ScrollStackTests` exercises proportional thumb geometry, exact endpoints, zero
page/range, extreme finite double limits, invalid input preservation, both axes,
primary-pointer capture, track paging, wheel/key input, deactivation, disabled
input, transformed root input, State validation/rollback, canonical semantic
number actions, owner-thread enforcement, callback deletion and bounded feedback.

Stack checks cover automatic first selection, hidden oversized-page measurement
exclusion, inactive animation suspension, persistent editor state and separate
composition cancellation, page identity after deletion, same-index replacement,
custom page construction, invalid indices/adoption, coherent empty/reset state,
semantic grouping, callback-safe deletion and bounded reentrant selection.

The existing StyledScroll, EssentialWidgets, InputFoundation, Layout, Semantic
and other suites continue to cover the shared collection/input/layout contracts.
The compiled ScrollStack example checks the common application API.

## Actual framebuffer coverage

`ScrollStackGallery::Verify` runs in the production native/headless Window and
Renderer and writes `scroll-stack-widgets.ppm` and `scroll-stack-report.txt`.
It verifies styled proportional thumbs, real hover and pressed pixels, production
captured dragging, page gradient replacement, narrow/wide active-page allocations,
exact retained/full pixel equality and zero settled repaint work. Value-only
redraws must preserve style, measurement and arrangement counters.

The demo opens **Scrolling & pages** and retains the earlier image gallery's
explicit tab index. Native Windows UI Automation checks the scrollbar control
type and exercises its RangeValue write/read path on the owning dispatcher.

## Validation status

- Final focused GNU 13 Release behavior run: **73 checks passed**.
- Final formatted GNU 13 Release: **41/41 passed**, 18.97 seconds, including
  software-GL GPU integration and rendering-profile smoke checks. The earlier
  complete development run also passed 41/41 in 19.39 and 20.71 seconds.
- No C++ compiler warnings observed. The existing libunibreak CMake developer
  scope warnings remain unchanged.
- Final GNU 13 Debug (`-O1 -g1`) ASan/UBSan: **41/41 passed**, 92.71 seconds,
  including GPU integration (32.87 seconds) and rendering-profile smoke (9.38 seconds).
  Every generated object and executable had a valid ELF header before testing.
  Leak detection was disabled under host tracing; third-party libraries were not
  rebuilt with instrumentation. The earlier pre-hover-fix checkpoint also passed
  41/41 in 99.76 seconds and is not substituted for the final-source result.
- Independent Linux CI (`120417940620`): **41/41 passed**, 25.41 seconds,
  including actual software-GL GPU integration (14.05 seconds) and rendering
  profile smoke (7.00 seconds). The isolated foundation CPU ThreadSanitizer
  configuration passed **6/6**, 3.39 seconds; this does not claim that native/GL
  or every widget control is ThreadSanitizer-instrumented.
- Windows CI (`120417941179`): MSVC Release **42/42 passed**, 10.13 seconds;
  MSVC Debug **42/42 passed**, 61.09 seconds. Windows native UIA verifies the
  scrollbar control type and canonical RangeValue write/read.
- Actual native Win32/WGL framebuffer integration: **PASS**, with staged
  Mesa 26.2.4 llvmpipe (LLVM 23.1.2), followed by the PowerShell rendering-profile
  smoke flow (`-Frames 12 -Warmup 4 -Rate 0`). This is real native context/input
  and framebuffer execution on a software driver, not a hardware speed result.
- Five additional native Debug lifecycle runs: **99 checks each**, all passed.
- The complete final-source [CI run](https://github.com/jamedev/EStudioUI/actions/runs/39954728552)
  concluded **success**. No C++ compiler warnings were observed in either job.

Software GL pixel checks establish rendering correctness and work accounting,
not a hardware GPU utilization or 60 FPS claim. Real assistive technology,
interactive IME and mixed-monitor acceptance remain on the existing platform
checklist. F10b document editing and F14 extraction remain deferred.

[Saved actual framebuffer pixels](scroll-stack-widgets-0-21.png) are converted
directly from the C++ probe PPM (760 x 520), showing the selected blue page and
its moved scrollbar; this is a contract fixture, not the interactive gallery.
PNG SHA-256: `4c0c706594c72feba5a3e3777f2ce0e9e132328a60629ba763dd5c28c637b853`.

## Local thumb hover follow-up

An isolated production-UiContext probe reproduced track hover incorrectly
selecting the thumb hover dimensions (60 -> 30 pixels while the pointer remained
on the track). Track/thumb parts now use the same explicit shared palettes as
collection scrollbars, and paint/hit geometry resolve thumb hover once from the
neutral thumb. Regression checks verify track isolation, shrinking hover geometry
and restoration; real pixels verify neutral track hover and the correct hover
fill even when the pointer lies outside the shrunken thumb. No unrelated engine
state/animation policy was changed. Root widget CSS animation remains supported;
shared scrollbar palettes do not run independent part timelines.

## Interactive gallery capture

[Actual styled gallery](scroll-stack-gallery-0-21.png) is the shipped
ScrollStackGallery constructor rendered in an isolated 1,000 x 650 production
headless Window, with all eight demo stylesheets including the dark theme.
It is an actual C++ framebuffer capture, converted directly from PPM to PNG.
The contract fixture above remains separate from this interactive-panel capture.

PNG SHA-256: `0a7ff1302a5ec155102c4c2733d72cf518c268d6da2a8030ba2650314bec2301`.
