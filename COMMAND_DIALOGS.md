# Commands and asynchronous dialogs

The 0.19 command/dialog batch builds on the existing input and popup stack. There
is one event loop, one focus/capture pipeline and one GPU renderer. All direct
configuration/open/close/result operations run on the UI owner thread. Workers
use the existing typed handles/dispatcher; result callbacks may close windows,
remove widgets, open another presentation, or throw. Observer errors remain
inspectable on the returned session.

## Ordinary use

```cpp
auto& bar = ui.Root().Add<EStudioUI::MenuBar>();
auto& file = bar.AddMenu("File");
auto save = ui.CreateAction("Save sketch");
save->SetShortcut({EStudioUI::Key::S, EStudioUI::ShortcutModifier::Primary});
file.AddAction(save).SetIcon("S");
save->Triggered().Connect([] { /* save the application document */ });

auto& rename = EStudioUI::InputDialog::Create(bar, "Rename sketch", "Name", "Sketch 01");
file.AddAction("Rename").Clicked().Connect(
    [prompt = EStudioUI::WidgetRef(&rename)]
    {
        if (auto* live = dynamic_cast<EStudioUI::InputDialog*>(prompt.Get()))
            live->Open().OnFinished([](EStudioUI::InputDialogResult result)
            {
                if (result.m_Text)
                {
                    // Owned accepted UTF-8; safe even if the dialog has been destroyed.
                }
            });
    });
```

[examples/CommandDialogs.cpp](examples/CommandDialogs.cpp) compiles the ordinary
path against the actual public API. The **Menus & dialogs** gallery demonstrates
menus, nested actions, confirmations, validated input, modeless tools and an
anchored interactive popover.

## MenuBar and MenuItem

`MenuBar::AddMenu` returns an ordinary persistent `ContextMenu`. Existing
`AddAction`, shared actions, separators and nested `AddMenu` remain available.
`ContextMenu::AddAction` now returns the more capable `MenuItem&`, convertible to
the previously returned `ToolButton&`. Recompile consumers against the updated
headers. No event-loop or platform key normalization belongs in a menu widget.

Menus use normal buttons, not native OS menus. A bar is one Tab stop. Down,
Up, Enter and Space open its current entry; Left/Right switch eligible top-level
menus. Inside menus, Up/Down/Home/End select eligible actions, Right opens a
submenu, Left closes a submenu or selects the previous top-level menu, and
Escape dismisses the top presentation. Pointer-down opens/toggles top-level
entries; hovering another trigger switches while a menu is open. Commands close
their menu chain before execution and preserve an enclosing modal dialog.

Shared action shortcuts are associated with the bar before menus are first
shown; closing a menu does not disable its commands. The same action used by
several presentations is deduplicated by the existing shortcut resolver. Action
enabled/visible/checked changes remain canonical. `MenuItem` displays the first
formatted shortcut automatically; `SetShortcutHint` supplies a display-only hint
for a standalone item. `SetIcon` accepts short UTF-8 glyphs using registered
fonts. Since 0.20, `SetImageIcon(Icon)` supplies shared raster resources through
the existing leading slot; see [IMAGE_RESOURCES.md](IMAGE_RESOURCES.md). SVG
resources remain a future adapter.

There are at most 128 top-level menus per bar and 4,096 presentations per menu. Menus are intended for bounded
command sets and own one ordinary item presentation per action; they are not a
virtualized replacement for large searchable datasets. Parent menus retain
detached submenus until attachment; the context then owns them with weak logical
owner identities. Do not remove internal menu triggers through generic tree APIs.
Use `GetButton` for title/enabled/visible customization. Invalid indices and
externally removed composition parts are diagnosed.

## Popup placement and ownership

`Popup::Create(owner)` creates a root-owned surface with logical CSS/semantic
ancestry through the owner. Add ordinary widgets directly to the popup.
`Open(anchor)` follows the anchor through layout, CSS transforms and viewport
changes. Below/Above/Right/Left flip to the opposite side when it fits, then clamp
to the viewport. Explicit Fixed sizes and hard minima retain precedence; an
overlarge surface is clipped instead of silently violating its size contract.
Width is resolved before remeasuring wrapping height. `Open(Point)` retains the existing window-coordinate placement.
`PopupPlacement::Center` centers on the current viewport on every arrangement.
Directional placement requires a visible live anchor outside the popup, in the
same context. Removing/hiding it closes the presentation.

`Open` returns the current pending session when already open. It does not replace
a presentation or change its options implicitly; close and open to reconfigure.
`Close(PopupResult)` settles the branch. Existing `PopupSession` dismissal causes,
late subscriptions and contained callback errors apply. `UiContext::GetPopupSession`
observes a current presentation without retaining a widget/window.

