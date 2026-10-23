# ScrollBar / StackedWidget 0.21 verification

The implementation adds W15 standalone ScrollBar and W17 StackedWidget on the
existing foundation contracts. No dependency or new continuous timer is added.
Source and evidence commit identities and completed integration results are
recorded below as validation finishes; pending checks are not claimed as passed.

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

- Initial focused GNU 13 Release behavior run: **69 checks passed**.
- Final formatted GNU 13 Release: **41/41 passed**, 20.71 seconds, including
  software-GL GPU integration and rendering-profile smoke checks. The earlier
  complete development run also passed 41/41 in 19.39 seconds.
- No C++ compiler warnings observed. The existing libunibreak CMake developer
  scope warnings remain unchanged.
- ASan/UBSan and Windows CI: pending at this source/evidence checkpoint.

Software GL pixel checks establish rendering correctness and work accounting,
not a hardware GPU utilization or 60 FPS claim. Real assistive technology,
interactive IME and mixed-monitor acceptance remain on the existing platform
checklist. F10b document editing and F14 extraction remain deferred.

[Saved actual framebuffer pixels](scroll-stack-widgets-0-21.png) are converted
directly from the C++ probe PPM (760 x 520), showing the selected blue page and
its moved scrollbar; this is a contract fixture, not the interactive gallery.
PNG SHA-256: `4c0c706594c72feba5a3e3777f2ce0e9e132328a60629ba763dd5c28c637b853`.
