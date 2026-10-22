# Background work and UI ownership

EStudioUI keeps live widgets, layout, input, and GL resources on their owner
thread. Background work uses typed UI handles: capture a handle once, compute
with owned data, and receive the live target in the completion callback. The
framework handles delivery, target lifetime, request replacement, and cancellation.

These are implemented APIs. Observable model/property bindings and the remaining
Gate B work are tracked separately in [FOUNDATIONS_ROADMAP.md](FOUNDATIONS_ROADMAP.md).

## The usual path

Create `Application` before its windows. It schedules every registered window
through one main-thread event loop. `Application::Tasks()` lazily supplies a
shared background pool. A `RequestScope` makes repeated requests replace earlier
results, which is useful for search, loading, previews, and geometry generation.

This complete C++20 example uses the font copied beside the demo by the build:

```cpp
#include <EStudioUI/Application.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/Window.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

int main(int, char** argv)
{
    using namespace EStudioUI;
    Application app;
    const auto assets = std::filesystem::absolute(argv[0]).parent_path() / "assets";
    Window window({}, assets / "NotoSans.ttf");
    auto& result = window.Ui().Root().Add<Label>("Ready");
    auto& calculate = window.Ui().Root().Add<Button>("Calculate");
    const auto target = window.Ui().GetHandle(result);
    RequestScope requests;

    calculate.Clicked().Connect(
        [&app, &requests, target]
        {
            app.Tasks().RunLatest(
                requests, target,
                [](TaskContext& context)
                {
                    std::uint64_t sum = 0;
                    for (std::uint64_t i = 0; i < 1000000; ++i)
                    {
                        if (i % 4096 == 0)
                            context.ThrowIfCancellationRequested();
                        sum += i;
                    }
                    return std::to_string(sum);
                },
                [](Label& label, std::string text) { label.SetText(std::move(text)); });
        });
    return app.Run();
}
```

Use `Run(target, work, success)` for independent operations. Use
`RunLatest(scope, target, work, success)` when only the latest request matters.
Work may accept `TaskContext&`, `std::stop_token`, or no arguments. A value result
is moved to `success(target, result)`; void work uses `success(target)`. Work,
completion captures, and returned values can be move-only. Return owned values,
not references to worker-local or changing shared storage.

The returned `Task` is an optional cancellation/status handle; destroying that
handle does not cancel the operation. `RequestScope::Cancel()` and scope
destruction cancel their tasks without waiting. The scope must outlive any callback
that captures it by reference, as it does in the example's event-loop lifetime.

Keep window objects alive while they are in use: `Application` schedules them but
does not take their C++ ownership. Additional `Window` objects participate in the
same loop. `Window::Run()` remains a convenience that also services the other
registered windows. Nested event loops are rejected.

## Progress and errors

Optional callbacks share the result's live target and owner-thread delivery:

```cpp
#include <EStudioUI/Application.h>
#include <EStudioUI/Label.h>

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

EStudioUI::Task StartCalculation(EStudioUI::Application& app,
                                EStudioUI::UiHandle<EStudioUI::Label> target)
{
    using namespace EStudioUI;
    UiTaskOptions<Label> options;
    options.m_Progress = [](Label& label, double fraction)
    {
        label.SetText(std::to_string(static_cast<int>(fraction * 100)) + "%");
    };
    options.m_Error = [](Label& label, std::exception_ptr error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception& failure)
        {
            label.SetText(failure.what());
        }
        catch (...)
        {
            label.SetText("Calculation failed");
        }
    };
    return app.Tasks().Run(
        target,
        [](TaskContext& context)
        {
            std::uint64_t sum = 0;
            for (int block = 0; block < 100; ++block)
            {
                context.ThrowIfCancellationRequested();
                for (int i = 0; i < 10000; ++i)
                    sum += static_cast<std::uint64_t>(block * 10000 + i);
                context.ReportProgress(static_cast<double>(block + 1) / 100);
            }
            return std::to_string(sum);
        },
        [](Label& label, std::string text) { label.SetText(std::move(text)); }, options);
}
```

Progress must be finite and in `[0,1]`. Pending progress coalesces per task, so a
fast producer does not allocate a UI command for every intermediate visible
value. Intermediate progress can be superseded or omitted under overload; the
result uses an ordinary queued delivery. `ReportProgress()` returns false when
delivery is no longer useful.

`Task::GetStatus()` and `IsReady()` never wait. Outcomes are `Completed`,
`Cancelled`, `Superseded`, `TargetExpired`, `Rejected`, and `Failed`; computation
and delivery also expose `Queued`, `Running`, `AwaitingUi`, and `Delivering`.
Completion means the result callback finished. A worker failure with an error
callback remains `Failed` after that callback runs. Without an error callback,
inspect `GetError()` or use `RethrowIfFailed()`. A result-delivery rejection or
callback failure can also be inspected through `GetDelivery()`.

## Posting an update from an existing worker

