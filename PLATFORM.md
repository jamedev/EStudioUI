# Platform services and widget semantics (0.15, initial F11 increment)

Use `window.Platform()` or `ui.Platform()` for native services. A window installs
its adapter automatically; standalone contexts default to deterministic memory
services. No additional third-party library is required. Windows integrations
use the Windows SDK. This increment does **not** finish F11 or Gate B.

## Supported adapters

| Service | Windows native window | Other native GLFW backends | Headless/memory |
| --- | --- | --- | --- |
| UTF-8 text clipboard | Native CF_UNICODETEXT | GLFW text clipboard | Emulated |
| HTML, file paths, custom MIME clipboard | Native CF_HTML/CF_HDROP/registered formats | Unsupported | Emulated |
| Open/save/folder pickers | Native IFileDialog on a dedicated STA | Unsupported | Scripted results |
| Incoming file drops | Owning GLFW callback payload | Owning GLFW callback payload | Injected shared input |
| Standard cursors | Native GLFW cursors | Native, subject to backend availability | Emulated |
| Window icons | Native | Unsupported on Cocoa/Wayland | Unsupported |
| Logical sizing and input scaling | Native | Backend-specific native mapping | Emulated |
| Physical desktop coordinates | Native client-to-screen | X11; unsupported on Cocoa/Wayland here | Unsupported unless explicitly injected |
| Owned windows | Native HWND owner plus logical close relationship | Logical close relationship, reported emulated | Emulated |
| Basic widget accessibility | Initial Windows UI Automation bridge | Unsupported | Semantic queries only |
| Drag source / IME composition | Unsupported in this increment | Unsupported | Unsupported |

`GetCapabilities()` distinguishes `Native`, `Emulated`, and `Unsupported` per
`PlatformFeature`. Individual calls can still fail or be busy. Check their
owned status/diagnostic instead of assuming that a capability guarantees success.
GLFW-reported cursor/focus errors remain observable through operation results.
A successful focus request does not guarantee OS foreground activation. Richer clipboard writes on the text-only adapter return Unsupported;
they do not discard formats silently. Manual/native readiness checks remain
separate from deterministic adapter tests.

## Clipboard values

```cpp
EStudioUI::ClipboardData data;
data.m_Text = "Project name";
auto result = window.Platform().WriteClipboard(data);
auto snapshot = window.Platform().ReadClipboard();
```

`ClipboardData` owns optional text and HTML, filesystem paths, and a MIME-to-byte
map. Text is strict UTF-8 without embedded NULs. Service reads and writes use LF;
Windows native text uses UTF-16/CRLF. HTML is an owned fragment, not a trusted
rendering instruction. Registered application formats use
`EStudioUI.MIME:<mime>` with a bounded binary envelope and explicit byte length,
including empty/binary-zero payloads. They are an EStudioUI interchange contract,
not automatic support for arbitrary registered clipboard formats. MIME keys use
canonical ASCII lowercase on reads/writes; case-insensitive duplicates and reserved
text names are rejected before publication. Keys are a token type/subtype without
parameters. Native reads also tolerate the registered prefix's original case.

Validation happens before native mutation. The aggregate budget is 8 MiB, with
at most 512 file paths and 32 custom formats. Windows file paths must be absolute;
legacy ANSI CF_HDROP is diagnosed rather than decoded with an implicit locale.
Native reads bound external allocations and reject malformed payloads.
Opening a busy clipboard returns Busy immediately, without sleeping on the UI
owner. After Windows empties the clipboard, an OS publication failure can leave
earlier formats present; its diagnostic states this. No atomic cross-format OS
transaction is promised.

The previous `UiContext::GetClipboard/SetClipboardText` convenience path delegates
to these services unless an explicit compatibility override is installed. Explicit
service results expose native failures; the convenience read returns empty text
on failure and leaves the diagnostic in `GetLastResult()`.

## Asynchronous native file dialogs

```cpp
auto session = window.Platform().ShowFileDialog({});
session.OnFinished([](const EStudioUI::FileDialogResult& result)
{
    if (result.m_Status == EStudioUI::FileDialogStatus::Accepted)
    {
        // Use result.m_Paths, which owns the selected filesystem paths.
    }
});
```

