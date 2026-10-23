# Application workspaces (0.22)

This increment adds editable file/folder fields, an application shell, and
persistent tool panels. It reuses the existing GPU renderer, compiled CSS,
platform services, State bindings, actions, popup stack, layouts and scheduler.
There is no new runtime dependency.

The gallery's **Application workspace** page opens a real separate window. Its
menus and search field occupy the title row on Windows. The example contains a
central document view, overflowing toolbars, Project/Properties/Output panels,
file/folder controls and workspace save/restore commands. The compiled
[public API example](examples/ApplicationWorkspace.cpp) uses ordinary composition.

## Small application path

```cpp
#include <EStudioUI/MainWindow.h>
#include <EStudioUI/FilePathEditor.h>

void Build(EStudioUI::Window& window)
{
    using namespace EStudioUI;
    auto& main = window.Ui().Root().Add<MainWindow>(window);
    main.TitleBar().Menus().AddMenu("File").AddAction("Open…");
    auto& search = main.TitleBar().Content().Add<TextBox>();
    search.SetWidth(SizeRule::Fixed(220));
    search.SetPlaceholder("Search");
    main.AddToolBar().AddButton("Save");
    auto& properties = main.Docks().AddDock("properties", "Properties", DockArea::Right);
    properties.Content().Add<FilePathEditor>();
    main.Central().Add<Label>("Application content");
    main.Status().SetMessage("Ready");
}
```

Add the shell directly to its containing window's root. `MainWindow` supplies
zero root padding/gap so the title row reaches the client edge. The shell,
title bar and dock manager remain associated with that window. Application
content, panels and ordinary child compositions can be moved separately.

## Title bars and native behavior

`WindowTitleBar::Menus()` is the normal MenuBar. `Content()` is an ordinary
clipped horizontal widget container for search, labels, buttons and custom
controls. Menus and title content shrink/scroll inside their viewports; the
caption commands retain fixed space at the right. Wheel scrolling and focus
reveal keep clipped controls reachable. Blank title-row regions delegate to
native movement. `Widget::SetWindowDragRegion(true)` marks an exact hit target
as draggable; interactive children keep ordinary client input by default.

On Windows, the adapter extends the client frame while retaining the decorated
HWND, thick resize frame, system menu, and native minimize/maximize styles.
Prepared hit regions return `HTCAPTION`, `HTMINBUTTON`, `HTMAXBUTTON`, and
`HTCLOSE`; the operating system owns caption move/resize/commands. The maximize
region supplies Windows' Snap Layout hit-test protocol. The existing resize
pump continues servicing the normal capped scheduler inside native move/size
loops. Hit testing itself never lays out, advances frames or pumps events.

`Window::GetTitleBarSupport()` reports **Native** on Windows and **Emulated** in
headless verification. Other native platforms currently report **Unsupported**:
they keep the ordinary OS title bar and display the configurable UI title row
inside the client area, without duplicate minimize/maximize/close controls.
Actual Windows 11 Snap Layout flyouts, OS snap gestures, and physical
mixed-monitor DPI transitions require interactive verification; synthetic hit
tests and software GL are not evidence of those experiences or hardware FPS.

`SetTitle`, `Minimize`, `Maximize`, `Restore`, `IsMaximized`, `SetPosition`,
`SetWindowBounds` and `GetWindowBounds` expose the corresponding owner-side
operations. Desktop bounds are physical pixels. Native placement remains subject
to platform capabilities and work-area rules. The containing UI and controls use
logical coordinates. Headless windows provide explicitly emulated desktop
coordinates for deterministic drag tests.

## File and folder controls

`FileDialog::Open`, `Save` and `SelectFolder` take an attached logical Widget
owner and copied FileDialogOptions. They return the existing asynchronous
FileDialogSession. Destroying the logical owner cancels the request. Window
closure uses the existing platform owner/STA retirement rules; no nested loop
or GUI-thread wait is introduced.

`FilePathEditor` composes a normal TextBox, browse button and recent-path menu.
`FolderPicker` selects folders by default. `SetDialogOptions` selects open/save/
folder mode, title, filters, initial location and default extension. These fields
accept one path; standalone Open dialogs can allow multiple selection.

The canonical value is `std::filesystem::path`. Conversion preserves exact
single-line UTF-8, admits at most 65,536 bytes and rejects embedded NUL/control
characters or values the configured editor cannot represent. There is no
implicit normalization, existence check or filesystem scan. `Editor()` exposes
placeholder/read-only/validator customization; `CanSetPath` checks exact
representability. `BindPath(field, State<filesystem::path>)` participates in
normal final-candidate validation, coherent rollback and receiver teardown.
Application-specific existence/permission checks belong in background work.

Browsing coalesces repeated requests, tracks an owner-side session and ignores
accepted results after a newer edit/options change or transfer to another
context. Recent paths retain at most sixteen distinct nonempty entries, with
accepted selections placed first. `PathChanged` delivers owned snapshots with
bounded reentrant notification; `BrowseFinished` reports terminal picker
outcomes. A picker already started before a panel transfer remains owned by its
original native surface until completion/cancellation; its result cannot
silently overwrite the transferred field.