Obtain handles on the owner thread with `ui.GetHandle(widget)` or
`window.GetHandle()`. Copy them into existing jobs, threads, or producers.
`UiHandle` retains an endpoint and immutable target identity, without extending
widget lifetime or exposing a worker-side widget pointer.

```cpp
#include <EStudioUI/Label.h>
#include <EStudioUI/UiHandle.h>

#include <string>
#include <utility>

EStudioUI::DispatchReceipt PublishStatus(EStudioUI::UiHandle<EStudioUI::Label> target,
                                       std::string text)
{
    return target.PostLatest("status", [text = std::move(text)](EStudioUI::Label& label)
                             { label.SetText(text); });
}
```

`Post(callback)` preserves each accepted operation. `PostLatest(key, callback)`
replaces an unstarted publication with the same target/key; its older receipt
settles as `Superseded`. Use replaceable publications for progress/status/preview
values, and ordinary posts for edits or other commands that must be retained.
Both methods always queue, including calls made by the UI owner. Payloads and
captures must own the data needed later.

Posting after shutdown is rejected. A receiver removed before delivery produces
`TargetExpired`. A callback that starts while the receiver is alive may finish;
cancellation does not interrupt it or roll back its changes. Handles cannot
access a newly created widget that reuses an old address.

For window-level actions, `window.GetHandle().Post([](Window& live) { live.Close(); })`
is suitable from an existing worker. Direct `Window::Close()` and
`Window::RequestRepaint()` remain owner-thread operations.

`Close()` is idempotent and immediately hides the native window as well as
retiring its command endpoint. Native title-bar closure uses the same path.
A retained `Window` still owns its context and UI resources until its C++ owner
releases it; destruction during a callback remains deferred safely. Applications
that retain window collections should remove closed owners when convenient.

## Queue policy and acknowledgements

The default UI queue holds at most 4,096 pending records. An ordinary post to a
full queue returns `Rejected` with `DispatchRejectionReason::Capacity`; closure
and empty endpoints have separate rejection reasons. Nothing blocks a producer
waiting for queue space. The application chooses retry, backpressure, or an error
policy for nonreplaceable commands.

`DispatchReceipt` supplies `GetStatus()`, `GetRejectionReason()`, `Cancel()`,
`GetError()`, `RethrowIfFailed()`, and `OnFinished()`. Cancellation affects pending
work; running callbacks finish. Exceptions from UI commands settle their receipts
as `Failed`. Completion observers run once on the settling or registering thread,
outside queue locks; they are not automatically UI callbacks. Observer exceptions
are contained and available through `GetObserverError()`.

Do not block the UI waiting for its own work. `WaitFor()` rejects pending waits
on the owner thread; use continuations or receipt observation. A worker waiting
for UI delivery must still cooperate with application shutdown.

The application loop drains a bounded entry snapshot, normally up to 256 records
and 2 ms per window per turn. A running callback is not preempted, so keep UI
commands short. Newly posted work waits for a later drain. `GetStats()` exposes
occupancy, high-water mark, outcomes, and maximum enqueue-to-start latency.
Standalone `UiContext` users drive their dispatcher explicitly while it is idle;
`Window` and `Application` do this as part of normal service/rendering.

## Signals and subscription lifetime

`signal.Connect(callback)` is a direct connection: the callback executes in the
emitter's thread. Registry operations and emission support concurrent callers;
application callbacks synchronize their own shared data. A direct connection
that touches widgets must be emitted on their owner.

Pass a UI handle to connect an existing producer to a receiver:

```cpp
#include <EStudioUI/Label.h>
#include <EStudioUI/Signal.h>

#include <string>
#include <utility>

EStudioUI::Connection ConnectStatus(EStudioUI::Signal<const std::string&>& status,
                                    EStudioUI::UiHandle<EStudioUI::Label> target)
{
    return status.Connect(target, [](EStudioUI::Label& label, std::string text)
                          { label.SetText(std::move(text)); });
}
```

The handle overload and `ConnectQueued()` always queue. Each emission copies its
arguments into owning values before returning; a reference to `std::string` is
therefore copied safely. Top-level pointers, `string_view`, spans,
`reference_wrapper`, initializer lists, and noncopyable broadcast arguments are
rejected by the queued overload. Application-defined payloads must also own their
transitive data; a shallow copy cannot establish the lifetime of borrowed fields.

Disconnecting suppresses pending listener invocation, and a listener already
selected may finish. Sender destruction also suppresses its pending queued
listeners. Receiver/dispatcher shutdown disconnects the bound route. Keep the
sender object alive while producer threads can call its methods.

`Connection` is nonowning and does not disconnect on destruction. Use
`ScopedConnection` when a C++ scope owns the subscription. Disconnection retires
registry captures, while an in-flight emission or queued command can retain its
own callback/payload until that operation retires. Capture owning data; keep
widgets and native resources under their UI owner rather than owning them in an
arbitrarily retired callback capture.

Automatic receiver/dispatcher disconnection makes the route inactive promptly;
its registry captures can remain until a later connection change, explicit
`Disconnect()`/`DisconnectAll()`, or sender destruction prunes the registry.
Explicit `Connection::Disconnect()` requests that pruning immediately. An
in-flight emitter can still retain its previously selected snapshot.

