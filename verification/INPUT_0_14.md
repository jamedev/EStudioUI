# Input, shared actions, focus, and popup sessions / 0.14 verification

Recorded 2026-10-22. Public contracts: [INPUT.md](../INPUT.md).
Complete compiled application: [examples/InputActions.cpp](../examples/InputActions.cpp).
The demo opens **Input & actions** by default.

## Local results

| Check | Observed result |
| --- | --- |
| Linux GCC 13 Release | 32/32 CTest suites pass, including actual framebuffer integration |
| AddressSanitizer + UndefinedBehaviorSanitizer | 32/32 CTest suites pass, including the actual renderer |
| Isolated CPU ThreadSanitizer | 6/6 suites pass, including ActionDomain |
| InputFoundation | 86 input/action/focus/popup checks pass |
| ActionDomain | 140 headless command/lifetime/queue checks pass |
| Complete input example | EStudioUIInputExample builds with the normal demo configuration |
| Renderer used locally | GLFW null platform + OSMesa software OpenGL; no hardware GPU throughput claim |

Raw records: [Release](input-release-tests.txt),
[ASan/UBSan](input-sanitizer-tests.txt), [TSan](input-thread-tests.txt),
[native-adapter/framebuffer report](input-foundation-report.txt), and
[generated-output recovery](input-generated-recovery.txt).

Several zero-byte generated object files and an invalid generated executable
were encountered during sanitizer compilation on this host. Those outputs were
removed and rebuilt; generated ELF headers were checked before the final run.
The final build/link succeeds and all 32 instrumented suites pass. The recovery
record preserves this distinction from source/runtime failures. LeakSanitizer is
disabled here (`detect_leaks=0`) because of the host's process-tracing restriction;
these checks do not establish leak freedom or instrument Mesa/other dependencies.

## Command and input contracts

- One shared action updates toolbar, button, menu, and split-default presentation
  through coherent validated State/StateStore revisions. Invalid candidates do
  not replace the previous command state.
- Duplicate presentations execute a shortcut once. Widget and nearest-subtree
  precedence, exact modifiers, disabled/hidden associations, repeat policy, AltGr,
  ambiguity, and expiry are checked. An isolated 10,000-widget fixture examines
  exactly one surviving registered association and performs no extra intrinsic
  measurement or style resolution for its command dispatch.
- A worker queues 64 ordered action requests in the headless test. All complete
  once on the owner, in producer order. Wrong-thread execution is rejected;
  receiver/domain expiry suppresses queued work. Command failure remains on its
  receipt and the execution guard recovers.
- Owning events preserve logical/physical keys, scan codes, timestamps, and pointer
  identity/device/button/pressure. Embedded OpenGL scenes retain complete keyboard
  metadata. Transformed forward/inverse coordinates and keyboard context-menu
  anchoring agree.
- Local Tab scopes, modal barriers, nested menus, top-layer dismissal, fallback
  focus, callback destruction, asynchronous results, observer errors, replacement
  reentrancy, deactivation, and context shutdown are checked. A destroyed wrapper
  or popup is not retargeted to a replacement object.

The headless action suite compiles against the private isolated CPU test runtime,
without GLFW/font/render sources or native initialization. This is evidence for
the generic command boundary, not an installed modular SDK; F14 remains deferred.

## Actual renderer and native-adapter checks

The real C++ renderer draws the input page and a modal panel with an owned chooser
above it. The following image is a lossless transcode of its 1180 x 820 PPM output:

![Shared commands and nested modal chooser](input-foundation.png)

The retained framebuffer and a full redraw match exactly for the stacked
presentation. The Apply button settles the modal response and restores launcher
focus. After the page settles, an unchanged repaint submits zero retained GPU
instances. This does not mean zero CPU recording work.

The integration probe invokes the installed production GLFW key, character,
pointer, and focus callbacks. It verifies scan-code/modifier/time preservation,
committed Unicode text, queued reentrant native input, cancellation of deferred
input after native focus loss, rejection of nested native event pumping, and
input-time resize deferral followed by correct geometry on the next real frame.
Native focus loss during painting is also deferred and applied after traversal.
Existing constraint/live-resize, rich CSS, canvas, model, and shared-context GPU
probes also pass in the complete suite.

Callback invocation through a headless adapter does not validate a real keyboard
layout, native IME candidate placement, assistive technology, mixed-monitor DPI,
pen/touch hardware, or Windows GPU drivers. Those remain explicit F11/F10a/F15
platform work. No runtime dependency was added for F09. The Windows CI workflow
uses the existing one-shot PowerShell build for MSVC Release/Debug; inspect the
checks on the published revision for its platform results.
