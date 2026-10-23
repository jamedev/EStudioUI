# Command/dialog batch verification (0.19)

Delivered: W24, W26, W27 and W28, with glyph icons and in-window dialog surfaces.
[COMMAND_DIALOGS.md](../COMMAND_DIALOGS.md) records ownership, keyboard behavior,
size/resource bounds and the remaining native-window/image/workspace scopes.
The compiled [public example](../examples/CommandDialogs.cpp) uses the delivered
API, and the interactive **Menus & dialogs** tab starts idle.

Local GNU 13 Release: **38/38** CTest suites pass, including **68** command/dialog
checks and real software-GL framebuffer integration (14.51 seconds total).
No compiler warnings. Executable ELF headers were checked before this run; an
environment-generated empty demo executable was discarded and relinked, rather
than treating its empty invocation as GPU evidence.

CPU coverage includes anchored edge flips, live transform placement without
intrinsic remeasurement, explicit fixed-size preservation, weak anchor/owner
expiry, one session per presentation, callback deletion/errors, default/cancel
responses, editor validation, nested popovers, modeless workspace interaction,
Escape and focus, owned input results after reopening/removal, menu pointer and
keyboard navigation, disabled actions, submenu chains, persistent shortcut
association/rebinding and canonical semantic actions. The shared input/phase,
font, composition and lifetime regression suites remain enabled.

Actual renderer probes cover menus with glyph/shortcut/arrow content, rich
input/confirmation dialogs, interactive popovers, intermediate resize and light
style composition. Menu/popover retained pixels match full redraws exactly.
Static gallery and open menu frames have zero repaint pixels; no continuously
requested animation frames. Bounds and an actual visible anchor are checked.
Driver: Mesa llvmpipe software GL. This proves pixels and bounded work, not
hardware GPU utilization or 60 FPS.

ASan/UBSan: **38/38** suites pass, including the real GPU probe (86.06 seconds
total; GPU 32.85 seconds). Leak detection is disabled under host tracing; native
third-party libraries/Mesa are uninstrumented. Tested code: `d1faaa8`.
A confirmed zero-byte generated Style object was rebuilt before sanitizer checks.
Linux CI also passes **38/38** Release/software-GL suites and **6/6** isolated
ThreadSanitizer suites. Windows compiled the library without warnings; its new
native test required an explicit owned BSTR for the UI Automation value setter.
That Windows-only test correction is being reverified in CI.
[CI run](https://github.com/jamedev/EStudioUI/actions/runs/39902816587).

NativePlatform includes actual Windows UI Automation menu-bar/expansion,
dialog-role/modal availability and input-value/acceptance probes. Hosted Windows
CI explicitly skips GPU; Build.ps1 defaults to a native GPU check on developer
machines. Human installed-language IME, Narrator and mixed-monitor acceptance
remain in [PLATFORM_ACCEPTANCE.md](PLATFORM_ACCEPTANCE.md).


![Actual command gallery](commands-gallery-0-19.png)

![GPU command menu](commands-menu-0-19.png)

![Validated input prompt](commands-input-0-19.png)