Default queued connections make capacity overload visible by throwing from
`Emit()`. For application-specific policy, use the third
`Connect(target, callback, receiptObserver)` argument. That observer executes on
the emitter thread and receives every post receipt; inspect it and attach an
`OnFinished` observer as needed. It must not touch UI directly from a worker.

A signal receipt acknowledges the queued command, not a business-level listener
effect. If a connection is disconnected after posting, the command wrapper may
run and suppress the listener; the receipt then settles `Completed`. Expired
receivers, cancelled commands, overload, and command exceptions retain their
normal receipt outcomes. Use an application-level acknowledgement if a producer
needs confirmation of a specific model change.

## Owner access and traversal phases

Widget/UI getters, setters, ownership changes, text/font/renderer services, and
native window calls use owner-thread diagnostics. Read values on that owner and
capture an owned snapshot for workers. Post a read operation when a fresh value
is required asynchronously. `WidgetRef` is an owner-thread observer, not a
worker-side dereference mechanism.

Standalone `CanvasScene` and `SketchDocument` values are CPU models that can be
owned by a worker. References obtained from a live `CanvasWidget::GetScene()` or
`SketchWidget::GetDocument()` remain owner-thread references. Keeping such a
reference does not bypass the contract; individual model operations do not add
automatic thread checks or synchronization. Copy an owned input snapshot on the
UI owner, build independent results on a worker, then publish a batched update
or swap on the owner. Call `SceneChanged()` after direct model edits before
presentation; direct sketch scene imports also require `ClearHistory()` when
they invalidate retained undo commands.

Structural mutation during style, measurement, layout, surface preparation,
paint, submission, or inspection is rejected before changing the traversed tree.
Ordinary deletion-safe input callbacks retain their supported behavior. Queue a
commit with a typed handle, or use owner-only `UiContext::Defer()`:

```cpp
#include <EStudioUI/UiContext.h>

EStudioUI::DispatchReceipt RemoveAfterTraversal(EStudioUI::UiContext& ui,
                                              EStudioUI::Widget& child)
{
    const EStudioUI::WidgetRef observed(&child);
    return ui.Defer(
        [observed](EStudioUI::UiContext&)
        {
            if (auto* live = observed.Get())
                if (auto* parent = live->GetParent())
                    parent->RemoveChild(*live);
        });
}
```

The observed child may disappear first; then this deferred operation does
nothing. `Defer()` itself is called on the owner, and callbacks run in a later
idle drain. Use `GetPhase()` for diagnostics. Calling `Drain()` recursively or
inside an active traversal is rejected.

## Cancellation, executors, and shutdown

A widget lifetime ending, its dispatcher closing, scope cancellation, or a newer
request suppresses pending task delivery and requests the worker's stop token.
Check that token or use `TaskContext` during long operations. Cancellation is
cooperative: it cannot forcibly terminate code that ignores the token. Once a UI
completion is `Delivering`, `Task::Cancel()` returns false and that callback
finishes normally.

`BackgroundTasks` owns a bounded pool by default: automatic worker count is
between one and four, with room for 256 waiting jobs. Construct a separate pool
with `BackgroundTaskOptions` to choose those limits. A full worker queue yields
a `Rejected` task. The UI result queue has its own separate bound.

The constructor taking `BackgroundTasks::Executor` integrates an existing
scheduler. It accepts `bool(std::function<void()>)`: return true after accepting
ownership, or false without running/retaining the job. Exceptions are reported
as rejected submission errors. The injected capacity bounds accepted unfinished
computations. Executors may run inline; UI delivery is still queued. The executor
must support its producer call pattern and keep captured services alive for
concurrent `Run()` calls.

`Shutdown()` rejects new work, cancels outstanding tasks, and requests stop
without joining. Destruction additionally joins owned workers, which must
cooperate. Framework workers publish completion without waiting for the UI
callback; application work must not wait for UI delivery during shutdown.
Application-owned executors are not joined or destroyed by EStudioUI. Their
accepted closures retain independent bookkeeping and remain safe after the
`BackgroundTasks` wrapper is destroyed.

Closing a window closes its UI delivery endpoint and cancels its bound tasks.
`Application::Quit()` closes the registered windows; application destruction
closes remaining windows and retires its shared task pool. Destroy UI objects and
native resources on their owner thread. A dispatcher closes before native wakeup
resources are torn down, and its `Close()` quiesces in-flight wake callbacks.

Framework callback entry points retain the containing window's UI resources
until the active call and its cleanup finish, including direct animation/control
calls and manual dispatcher draining. Closing delivery takes effect immediately.
This does not extend an individual widget's lifetime after explicit removal.
Externally owned standalone contexts, fonts, and renderers must outlive calls
that use them.

The rendering path remains owner-context rendering. Existing OpenGLWidget
callbacks and GPU synchronization have the contracts in
[OPENGL_WIDGET.md](OPENGL_WIDGET.md); background CPU tasks do not change those
callbacks into worker rendering.
