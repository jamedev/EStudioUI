# EStudioUI widget development roadmap

Recorded: 2026-10-22. Baseline: EStudioUI 0.8.0, commit `dc14170`.

This is the agreed widget coverage roadmap within EStudioUI's complete C++
application framework. Entries below distinguish delivered increments from remaining work. Complete the applicable gates in [FOUNDATIONS_ROADMAP.md](FOUNDATIONS_ROADMAP.md)
before expanding the catalog. Update an entry only with implementation and
verification evidence; a styled demonstration alone does not complete a library
control.

The long-term target includes complete application workspaces and reusable
framework services for applications with or without a UI. First build broad,
usable widget coverage; then extract reusable modules using real consumers to
establish their boundaries. F14 modularization/SDK packaging is deferred and
does not block widgets. Specific networking, database, multimedia, or other
systems require their own scoped roadmap; this scope change does not mark them
implemented. Keep the C++20, GPU drawing, permissive dependencies, explicit
ownership, CSS, and low idle-work requirements.

F09 input/actions/focus is delivered in 0.14, including shared command presentations
and nested menus on existing controls. The 0.16 F11 composition/virtual semantic implementation is documented in
[PLATFORM.md](PLATFORM.md); F10a is delivered and automated Gate B integration is verified. The first
common-control batch ships in 0.18 and the command/dialog batch in 0.19; human native/composition/accessibility
acceptance continues through the platform checklist. F10b document editing and
W04/W05 remain a later dedicated track; basic forms and workspaces do not wait
for a document engine. These are sequencing changes, not new shipped features.

The 0.23 [property-editing increment](PROPERTY_EDITING.md) supplies the next
inspector controls and their canonical validation/binding boundary. The first
[EStudioUICreator application](CREATOR.md) consumes them and records missing
framework blocks. Its JSON/runtime builder and direct C++ generator share an
explicit extensible adapter catalog; construction coverage does not mean every
widget API or content-slot configuration is serialized. F10b and F14 remain
deferred. See [verification](verification/PROPERTY_CREATOR_0_23.md).

The 0.24 [document workspace](DOCUMENT_WORKSPACE.md) delivers W22 close/reorder/
detach tabs, unsaved indicators and asynchronous Save/Discard/Cancel workflows.
Reusable bounded `UndoHistory` now serves the gallery and Creator. F16 versioned
application-session restore and F10b document editing remain separate work.

The 0.26.1 [text-readiness milestone](TEXT_READINESS.md) hardens shared font
validation and bounded GPU glyph churn, adds deterministic text profiles and a
focused Windows acceptance runner. F10b begins with separate multiline document
storage and incremental paragraph layout before rich editor widgets; physical
IME/AT/DPI acceptance remains explicitly recorded human work.

F10b begins in 0.27 with widget-independent immutable paragraph storage and
transactional edits/history. The multiline editor/view still follows incremental
layout; rich documents and DOCX interchange follow their own semantic/fidelity
gates. [Document architecture](DOCUMENT_ARCHITECTURE.md) records the reviewed
WordprocessingML and FastOoxml boundaries without introducing an XML-shaped core.

## Current coverage

The library already includes `Widget`, `Label`, `Button`, `ToolButton`,
single-line `TextBox`, `NumericInput`, `Slider`, `SplitToolButton`, `TabWidget`,
`ToolBar`, `ContextMenu`, `ListWidget`, `TreeWidget`, `TableWidget`, `CanvasWidget`,
`SketchWidget`, `OpenGLWidget`, `CheckBox`, `RadioButton`/`RadioGroup`, `ComboBox`,
`ScrollArea`, `Splitter`, `GroupBox`, `Frame`, `Separator`, `ProgressBar`,
`BusyIndicator`, `ToolTip`, `MenuBar`, `MenuItem`, `Popup`, `Dialog`,
`DialogButtonBox`, `MessageBox`, `InputDialog`, `ImageView`, shared `Image`/`Icon` resources,
`CollapsiblePanel`, `PropertyGrid`, `ColorEditor`/`ColorPicker`, `UnitEditor`, `VectorEditor`,
`DocumentWorkspace`, `TreeTable`, `SearchBox`, `CompletionPopup`, `CommandPalette`,
`ValidationMessage`, `NotificationCenter`, `LogView`, `SegmentedControl`,
`MultiSelectComboBox`, `TagEditor`, `RangeSlider`, `SelectableLabel`, `LinkLabel`,
`FontEditor`, `FontPicker`, `DateEdit`, `TimeEdit`, `DateTimeEdit`, `Calendar`,
and custom painting through `PaintWidget`/`Painter`.