`FileDialogOptions` selects OpenFile, SaveFile, or SelectFolder and owns the title,
initial folder, save filename/extension, filters, and optional multiple selection.
Invalid combinations fail before opening anything. The compiled
[example](examples/PlatformServices.cpp) shows a weakly observed result caption.

A session settles once as Accepted, Cancelled, Unsupported, Busy, OwnerClosed,
or Failed. `Cancel()` requests native stop and immediately settles the logical
result. Native cleanup continues asynchronously. Late observers run immediately
on the owner. Callback references last through that callback; copy the result or
its paths to retain them. Observer errors are contained and available through
`GetCallbackError()`. Sessions have no blocking Wait and create no nested framework
event loop. Completion observers should observe widgets weakly or use typed UI
handles. There are at most eight pending sessions per adapter and 256 observers
per session. A Windows window has at most one active native picker; a new request
can be Busy while a cancelled picker's STA still unwinds.

Windows creates/configures/shows the real IFileDialog on a dedicated COM STA.
Its cancellation message runs on that STA. Windows can disable the picker owner
while its modal native dialog is shown; other framework windows continue through
the shared application loop. A reserved result slot survives dispatcher overload;
the queue publication is a wake hint, not the only owner of the result.

Window closure first settles logical sessions and hides/closes the surface.
The HWND, adapter and GLFW runtime remain alive until native workers release
them. Replacing an adapter similarly retains unwinding adapters (at most eight)
instead of joining an active native picker on the UI owner. The ordinary
`Application` loop handles this. Advanced manual hosts must keep pumping native
messages and `ui.PumpPlatformServices()` until `ui.HasPendingPlatformWork()` is
false before destroying their native owner or context. They must not destroy a
standalone UiContext with outstanding native work. Custom native adapters must
call Close while their derived cancellation state remains alive, and accurately
report NativeWorkPending until all native-owner-dependent work has unwound.

## File drops, cursors, metrics and ownership

`widget.SetAcceptDrops(true)` enables `FilesDropped()`. `FileDropEvent` owns paths,
position, modifiers and source timestamp. Native paths are copied before the GLFW
callback returns. Drops use the same transformed hit testing, preview and logical
bubbling as shared input, stop at popup/modal barriers, and are capped at 512
paths/8 MiB. SetAcceptDrops is opt-in; a plain widget does not consume a drop.
This is incoming file transfer, not an outgoing drag-and-drop protocol.

`widget.SetCursor(CursorShape::Hand)` supplies an inheritable standard pointer
shape; nullopt restores inheritance. The nearest hover ancestor wins, including
logical popup ancestry. TextBox defaults to the text cursor. Explicit native
`SetCursor` and validated RGBA8 `SetWindowIcon` calls are also available. Icon
images are at most 1024 by 1024 pixels.

Widget geometry, `WindowOptions` dimensions, `Window::SetSize`, and shared input
use logical UI units. `WindowMetrics` reports the logical client, framebuffer,
content scale, native input scale, physical pixel scale and optional desktop
origin. Windows/X11 physical client input is normalized by content scale. Cocoa
and Wayland GLFW client units are already logical. The framebuffer scale stays
independent; do not divide coordinates by DPI twice. `FromNative`, `ToScreen`,
`FromScreen` validate finite values, handle negative monitor origins, and reject
unavailable desktop conversion. Mixed-monitor hardware evidence is still needed.

`WindowOptions::m_Owner` establishes native ownership on Windows and a portable
weak close relationship. The parent must be live at creation; closing it closes
the child. This does not transfer C++ ownership: the application keeps its normal
unique_ptr/stack owner. A closed child can remain a C++ object safely while the
native parent is later destroyed.

## Semantic queries and custom controls

```cpp
#include <EStudioUI/SemanticTree.h>

editor.SetAccessibleName("Project title");

auto& tree = window.Ui().Semantics();
auto node = tree.GetNode(tree.GetId(editor));
```

