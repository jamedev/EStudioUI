# Input, actions, focus, and asynchronous popups

F09 is the 0.14 input foundation. It adds native-independent owning events,
transactional shared commands, scoped shortcuts, local focus scopes, and a bounded
popup/modal stack. Existing widget hooks remain supported through adapters.
F11 native platform/accessibility services and F10a font/composition services
remain separate milestones. This is not a new document or dialog widget family.

The demo opens **Input & actions** by default. It contains shared toolbar/menu
commands, an editable field, a local Tab scope, nested menus, and a modal panel
assembled from existing widgets. [examples/InputActions.cpp](examples/InputActions.cpp)
is a complete application compiled as `EStudioUIInputExample` with the demo build.
The example expects `assets/NotoSans.ttf` relative to its working directory.

## One command, several presentations

```cpp
auto save = ui.CreateAction("Save document");
save->SetShortcut({EStudioUI::Key::S, EStudioUI::ShortcutModifier::Primary});
toolbar.AddAction(save);
menu.AddAction(save);
button.SetAction(save);
```

Connect business behavior to `save->Triggered()`. The same command may appear in
several buttons, toolbars, menus, or split-tool choosers. `Button::Trigger()` and
`Action::Trigger()` are the programmatic activation paths; manually emitting a
legacy `Clicked` signal only delivers that signal.

`ActionState` contains text, enabled/visible state, checkable/checked state, and
shortcut alternatives. `Action::Properties()` is the ordinary `State<ActionState>`
from [STATE.md](STATE.md), so the existing validation, equality, transactions,
bounded notification rounds, revisions, subscriptions, and worker publication
contracts apply. `SetCheckable(false)` also clears checked state. An invalid
shortcut or checked noncheckable value rejects the complete candidate.

Use `ui.States().Batch(...)` to change several properties/actions coherently.
Trigger commands after the batch commits. Command execution during a state update
is diagnosed rather than exposing an incomplete application state. A checkable
action commits its toggle before `Triggered`; recursive execution of the same
action returns false. Direct listener exceptions propagate with the normal signal
semantics, and the execution guard is restored after failure.

`CreateAction` uses the context's state domain. For a longer-lived shared domain,
construct `Action(StateStore&, text)` with an application-owned store; this needs
no window initialization. The action and its store remain owner-thread objects.
If the store expires first, presentations disable themselves, shortcuts skip the
expired command, and `Trigger()` returns false. Snapshot/property reads on the
expired domain are diagnosed. `GetStopToken()` remains queryable on the owner.

For a worker command request, capture a handle on the owner, then use it without
touching the action or its state:

```cpp
auto handle = save->GetHandle();
// The handle can now be copied into a worker.
auto receipt = handle.Post([](EStudioUI::Action& live) { live.Trigger(); });
```

Delivery is queued even when posted by the owner. Wrapper/domain expiry prevents
late resolution; no command retains or retargets a destroyed action. Receipts
report overload, cancellation, expiry, callback errors, and completion using the
existing [THREADING.md](THREADING.md) contracts. Ordered command requests are not
automatically coalesced.

## Shortcut resolution

Each `Shortcut` defines a key, exact `ShortcutModifier` requirements, logical or
physical matching, and optional auto-repeat. `Primary` selects Control on Windows/
Linux and Command on macOS. `Control` and `Meta` can also be specified explicitly.
`Shortcut::ToString()` supplies a readable presentation hint. Alternative gestures
use `Action::SetShortcuts`; this version matches individual key gestures.

`Widget::AddAction(action, scope)` associates a command independently of a button.
Button presentations automatically register their associated action. Registrations
belong to the widget's unique lifetime and context; hiding/disabling the
registration owner prevents that association from participating. Explicit root
associations are useful for commands that must work without a visible button.

| Scope | Eligibility and precedence |
| --- | --- |
| `Widget` | Registration owner has focus; highest precedence |
| `Subtree` | Focus is inside the logical subtree; nearest matching ancestor wins |
| `Window` | Registration owner is visible/enabled in this context; lowest precedence |