Existing collections render viewport model entries rather than creating a widget
per row. Table sorting, column resizing, and basic editing already work. The
abstract `ScrollWidget` supplies collection scrolling; it is not a generic
scrolling container. The custom dial in the demo is not a public library `Dial`.

Priority means: **Essential** for ordinary applications, **High** for a powerful
engineering/CAD workspace, **Later** after the shared foundations and common
controls. Entries marked **0.18**, **0.19**, **0.20**, **0.21**, **0.22**, **0.23** **0.24**, **0.25** or **0.26** are delivered; the other catalog entries remain planned.

F09 adds shared action presentations to the existing buttons, toolbars, split
choosers, and context menus. Nested command menus are implemented through
`ContextMenu::AddMenu`; W24 adds MenuBar and MenuItem glyph icons/shortcut hints in 0.19.
No other planned widget family is marked delivered by the input milestone.

## Input and display

| ID | Widget/family | Scope | Priority |
| --- | --- | --- | --- |
| W01 | CheckBox | Two-state and indeterminate state, label/icon, keyboard operation | Essential — **0.18 delivered** |
| W02 | RadioButton and grouping | Exclusive selection and reusable grouping policy | Essential — **0.18 delivered** |
| W03 | ComboBox | Read-only/editable modes, virtualized popup, keyboard search, completion hooks | Essential — **0.18 delivered** |
| W04 | PlainTextEditor | Multiline text, wrapping, cross-line selection, scrolling, history, search/replace | Later document track; essential there |
| W05 | RichTextEditor / RichTextView | Editable/read-only presentations of one structured document model | Later document track; essential there |
| W06 | ImageView / image and icon display | Aspect fit/fill, alignment, PNG/JPEG/RGBA resources, shared command icons, DPI source choice and bounded context residency | Essential — **0.20 delivered** |
| W07 | ProgressBar / BusyIndicator | Determinate/indeterminate progress; hidden indicators stop requesting frames | Essential — **0.18 delivered** |
| W08 | SearchBox / CompletionPopup | Search/clear actions, reusable suggestions, stale-safe asynchronous results and explicit worker cancellation | High — **0.26 delivered** |
| W09 | Switch / SegmentedControl | Boolean/exclusive selection presentations sharing normal control semantics | High — **0.26 SegmentedControl delivered; Switch remains pending** |
| W10 | MultiSelectComboBox / TagEditor | Checkable choices, removable tags, keyboard interaction | High — **0.26 delivered** |
| W11 | RangeSlider | Bounded interval selected by two handles | High — **0.26 delivered** |
| W12 | Dial | Public reusable circular value control | Later |
| W13 | Hyperlink / richer Label | Application-routed LinkLabel, single-line SelectableLabel; existing Label supplies wrapping | High — **0.26 delivered for links/selectable text; integrated icon labels remain pending** |

Password presentation, input masks, and completion hooks extend `TextBox`.
Integer/floating-point stepping remains based on `NumericInput`. Variants that
only change appearance should share implementations and semantic roles.

## Containers and application structure

| ID | Widget/family | Scope | Priority |
| --- | --- | --- | --- |
| W14 | ScrollArea | Scroll arbitrary child controls/forms with consistent clipping and focus reveal | Essential — **0.18 delivered** |
| W15 | ScrollBar | Standalone reusable horizontal/vertical control sharing collection scrollbar logic | Essential — **0.21 delivered** |
| W16 | Splitter | Nested user-resizable panes, hard minima, saved positions | Essential — **0.18 delivered** |
| W17 | StackedWidget | Persistent pages without requiring a visible tab strip | Essential — **0.21 delivered** |
| W18 | GroupBox / Frame / Separator | Titled/checkable groups and reusable visual structure | Essential — **0.18 delivered** |
| W19 | CollapsiblePanel / Accordion / ToolBox | Expandable inspector sections and categorized tools | High — **0.23 CollapsiblePanel delivered; Accordion/ToolBox pending** |
| W20 | MainWindow | Central workspace, title-row menus/widgets, toolbars, status, owned windows | High — **0.22 delivered; interactive acceptance recorded separately** |
| W21 | DockWidget / DockManager | Four edge tab groups, splitters, dock/float, grip dragging, tabify/reorder and bounded restore | High — **0.22 delivered**; arbitrary nested dock trees remain later |
| W22 | DocumentWorkspace | Multiple documents, close/reorder/detach tabs, unsaved-state indicators | High — **0.24 delivered, with save/discard/cancel and shared history** |
| W23 | Overlay / Drawer | Anchored or sliding temporary panels | Later |

