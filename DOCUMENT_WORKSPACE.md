# Documents and undo history (0.24)

`DocumentWorkspace` owns persistent document views with stable application IDs.
Tabs have close buttons, unsaved indicators, captured pointer reordering and
native drag-out detachment. Closing one document, closing all documents and
closing a floating or containing window use the same asynchronous
Save / Discard / Cancel policy. `UndoHistory` supplies reusable transactional
commands independently of widgets and graphics initialization.

The **Documents & history** gallery page is a working consumer: create load cases,
edit their numeric values, group edits, undo/redo, save JSON through the actual
platform picker, reorder tabs, detach and reattach their existing views. File
writes run on bounded application workers. The Creator also uses `UndoHistory`
for its existing design edits; it remains a single-design application.

## Small common path

```cpp
using namespace EStudioUI;

auto& shell = window.Ui().Root().Add<MainWindow>(window);
auto& workspace = shell.Central().Add<DocumentWorkspace>(window);
auto history = std::make_shared<UndoHistory>(window.Ui().States());
auto& view = workspace.AddDocument({
    "load-case-1", "Load case 1", history,
    [/* application model and persistence service */](DocumentSaveRequest request)
    {
        // Capture owned canonical data here, before opening a picker or starting I/O.
        // Complete with Saved only after that exact snapshot has been persisted.
    }});
view.Add<Label>("Persistent application controls go here");

auto& tools = shell.AddToolBar();
tools.AddAction(workspace.UndoAction());
tools.AddAction(workspace.RedoAction());
tools.AddAction(workspace.SaveAction());
tools.AddAction(workspace.CloseAction());
```

The save body above is a contract illustration, not a complete persistence
handler. [The compiled example](examples/DocumentWorkspace.cpp) accepts an
application persistence function; [the gallery](demo/DocumentGallery.cpp)
implements an actual picker and atomic JSON replacement. Applications retain
their canonical State models for as long as their documents need them. The
compiled TextBox example commits on `Submitted` (Enter); use `TextChanged` for
live string editing or a focus-loss commit for an application-owned draft.

`UndoAction`, `RedoAction`, `SaveAction` and `CloseAction` are shared commands
following the active stable document, with Primary+Z, Primary+Y/Shift+Z,
Primary+S and Primary+W shortcuts. Floating hosts have their own action-domain
presentations of the same document/history. UI access stays on its owner;
workers use owned data, typed handles and save tickets.

## Transactional history

Create a history in the same StateStore as the values it edits:

```cpp
State<double> load(ui.States(), 20.0);
State<double> factor(ui.States(), 1.0);
UndoHistory history(ui.States(), {128, 64 * 1024 * 1024});

history.Set(load, 30.0, "Change load");
history.Group("Nominal case", [&]
{
    history.Set(load, 100.0, "Nominal load");
    history.Set(factor, 1.5, "Safety factor");
});
history.Undo();  // Both grouped values return to the prior coherent state.
history.Redo();
```

`Set` records owned before/after snapshots, suppresses State-equal values and
checks receiver/domain lifetime. `Execute(label, redo, undo, payloadBytes)`
supports custom commands. Its callbacks must mutate canonical values in the
supplied StateStore; database, filesystem and arbitrary external side effects
cannot be rolled back by a State transaction. Both command effects and timeline
metadata commit together. Final-candidate validation or a throwing command rolls
back State and history. An observer exception after a committed notification
does not rewind a successfully committed edit.

Groups run forward in order and undo in reverse order. Nested groups are bounded
to 16 levels and a pending group to 4,096 commands. Retention limits cover both
undo and redo together: default 128 commands and 64 MiB estimated bytes, with
1–4,096 commands and at least 256 bytes configurable. Pass accurate payload
estimates for dynamic strings, trees and custom closures. Static command/label
costs are included; byte counts are estimates, not heap measurements. Whole old
commands are pruned. An oversized edit may commit without retaining an undo
command; its dirty value remains tracked. This is deliberate bounded retention.

`HistoryState` exposes labels, counts, estimated bytes, dirty state and the
current checkpoint. `Subscribe` delivers owned coherent snapshots; one failing
subscriber does not starve other subscribers. `RecordApplied` bridges an already
committed external edit. It cannot retroactively make that earlier publication
atomic; prefer `Set` or `Execute` for new code. The Creator uses this bridge for
its existing canonical design subscription, with 32 commands and a 64 MiB
estimated budget, and retains its existing saved-value comparison semantics.

