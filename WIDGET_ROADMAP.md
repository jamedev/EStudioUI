# EStudioUI widget development roadmap

Recorded: 2026-10-22. Baseline: EStudioUI 0.8.0, commit `dc14170`.

This is the agreed widget coverage roadmap for developing EStudioUI into a
capable C++ desktop UI toolkit. Entries below describe planned work, not shipped
APIs. Complete the applicable gates in [FOUNDATIONS_ROADMAP.md](FOUNDATIONS_ROADMAP.md)
before expanding the catalog. Update an entry only with implementation and
verification evidence; a styled demonstration alone does not complete a library
control.

The long-term target is to replace the Qt GUI layer for our applications. This
does not imply reproducing Qt's networking, database, multimedia, or every other
module. Keep the C++20, GPU drawing, permissive dependencies, explicit ownership,
CSS, and low idle-work requirements.

## Current coverage

The library already includes `Widget`, `Label`, `Button`, `ToolButton`,
single-line `TextBox`, `NumericInput`, `Slider`, `SplitToolButton`, `TabWidget`,
`ToolBar`, `ContextMenu`, `ListWidget`, `TreeWidget`, `TableWidget`, `CanvasWidget`,
`SketchWidget`, `OpenGLWidget`, and custom painting through `PaintWidget`/`Painter`.

Existing collections render viewport model entries rather than creating a widget
per row. Table sorting, column resizing, and basic editing already work. The
abstract `ScrollWidget` supplies collection scrolling; it is not a generic
scrolling container. The custom dial in the demo is not a public library `Dial`.

Priority means: **Essential** for ordinary applications, **High** for a powerful
engineering/CAD workspace, **Later** after the shared foundations and common
controls. All catalog entries below are **planned** at this baseline.

## Input and display

| ID | Widget/family | Scope | Priority |
| --- | --- | --- | --- |
| W01 | CheckBox | Two-state and indeterminate state, label/icon, keyboard operation | Essential |
| W02 | RadioButton and grouping | Exclusive selection and reusable grouping policy | Essential |
| W03 | ComboBox | Read-only/editable modes, virtualized popup, keyboard search, completion hooks | Essential |
| W04 | PlainTextEditor | Multiline text, wrapping, cross-line selection, scrolling, history, search/replace | Essential |
| W05 | RichTextEditor / RichTextView | Editable/read-only presentations of one structured document model | Essential |
| W06 | ImageView / image and icon display | Aspect fit/fill, alignment, scaling, DPI-aware resources | Essential |
| W07 | ProgressBar / BusyIndicator | Determinate/indeterminate progress; hidden indicators stop requesting frames | Essential |
| W08 | SearchBox / CompletionPopup | Search/clear actions, reusable suggestions, cancellable asynchronous results | High |
| W09 | Switch / SegmentedControl | Boolean/exclusive selection presentations sharing normal control semantics | High |
| W10 | MultiSelectComboBox / TagEditor | Checkable choices, removable tags, keyboard interaction | High |
| W11 | RangeSlider | Bounded interval selected by two handles | High |
| W12 | Dial | Public reusable circular value control | Later |
| W13 | Hyperlink / richer Label | Links, selectable text, icons, wrapping | High |

Password presentation, input masks, and completion hooks extend `TextBox`.
Integer/floating-point stepping remains based on `NumericInput`. Variants that
only change appearance should share implementations and semantic roles.

## Containers and application structure

| ID | Widget/family | Scope | Priority |
| --- | --- | --- | --- |
| W14 | ScrollArea | Scroll arbitrary child controls/forms with consistent clipping and focus reveal | Essential |
| W15 | ScrollBar | Standalone reusable horizontal/vertical control sharing collection scrollbar logic | Essential |
| W16 | Splitter | Nested user-resizable panes, hard minima, saved positions | Essential |
| W17 | StackedWidget | Persistent pages without requiring a visible tab strip | Essential |
| W18 | GroupBox / Frame / Separator | Titled/checkable groups and reusable visual structure | Essential |
| W19 | CollapsiblePanel / Accordion / ToolBox | Expandable inspector sections and categorized tools | High |
| W20 | MainWindow | Central workspace plus menus, toolbars, status area, owned windows | High |
| W21 | DockWidget / DockManager | Dock, float, tabify, rearrange, and restore panels | High |
| W22 | DocumentWorkspace | Multiple documents, close/reorder/detach tabs, unsaved-state indicators | High |
| W23 | Overlay / Drawer | Anchored or sliding temporary panels | Later |

