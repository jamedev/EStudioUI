# Windows custom title-bar correction

This correction follows the reported extra native caption and displaced caption
buttons in EStudioUICreator. It applies to every `MainWindow` using the same
Windows backend.

## Frame and input ownership

The HWND retains its native caption/resize/minimize/maximize/system-menu styles.
The custom client calculation removes the standard caption inset, returns zero
instead of preserving native caption pixels, and retains a one-physical-pixel
restored top border. Activation and themed-caption repaint handling prevent a
native caption from reappearing over the GPU content. DWM continues managing the
frame shadow; the native move/size loop uses the existing paced scheduler.

Prepared widget geometry supplies hit testing, including the complete painted
caption-button rectangles. DWM's standard button rectangles cannot override
these. Non-client button presses become owning framework pointer events, with
native capture for moves/releases outside the caption. Cancellation, capture
loss, deactivation, receiver expiry and callback-driven retirement cannot leave
a pressed control or a native capture behind. Ordinary client controls, title
dragging and native system menus retain their existing paths.

Native caption presses preserve client keyboard focus and active text preedit
through the owning pointer event policy. Caption controls are not client Tab
stops. Regular client presses keep their normal focus behavior.

Button callbacks execute the containing window's commands. A maximized window
displays a restore symbol and exposes `Restore window` in accessibility metadata.
The title-row commands are adjacent, and the bundled close-button press uses the
same red family as its hover state. No window-title font glyph dependency is
introduced.

Frame changes preserve client dimensions. Custom-frame `SetSize()` uses actual
client/outer differences instead of GLFW's standard caption-height calculation.
Maximized client calculation preserves auto-hidden taskbar reveal edges.
Client insets are bounded by the proposed native rectangle, including empty
and tiny minimize-transition frames, so these transitions cannot publish
negative client or framebuffer dimensions. Invalid native metric diagnostics
include the raw client/framebuffer dimensions and content scale.

## Verification

The final source revision and Windows/Linux CI run will be recorded once the
native minimize regressions complete on `feature/canvas-sketcher`.

Local GNU 13 Release passes all 46 suites in 19.06 s, including actual
retained/full GPU framebuffer, workspace and focus/composition checks.
Focused AddressSanitizer/UndefinedBehaviorSanitizer verification passes
`CreatorGpuIntegration`, `WorkspaceCore`, `InputFoundation` and `GpuIntegration`
(4/4, 65.23 s; leak detection disabled for this graphics run).

A MinGW-w64 C++20
cross-syntax pass compiles the Windows branches of `NativeWindowChrome.cpp`,
its native tests, the GLFW caption probe, `Window.cpp`, `WindowTitleBar.cpp`
and `WindowCommandButton.cpp` with `-Wall -Wextra -Wconversion`, without warnings.
This compile check is separate from execution on Windows.

The expanded native test checks actual HWND client origin and dimensions, native
resize corners/styles, system maximize/restore, caption-paint suppression,
prepared button hit roles, owning pointer sequences, outside motion/release,
cancellation, capture loss, callback retirement and receiver expiry. It also
checks tiny/empty client calculations, actual minimize/restore dimensions and
scheduler ticks inside a real `SC_SIZE` modal loop.

Both the gallery and Creator native GPU self-tests now run an isolated owned
GLFW/MainWindow probe. It checks the original application's client origin and
the isolated frame, embeds a real editor in the title row, and sends native
caption messages through the production adapter. It verifies painted hover and
press state, outside release/cancellation, and single-click native minimize,
maximize, restore and close. Captures come from the actual C++ renderer.
Headless workspace checks exercise the restore symbol and accessible name.
GPU captures contain client content; removal of the native caption is checked
separately against the actual HWND outer/client origins, not inferred from a
headless image. Native probes use the window's normal monotonic clock so native
callbacks cannot invalidate a separate manual test clock.

Windows CI with the final minimize regressions is pending.
Windows 11 Snap flyout appearance, physical snap gestures and actual
mixed-monitor DPI transitions remain interactive acceptance checks; the native
hit-test contract alone does not establish those experiences.

## Primary references

- [Chromium HWND message handling](https://chromium.googlesource.com/chromium/src/+/main/ui/views/win/hwnd_message_handler.cc): custom-frame hit testing, activation, caption repaint suppression and client input ownership.
- [Microsoft: Custom Window Frame Using DWM](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe): client calculation, frame changes and native non-client contracts.
- [Microsoft: Support snap layouts for desktop apps](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-snap-layout-menu): the custom maximize button's `HTMAXBUTTON` contract.
- [rossy/borderless-window](https://github.com/rossy/borderless-window): native frame bookkeeping, activation repaint suppression and auto-hide appbar handling.

The implementation uses the existing Win32/DWM backend. Runtime dependencies are
unchanged; these projects serve as references rather than bundled components.
