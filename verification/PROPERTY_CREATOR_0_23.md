# Property editing and EStudioUICreator: 0.23 verification

This increment adds typed property editing and a first real authoring application.
The source uses the existing EStudioUI renderer; captures below are actual C++
framebuffers, converted losslessly from PPM to PNG. They are not mockups.

## Automated evidence

Local GNU 13 Release passes **46/46 CTest suites**, including the existing GPU
integration and rendering-profile suites. The final complete pass took 15.49 s
on this software-GL host. New suites report:

| Check | Evidence |
| --- | --- |
| PropertyEditing | 37 checks: canonical units and affine conversion, dimension rejection, vector atomicity, RGBA/HSV/hex behavior, State binding rollback, observer failure recovery, bounded reentrant feedback, 10,000 virtual rows with one editor, lazy semantic edits and dataset expiry |
| UiDocument | 22 checks: JSON round trip, duplicate keys/IDs, versions, bounds, unknown types/properties, invalid references and text, integer grid schema, actual typed construction, constraints, staged failure preserving the host, custom adapters and source escaping |
| CreatorGenerated | 97 checks: a build-time generator exports direct C++ for all 40 registered types, the build compiles that source, and the test compares actual widget identities/geometry and exact UTF-8 label bytes against JSON/runtime construction |
| CreatorGpuIntegration | 24 checks: real inspector commits, preview identity reuse, undo/redo, typed palette creation, JSON/C++ outputs, color popover dismissal, invalid-value rollback, captured movement/resizing, one undo per gesture, zero idle repaint, retained/full framebuffer equality and canvas-hosted runtime widget interaction/placement/pixels |

`examples/PropertyEditing.cpp` and `examples/RuntimeUi.cpp` compile in the ordinary
build. The latter covers both ordinary container and sparse canvas hosting APIs.
The generator does not feed JSON to its generated C++; both paths independently
construct widgets from the same owned design values.

The Release build reports no C++ compiler warnings. The existing libunibreak
package emits its pre-existing CMake developer-scope warnings. A host file-cache
materialization removed execute permission from several generated ELF files;
permissions were restored before the final complete CTest pass. No source change
was needed for that host issue.

ASan/UBSan and Windows CI results are recorded after their runs complete. They are
not implied by the local Release result. Leak detection must be disabled on this
traced host; address and undefined-behavior checks remain enabled.

## Actual renderer captures

![Creator with a real dashboard and property inspector](creator-wide.png)

![Anchored RGB/HSV/alpha editor in the real inspector](creator-color-picker.png)

![Runtime-built dashboard hosted by the existing infinite canvas](creator-canvas.png)

The canvas test verifies ordinary pointer activation after construction and
updated host geometry after pan/zoom. Existing canvas hosts resize and relayout
controls when zoom changes the world rectangle; they do not promise uniform
scaling of every glyph or unbounded populations of full widget subtrees.

## Build and platform contracts

`Build.ps1` now builds the Creator, runs its native GPU self-test after the gallery
self-test, and supports `-RunCreator`. It retains Windows PowerShell 5.1 syntax and
the existing pinned `x64-windows-estudioui` triplet. Windows CI stages the Creator
beside the isolated Mesa runtime and runs the real Win32/WGL path. The software
renderer establishes framebuffer correctness, not hardware frame throughput.

Only one new dependency is added: private header-only nlohmann/json 3.11.3#1,
MIT, selected by the existing registry baseline with default features disabled.
The authoritative license is committed and staged with the application.

Native input methods, actual assistive-technology sessions, mixed-monitor DPI,
physical snap/title-bar behavior, and hardware GPU profiling remain human
acceptance work. The new virtual inspector has semantic protocol tests; these do
not substitute for real screen-reader acceptance.

## Deliberate first-increment boundaries

See [CREATOR.md](../CREATOR.md) for the prioritized missing blocks. The initial
adapter catalog constructs 40 actual embeddable widget types, but does not
serialize every type's model, actions, resources or all content pages. Visual CSS
editing covers a first useful subset, with full existing CSS available through a
line-based source editor. The application uses four existing dock groups,
single-selection authoring and bounded snapshot history; it is not a completed
Qt Designer replacement. F10b document editing and F14 extraction remain deferred.