## Commands, dialogs, feedback, and pickers

| ID | Widget/family | Scope | Priority |
| --- | --- | --- | --- |
| W24 | MenuBar / nested Menu | Submenus, glyph icons, shortcut hints, checked actions; extend ContextMenu | Essential — **0.19 delivered** |
| W25 | ToolTip | Delayed pointer/focus help and reliable dismissal | Essential — **0.18 delivered** |
| W26 | Popup / Popover | Shared anchored interactive content and dismissal/focus behavior | Essential — **0.19 delivered** |
| W27 | Dialog | In-window modal/modeless operation, ownership, default/cancel actions | Essential — **0.19 delivered** |
| W28 | MessageBox / InputDialog / DialogButtonBox | Reusable confirmation, errors, input requests, standard responses | Essential — **0.19 delivered** |
| W29 | FileDialog / FolderPicker / FilePathEditor | Asynchronous open/save/folder selection, filters, recent paths, canonical path bindings | Essential — **0.22 delivered** |
| W30 | StatusBar | Message and permanent application widget area | High — **0.22 delivered** |
| W31 | Banner / Toast / ValidationMessage | Nonmodal and field-level feedback with live-region semantics | High — **0.26 NotificationCenter and ValidationMessage delivered; real AT speech remains a human gate** |
| W32 | ColorPicker / ColorEditor | Palette, RGB/HSV, alpha, hexadecimal input | High — **0.23 delivered** |
| W33 | FontPicker / FontEditor | Registered family, size, style and live preview using the shared font service | High — **0.26 delivered** |
| W34 | DateEdit / TimeEdit / DateTimeEdit / Calendar | Validated optional civil input, calendar and time picker | **0.26 delivered; localization/timezones remain separate** |
| W35 | CommandPalette | Searchable shared actions and shortcut hints | High — **0.26 delivered** |
| W36 | Wizard / Stepper / Breadcrumb | Guided workflows and hierarchical navigation | Later |

Standard dialogs should share platform-service interfaces, with documented native
or toolkit implementations. Define asynchronous dialog results before adding
modal convenience APIs that could introduce nested event loops.

## Data-heavy views

| ID | Widget/family | Scope | Priority |
| --- | --- | --- | --- |
| W37 | PropertyGrid / PropertyInspector | Categories and typed editors for booleans, enums, strings, numbers, colors, vectors | High — **0.23 virtual inspector delivered; mixed/multiple-selection editing pending** |
| W38 | TreeTable | Hierarchy with columns, shared selection, captured subtree moves, typed editing and virtual semantics | High — **0.25 delivered** |
| W39 | IconGrid / ThumbnailView | Virtualized assets, materials, documents, images | High |
| W40 | FileBrowser | Filesystem model with tree/list/thumbnail presentations | Later |
| W41 | FilterPanel / FilterEditor | Composable filtering for reusable data models | Later |

Extend existing list/tree/table controls with external/lazy models, stable item
identity, selection models and multiselection, delegates and typed editors,
icons/check states, drag/drop/reordering, proxy filtering/sorting, reorderable and
hideable columns, richer headers, and optional frozen rows/columns. Preserve
convenience in-memory adapters. Do not duplicate a data model for every view.

Extend `TabWidget` with close/reorder/detach/overflow behavior, and command widgets
with common Action/Command objects. These are extensions to shipped controls.

## Text documents

F10a first supplies font identity/fallback, shaped control text, selection/caret
mapping, composition, and wrapping display text for ordinary widgets. W04/W05
later reuse those services through F10b, with document storage/layout/editing
separate from the existing single-line `TextBuffer`.
Document-editor completion does not gate the first controls or workspaces.

| Stage | Scope |
| --- | --- |
| Document foundation | Paragraph storage, positions, edit commands, history byte limits, cross-line selection, incremental layout |
| Plain multiline editor | Wrapping, navigation, clipboard, search/replace, scroll integration, IME and direction handling |
| Rich document | Styled runs, font families/styles/sizes, colors, headings, alignment, indentation, lists, links |
| Embedded content | Images, tables, inline objects with explicit layout contracts |
| Document tooling | Formatting toolbar, outline, reusable styles, find/replace |
| Optional publishing | Pagination, rulers, page setup, print preview, printing/export adapters |

The read-only viewer reuses document layout. Layout invalidates affected
paragraphs and paints the viewport. HTML/Markdown interchange and PDF/office
formats require explicit supported subsets and separate adapters; no format
compatibility is implied by the presence of a rich-text editor.