## Commands, dialogs, feedback, and pickers

| ID | Widget/family | Scope | Priority |
| --- | --- | --- | --- |
| W24 | MenuBar / nested Menu | Submenus, icons, shortcut hints, checked actions; extend ContextMenu | Essential |
| W25 | ToolTip | Delayed pointer/focus help and reliable dismissal | Essential |
| W26 | Popup / Popover | Shared anchored interactive content and dismissal/focus behavior | Essential |
| W27 | Dialog | Modal/modeless operation, ownership, default/cancel actions | Essential |
| W28 | MessageBox / InputDialog / DialogButtonBox | Reusable confirmation, errors, input requests, standard responses | Essential |
| W29 | FileDialog / FolderPicker / FilePathEditor | Open/save/folder selection, filters, recent paths | Essential |
| W30 | StatusBar | Messages, coordinates, contextual state, progress | High |
| W31 | Banner / Toast / ValidationMessage | Nonmodal and field-level feedback with accessible announcements | High |
| W32 | ColorPicker / ColorEditor | Palette, RGB/HSV, alpha, hexadecimal input | High |
| W33 | FontPicker / FontEditor | Family, size, style, preview using the shared font service | High |
| W34 | DateEdit / TimeEdit / DateTimeEdit / Calendar | Validated temporal input and popup calendar | Later |
| W35 | CommandPalette | Searchable shared actions and shortcuts | High |
| W36 | Wizard / Stepper / Breadcrumb | Guided workflows and hierarchical navigation | Later |

Standard dialogs should share platform-service interfaces, with documented native
or toolkit implementations. Define asynchronous dialog results before adding
modal convenience APIs that could introduce nested event loops.

## Data-heavy views

| ID | Widget/family | Scope | Priority |
| --- | --- | --- | --- |
| W37 | PropertyGrid / PropertyInspector | Categories and typed editors for booleans, enums, strings, numbers, colors, vectors | High |
| W38 | TreeTable | Hierarchy with columns for assemblies, layers, model properties | High |
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

W04/W05 share text/font/input services, but a document editor needs a separate
storage/layout/editing layer from the existing single-line `TextBuffer`.

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
| W42 | Unit-aware scalar editors | Unit conversion, dimensional validation, display precision | High |
| W43 | Vector / matrix / coordinate-system editors | Composition of typed value editors | High |
| W44 | LogView / Console | Bounded streaming content, filtering, search, optional command input | High |
| W45 | CodeEditor | Plain editor plus gutter, syntax styling, completion/folding interfaces | Later |
| W46 | Plot / Chart / Legend / ColorMapEditor | Scientific data display with explicit data/LOD/performance contracts | Later |
| W47 | Document viewers and media integrations | Optional PDF/web/media adapters with independent dependency/license review | Later |

A ribbon is an optional composition of tabs, command groups, and toolbars.
Specialized CAD manipulators, rulers, guides, and selection handles extend the
canvas/sketcher layer rather than becoming mandatory core form controls.

## Delivery order and completion rules

1. Complete foundation Gates A/B for common controls, as described in
   [FOUNDATIONS_ROADMAP.md](FOUNDATIONS_ROADMAP.md). Establish text, platform,
   accessibility, and resource interfaces early; Gate C progresses with the
   text and application tracks.
2. First common-control batch: W01, W02, W03, W14, W16, W18, W07, W25.
   Build the shared popup/scroll/grouping mechanisms once.
3. Develop the document foundation and W04/W05 as a substantial track with their
   own text/input/layout acceptance gates.
4. Deliver application commands, dialogs, and native services; then docking,
   property editing, and stronger data views.
5. Add optional engineering modules according to actual application needs.

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
  keyboard/semantic patterns; native desktop accessibility still needs adapters.
- [Current architecture](ARCHITECTURE.md), [CSS contract](CSS.md), and
  [0.8 verification](verification/WIDGET_STYLING_0_8.md).