Semantic identities are lifetime unique and independent of CSS identifiers or
addresses. Nodes own role/name/help/value/state/actions, physical/logical parent
identity, transformed axis-aligned clipped bounds, optional desktop bounds and
actual focus. Physical paint records never become semantic nodes. Owned popups
appear under their logical owner, closed/hidden popups leave child navigation,
and modal barriers disable underlying semantic actions. HitTest uses the exact
shared transformed/rounded input geometry; reported rectangles are conservative
axis-aligned bounds when rotated or rounded clipping is involved.

The service builds an index of actual widgets once when first requested, then
maintains it on attach/detach. It never creates collection-row/canvas-item nodes
or rebuilds a tree on each frame. GetChildren pages are capped at 256 identities;
paging scans the actual sibling list up to the requested offset. No paint,
Prepare, style resolution, or layout is triggered by a query. Geometry describes
the last prepared frame. Direct queries/actions belong to the UI owner and are
rejected during input/frame traversal or staged shared-state updates.

Built-in buttons, checkable tool buttons, text boxes, sliders and numeric controls
expose canonical activation/toggle/value/range/focus actions. Values respect
read-only/validator/range policies. An invalid numeric value is rejected, not
silently clamped. Deleting a target expires its identity; a replacement cannot
inherit stale native provider access. Other existing families have container
roles but no advertised virtual-row/grid/selection patterns yet.

Custom controls override the protected `DescribeSemantics()` and
`OnSemanticAction()` hooks, alongside their existing paint/input extension points.
Metadata hooks are pure inspections: no UI/shared-state mutation or event pumping.
Structural/traversal/state guards diagnose unsupported reentrancy and restore
phases on exceptions. Explicit accessible name/help/role overrides apply above
hook defaults. Keep action masks truthful and implement actions through the same
canonical behavior used by pointer/keyboard commands.

## Initial Windows accessibility bridge and verification

Real Windows windows attach a WM_GETOBJECT UI Automation fragment provider.
Native clients can navigate actual widget nodes, read stable runtime identities,
names, state and physical bounds, hit-test/focus, and use basic Invoke, Value,
RangeValue and Toggle patterns. Client-thread requests cross typed dispatcher
handles; they never read widgets from a worker. Queries/synchronous value edits
have a one-second bounded dispatch wait. Pending timed-out requests are cancelled;
an already-running callback cannot be interrupted. Invoke enqueues canonical activation; Toggle and value/focus requests commit
through the synchronous owner path. Closed queues/windows expire retained providers before native teardown.

The bridge tracks at most 64 previously queried nodes for initial name/value/
enabled/offscreen/range/toggle events and actual focus changes. It checks a cheap
semantic revision and performs no semantic scan when no clients listen. This is
an initial bounded event implementation, not complete notification/TextPattern/
virtualization support or a screen-reader-readiness claim. Native window/COM
thread reentrancy remains guarded; queries made during an unsafe owner traversal
return an explicit native failure rather than pumping a nested loop.

The **Platform services** header command opens the working gallery. Its
self-test uses injected services, production file-drop callbacks, owned windows,
semantic work counters and an actual framebuffer capture. The ordinary build
also compiles the public platform example.

On Windows, NativePlatform uses a real UI Automation client on an MTA thread
while the owner pumps its ordinary messages/dispatcher. The optional native
clipboard/picker probe actually changes the OS clipboard and opens/cancels
native dialogs. Enable it explicitly:

```powershell
$env:ESTUDIOUI_NATIVE_PLATFORM_TESTS = '1'
./Build.ps1 -Configuration Release -SkipGpuTest
Remove-Item Env:ESTUDIOUI_NATIVE_PLATFORM_TESTS
```

Windows CI opts into those probes. Normal Build.ps1 still runs the hidden-window
UI Automation probe without changing the clipboard or opening pickers. See
[verification](verification/PLATFORM_0_15.md) for measured results. IME/composition
with F10a, virtualized semantic children, remaining UIA events/patterns, real
Narrator/other assistive-technology sessions, mixed-DPI monitor migration and
native hardware GPU timing remain open acceptance work.