The existing 64-layer depth limit still applies. `PopupOptions::m_AllowOwnerInput`
is an explicit nonmodal exception for an owned popup's logical owner subtree,
used by menu bars; it never bypasses a modal barrier. `m_Modeless` allows outside
pointer/focus interaction while keeping the presentation open, and cannot be
combined with `m_Modal`. Background shortcuts and Escape keep their focused
workspace behavior; command/popover surfaces can open above the modeless tool.
The existing single-chain policy remains: opening another unrelated modeless
surface replaces that chain; independent floating tool windows remain workspace work. Default popup behavior remains unchanged.

## Dialog and standard responses

`Dialog::Create(owner, title)` returns a persistent in-window surface with a
header, `Content()` and `Buttons()`. Add application controls to `Content()`;
ordinary layout strategies and code size rules apply. The header close button
rejects with Cancel. `Open()` is modal, centered and asynchronous. `Open(false)`
is modeless: outside controls stay interactive and the dialog remains open.
Neither form creates a native window, blocks the owner, pumps events or spins a
nested event loop. Native top-level dialogs/window decoration remain separate
workspace work.

`SetButtons` accepts unique `StandardButton` values and selects the first accepting
button as the Enter default. Use `SetDefaultButton` for a custom owned button, or
null to disable the default. Focus starts at `SetInitialFocus`, the first eligible
content control, or the default. Tab navigation stays in the dialog while it has
focus. A focused button retains its own Enter behavior. An editor with
Intermediate/Invalid validation cannot activate the default; IME preedit remains
in the shared composition pipeline. Content controls may consume a key in their
own preview hook before the dialog.

| Responses | Role | Automatic outcome |
| --- | --- | --- |
| OK, Yes, Save | Accept | Accepted with the stable response value |
| Cancel, No, Close | Reject | Rejected with the stable response value |
| Discard | Destructive | Accepted with the distinct Discard response value |
| Apply, Reset, Help | Their named role | Remains open; handle `Buttons().Clicked()` |

Captions can be localized/customized through `Buttons().GetButton(id)->SetText`;
identity and role do not depend on caption text. The standard row supports ten
unique responses. Candidate validation happens before existing buttons change.
Use `Dialog::SetButtons` to update the automatic default as well as the row.
`Accept(int)` and `Reject(int)` also support application-defined responses.
Sessions distinguish Escape, rejection, owner loss, parent closure and acceptance.

`MessageBox` adds a wrapping message, accessible description and
information/question/warning/error CSS classes. It defaults to OK; configure a
confirmation with Save/Discard/Cancel or Yes/No. It performs no application I/O.

`InputDialog` composes one ordinary `TextBox`. `Editor()` exposes the same
validator, placeholder, byte limit, selection, shaping and IME APIs as any other
single-line editor. Both OK and Enter require `ValidationState::Acceptable`.
`InputDialogSession` returns an `InputDialogResult` with the popup cause and an
optional owned accepted string. Cancellations never return partial text. Text is
copied once before acceptance callbacks, and previous results survive reopening,
editor removal and window destruction. This is not the deferred multiline
document editor.

## CSS and work

Ordinary root/descendant/decorative selectors, gradients, shadows, transforms and
transitions use the existing compiled CSS engine. Menu items remain `toolbutton`
nodes with `.menu-action`; their `::label`, `::icon`, `::shortcut`, `::mark` and
`::arrow` parts paint cached glyphs/analytic strokes. Top-level nodes are
`menubar` and `menu-trigger`; open triggers have `.menu-open`.

Dialogs use `dialog`, `dialog-header`, `dialog-title`, `dialog-close`,
`dialog-content`, `dialogbuttonbox`, `.dialog-default` and role classes such as
`.dialog-accept`/`.dialog-destructive`. Message/input nodes add `.message-box`,
`.message-question`/`.message-warning`/`.message-error`, `.input-dialog`,
`message-text`, `input-prompt` and `.dialog-editor`. Popovers use `popup`.
Menu/submenu triggers expose canonical Expand/Collapse actions. Both bundled themes supply defaults; `assets/command-dialogs.css` adds the rich
gallery presentation. No stylesheet parsing occurs during painting or animation
ticks. These controls request no continuous frames of their own; focused editor
carets and explicitly configured CSS motion retain their normal scheduling.

MenuBar and Dialog add semantic roles. Buttons/editors retain canonical actions,
modal availability and transformed/DPI bounds. Windows maps the roles to menu
bar/window control types; this does not claim a separate native window provider.
Human Narrator/installed-language IME/mixed-monitor acceptance remains tracked
in [verification/PLATFORM_ACCEPTANCE.md](verification/PLATFORM_ACCEPTANCE.md).
