# Typed state, bindings, and invalidation

`State<T>` holds an application value on its UI owner thread. `StateStore`
publishes related values together; bindings keep controls synchronized with those
commits. `UiContext::States()` creates the context's store lazily. These APIs are
the first F05/F07 slice; external collection models and the rest of Gate B remain
open in [FOUNDATIONS_ROADMAP.md](FOUNDATIONS_ROADMAP.md).

## Two controls, one value

This complete application uses the font copied beside the demo executable. Run
it from that executable's directory, or supply your own font path.

```cpp
#include <EStudioUI/Application.h>
#include <EStudioUI/Bindings.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/Window.h>

#include <string>

int main()
{
    using namespace EStudioUI;
    Application app;
    Window window(WindowOptions{}, "assets/NotoSans.ttf");
    auto& ui = window.Ui();
    auto& slider = ui.Root().Add<Slider>();
    auto& numeric = ui.Root().Add<NumericInput>();
    auto& label = ui.Root().Add<Label>();
    slider.SetRange(0, 100);
    numeric.SetRange(0, 100);

    State<double> amount(ui.States(), 25.0);
    State<std::string> status(ui.States(), "Ready");
    BindValue(slider, amount);
    BindValue(numeric, amount);
    BindText(label, status);

    ui.States().Batch([&]
    {
        amount.Set(50.0);
        status.Set("Both controls share this value");
    });
    return app.Run();
}
```

Binding creation applies the initial value immediately. `BindValue` is two-way
by default: slider changes and committed numeric values update the state, then
all bound views. NumericInput's intermediate editing text stays local until a
value commits. Signals matching the canonical value are no-ops; newer reentrant
control edits publish through a later notification round. Failed-edit restoration
separately suppresses writeback. Use `BindingMode::OneWay` for display-only state.

Bindings do not own their state or widget. Destroying either endpoint, its store,
or its context disconnects propagation. Declare application state outside the
callbacks that use it and keep it alive through the interaction, as above.

| Helper | Value | Default direction |
| --- | --- | --- |
| `BindText(Label&, state)` | `std::string` | State to label |
| `BindText(TextBox&, state)` | `std::string` | Two-way |
| `BindValue(NumericInput&, state)` / `BindValue(Slider&, state)` | `double` | Two-way |
| `BindChecked(ToolButton&, state)` | `bool` | Two-way; enable user toggling with `SetCheckable(true)` |
| `BindEnabled(widget, state)` / `BindVisible(widget, state)` | `bool` | State to the local programmatic flag |
| `Bind(widget, state, setter)` | Application type | State to custom setter |

## Validation and transaction boundaries

Pass a validator as the third `State` constructor argument, or retain a connection
from `AddValidator`. A validator throws to reject a value; initial construction
validates its initial value. Adding a validator does not validate the existing
value. Equality defaults to `operator==`; a fourth constructor argument supplies
custom equality, required for a type without `operator==`.

`Set` returns false for equality with the current staged candidate. An unchanged
final value produces no change notification or revision increment. A successful
outer batch publishes every changed value under one store revision before any
ordinary observer runs. Each `StateChange<T>` owns immutable before/after
snapshots and that revision.

Validation runs on the final candidates, after the batch body. Reads during the
body and validation see the staged candidates, so validators can check coherent
relationships between values. Validators and equality callbacks must be pure:
reads are permitted; state mutation and subscription/validator registration are
rejected. This complete function permits an intermediate invalid pair while
validating its final range:

```cpp
#include <EStudioUI/State.h>

#include <stdexcept>

void UpdateRange(EStudioUI::StateStore& store)
{
    using namespace EStudioUI;
    State<double> lower(store, 0.0);
    State<double> upper(store, 10.0);
    ScopedConnection lowerRule(lower.AddValidator([&](double value)
    {
        if (value > upper.Get())
            throw std::invalid_argument("Lower bound exceeds upper bound.");
    }));
    ScopedConnection upperRule(upper.AddValidator([&](double value)
    {
        if (value < lower.Get())
            throw std::invalid_argument("Upper bound precedes lower bound.");
    }));
    store.Batch([&]
    {
        lower.Set(20.0);
        upper.Set(30.0);
    });
}
```

Nested batches are savepoints. A nested body failure discards its own edits;
the outer body can catch it and continue. Nested success merges into the outer
candidate set without publishing. A body, allocation, equality, or validation
failure before publication restores the affected savepoint and preserves the
original exception. `SubscribeReconcile` reconciles eager presentation after that
restore and when staged edits finish at their original value. These net-no-op
values need no change event or revision, but a control edited eagerly inside the
batch must still return to its canonical value. Reconciliation also covers such
values in a batch that changes other states. Its callbacks may read the resulting
candidates but may not mutate state. A reconciliation exception does not replace
the transaction failure; inspect it with `StateStore::GetReconciliationError()`.

