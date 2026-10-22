# F11 composition and virtual accessibility / 0.16

Recorded 2026-10-22. This increment extends the 0.15 native-service baseline with
the shared text-composition protocol, GPU preedit presentation, Windows IMM input,
virtual collection/canvas semantics and additional Windows UI Automation patterns.
Public contracts and explicit budgets are in [PLATFORM.md](../PLATFORM.md).

## Verification status

The final source passes local Release verification. Sanitizer and native Windows
verification are in progress; their results are recorded only after completion.

| Check | Status |
| --- | --- |
| GCC 13 Release and actual renderer integration | 35/35 suites passed; final rebuild has no compiler warnings |
| Semantic fixture | 169 checks passed, including 100,000-row bounded queries and focus-callback dataset replacement |
| Portable text input | 36 checks passed |
| AddressSanitizer + UndefinedBehaviorSanitizer | Pending |
| Windows MSVC Release and Debug | Pending |
| Repeated native cancellation/COM teardown | Pending |
| Real UI Automation collection/text/event clients | Pending |
| Actual HWND/IMM protocol probe | Pending |
| Interactive installed-language IME conversion | Not tested in this host |
| Narrator / other assistive technology | Not tested in this host |
| Physical mixed-monitor DPI migration | Not tested in this host |

## Regression scope

Raw results: [Release](platform-0-16-release-tests.txt),
[actual renderer report](platform-0-16-gpu-report.txt).

![GPU preedit with canonical text kept separate](platform-composition-0-16.png)

The capture is the actual C++ framebuffer, losslessly converted from PPM. It uses
GLFW's null platform and OSMesa/llvmpipe; the displayed composition is injected
through the shared protocol, rather than a screenshot of an installed Windows IME.

- Temporary preedit does not mutate committed text, selection, validation or undo
  history. Commit is one canonical validated edit. Cancel/focus loss/programmatic
  replacement/owner closure expire the old session. UTF-8/UTF-16 boundaries,
  read-only controls, custom-client purity and bounded payloads are exercised.
- The actual C++ renderer checks that preedit changes pixels, cancellation
  restores the editor, and one undo reverses a commit. Virtual list/table reveal,
  hit testing, selection and editing are compared with actual geometry and pixels.
- A 100,000-row computed model records bounded item accesses for sampled semantic
  queries, without per-row widgets or style/layout work. Stable item identities
  survive sorting/filtering/moving; deletion, dataset replacement and owner death
  expire retained IDs. Tree, table, tab and canvas paths share canonical actions.
- Real Windows UI Automation probes exercise control actions, virtual selection,
  realization, tree expansion, grid cells, single-line text ranges, Unicode
  navigation and retained-provider expiry. Native provider methods marshal to
  the UI owner; clients do not read widgets on worker threads.
- The native input probe uses real HWND and IMM APIs with injected Win32 messages.
  It proves protocol/lifetime behavior, not a human language conversion session
  or the visual behavior of every installed Windows input method.

## Native picker timeout investigation

The 0.15 verification had a Windows Debug native-test timeout. A diagnostic run at
`2648c9af82e397c76e3a4b00d26a4e742a0ce378`
([CI 39855143350](https://github.com/jamedev/EStudioUI/actions/runs/39855143350))
also reproduced a Release timeout. Its flushed stages show that UI Automation
actions, provider closure, COM client teardown, clipboard checks, and open/save
picker cancellation completed. The last stage was folder-picker cancellation.

Cancellation now remains armed on the picker STA until Show returns, including
initialization callbacks and timer retries. This addresses a cancellation request
arriving before the shell is ready to close; that timing explanation is an
inference from the code and stage trace. The final native run must establish
whether the fix resolves the observed hang. Tests use a known local initial
folder and report a failed assertion before waiting for native cleanup.

## Acceptance boundary

Follow [native acceptance](PLATFORM_ACCEPTANCE.md) for interactive IME, Narrator,
multiwindow focus and physical mixed-DPI observations. A direct UI Automation
client is a real native integration test, but it is not a screen-reader session.
Software OpenGL/sanitizers do not establish native driver speed or hardware FPS.

F10a font resolution/fallback and complete script/direction shaping remain next.
F10b document storage/editing, outgoing drag sources, additional platform adapters,
and broader F15 hardware evidence retain their separate scopes. Canvas semantics
identify top-level persistent scene items; immutable geometry inside a group is
part of that independently selectable group, rather than a new persistent item.
