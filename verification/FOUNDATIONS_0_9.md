# Execution and lifetime foundations / EStudioUI 0.9

Recorded 2026-10-22. These checks exercise the actual C++ implementation. Runtime
dependencies remain GLFW 3.4, FreeType 2.13.3, HarfBuzz 10.2.0, and utf8proc 2.10.0;
no task, signal, or concurrency package was added.

## Shipped behavior

| Boundary | Evidence |
| --- | --- |
| Owner access | Widget/base/control/service checks reject foreign-thread access before UI state or GL work; expired nonempty WidgetRef access still checks its owner |
| Dispatch | Bounded multiproducer posting, per-producer ordering, explicit rejection, target/key replacement, queue statistics, exception and observer isolation, callback-safe close |
| Lifetime | Unique target IDs prevent address reuse; receiver/dispatcher stop tokens retire queued subscriptions and task delivery; no worker resolves a widget; direct callbacks/manual drains retain window resources through call cleanup |
| Tasks | Owned/injected executors, move-only results, progress, errors, cancellation, stale generations, overload, shutdown and cooperative worker join |
| Signals | Concurrent registry operations, direct emitter-side semantics, queued owned payloads, receiver expiry, disconnect/sender deletion before queued invocation |
| UI phases | Structural changes during traversal reject before mutation; a real paint callback defers a successful change to the next owner boundary |
| Application | One loop updates two independent framebuffers; worker work wakes idle delivery; closing one window preserves another; callback-driven destruction and nested-loop diagnostics |
| Clock | Queued animations start at the current frame time after idle, retain exact endpoints, and produce the expected actual midpoint pixels |
| Common workflow | Async gallery builds a 200,000-item scene and BVH on a worker, replaces/cancels requests, removes/recreates its receiver, and publishes a coherent scene |

Public contracts and examples are in [THREADING.md](../THREADING.md). Ease-of-use
acceptance rules are in [DEVELOPER_EXPERIENCE.md](../DEVELOPER_EXPERIENCE.md).

## Verification runs

- Release: **22/22** CTest suites, including actual software OpenGL integration.
- ASan/UBSan: **22/22** suites with EStudioUI/test/demo C++ instrumented.
- Isolated ThreadSanitizer: **3/3** concurrency suites, without UI/GL dependencies.

Raw logs are [foundation-release-tests.txt](foundation-release-tests.txt),
[foundation-sanitizer-tests.txt](foundation-sanitizer-tests.txt), and
[foundation-thread-tests.txt](foundation-thread-tests.txt). Native probe output is
[application-foundation.txt](application-foundation.txt); the async scene/phase
checks are in [foundation-gallery.txt](foundation-gallery.txt).

The local compiler is GCC 13.3.0. The GL driver is llvmpipe, LLVM 17.0.6,
OpenGL 4.5 core, Mesa 24.0.5. PNGs are lossless conversions of PPMs written by
`Renderer::SavePpm`, including [foundation-async.png](foundation-async.png).
The settled gallery submits zero draw calls. This establishes rendering/work-count
correctness; it does not measure Windows hardware throughput.

ASan uses `detect_leaks=0:halt_on_error=1`; this host's process tracing prevents
LeakSanitizer. UBSan and TSan use `halt_on_error=1`. Third-party static libraries
and Mesa are not sanitizer-instrumented. No leak-free claim is made.

## Reproduce

The normal Windows command remains:

```powershell
.\Build.ps1 -Configuration Release -Run
```

It installs dependencies, compiles, runs CPU and native GPU checks, then opens the
gallery. Hosted Windows CI explicitly skips GPU checks because it cannot promise
OpenGL 3.3. Native Windows input/clipboard, mixed-monitor DPI, accessibility, and
hardware profiling still require target-machine validation.

For Linux, use `scripts/Build-Linux.sh Release` with the documented system/vcpkg
prerequisites. A separate ASan/UBSan build uses
`-DESTUDIOUI_ENABLE_SANITIZERS=ON`. The concurrency layer has a dependency-free
isolated build:

```bash
cmake -S . -B build/thread-checks -G Ninja -DESTUDIOUI_BUILD_THREAD_TESTS=ON
cmake --build build/thread-checks -j 2
ctest --test-dir build/thread-checks --output-on-failure
```

## Remaining foundation gates

Gate B remains planned: typed state/transactions, shared models and selection,
incremental invalidation, constrained measurement/layout strategies, input and
semantic/platform interfaces, and SDK packaging. Gate C covers text/font/document
services, native adapters, and resource budgets. This release does not add state
bindings or permit arbitrary concurrent widget mutation or worker GL rendering.
