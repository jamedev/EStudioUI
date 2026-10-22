# EStudioUI developer experience contract

EStudioUI should make ordinary application code short, readable, and safe while
keeping advanced behavior accessible. Ease of use is an acceptance criterion for
every foundation and widget change, alongside correctness, performance, and the
permissive-dependency policy. Apply this document throughout
[FOUNDATIONS_ROADMAP.md](FOUNDATIONS_ROADMAP.md) and
[WIDGET_ROADMAP.md](WIDGET_ROADMAP.md).

This contract guides implementation and review. Feature status and verification
evidence belong in the roadmaps; an API proposed there is not automatically
available here.

## Keep the common path small

- A basic application can create a window, add controls, connect callbacks, and
  run without subclassing framework objects or configuring execution services.
- Background work uses a live-target handle and a computation/completion pair.
  The normal path handles UI delivery, receiver expiry, and task outcome without
  application-managed threads, mutexes, or event-loop plumbing.
- Supply useful defaults for worker count, queue capacity, frame pacing, styles,
  and sizing. Create optional services lazily. Keep their limits discoverable.
- Reuse familiar C++20 values, functions, ownership, and RAII. Public concepts
  have one documented responsibility and consistent naming.
- Default to composition. Require a paint or event override only when the
  application actually needs custom behavior.

For example, the foundation API supports this small, complete function:

```cpp
#include <EStudioUI/Application.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/UiContext.h>

#include <string>
#include <utility>

EStudioUI::Task RefreshStatus(EStudioUI::Application& app,
                             EStudioUI::UiContext& ui,
                             EStudioUI::Label& status)
{
    return app.Tasks().Run(
        ui.GetHandle(status),
        [] { return std::string("Background work completed"); },
        [](EStudioUI::Label& live, std::string message)
        {
            live.SetText(std::move(message));
        });
}
```

The worker owns its input/result values; the completion receives a live target on
its owner thread. Longer computations can accept the task context or stop token
for progress and cooperative cancellation. The task handle exposes cancellation
and the eventual outcome when the application needs them.

## Make semantics easy to predict

| Concern | Required behavior |
| --- | --- |
| Direct calls | Owner-thread operations execute synchronously; unsupported access is diagnosed before owner state changes |
| Posted calls | Queued delivery stays queued even when the caller is already on the owner |
| Reads | Return current owner-side state, an owned snapshot, or an explicitly asynchronous result |
| Lifetime | Receiver-bound work cannot resolve a destroyed or replaced object; scoped connections/tasks have documented teardown |
| Cancellation | Distinguish requesting cooperative stop, suppressing pending delivery, and interrupting an already running callback |
| Replacement | A newer request can supersede stale results; progress coalescing has an observable terminal outcome |
| Overload | Bounded work queues expose rejection/backpressure; accepted ordered edits preserve their delivery semantics |
| Errors | Programmer misuse receives a useful diagnostic; asynchronous failure remains inspectable on its receipt/task |
| Updating state | Validation, no-op updates, notifications, and transaction boundaries follow a shared contract |
| Performance | Document whole-model scans, copies, blocking operations, resource limits, and other potentially expensive work |

Keep the asynchronous boundary visible in the API while automating routine
marshaling. Use typed handles and owned payloads in examples. Raw references,
borrowed views, and unrelated objects captured by a callback retain their normal
C++ lifetime requirements.

Shutdown cancels UI delivery promptly. Owned workers follow the documented join
policy; injected executors retain their application's ownership. Applications
must be able to understand a task that ignores cooperative cancellation without
depending on hidden waits or an already stopped UI loop.

## Offer explicit escape hatches

The common path should share implementation with the advanced path. Preserve
configurable executors, queue/frame budgets, direct model adapters, custom
painting, CSS parts, explicit sizing, native services, and external scene hooks.
Document their ownership, thread, and phase requirements at the entry point.

Applications can opt into those controls progressively. An advanced integration
should not require copying internal classes, bypassing lifetime checks, patching
the renderer, or rewriting the application around a second API family.

## Design future bindings and models for application code

Before adding a binding/property/model API, demonstrate these scenarios:

- A worker publishes progress; one binding updates the live view and coalesces
  redundant values under load.
- Several controls edit one typed value with consistent validation, equality,
  notification ordering, and feedback-loop handling.
- A model serves multiple views without duplicating its data; stable item IDs
  preserve selection through sort/filter/move operations.
- A batch of changes commits a coherent revision and schedules necessary derived
  work once. Unrelated style and measurement caches remain reusable.
- Model snapshots and asynchronous results carry explicit ownership and revision
  semantics. Receiver/window/request lifetimes end without manual disconnect
  bookkeeping in each callback.

These scenarios are exercised by the APIs in [STATE.md](STATE.md) and
[MODELS.md](MODELS.md). The foundation roadmap records remaining framework gates
and provider/platform work.

## Review usability with working code

Every public feature should have a small example using the actual API. Compile
documentation examples with the delivered headers, and exercise examples that
demonstrate lifecycle, threading, or rendering against the relevant real service.
Keep examples and implementation synchronized; show the first useful action
before optional configuration and advanced tuning.

Before accepting a public API, check:

1. Can a reader identify the entry point, ownership, and result from the example?
2. Does the ordinary case require only concepts relevant to that task?
3. Are defaults useful, with optional settings discoverable rather than mandatory?
4. Can a callback close its target, cancel work, or throw with a defined outcome?
5. Are thread/phase violations and invalid values diagnosed at the responsible API?
6. Are cancellation, supersession, overload, and shutdown observable and documented?
7. Can the application supply its own executor, model, style, or paint logic through
   supported extension points?
8. Do examples avoid unnecessary raw captures and manual lifecycle plumbing?
9. Can diagnostics explain unexpected layout, invalidation, queued work, and cost?
10. Does the feature preserve the tested common path when advanced options are unused?

Prefer simplifying a confusing call site before adding another convenience alias.
Record changes to public semantics in the relevant guide and roadmap, with a
migration example when callers must adapt. Retain the repository's readable
English identifiers, Allman braces, and brief `//!` header documentation.

## Layout as composition

The ordinary path is SetLayout(FormLayout{}), SetGap, and explicit size rules;
wrapping labels report their required height without application geometry loops.
Row/column, grid and flow containers share allocator and measurement contracts.
SetBaselineChild lets composed controls choose their editor without a paint or
layout subclass. Strategy values have ordinary ownership and are replaced at an
idle owner boundary. Custom strategies and measurement hooks are pure queries.

Review allocations with GetLayoutReport().Explain or GetLayoutDiagnostics.
Reports name affected children and show track rules, limits and final sizes;
percentage/content cycles have a deterministic fallback. Keep reports owned and
bounded with the actual widget tree, keep model rows virtual, and retain the
paint-only and unrelated-measurement work gates. See [LAYOUT.md](LAYOUT.md) for
complete compiled examples and migration/extension contracts.
