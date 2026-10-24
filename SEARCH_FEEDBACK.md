# Search and application feedback (0.26)

`SearchBox` composes the existing grapheme-aware `TextBox`, a clear action and
`CompletionPopup`. Suggestions retain editor/IME focus; Up/Down selects and
Enter accepts. The committed text, not composition preedit, drives a 150 ms
GUI-clock debounce. Set it with `SetDebounce`.

```cpp
auto& search = parent.Add<SearchBox>();
search.SetSuggestions({{"mesh", "Generate mesh", "Prepare finite elements"},
                       {"solve", "Solve model", "Run analysis"}});
search.CompletionAccepted().Connect([](CompletionItem item) {
    // Route item.m_Id through application policy.
});
```

The catalog is Unicode case-folded once when set. Each query performs an explicit
bounded substring scan, capped at 4096 catalog records / 1 MiB. The popup stores
at most 256 results / 256 KiB and paints viewport rows through `ItemModel` and
`ListWidget`. Stable nonempty application IDs are required. Details remain owned
metadata; the default list displays the suggestion caption.

For asynchronous results, observe `QueryRequested(std::string, uint64_t)`.
Capture owning text and the request ID on a worker; return through
`UiContext::GetHandle(search).Post(...)` or `BackgroundTasks::Run`. Call
`SetCompletions(request, results)` on its live owner. Every canonical edit and
focus loss and suggestion dismissal invalidate old IDs immediately. Stale results and results for an
unfocused/hidden/disabled field return false and cannot reopen a popup. The
application owns cancellation, workload limits and queue rejection/error policy.
The framework's `RequestScope` can additionally cancel superseded worker tasks.
`GetQueryError()` exposes a contained debounced query/observer exception.

`CompletionPopup::Create(owner)` can be used by other controls. `SetItems`,
`Show(anchor)`, `Move`, `Accept` and its owning `Accepted` payload form the
reusable presentation contract. `GetList()` exposes the ordinary virtual view.

`CommandPalette::Create(owner)` takes shared `Action` instances with `SetActions`.
`Show()` returns a normal asynchronous modal `PopupSession`. Search matches
Unicode case-folded captions; keyboard navigation retains field focus. The
palette closes before invoking the canonical command and rechecks its lifetime,
visibility and enabled state at execution. `Refresh()` re-evaluates live captions
and eligibility; open/search also refresh. The bounded catalog accepts 4096
commands / 1 MiB of captions and presents at most 256 matches. It does not create
replacement commands or a private command execution system.

`ValidationMessage::Bind(editor, invalidText, intermediateText)` observes the
existing validator. Acceptable input clears feedback. Intermediate input stays
editable and can show a warning. Use `SetMessage(text, severity)` for application
or server-side validation. Empty text hides the message. Severity CSS classes are
`information`, `success`, `warning` and `error`.

`NotificationCenter::Create(ui)` creates a viewport-corner overlay. `Show(text,
severity, seconds)` returns a stable ID; `Dismiss(id)` and `Clear()` remove
messages. Zero timeout is persistent; the default is five seconds. At most four
visible messages are retained, evicting the oldest. Messages are limited to 16
KiB and timeouts to one day. Notifications remain below modal/popover surfaces
and cannot replace their sessions or steal focus.

`UiContext::AddOverlay` supports four viewport corners and a finite margin. The
root owns overlays; they are excluded from normal flow and arranged again on
resize. Input, focus and accessibility use the same modal boundary as other
controls. There are at most 64 overlays per context. `UiContext::After` supplies
move-only RAII `UiTimer` deadline registrations, including a receiver-bound
variant. Callbacks run on idle turns, at most 64 per turn; zero-delay callbacks
scheduled by callbacks wait for the next turn. The queue accepts 1024 timers.
Cancellation and target/context destruction retire pending callbacks. Exceptions
remain available through `GetError()`.

Neither debounce nor static notification expiry requests continuous rendering.
The normal native event scheduler wakes for the earliest pending timer. Timers
and UI objects remain owner-thread-only; producer code uses typed handles.

`LogView` exposes a virtualized read-only table and a thread-safe `LogSink`.
`GetSink()` requires attachment. `sink.Append(LogEntry)` performs no UI access;
it owns bounded CPU data and wakes the existing dispatcher. A full inbox rejects
new entries with false and increments `GetDroppedCount()`. It accepts 512 pending
entries / 1 MiB. Each message is at most 16 KiB, with a source of at most 256
bytes. One idle turn drains the pending batch and publishes the canonical model
once. The current view retains 2000 entries by default; `SetCapacity` permits
1–10000, additionally bounded to two MiB of payload. Old entries are evicted.

The immutable model rebuild is explicitly O(retained rows) per batch; rendering
and text caches remain viewport-bounded. Surviving row IDs are stable. Retained
external snapshots can extend old data lifetime beyond the current-store budget.
`SetFollowTail(false)` lets users inspect older rows without new messages moving
selection. `Clear` clears retained entries; producers can append again. Sink
copies remain safely closed after the widget dies. No UI state/model is retained
by a worker's last sink reference.

The **Search & feedback** demo page includes real suggestions, shared commands,
validation, notifications and worker logs. All controls use the existing renderer
and pinned dependencies.

Live feedback supplies `SemanticLiveSetting::Polite` or `Assertive`; Windows
exposes UI Automation LiveSetting and live-region change events through its
existing bounded observed-node set. Human screen-reader speech behavior remains
an explicit platform acceptance gate. Primary contracts: Microsoft's
[LiveSetting property](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-automation-element-propids)
and [live-region event](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-event-ids).

See the [verification record](verification/PRODUCTIVITY_0_26.md) for exact tested source and platform evidence.
