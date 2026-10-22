# Typed state and incremental updates / EStudioUI 0.10

Recorded 2026-10-22. This is the first F05/F07 slice of Gate B. It adds no runtime
dependencies: GLFW 3.4, FreeType 2.13.3, HarfBuzz 10.2.0, and utf8proc 2.10.0
remain pinned. Public contracts and complete examples are in [STATE.md](../STATE.md).

## Delivered contracts

| Boundary | Evidence |
| --- | --- |
| Coherent state | Final-candidate validation, one revision per outer commit, nested savepoints, all values published before observers, equality suppression, immutable owned change snapshots |
| Failure and feedback | Rollback before publication, deferred observer writes, bounded notification rounds, observer exception isolation, reconciliation after rollback and discarded/net-no-op candidates |
| Bindings | Initial synchronization, two-way scalar/text/toggle bindings, visible/enabled bindings, custom setters, precommit representability checks, canonical restoration and inspectable errors |
| Lifetime and phases | State/store/receiver expiry, owned worker handles, independent-context target checks, reads during measurement/painting, rejected traversal mutation and nested UI frame/event processing |
| Native integration | Deleting a window inside a binding setter or batch retains active resources; resizing inside a batch renders after commit; nested Window::Run rejects before polling |
| Incremental work | Explicit invalidation impacts, retained intrinsic/aggregate measurement caches, dependency-aware arrangement, exception-safe retry, hidden-widget and percentage/capped-size regressions |
| Actual rendering | Interactive shared controls, coherent summary, rollback, worker progress, independent sibling cache reuse, existing retained/full framebuffer comparisons and settled zero draw calls |

All five complete C++ examples in STATE.md pass GCC 13 C++20 syntax checks.
The demo opens the State tab by default; its capture below comes from the actual
C++ renderer, with no substituted UI artwork.

![State gallery framebuffer](state-foundation.png)

## Verification

- Release: **26/26** CTest suites, including actual software OpenGL integration.
- ASan/UBSan: **26/26** suites, including actual software OpenGL integration.
- Isolated ThreadSanitizer: **4/4** suites, including state and transaction tests.

New CPU suites contain 83 state, 52 binding, 47 invalidation, and 26 control-update
checks. Raw logs are [state-release-tests.txt](state-release-tests.txt),
[state-sanitizer-tests.txt](state-sanitizer-tests.txt), and
[state-thread-tests.txt](state-thread-tests.txt). The actual gallery report is
[state-foundation.txt](state-foundation.txt).

### Work counters

These are observed engine operations, not timing or GPU-utilization estimates.
The 10,000-widget fixture changes one isolated widget after initial preparation:

| Edit | CSS resolutions | Intrinsic measurements | Aggregate calculations | Arrangements |
| --- | ---: | ---: | ---: | ---: |
| Non-inherited paint property | 0 | 0 | 0 | 0 |
| Caption/content | 0 | 1 | 3 | 102 |

The content edit changes an allocation dependency path, so sibling arrangements
are legitimate. Their intrinsic measurements and unrelated styles remain cached.
The actual State gallery bound edit reports 0 CSS resolutions, 4 intrinsic
measurements, and 15 arrangements. Its independent sibling performs zero new
measurements or arrangements. A burst of 2,000 replaceable worker publications
supersedes 1,999 pending updates. The settled frame submits zero draw calls.

The local compiler is GCC 13.3.0. Framebuffer checks use llvmpipe, LLVM 17.0.6,
OpenGL 4.5 core, Mesa 24.0.5-1ubuntu1. The PNG is a lossless conversion of the PPM
written by Renderer::SavePpm. Software GL establishes correctness and work reuse;
it does not establish native Windows GPU throughput.

ASan uses `detect_leaks=0:halt_on_error=1`, because host process-tracing restrictions
prevent LeakSanitizer. UBSan and TSan use `halt_on_error=1`. EStudioUI, tests, and
the demo are instrumented; third-party static libraries and Mesa are not. No
leak-free claim is made.

## Reproduce

The normal Windows one-shot command remains:

```powershell
.\Build.ps1 -Configuration Release -Run
```

It installs x64-windows-estudioui dependencies, compiles, tests, and opens the
gallery. Hosted Windows CI explicitly skips GPU tests because a suitable OpenGL
context is not guaranteed. Native Windows keyboard/clipboard, mixed-monitor DPI,
accessibility, and hardware profiling require target-machine validation.

Linux uses `scripts/Build-Linux.sh Release` with the documented vcpkg/system
prerequisites. A separate ASan/UBSan build enables
`-DESTUDIOUI_ENABLE_SANITIZERS=ON`. Isolated concurrency checks need no UI dependencies:

```bash
cmake -S . -B build/thread-checks -G Ninja -DESTUDIOUI_BUILD_THREAD_TESTS=ON
cmake --build build/thread-checks -j 2
ctest --test-dir build/thread-checks --output-on-failure --no-tests=error
```

## Remaining foundation work

Gate B remains open. Shared external collection models/selection, constrained
measurement and layout strategies, input/focus/command contracts, semantic and
platform interfaces, and install/export consumer integration remain on the
[foundation roadmap](../FOUNDATIONS_ROADMAP.md). This slice does not establish
arbitrary concurrent widget mutation, a render thread, or a complete Qt replacement.