## Optional engineering modules

| ID | Module/control family | Scope | Priority |
| --- | --- | --- | --- |
| W42 | Unit-aware scalar editors | Unit conversion, dimensional validation, display precision | High — **0.23 affine UnitEditor delivered** |
| W43 | Vector / matrix / coordinate-system editors | Composition of typed value editors | High — **0.23 VectorEditor(2/3/4) delivered; matrices/frames pending** |
| W44 | LogView / Console | Bounded worker streaming with virtualized read-only rows | High — **0.26 LogView delivered; dedicated filtering/search UI and command console remain pending** |
| W45 | CodeEditor | Plain editor plus gutter, syntax styling, completion/folding interfaces | Later |
| W46 | Plot / Chart / Legend / ColorMapEditor | Scientific data display with explicit data/LOD/performance contracts | Later |
| W47 | Document viewers and media integrations | Optional PDF/web/media adapters with independent dependency/license review | Later |

A ribbon is an optional composition of tabs, command groups, and toolbars.
Specialized CAD manipulators, rulers, guides, and selection handles extend the
canvas/sketcher layer rather than becoming mandatory core form controls.

## Delivery order and completion rules

1. Build on delivered F09 input/actions/focus; finish F11 and F10a and the
   Gate B evidence in [FOUNDATIONS_ROADMAP.md](FOUNDATIONS_ROADMAP.md). Carry the
   relevant F12 resource limits, F13 diagnostics, and F15 checks with each feature.
   Deferred F14 packaging and F10b document editing are not prerequisites.
2. Delivered first common-control batch (0.18): W01, W02, W03, W14, W16, W18, W07, W25.
   [Contracts](COMMON_CONTROLS.md) and [evidence](verification/WIDGETS_0_18.md) record
   actual behavior, bounds and native/manual acceptance.
   Build the shared popup/scroll/grouping mechanisms once.
3. Command/dialog batch (0.19): W24, W26, W27 and W28. See
   [contracts](COMMAND_DIALOGS.md) and [evidence](verification/COMMAND_DIALOGS_0_19.md).
   Image/icon resources follow in 0.20; standalone ScrollBar/StackedWidget in 0.21.
   File/path controls and the initial main-window/docking workspace follow in 0.22.
   Continue property editing, search/completion, feedback and stronger data views.
   Develop F16 history/workspace services with their application consumers.
4. After broad widget/workspace use establishes the boundaries, revisit F14 to
   extract reusable framework modules and prove installed UI/headless consumers.
5. Develop F10b and W04/W05 later with their own document/input/layout acceptance
   gates. Add optional engineering modules according to actual application needs.

Every delivered family requires consistent CSS parts/states, focus/keyboard and
accessibility semantics, lifetime-safe callbacks, documented threading, and
input geometry matching paint. Model views require viewport virtualization.
Animations and asynchronous work require cancellation/visibility rules. Validate
behavior and actual renderer output; preserve the full-redraw reference and
zero-idle-work checks. Do not require a separate widget subclass for every
visual variation.

## References