Repeated presentations of one action execute it once. Different eligible actions
at the same winning precedence consume the gesture without executing either.
`GetLastShortcutStatus()` reports `Unmatched`, `Triggered`, or `Ambiguous` for the
last key event. Disabled/invisible/expired commands are excluded. A modal barrier
also excludes associations behind that modal surface. Shortcuts close ordinary
menu layers before running a selected command, preserving an enclosing modal.

No widget-tree scan is performed to discover shortcut candidates: work scales with
registered associations. `UiStats::m_ShortcutChecks` records the actual work.
Widget/subtree eligibility follows logical ancestry, including owned overlays.

## Owning event payloads and widget extension

`UiContext::DispatchInput(InputEvent)` validates one payload before routing it:

| Payload | Preserved information |
| --- | --- |
| `KeyboardEvent` | Logical key, physical position, native scan code, repeat, modifiers, source timestamp, printable key name |
| `TextInputEvent` | Owning committed UTF-8 text and source timestamp |
| `PointerEvent` | Position, phase, changed/held buttons, click count, pointer identity/device, timestamp, optional pressure, transformed clip membership |
| `ScrollEvent` | Position, wheel deltas, modifiers, source timestamp |

Positions enter the router in window coordinates. Receiving widget hooks get
inverse-transformed layout coordinates while retaining the other pointer fields.
Native non-client presses set `PointerEvent::m_PreserveFocus` to retain client
keyboard focus and editor composition. The default remains false for ordinary
client clicks; this policy does not bypass preview, modal or capture routing.
`ToLayoutPoint` and `ToWindowPoint` support explicit conversion; popup anchors are
window coordinates. Keyboard context-menu requests and split/submenu anchors also
follow the source's visual transform.

Override `Widget::OnInput` or `OnInputPreview` for rich metadata and return whether
the event was handled. The default preview adapter calls `OnKeyPreview`; default
delivery calls the existing `OnKey`, `OnText`, pointer, and scroll hooks. Existing
subclasses therefore keep working. Preview paths use weak observations. A preview
that moves focus does not send the same key to the new receiver. Intercepting a
captured release cancels the old capture state.

The legacy context methods `KeyEvent`, `TextInput`, `PointerMove/Down/Up`, and
`Scroll` synthesize owning payloads. Committed text is not synthesized from key
presses. Physical key names identify US-layout positions; printable names come
from the active GLFW layout. Named ASCII logical keys are mapped from those names;
other printable names are retained and named-key matching falls back to physical
position. Real keyboard-layout coverage belongs to the F11 platform matrix.

`Modifiers::m_Control` retains its existing platform-primary meaning;
`m_PhysicalControl` and `m_Meta` preserve the separate native modifiers. Adapters
may mark `m_AltGraph`; shortcut matching then rejects the gesture, and compatibility
editing hooks do not mistake it for Ctrl+A/C/etc. The current GLFW adapter detects
the Control/right-Alt combination; full native composition/layout services remain
F11/F10a work. Pointer payloads support pen/touch metadata, but the GLFW adapter
currently supplies a mouse, not native pen/touch integration.

The context has one captured pointer/device/button tuple. A different pointer or
button cannot end it. `Cancel`, deactivation, popup transitions, and lifetime loss
retire capture. User hooks may destroy receivers; continue using a weak reference
after a callback before accessing your own widget again.

Nested public input delivery, frame advancement/painting, native event pumping,
or dispatcher draining during an input callback is diagnosed. Use `Defer`, a typed
handle, or a dispatcher post for the next operation. Explicit `Prepare(width,
height)` remains a safe geometry-only refresh for editors, animation endpoints,
and popup/focus changes; traversal hooks and staged transactions still reject it.
Native input arriving reentrantly is queued as owned ordered payloads rather than
delivered inside the current callback. Native focus loss invalidates deferred input
from that focus session. Native focus callbacks received during a frame defer UI deactivation until the
next safe owner boundary. `Window::KeyPressed` remains a legacy raw notification;
application commands should use actions instead of binding a second execution
path to that notification.

