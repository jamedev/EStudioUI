# Shared collection foundations / EStudioUI 0.11

Recorded 2026-10-22. This is the F06 core of Gate B. Public contracts, complete
applications, cost policies, and provider boundaries are in [MODELS.md](../MODELS.md).
Runtime dependencies remain GLFW 3.4, FreeType 2.13.3, HarfBuzz 10.2.0, and
utf8proc 2.10.0. No runtime package was added.

## Delivered behavior

| Boundary | Observed evidence |
| --- | --- |
| Typed shared records | One canonical model serves two tables, a list, and a tree; numeric fields retain raw types and formatted units; typed copies preserve application fields |
| Immutable data | Old snapshots remain readable after edits and native owner destruction; COW pages preserve unaffected records/indexes |
| Transactions | Final-candidate validation, scalar/model coherent commits, equality suppression, rollback, 10,000-edit single publication, stale-revision rejection |
| Selection | Stable IDs through sort/filter/move; hidden selection retained; deletion prunes before model observers; multi/range/toggle/bulk policies; receiving view deletion during callbacks |
| Projections | Typed stable sibling sort, ancestor-preserving filters, chained rule changes, throwing-predicate rejection before upstream publication, reuse for declared unrelated fields |
| Editing/delegates | Actual text input changes a numeric model field; invalid commits preserve input/canonical values; external edit conflicts reject overwrite; sorting follows editor identity; deletion cancels editing; custom GPU cell drawing |
| Viewport work | Bounded table width samples and expanded-branch tree prefixes; random 100,000-row lookup; no child widget per row/cell |
| Lifetime/phases | Owner-only facades, owned worker snapshots, expired model/store handles, standalone-store traversal rejection, native window deletion during model notification |
| Worker publication | Request supersession, revision-checked owned replacements, and business-key identity preservation after deleting a record before reload |
| Actual rendering | Retained/full real framebuffers match byte-for-byte; four-view recording accesses bounded visible records; settled frame submits zero draw calls |

The complete application and two complete functions in MODELS.md pass GCC 13
C++20 syntax checks. The demo opens Models by default. The capture is a lossless
conversion of the PPM produced by the actual Renderer::SavePpm implementation.

![Shared model gallery](models-foundation.png)

## Verification runs

- Release: **28/28** CTest suites, including software OpenGL integration.
- ASan/UBSan: **28/28** suites, including software OpenGL integration.
- Isolated ThreadSanitizer: **5/5** CPU suites, including the actual model sources.

The model suite contains 810 checks, including independent tree-prefix versus
full-traversal comparisons. Model-view coverage contains 33 checks. Raw evidence:
[models-release-tests.txt](models-release-tests.txt),
[models-sanitizer-tests.txt](models-sanitizer-tests.txt),
[models-thread-tests.txt](models-thread-tests.txt), and the actual gallery report
[models-foundation.txt](models-foundation.txt).

### Work counters

These count engine/provider operations; they are not FPS or GPU-utilization
claims. The source dataset has 100,000 records, including 12 nested children.
Tables/lists display root rows; the tree displays expanded hierarchy rows.

| Fixture | Observed work |
| --- | --- |
| Two same-page field edits in 100,000 typed records | Exactly 256 record copies; unchanged pages and identity indexes shared |
| One Apply containing 10,000 field edits | One canonical model notification/revision |
| Quiet fill-column text edit shared by collection views | 0 CSS resolutions, 0 intrinsic measurements, 0 arrangements |
| Sampled preparation of a 100,000-record content column | 32 value reads for the requested 32-row sample |
| Distant flat tree row lookup | One item lookup; no complete flattening |
| Actual four-view frame recording | 208 value/role reads and 88 item lookups |
| Settled actual gallery frame | 0 GPU draw calls |

Relevant sorting/filtering changes and explicit structural rebuilding may scan
the whole source. Page-directory copies scale with page count. Selected-set edits
copy the selected set; bulk selection has an explicit replacement operation.
The documentation states those costs and the width-hint/sample/visible/exact
measurement policies. View-local data/widget copies per record are unnecessary.

The local compiler is GCC 13.3.0. Framebuffer checks use llvmpipe, LLVM 17.0.6,
OpenGL 4.5 core, Mesa 24.0.5-1ubuntu1. Software GL verifies pixels and work counts;
native Windows GPU throughput requires target-machine profiling.

ASan uses `detect_leaks=0:halt_on_error=1` because host process-tracing restrictions
prevent LeakSanitizer. UBSan and TSan use `halt_on_error=1`. Framework, tests, and
demo C++ are instrumented; third-party libraries and Mesa are not. No leak-free
claim is made. A temporary host-only launcher restores executable permission
before sanitizer tests and executes the same compiled binaries; repository build
scripts and CI do not require that host workaround.

## Reproduce and remaining work

The Windows command remains:

```powershell
.\Build.ps1 -Configuration Release -Run
```

It installs x64-windows-estudioui dependencies, compiles, tests, and opens Models.
Hosted Windows CI explicitly skips GPU checks and runs CPU Release/Debug suites.
Native Windows input/clipboard, DPI, accessibility, and GPU profiling remain
target-machine work.

Linux uses `scripts/Build-Linux.sh Release`. A separate instrumented build enables
`-DESTUDIOUI_ENABLE_SANITIZERS=ON`. Isolated model/concurrency checks use:

```bash
cmake -S . -B build/thread-checks -G Ninja -DESTUDIOUI_BUILD_THREAD_TESTS=ON
cmake --build build/thread-checks -j 2
ctest --test-dir build/thread-checks --output-on-failure --no-tests=error
```

External immutable/lazy ModelData providers have nonblocking query contracts;
built-in asynchronous database/network page fetching and eviction remain future
provider work. Tooltips and checkbox semantics have data roles; their widgets
remain on the widget roadmap. Canvas geometry keeps its existing storage/BVHs;
applications map selection identities explicitly. Gate B remains open for F08,
F09, F11 interfaces, and F14 packaging in the [foundation roadmap](../FOUNDATIONS_ROADMAP.md).
