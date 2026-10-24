# First F10b document increment (0.27)

Date: 2026-10-24. Tested source:
[`ac380caabb10d4547eceaefc2ce4778c1be288ca`](https://github.com/jamedev/EStudioUI/commit/ac380caabb10d4547eceaefc2ce4778c1be288ca).
[Source CI run](https://github.com/jamedev/EStudioUI/actions/runs/40121025021).
This follow-up records evidence; runtime and build inputs remain unchanged.

## Local acceptance

Final-source Release: **58/58 CTest suites passed**, 31.04 seconds. The new
TextDocument suite passes **466 checks**. State/transaction coverage passes
**92 checks**, including notification-phase inspection. The standalone
TextDocument example compiles and runs without creating a UI or GL context.
All existing public examples and Creator-generated C++/class exports also build.

The document tests cover exact Unicode/whitespace/tabs, CRLF/CR normalization,
trailing paragraphs and U+2028 hard breaks, invalid UTF-8 rejection, combining
sequence boundaries and insertion across a grapheme boundary, exact NUL
preservation, foreign/stale/forged positions, split/join/cross-paragraph edits,
stable IDs, inverse changes, equality no-ops, save checkpoints, unsaved reset,
undo/redo and grouped final-candidate rollback. Observer tests cover direct and
history-replay rejection from another state's notifications, ordered posted
follow-up commands, document deletion during publication, expired delivery and
state-domain teardown. Retained snapshots are read on workers after their
producer has been destroyed.

A 100,000-paragraph story receives a local edit. Tests check exact paragraph and
byte counts, shared payload identity for unrelated regions, retained old
snapshots, local invalidation, undo-root identity and a retained command estimate
smaller than one eighth of the serialized document. A separate deterministic
300-splice sequence compares the public model with a plain-string reference and
walks its retained undo/redo history. Assertions aggregate large inventories
rather than inflate the check count per row.

Release also includes actual OSMesa framebuffer regression checks for the demo,
Creator and database browser, plus existing rendering-profile smoke. The new
document store does not change renderer behavior or add a multiline widget; no
synthetic editor screenshot or document GPU-performance result is claimed.

Final-source ASan/UBSan: **58/58 suites passed**, 139.94 seconds, including real
framebuffer regressions and profile smoke. The standalone example also passes
under the sanitizers. Leak detection is disabled because this managed host
prevents LeakSanitizer from inspecting its process/thread tree; address and
undefined-behavior checks remain enabled, including the Creator generator.

Source CI Linux passes **58/58 suites**, 32.36 seconds. Isolated CPU
ThreadSanitizer passes **6/6**, 3.30 seconds. The independent native Windows
caption probe passes **51 checks** and observes six SC_SIZE scheduler ticks.

The exact-source CI run completed with all three jobs green:

| Check | Result |
| --- | --- |
| Windows/MSVC Release, x64-windows-estudioui | 58/58 suites, 10.28 s |
| Windows/MSVC Debug, same triplet | 58/58 suites, 120.95 s |
| Native Win32/WGL software OpenGL | Demo, Creator, database browser, rendering-profile smoke and text profile passed |
| Existing native text/cache regressions | 134 checks |
| Native Creator / database browser | 70 / 308 checks |
| Independent native caption regression | 51 checks; six SC_SIZE scheduler ticks |
| Additional native Debug lifecycle runs | Five runs, 103 checks each |
| Linux/OSMesa CI | 58/58 suites, 32.36 s |
| Isolated CPU ThreadSanitizer | 6/6 suites, 3.30 s |
| Local ASan/UBSan | 58/58 suites, 139.94 s |

No C++ compiler warnings were observed in final local or source-CI logs.
The pinned libunibreak port retains its existing CMake author warnings about
PARENT_SCOPE. PowerShell 7 parsing and Windows PowerShell 5.1 build/acceptance
orchestration pass. Software GL establishes pixel/work correctness, not physical
GPU throughput. No new physical IME, Narrator or mixed-monitor session occurred.

## Architecture and interchange scope

[DOCUMENT_ARCHITECTURE.md](../DOCUMENT_ARCHITECTURE.md) records primary
WordprocessingML sources and the FastOoxml audit at DeckShow
`8c7e9a41de6ca64df4cb05c6ef7167dab00da05e`. The audit reviewed the actual
Reader/Sink implementation, package API, coverage, build options and dependency
notices. FastOoxml's DOCX surface is currently a streaming structural/property
projection; typed DOCX writing and style resolution remain future work.

The EStudioUI core contains no ZIP/XML/widget/GL representation and adds no
runtime dependency. Future DOCX adapters must map typed paragraphs/inlines,
styles, stories and relationships, not flatten arbitrary XML runs into the
plain-text newline parser. No DOCX import/export, rich-text model, editor widget,
Word layout equivalence or lossless arbitrary-package round trip is claimed.

## Cost boundaries and remaining work

- Local edits share untouched paragraphs and copy tree paths; edited paragraph
  text and dense grapheme indexes are rebuilt in proportion to paragraph length.
- Byte/paragraph admission limits bound content, not total allocator RAM. Dense
  size_t indexes can add about eight times ASCII text bytes on x64, plus metadata.
- History uses conservative changed-payload/path estimates. External immutable
  snapshots retain their own storage outside history retention limits.
- Indexed paragraph access traverses the balanced tree; FindParagraph(id) is an
  explicit linear convenience scan. Grouped/nonadjacent changes conservatively
  invalidate the whole story.
- Positions bind to an exact immutable content root. Persistent selection/anchor
  mapping and incremental line layout are next; old ordinals are not reused as
  permanent anchors.
- Multiline input/selection, UIA ranges, physical IME/AT/mixed-DPI sessions,
  rich document structure/styles and DOCX fidelity have separate acceptance gates.