Observers registered with `Subscribe` run in connection order for each value;
values notify in first-edit order. They see the committed current round. Writes
from an observer are deferred to a later coherent round, rather than changing
what remaining observers read. An explicit batch inside an observer reads its
own candidates. Feedback is bounded to 64 notification rounds by default;
`StateStore(dispatcher, limit)` selects another positive bound. Exceeding it
discards still-deferred edits and reports an error; already published revisions
remain committed. Ordinary observer failures also leave committed values intact:
remaining observers and deferred rounds run, then the first failure is rethrown
to the synchronous caller or recorded by the enclosing dispatch receipt.
The `Subscribe(receiverHandle, callback)` overload always queues owned change
snapshots to a live receiver; an optional receipt observer exposes each delivery
outcome. See [THREADING.md](THREADING.md) for queued signal semantics.

`Get()` returns an owner-side reference that expires on the next value update or
at the end of its enclosing batch. Capture `GetSnapshot()` on the owner when a
worker or a later callback needs an owned immutable value. `State` and
`StateStore` have stable identity, cannot be copied, and must be destroyed on
their owner thread. A standalone store can use an existing owner `UiDispatcher`.

## Rejected edits and binding lifetime

Built-in numeric bindings require finite values inside every bound view's range;
text bindings require exact text representability, including the editor's byte
limit, sanitation, and validator policy. They validate the initial value and
register checks for later state commits, including one-way bindings. Configure
compatible ranges and text policies before binding; coordinate later policy
changes with the canonical value. State values are not silently clamped or
truncated to make views agree.

A rejected control-origin edit restores the current canonical state in that
control and records the exception in `Binding::GetError()`. A failed outer batch
also reconciles affected bound controls. `ClearError()` clears the saved error;
`SetErrorHandler` installs an owner-side callback for later failures, after the
restore. Prefer a handler that does not throw. Programmatic `state.Set` failures
still propagate to their caller. An observer error after publication is a
committed-update error, so restoring a view uses the already committed value.
If changed control constraints make restoration impossible, the binding
disconnects and records a descriptive error.

`Binding` is persistent: discarding its handle leaves the binding active.
Keep a handle for explicit `Disconnect`, error inspection, or an error handler;
use `ScopedBinding` for scope-based disconnection. Copies share disconnection
and error state. `Disconnect`, `IsConnected`, `GetError`, and `ClearError` can be
used from another thread; creation and error-handler changes belong to the owner.
State subscriptions likewise return persistent `Connection` handles; use
`ScopedConnection` when callback captures end with a scope.

```cpp
#include <EStudioUI/Bindings.h>
#include <EStudioUI/NumericInput.h>

bool PreviewBinding(EStudioUI::NumericInput& numeric,
                    EStudioUI::State<double>& amount, double proposed)
{
    auto binding = EStudioUI::BindValue(numeric, amount);
    EStudioUI::ScopedBinding scope(binding);
    numeric.SetValue(proposed);
    return !binding.GetError();
} // Disconnects both directions; neither endpoint is destroyed.
```

## Worker publication and traversal phases

Capture `amount.GetHandle()` on the owner and pass that handle by value to a
worker. The worker posts an owned value; only queued owner delivery resolves the
state. For replaceable progress or previews:

```cpp
#include <EStudioUI/State.h>

EStudioUI::DispatchReceipt PublishAmount(
    EStudioUI::UiHandle<EStudioUI::State<double>> amount, double value)
{
    return amount.PostLatest("amount", [value](EStudioUI::State<double>& live)
    {
        live.Set(value);
    });
}
```

The receipt distinguishes delivery, supersession, capacity rejection, expiry,
and callback failure. Use `Post` for ordered changes that must not coalesce;
[THREADING.md](THREADING.md) describes queue budgets and task publication.
Workers may read snapshots captured on the owner, not call the state getters,
setters, or `GetHandle()` directly.

Snapshots make the top-level `T` const. Application-defined graphs and smart
pointer pointees must also remain immutable while shared; the framework cannot
remove external mutable aliases. Known borrowed top-level values such as raw
pointers, `string_view`, spans, and reference wrappers are rejected. Prefer owned
strings/containers or explicit shared immutable CPU data. A snapshot or captured
value may be destroyed on the thread that releases its last owner. Keep widgets
and native resources under their UI owner; avoid putting their teardown inside
worker-shared value graphs.