`CaptureCheckpoint` identifies an exact history value, independently of pruning
and undo-stack indices. Capture an owned model snapshot and its checkpoint at
the normal owner-side save boundary. `MarkSaved(checkpoint)` marks only that
value; a newer edit remains dirty, and undoing to the saved value becomes clean.
Cross-history checkpoints are rejected. Branches never reuse value identities.
`Clear()` preserves dirtiness; `Clear(true)` explicitly marks the current value
saved. Histories and their model domain must remain alive for direct commands.
`GetHandle()` provides receiver-bound owner dispatch without worker UI access.

## Saving and close approval

`SaveDocument(id)` first releases focused editor state so normal focus-loss
commits enter the canonical model before the ticket is captured. Custom controls
must likewise commit their drafts through the ordinary focus/capture contract.
The application save callback then captures immutable data matching
`request.GetCheckpoint()`. Complete every accepted path:

| Outcome | Ticket completion | Close policy |
| --- | --- | --- |
| Exact snapshot persisted | `Complete({DocumentSaveStatus::Saved})` | Clean checkpoint updated; newer edits still need approval |
| Picker/user cancellation | `Complete({DocumentSaveStatus::Cancelled})` | Views retained; close session cancelled |
| I/O failure/task rejection | `Complete({DocumentSaveStatus::Failed, message})` | Views retained; error exposed through DocumentInfo |

Tickets are copyable, independently owned and worker-safe. `Complete` accepts
one terminal result, returns false for duplicate/expired results and wakes the
owner even when ordinary producer posting is saturated. Status/messages are
validated; messages are single-line UTF-8 up to 8,192 bytes. Check the stop token
for cooperative work cancellation. The owner consumes results in its existing
idle-service turn, without nested frame/event loops or a new polling timer.
Dropping an uncompleted live ticket leaves that save pending: applications must
settle cancellation, failure and task-rejection paths too.

`RequestClose(id)` and `RequestCloseAll()` return `DocumentCloseSession` handles.
Use `OnFinished` or inspect the owned result; terminal statuses are Closed,
Cancelled, Failed and OwnerClosed. Callback failures are contained and available
through `GetCallbackError`. Workspace presentation errors have the corresponding
workspace accessor. A handler may remove its owner safely.

Close All preflights **every requested document before removing any view**.
Cancel or failed saving retains the requested views. Already successful disk
writes are not undone. Every approval is stamped with its history checkpoint
and external-edit epoch: edits during an older save or another document's prompt
invalidate stale approval and cause another decision. `SetDocumentModified(true)`
advances the epoch even when the indicator was already dirty. When a history is
supplied, its dirty state and explicit external dirtiness are combined.

One close workflow runs at a time. Repeating the same requested ID set coalesces;
a different concurrent set returns a failed session. Changing the document set
during Close All cancels that preflight. `CancelClose()` dismisses approval while
leaving documents and any running application save intact. Save is disabled in
the approval dialog when no persistence handler exists; Discard and Cancel remain
available. Hidden ancestor tabs are revealed before presenting a decision.

## Ownership, windows and bounds

Document IDs are nonempty single-line UTF-8 up to 256 bytes, unique in a workspace;
titles accept up to 1,024 bytes. At most 128 actual views are retained. Inactive
tabs keep their identity but do not participate in layout, input or painting.
`Content(id)`, `MoveDocument`, `SetCurrentDocument`, `DetachDocument` and
`AttachDocument` all resolve current stable identities instead of cached indices.
The order accessor lists attached tabs, then detached documents in insertion order.

Construct with `Window&` for native detach and automatic containing-window close
approval. The parameterless constructor supports a UiContext-only workspace but
cannot create floating native windows. Dragging out reparents the existing view
to a real owned window with a shared rendering context. Reattach preserves controls,
focus/model connections and history. Floating windows inherit stylesheet revisions.
They use normal owner-thread rendering; this is not worker-thread GL rendering.

`Window::CloseRequested()` exposes a synchronous monotonic veto. `Cancel()` keeps
the actual surface alive; approval can retry `Close()` later. Native callbacks
clear the provisional close flag before dispatching policy. Non-idle close intents
are serviced later at the existing idle boundary. Close-observer exceptions veto
closure and are available through `GetCloseRequestError()`.

The application must keep its Window wrapper alive while approval is pending.
C++ destruction, parent teardown, fatal shutdown and `Application::Quit()` remain
forced lifetime boundaries. They settle pending sessions as OwnerClosed, expire
save tickets and retire native resources without joining workers. Use ordinary
window `Close()`/workspace requests for a user-approved exit workflow.

This increment supplies document lifecycle and reusable history. Versioned
application session restoration, crash recovery, cross-process document ownership,
arbitrary nested document split groups, multiline/rich text editing and SDK/module
extraction remain separate work. DockManager's existing versioned panel layout
format is unchanged.
