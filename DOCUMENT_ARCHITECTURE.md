# F10b document architecture and future DOCX interchange

Decision date: 2026-10-24. This is the boundary for the first document increment,
not a claim that EStudioUI implements a word processor or DOCX import/export.

## Responsibilities

Document storage and edit commands own logical content. A separate layout
service will derive paragraph measurements, lines, caret geometry and eventually pages.
Editor/view widgets will handle interaction and paint the visible layout through
the existing GPU renderer. File adapters will translate supported document
semantics and report interchange limitations. None of these responsibilities
requires the document storage service to include widgets, GLFW, OpenGL, ZIP or
XML headers.

The first implementation is a plain-text story: an ordered sequence of logical
paragraphs. It is useful independently of a UI. It is not the permanent schema
for every future document feature. A later rich document will compose block
containers, styled inline content and several independent text regions. Tables
will contain block content; headers, notes and text boxes will have their own
regions. The plain-text service must not become an OOXML element tree or silently
flatten all these structures into one string.

The future DOCX importer must construct typed paragraph/inline semantics from
structural events, rather than concatenate XML text and pass it through the
plain-text newline parser. Formatting spans and structural breaks must be mapped
before plain-text projection is requested.

## Review of the existing FastOoxml module

Reviewed DeckShow `main` at
[`8c7e9a41de6ca64df4cb05c6ef7167dab00da05e`](https://github.com/jamedev/DeckShow/commit/8c7e9a41de6ca64df4cb05c6ef7167dab00da05e).
The source and public headers take precedence over historical version labels in
its documentation.

- [Docx.h](https://github.com/jamedev/DeckShow/blob/8c7e9a41de6ca64df4cb05c6ef7167dab00da05e/modules/FastOoxml/include/fastooxml/Docx.h)
  already exposes a synchronous streaming Reader/Sink, story boundaries,
  paragraphs/runs, tables/rows/cells, property events, text/tab/break events,
  field instructions, hyperlinks and image references. Text callbacks may split
  text arbitrarily; consumers must copy borrowed data they retain.
- [Docx.cpp](https://github.com/jamedev/DeckShow/blob/8c7e9a41de6ca64df4cb05c6ef7167dab00da05e/modules/FastOoxml/src/docx/Docx.cpp)
  recognizes Strict and Transitional namespaces. It visits related parts once,
  projects compatibility fallback content with a warning and has explicit
  policies for deleted text and field instructions.
- [Package.h](https://github.com/jamedev/DeckShow/blob/8c7e9a41de6ca64df4cb05c6ef7167dab00da05e/modules/FastOoxml/include/fastooxml/Package.h)
  owns OPC parts, owner-scoped relationships and file-backed edits. Its atomic
  package save and unchanged-part copying can support a later editing adapter.
  They do not by themselves preserve semantics inside a rebuilt paragraph.
- [Coverage](https://github.com/jamedev/DeckShow/blob/8c7e9a41de6ca64df4cb05c6ef7167dab00da05e/modules/FastOoxml/docs/COVERAGE.md)
  explicitly limits DOCX to a structural/property projection: no typed DOCX
  writer or full style resolution is currently supplied.

That stream is an integration starting point, not the final rich-import
contract. The reviewed implementation does not expose all bookmark/comment/
revision ranges, individual note ownership, field boundaries or the outer
DrawingML inline/anchor geometry as typed events. Styles/numbering/theme parts
also need separate resolution. Extend FastOoxml's DOCX surface or use bounded
package/source mapping when these features are implemented; do not infer missing
semantics from its plain visible-text projection.

The future dependency should be an optional adapter using FastOoxml's Core/Docx
components. Do not copy its ZIP/XML implementation into EStudioUI, add a second
OOXML package engine, or require its XLSX/PPTX/Qt targets to edit plain text.
The reviewed CMake defaults enable the optional MPL-2.0 libeot decoder; configure
`FASTOOXML_WITH_EOT=OFF` for EStudioUI's previously agreed permissive-dependency
policy. No FastOoxml component or new dependency is linked in this increment.

## Semantic decisions informed by WordprocessingML

The decisions below are our design choices after reviewing the primary sources
listed at the end. They preserve room for DOCX and other formats without making
the native model depend on their serialized representation.

| Concern | Native document decision | Future adapter responsibility |
| --- | --- | --- |
| Paragraph versus line | Logical paragraph identity is independent of visual line wrapping; hard line breaks remain distinguishable | Map `w:p` separately from `w:br`; page/column breaks later need typed inline/block semantics |
| Text and positions | Preserve exact valid Unicode, tabs and repeated spaces; positions use explicit UTF-8 offsets with grapheme checks | Buffer/map text across arbitrary XML callbacks and runs; apply `xml:space` policy without stripping significant whitespace |
| Run boundaries | Stored text is not divided according to source XML run or shaping-run boundaries | Map formatting spans to logical positions; a source run boundary may occur inside a grapheme |
| Formatting | Future sparse paragraph and character properties, style IDs/inheritance and direct formatting | Keep omitted values distinct from explicit false/zero; account for toggle semantics and document/theme defaults |
| Widget CSS | Styles editor chrome and presentation defaults | Do not serialize computed widget CSS as document formatting |
| Sections and lists | Semantic ranges/definitions, independent of pagination output | Translate section boundaries and numbering definitions/overrides; do not bake list numbers into text |
| Tables and stories | Rich block containers and independently identified content regions | Preserve nested cells, main body, headers/footers, notes, comments and text boxes |
| Inline/floating objects | Resource references and text anchors separate from rendered geometry | Resolve owner-scoped relationships; retain inline versus anchored placement |
| Units and themes | Preserve precise unit-tagged/source semantic values when rich formatting arrives | Convert units at layout boundaries; retain theme/script-specific font/color intent rather than only resolved pixels/RGB |
| Unknown content | Explicit supported/editable subset and diagnostics | Retain package parts/source mappings where safe; reject destructive edits or use read-only placeholders when preservation cannot be guaranteed |

Rich layout must resolve paragraph-level bidi/script context across formatting
boundaries. An XML/style run is not an independently shaped paragraph; splitting
first and shaping each run in isolation would lose joining and directional
context. Visual shaping runs and grapheme boundaries remain derived mappings.

Source relationship IDs, XML node pointers and source run indices are never
application paragraph identities. Bookmarks, comments, fields, tracked changes,
links and content controls will require semantic ranges/atoms and position
mapping through edits. That work is separate from the first text store.

Preserving an unedited package part is different from safely preserving unknown
markup in an edited range. A future adapter session must own the source package,
part/range mappings and unsupported-feature diagnostics. Compatibility branches,
relationships and namespaces cannot be restored just by appending saved XML to
a rebuilt paragraph. Initial interchange will promise semantic fidelity for a
tested subset, with preservation/rejection rules. Byte-identical edited XML,
Word-identical pagination, field evaluation and floating-layout fidelity are
separate acceptance tracks.

## First implemented slice

`TextDocument` and `DocumentSnapshot` provide a plain-text story with an immutable
persistent paragraph tree. Unchanged paragraph payloads are shared across
snapshots and history. Indexed reads and tree-path edits avoid copying every
paragraph for each keystroke. Editing an affected paragraph rebuilds that
paragraph's text and grapheme index; this remains proportional to its length.
The first store has explicit byte, paragraph-length and paragraph-count limits.

The ordinary path is small; [the compiled example](examples/TextDocument.cpp)
also demonstrates worker snapshots, undo/redo and queued producer delivery:

```cpp
UiDispatcher dispatcher;
StateStore states(dispatcher);
TextDocument document(states);
document.Reset("Engineering notes\nBeam A\tLoad case 42\n");
const auto before = document.GetSnapshot();
const auto end = before.GetPosition(1, before.GetParagraph(1)->m_Text.size());
document.Replace(end, end, " — reviewed", "Review beam");
document.GetHistory().Undo();
```

The defaults admit 64 MiB of serialized UTF-8, up to one million paragraphs,
and at most 1 MiB per paragraph. These are content limits, not total RAM limits.
This first implementation stores a dense `size_t` grapheme-boundary index;
on x64 ASCII indexing can cost about eight times the text bytes, plus tree and
paragraph metadata. Future long-paragraph storage/indexing work must be measured
separately. `FindParagraph(id)` is an explicitly linear convenience scan;
normal edits use validated indexed positions and logarithmic tree traversal.

Snapshots can outlive their producer and can be read on workers. The mutable
document belongs to one StateStore owner. State-backed root publication and the
existing UndoHistory service commit document values/history coherently, including
validator rollback and command groups. A saved checkpoint identifies a historical
value rather than whichever revision is current when an asynchronous save ends.
Retained external snapshots retain their own storage independently of history
budgets.

Positions identify one story, exact immutable content root, paragraph ID, ordinal
and paragraph-local UTF-8 boundary. They reject foreign, deleted, mismatched or
stale coordinates. Undo can restore the exact historical root and make its old
positions valid again. Ordinals are snapshot coordinates, not persistent anchors.
The future layout/editor selection service must map anchors through edits rather
than reusing stale offsets. Direct edit/inverse-edit metadata supplies local
paragraph invalidation; unrelated roots use conservative full invalidation.

LF separates paragraphs in plain-text interchange; CRLF and CR normalize to LF.
Trailing LF preserves a final empty paragraph. U+2028 remains a hard line-break
character inside the paragraph, distinct from paragraph splitting and wrapping.
This is a plain-text convention, not the final representation of rich page/
column breaks. Invalid UTF-8 is rejected; NFC normalization and whitespace
sanitation are not performed. Paragraph edits occur on extended grapheme
boundaries. XML runs will be mapped before this segmentation when import arrives.
The native store can retain Unicode scalars such as NUL that XML interchange
cannot represent; a file adapter must diagnose such values rather than silently
dropping them.

Synchronous edits during StateStore observer dispatch are rejected to prevent
two observers from preparing independent replacements against the same committed
root and silently coalescing one away. Use the receiver-bound document handle's
queued `Post` path for ordered follow-up commands; ordinary explicit state
batches remain supported.

## Following increments and acceptance

1. Incremental paragraph/line layout over immutable snapshots; visible-line
   traversal, bounded retained layouts and long-paragraph work policies. Reuse
   shaping/font identities and the existing GPU cache. No visual line in storage.
2. A multiline editor/view with selection/anchor mapping, scrolling, shared
   TextInputClient composition and delta undo; then multiline accessibility and
   physical IME/DPI acceptance.
3. Native rich blocks/inlines, sparse style resolution and resources; tables,
   sections, lists, objects, document tooling and pagination incrementally.
4. Optional FastOoxml DOCX adapter and typed writer extension, with explicit
   fidelity diagnostics and preservation sessions. No changes to DeckShow's
   unrelated formats are required by this increment.

Before large asynchronous imports, add a format-neutral bulk builder and an
expected-revision root-adoption operation. A worker should be able to prepare
owned content within limits and publish it once on the document owner, without
replaying thousands of UI-side text edits or converting a rich import through
one flat string. The immutable storage boundary supports this addition; the
current snapshot API supplies worker reads, not an import-builder/adoption API.

The DOCX adapter corpus must cover fragmented callbacks/runs forming one
grapheme, exact spaces/tabs, paragraph/hard/page breaks, RTL/LTR mixtures,
inheritance versus explicit false, numbering restart, nested cell paragraphs,
sections, story references, image/link relationships and unsupported alternate
content. Validate generated packages and compare actual Office/LibreOffice
behavior for the supported subset before promising document interchange.

## Primary references reviewed

- [ECMA-376: Parts 1–4 and publication editions](https://ecma-international.org/publications-and-standards/standards/ecma-376/)
- [WordprocessingML structure and stories](https://learn.microsoft.com/en-us/office/open-xml/word/structure-of-a-wordprocessingml-document)
- [Runs and properties](https://learn.microsoft.com/en-us/office/open-xml/word/working-with-runs)
- [Paragraph styles](https://learn.microsoft.com/en-us/office/open-xml/word/how-to-create-and-add-a-paragraph-style-to-a-word-processing-document)
- [Break types](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.break?view=openxml-3.0.1)
- [Bold inheritance/toggle semantics](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.bold?view=openxml-3.0.1)
- [Section properties](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.sectionproperties?view=openxml-3.0.1)
- [Tables and cell content](https://learn.microsoft.com/en-us/office/open-xml/word/working-with-wordprocessingml-tables)
- [Markup compatibility and preservation](https://learn.microsoft.com/en-us/office/open-xml/general/introduction-to-markup-compatibility)

[Verification](verification/DOCUMENT_MODEL_0_27.md) records the exact tested
source, Windows Release/Debug, Linux, sanitizer and native framebuffer results.