State reads and the owner-side `ui.States()` accessor are valid during
measurement and painting.
Context-owned mutation, batch execution, and subscription/binding setup require
`UiPhase::Idle`. Use `UiContext::Defer` or a previously captured handle for a later
update. A complete state operation, including the `Set`/`Batch` body, validation,
reconciliation, and notifications, blocks reentrant frame preparation, rendering,
event-loop processing, or context-owned dispatcher draining until it finishes.
A binding's target must already belong to an idle context, independently of the
state's store.

## Targeted invalidation in custom widgets

Custom setters should verify access, suppress equivalent values, then declare
the derived work they affect. `Bind` calls a setter with its live widget on the
owner, applies the initial value immediately, and disconnects with either
endpoint. Its setter must accept every state candidate; put domain validation
on the state. A custom setter exception during notification cannot roll back an
already published revision. Other objects captured by a setter retain their
normal C++ lifetime requirements.

During painting, the protected `GetIntrinsicSize()` returns the widget's last
prepared intrinsic content size without invoking `Measure` again. It excludes
padding, border, and explicit size contracts. It is a cached result, not a fresh
measurement before the next preparation.

```cpp
#include <EStudioUI/Bindings.h>
#include <EStudioUI/Renderer.h>

#include <cmath>
#include <stdexcept>

class Meter final : public EStudioUI::Widget
{
  public:
    void SetLevel(double level)
    {
        VerifyAccess();
        if (!std::isfinite(level) || level < 0 || level > 1)
            throw std::invalid_argument("Level must be between zero and one.");
        if (m_Level == level)
            return;
        m_Level = level;
        Invalidate(EStudioUI::Invalidation::Paint);
    }

  protected:
    EStudioUI::Point Measure(EStudioUI::FontFace&) const override
    {
        return {120, 20};
    }

    void Paint(EStudioUI::Renderer& renderer, EStudioUI::FontFace& font,
               double time, float opacity) const override
    {
        Widget::Paint(renderer, font, time, opacity);
        auto fill = GetContentBounds();
        fill.m_Width *= static_cast<float>(m_Level);
        renderer.DrawSolid(fill, {0.1f, 0.6f, 0.9f, opacity}, GetClip());
    }

  private:
    double m_Level = 0;
};

void UseMeter(EStudioUI::UiContext& ui)
{
    auto& meter = ui.Root().Add<Meter>();
    EStudioUI::State<double> level(ui.States(), 0.25, [](double value)
    {
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::invalid_argument("Level must be between zero and one.");
    });
    EStudioUI::ScopedBinding binding(EStudioUI::Bind(
        meter, level, [](Meter& live, double value) { live.SetLevel(value); }));
    level.Set(0.75);
} // This example's local state and binding end here; the context owns the meter.
```

| Impact | Required work |
| --- | --- |
| `Paint` | Request a frame while retaining style and geometry caches |
| `Measure` | Invalidate the widget's intrinsic measurement and ancestor aggregates |
| `Arrange` | Update the widget and ancestor allocation path |
| `HitTest` | Refresh transformed input geometry and stationary hover |
| `Content` | Combine measure, arrange, hit-test, and paint for intrinsic-content changes |
| `Style` | Refresh the widget's computed style and paint parts |
| `InheritedStyle` | Refresh inherited presentation in descendants |
| `Selector` / `Structure` | Conservatively revisit tree-dependent selectors; structure also affects geometry |

Impacts can be combined with `|`. A caption change uses `Content`; it may move
allocation siblings while retaining their intrinsic measurements and unrelated
styles. Paint-only changes retain geometry. A selector or inherited-style
change can require a wider dependency closure. The no-argument `Invalidate()`
remains the conservative full style/layout path for existing custom widgets.
`RequestRepaint()` retains style and geometry caches.

No-op suppression applies to value changes, not every operation that happens to
receive equal data. `TextBox::SetText` can still reset selection/history,
`NumericInput::SetValue` can restore intermediate editor text, and collection
replacement can reset selection or scroll state. Completion signals such as
`SliderReleased` remain commands even when the value is unchanged.

`UiContext::GetStats()` reports cumulative style resolutions, intrinsic
measurements, aggregate measurement calculations/cache hits, layout passes,
and widget arrangements. Compare snapshots around an edit to inspect its logical
CPU preparation cost. These counters do not measure GPU throughput or prove
native driver performance. Framebuffer comparisons and the final verification
record remain separate acceptance evidence.