## Panels, ownership and layout

The initial DockManager layout has **four edge tab groups**: Left, Right, Top
and Bottom. Splitters resize the groups around `Central()`. Empty groups leave
layout; closed panels live in hidden storage and retain their content. A selector row appears when a group contains several panels; a single panel
uses its own header. Within each edge group, `Dock(panel, area, index)` controls tab order; `Tabify` inserts
beside an existing docked panel. This is a defined initial layout topology,
not an arbitrary recursive splitter tree or a document-tab workspace.

`AddDock(id, title, area)` admits at most 128 panels. IDs are unique printable
ASCII letters/digits/underscore/dot/hyphen, 1–128 bytes. Titles are independently
changeable single-line UTF-8, up to 4,096 bytes. `FindDock` resolves stable IDs.
Use `IsOpen` / `SetDockVisible` to query/change workspace membership; inactive
tab content is invisible to layout/input but its panel is still open.

`Float` moves the actual DockWidget into a real owned Window; `Dock` moves it
back. Header commands provide float/dock and close. A captured header grip drag
previews an edge target, docks on release near an edge, or floats outside those
targets. The separate native floating caption retains ordinary OS movement.
Automatic docking while dragging that native OS caption is not implemented;
use the panel's own grip or the dock command. Desktop grip dragging requires
screen-coordinate support (Windows/X11 and emulated tests); other platforms
can still use explicit dock/float commands.

Application widget identities, values, shared State bindings, selection/models,
actions and owned nested menus survive transfer. Input focus, capture, preedit
and active popup sessions retire before ownership changes. Owned popup widgets
and logical CSS ancestry migrate together. Font/layout caches retire when the
font service changes. Custom property animations owned by the old context end;
CSS presentation follows the destination stylesheet.

`Window::CreateOwnedWindow` shares the parent's GPU group, font configuration,
headless/native mode and compiled stylesheet. Floating windows also follow
later stylesheet replacements without reparsing during frames. Existing
OpenGLWidget scenes keep their separate shared context and producer/consumer
fences. Arbitrary cross-window transfers require compatible GPU share groups;
use CreateOwnedWindow rather than independent Window instances.

`Widget::ReparentTo` is owner-thread, idle-phase structural work. It rejects
roots, owned overlays, cycles, incompatible GPU groups and reentrant transfers.
A retained WidgetRef and a shared State binding follow a moved widget. A typed
UiHandle captured from the old context remains dispatcher-bound and reports
TargetExpired after a context transfer, including a later return to the original window. Use shared State for
long-lived worker data or obtain the destination's handle for direct delivery.
Keep application state in a store whose lifetime spans the workspace; a
floating window's private StateStore naturally expires when that host retires.

Native floating closure settles on the main owner's next safe queued turn:
the panel returns to hidden storage rather than losing its contents. Closing
the main window closes its owned floating surfaces. Retired native hosts are
released outside their close callback.

## Workspace state

`SaveState()` returns an owned text snapshot. `RestoreState(bytes, error)` fully
checks the signature/version, byte/count limits, numeric ranges, unique panel
IDs/tab positions and selected identities **before changing live state**.
Malformed/unsupported snapshots return false with a diagnostic and leave the
workspace unchanged. Unknown panel IDs are ignored, allowing applications with
optional panels to restore an older/newer workspace. Panels absent from the
snapshot retain their current state.

Version 1 saves edge placements, tab order/selection, open/floating state,
physical floating-window bounds and both splitter size sets. The payload is
limited to 64 KiB and 128 panel records. Application code chooses where/how to
store the string; no filesystem I/O occurs in DockManager. Native resource
creation or application callback failures during application are exceptions,
not a promise of rollback for OS failures. Main-window geometry, document tabs,
document contents, unsaved-state policy, monitor migration and arbitrary nested
dock trees are separate future work.

## Overflow, styling and work bounds

ToolBar keeps command helpers and concrete `Add<T>`, `AddChild` and RemoveChild
inside a clipped scrollable strip. `Content()` exposes the same container for
advanced composition. Left/right commands, wheel input and keyboard focus
reveal use the existing tab-strip offset algorithm. Root `GetChildren()` still
represents actual internal ownership; retain returned command references or
inspect Content instead of assuming child indices. TabWidget::SetTabBarVisible preserves page selection while hiding the selector
row and exposes group semantics in that mode. TabWidget::MoveTab changes
page/selector order without recreating pages or changing selected identity.

New containers and controls accept the same gradients, shadows, transforms,
state selectors and compiled CSS animation as existing widgets. See
[CSS hooks](CSS.md) and [working styles](assets/essential-widgets.css). Decorative
layers do not add native windows or child widgets.

There is no workspace timer, polling filesystem task, idle docking scan, text
rebuild or theme parse per frame. Panel count/persistence work is bounded by 128;
normal scrolling/focus only changes a local strip arrangement. Workspace scans
occur during explicit operations, serialization and stylesheet replacements.
Actual rendering checks compare retained/full pixels at narrow/wide sizes,
transfer shared GL surfaces, and verify zero settled repaint work. See
[verification](verification/WORKSPACES_0_22.md) for completed checks and limits.
