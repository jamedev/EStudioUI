# Initial F11 platform and semantic services / 0.15 verification

Recorded 2026-10-22. This is an initial working F11 increment, not completion of
F11 or Gate B. API contracts, capability distinctions and remaining scope are in
[PLATFORM.md](../PLATFORM.md).

## Local evidence

| Check | Observed result |
| --- | --- |
| GCC 13 Release, Linux | 34/34 suites, including actual GPU renderer integration |
| Platform contract fixture | 51 checks: UTF-8/formats/budgets, coordinates, asynchronous sessions, cancellation, observer errors, queue overload, owner/queue closure, adapter replacement and native host failures |
| Semantic fixture | 30 checks: roles/names/values/actions, focus, custom hooks, safe deletion/identity, popup hierarchy/modal barriers, query purity guards and DPI desktop bounds |
| Actual platform gallery | Injected rich clipboard and picker update a live GPU caption; production GLFW drop callback copies UTF-8 path storage; actual green drop-area pixels checked |
| Semantic query work | 50 actual-widget queries cause zero style resolution or layout recalculation |
| Native resource retirement fixture | Deleted C++ Window retains its native adapter while simulated native work is active, then releases it on a safe owner turn after unwind |
| Owned window fixture | Closing a parent closes a separately owned secondary Window while its C++ owner remains alive |

Raw results: [Release suites](platform-release-tests.txt),
[gallery report](platform-gpu-report.txt). The actual C++ renderer capture below
uses GLFW's null platform and OSMesa/llvmpipe, then lossless PNG conversion; it is
not a native OS picker screenshot or hardware GPU benchmark.

![Actual GPU platform gallery](platform-services.png)

## Native Windows and sanitizer acceptance

The Windows pipeline compiles and tests Release/Debug through the existing
Build.ps1 x64-windows-estudioui flow. The new NativePlatform executable uses a
real Windows HWND and a real UI Automation client on an MTA thread. It reads
actual semantic names and Invoke/Value/RangeValue/Toggle patterns, verifies
canonical widget updates and rejects retained-provider access after closure.
The UI owner pumps its ordinary messages and owning dispatcher throughout.

Windows CI sets ESTUDIOUI_NATIVE_PLATFORM_TESTS=1. This additionally publishes
and reads actual CF_UNICODETEXT, HTML, CF_HDROP and custom binary formats,
checks immediate Busy on another thread's clipboard lock, rejects malformed
external HTML, shows each real Open/Save/Folder picker before cancelling it,
and retains a replaced adapter until its STA unwinds. These are real native
integrations; the ordinary developer test run omits the clipboard-changing and
visible-picker sections unless explicitly opted in.

Sanitizer and native CI outcomes are recorded after those runs complete. The
source includes these checks; their presence alone is not a passing result.

## Remaining acceptance work

- IME/composition and caret/input geometry integrated with F10a font/text work.
- Virtualized list/tree/table/canvas semantic children, selection/grid/scroll and
  text patterns; complete structure/bounds/event notification coverage.
- Real Narrator and other assistive-technology sessions for representative apps.
- Native multi-monitor DPI migration, desktop focus/ownership scenarios and
  developer-PC Windows GPU checks through normal Build.ps1.
- Broader platform implementations and hardware performance evidence under F15.

There is no per-frame full semantic-tree rebuild, no per-collection-row widget
expansion and no new third-party runtime dependency. A Windows accessibility
capability reports an attached initial native bridge, not complete application
or screen-reader readiness.