## Focus scopes

`Widget::SetFocusScope(true)` makes Tab and Shift+Tab cycle inside that subtree.
It does not prevent an explicit focus change or a click into another region.
Hidden/disabled controls and overlay ownership children are excluded from ordinary
Tab traversal. Popups establish their own active focus boundary; modal surfaces
also prevent background focus and input. If an opened surface is not focusable,
its first eligible descendant receives focus.

## Popup and modal sessions

Create ordinary owned widget content, then open it through `ShowPopup`. Root-owned
overlays paint after the normal tree in stack order, with independent clipping.
`AddOwnedPopup` supplies a logical owner so CSS ancestry/inheritance, focus paths,
and lifetime cleanup follow a composed control without changing its layout.

`PopupOptions::m_Parent` selects an existing parent presentation. Without an
explicit parent, a context-owned popup infers one from its logical owner, which
allows a chooser inside a modal panel. Otherwise opening replaces the current
chain. This transient pointer becomes a weak observation; it is not retained in
stored options. Invalid parents and nonfinite anchors fail before replacing live
presentations. The stack is limited to 64 entries.

```cpp
auto session = ui.ShowPopup(panel, {120, 100}, EStudioUI::PopupOptions::Modal());
session.OnFinished([](EStudioUI::PopupResult result)
{
    // React to Accepted, Rejected, Escape, owner expiry, or context shutdown.
});
// In the Apply action, on the owner:
ui.ClosePopup(panel, {EStudioUI::PopupCloseReason::Accepted, 1});
```

There is no nested event loop or blocking wait. `PopupSession` is a copyable
owner-side result that outlives the popup/context. `OnFinished` runs once per
registration, immediately if already finished. All completion observers run even
if one throws; the first error is retained by `GetCallbackError()`. Capture owned
values or weak targets, and inspect `ContextClosed` before updating UI in a shutdown
completion. [examples/InputActions.cpp](examples/InputActions.cpp) shows this path.

Escape dismisses the top eligible layer. Outside presses dismiss ordinary layers
without activating background content; a press into a parent popup may dismiss
descendants and then operate that parent. Modal defaults consume outside presses
without closing the modal. `ClosePopup(widget, result)` closes that widget and its
descendants, while `CloseAllPopups` closes the chain. Children closed with a parent
receive `ParentClosed`; each session receives exactly one terminal result.

Closure removes the branch and hides its widgets before running result observers.
Focus returns to the previous live eligible target, with a fallback inside a
surviving popup scope when necessary. Completion observers may open a newer popup;
an interrupted replacement does not overwrite that newer presentation. Deactivation
and context teardown reject reopening. Directly hidden/removed or owner-expired
overlays are pruned at the next preparation boundary; destruction during the
opening focus callback is settled before `ShowPopup` returns.

`ContextMenu::AddMenu` supplies nested command menus. Right/Enter/click opens a
submenu, Left/Escape returns one level, and choosing a command dismisses its menu
chain before execution while retaining an enclosing modal surface. Shared commands
also integrate with `ToolBar::AddAction`, `Button::SetAction`, and
`SplitToolButton::AddAction`.

## Verification boundaries

`InputFoundationTests` exercises CPU routing, transactions, shortcut precedence,
bounded candidate work, callback destruction, worker handles, focus restoration,
modal barriers, and result lifetime. The Input gallery checks production GLFW
callbacks and compares stacked retained/full pixels from the actual C++ renderer.
See [verification/INPUT_0_14.md](verification/INPUT_0_14.md) for measured results.
Native adapters invoked by tests are not evidence of real keyboard-layout, IME,
screen-reader, hardware GPU throughput, or every platform's behavior.

GLFW's [input guide](https://www.glfw.org/docs/3.4/input_guide.html) documents the
physical-key, printable-name, and committed-character distinctions used by the adapter.