- [Qt widget gallery](https://doc.qt.io/qt-6/gallery.html): coverage comparison.
- [Qt rich text architecture](https://doc.qt.io/qt-6/richtext.html): document,
  cursor, and layout separation as a reference design.
- [W3C interaction patterns](https://www.w3.org/WAI/ARIA/apg/patterns/): useful
  keyboard/semantic patterns; the initial Windows accessibility adapter still needs
  virtualized providers, broader patterns and real assistive-technology evidence.
- [Current architecture](ARCHITECTURE.md), [CSS contract](CSS.md), and
  [0.8 verification](verification/WIDGET_STYLING_0_8.md).

The 0.18 and 0.19 batches add no new runtime dependency. Next catalog work can build on
these controls; document storage/editor families remain F10b and module extraction
remains deferred. [Image/icon resources](IMAGE_RESOURCES.md) ship in 0.20 with
per-feature F12 resource budgets and actual framebuffer checks. Standalone
ScrollBar/StackedWidget ship in [0.21](SCROLL_STACK.md). Native-service widgets and
broader workspace controls follow; document editing and module extraction remain deferred.

The 0.21 ScrollBar/StackedWidget increment is verified at `795f638`: Windows
Release/Debug 42/42 each, native Win32/WGL framebuffer checks and five extra
native Debug runs of 99 checks each; Linux/software GL 41/41 and isolated TSan
6/6; local ASan/UBSan 41/41. See [verification](verification/SCROLL_STACK_0_21.md)
for exact source, actual gallery/contract captures and remaining human platform gates.

The 0.20 image/icon increment is verified at `fbdf62d`: Windows Release/Debug
41/41 each, native Win32/WGL framebuffer checks and five extra native Debug runs
of 97 checks, Linux/software GL 40/40 and isolated TSan 6/6, local ASan/UBSan
40/40, and a standalone 800-decode resource ThreadSanitizer probe. See
[verification](verification/IMAGES_0_20.md) for actual pixels, resource contracts
and platform limits. No C++ compiler warnings were observed.


The 0.19 command/dialog batch is verified at `77ae3ac`: Windows 40/40 Release and
Debug, five extra native Debug runs of 95 checks, Linux/software GL 38/38, TSan
6/6 and local ASan/UBSan 38/38. The verification guide records actual captures,
zero-idle-work checks, native cancellation hardening and remaining manual gates.


The 0.22 increment implements W29, W20/W30 and the first W21 workspace topology,
including Windows title-row menus/widgets with native non-client behavior.
[WORKSPACES.md](WORKSPACES.md) defines ownership, bounds, cross-context handles,
CSS and persistence. [Verification](verification/WORKSPACES_0_22.md) distinguishes
automated checks from Windows 11 snap, mixed-DPI and interactive accessibility acceptance.
W22 document workspaces are delivered in 0.24. Arbitrary nested dock trees, F10b and F14 remain separate work.


Final 0.22 source `d2aa847` passes Windows Release/Debug **44/44** each,
native Win32/WGL framebuffer integration and PowerShell profile smoke, five
extra native Debug lifecycle runs (**99 checks each**), Linux **42/42**, isolated
TSan **6/6**, and local ASan/UBSan **42/42**. See the
[final-source CI run](https://github.com/jamedev/EStudioUI/actions/runs/39980739125)
and [verification record/captures](verification/WORKSPACES_0_22.md). Windows 11
flyouts, physical mixed-DPI/snap and real AT remain interactive acceptance work.


The 0.25 stronger-data-view increment delivers TreeTable, shared captured row
moves for table/tree/list views, column hiding/reordering/resizing, composed
bool/choice/numeric/color/null/text/binary cell editors and confirmed async edits.
Generic worker-safe ModelAdapter providers and the optional SQLite adapter support
portable typed filters, multi-key sort and bounded result windows. See
[DATA_VIEWS.md](DATA_VIEWS.md) and the working [database browser](SQLITE_BROWSER.md).
Selection is scoped to loaded records; persistent global selection, OS/cross-window
data drags, structural SQL operations and keyset pagination remain later work.


Final 0.25 source `5d304a8` passes Windows Release/Debug 54/54 each and native
Win32/WGL integration (browser 308 checks), native caption desktop-pixel checks,
Linux 54/54, isolated TSan 6/6 and local ASan/UBSan 54/54. The
[verification record](verification/DATA_VIEWS_0_25.md) includes actual captures and
remaining scope limits; [CI](https://github.com/jamedev/EStudioUI/actions/runs/40099945384)
is green for that exact source.

## Search, feedback and remaining common controls (0.26)

The requested priorities 7 and 8 are implemented together, using existing input,
model, state, font and renderer services. The two production demo pages, compiled
examples, general Creator registration and neutral theme rules provide working
consumers. See [search/feedback contracts](SEARCH_FEEDBACK.md) and
[common-control contracts](COMMON_PICKERS.md).

Completion IDs expire on edits, focus loss and dismissal. The bounded log inbox
works independently of ordinary UI queue capacity. Static feedback expiry uses
GUI-clock deadlines rather than per-frame polling. Visible row work and font
layouts remain bounded/cached. Range endpoints have virtual Slider identities;
calendar dates have stable virtual ListItem identities. Windows exposes feedback
live priorities through UI Automation's existing bounded observation path.

F10b structured document editing and F14 module extraction remain deferred.
Further catalog work includes IconGrid/ThumbnailView, guided navigation, richer
property adapters, a command console and application-specific model extensions.
This increment does not claim OS-wide font enumeration, timezone conversion or
complete specialized Creator serialization of every new value.

Final 0.26 source `790d039` is verified: Windows Release/Debug 56/56 each,
native demo/Creator/browser framebuffer integration, five extra native Debug
lifecycle runs, Linux 56/56, isolated TSan 6/6 and local ASan/UBSan 56/56.
See the [verification and captures](verification/PRODUCTIVITY_0_26.md) and
[source CI](https://github.com/jamedev/EStudioUI/actions/runs/40111062486).
