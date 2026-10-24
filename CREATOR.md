# EStudioUICreator: first application increment

EStudioUICreator is a native C++ application built with EStudioUI itself. It uses
`MainWindow`, title-row menus, shared actions, overflow toolbars, docking,
`PropertyGrid`, real controls in a preview, and asynchronous platform file pickers
and background file I/O. It is an initial authoring application, not a completed
Qt Designer replacement.

## Build and run

The ordinary CMake build enables `ESTUDIOUI_BUILD_CREATOR=ON` and produces
`EStudioUICreator` beside the gallery. Its own post-build step stages its theme,
fonts and license notices. It can also be built when the gallery is disabled.

On Windows, use `./Build.ps1 -RunCreator`. The one-shot script continues to install
the pinned `x64-windows-estudioui` dependencies, build and test the project. The
Creator uses OpenGL 3.3 through the same renderer; no Qt dependency is introduced.

Launch `EStudioUICreator --open dashboard.json` to open a design directly. For
repeatable actual-renderer checks:

```sh
EStudioUICreator --headless --self-test --output creator-verification
```

`--headless` requires the Linux GLFW null/OSMesa configuration. Native Windows
uses `--self-test` without `--headless`. Software GL establishes correctness,
not hardware performance.

## First workflow

1. Select a widget on the design surface or the Document outline. Use the Widgets
   palette to add a registered control to a selected container; selecting a leaf
   adds to the design root. GPU-drawn icons identify widget categories. Drag a
   palette row onto a visible container to add there; the highlighted content
   slot receives the widget. Flow layouts use the drop location for insertion
   order; Constraints layouts use its content-relative position. Nested
   containers use their explicit registered content slot.
2. Edit canonical widget values, size contracts and a first set of typed CSS
   properties in the Inspector. Categories and the inspector section collapse.
   Numeric dimensions offer logical pixels and points; vectors edit positions.
3. Choose Column, Row, Grid, Flow or Constraints on a container. Drag widgets under
   a Constraints parent, or resize a selected widget with its corner handle.
   Container/sibling edges and centers provide snapping guides. Optional 8 px
   grid snapping uses the parent origin; Keep alignment constraints turns matched
   guides into ordinary named solver anchors. Escape/capture loss cancels an
   unfinished geometry gesture. A completed gesture is one undo step. Flow
   layouts retain their item ordering.
4. Edit named linear anchor rules in the Constraints panel and apply the complete
   table. References are stable sibling IDs or `parent`, with a multiplier,
   offset, equality/inequality and solver strength. Invalid references and values
   preserve the canonical design. Existing runtime solver reports expose relaxed
   or conflicting rules; a visual conflict inspector remains to be built.
5. Edit source CSS in the CSS source panel, then Apply CSS. This first source
   editor is an editable table of lines; it does not pretend to be a document or
   code editor. The typed CSS inspector initially covers background, text/border
   colors, border width/radius, opacity and font size. Full existing EStudioUI CSS
   remains available in source, including selectors, parts and animation rules.
6. Switch Live preview to interact with actual framework widgets. Save JSON or
   export editable C++ independently. Undo/redo uses the framework `UndoHistory`
   service, retaining at most 32 snapshot commands and 64 MiB estimated payload.
   Saving marks the exact captured history checkpoint; newer edits remain dirty.

Typed CSS values are explicit C++ presentation overrides, with the framework's
ordinary precedence over stylesheet rules. Clear CSS overrides returns a selected
node to the source cascade. Source CSS belongs to the containing UI context; the
example scopes selectors under `#design-root` to avoid restyling the authoring
application. General subtree stylesheet scopes are an identified framework gap.

## Two independent output paths

`UiDocument` is an owned design value with stable nodes, typed property overrides,
content layout metadata, named linear constraints and optional source CSS.

**Runtime description:** `ToJson` writes an open version 1 JSON document.
`UiDocument::FromJson(bytes, registry)` validates it, and
`BuildUiDocument(parent, document, registry)` constructs real framework widgets.
The parent may be an ordinary window container or a widget container hosted in a
canvas; the builder does not create a native window or run an event loop. The
`BuildUiDocument(canvas, bounds, document, registry)` overload uses the existing
`CanvasWidget::HostWidget` mechanism to place an owned dashboard subtree at a
world-space rectangle. Camera movement updates its placement; zoom changes the
host rectangle and relayouts controls, rather than uniformly scaling glyphs and
all widget paint. These remain sparse real widgets, separate from the BVH's large
lightweight geometry population.

**C++ source:** `ToCpp` writes ordinary `BuildUi(Widget& parent)` code using
`Add<Button>()`, setters, content-slot APIs, layout strategies and actual constraint
expressions. It creates no JSON and does not call the document loader or registry
at runtime. `UiStyleSheet()` returns optional CSS source for the application to
apply explicitly; the host supplies fonts, resources and business logic. The
build-time generator exports a catalog, compiles that generated C++, and checks
it against runtime JSON construction. The generated code remains editable.

The Creator export command now uses `ToCppClass(registry, className)` and writes
a complete `ClassName.h` / `ClassName.cpp` pair. It defines a Widget-derived class
with owned child construction, readable typed child observer members, `Content()`,
`StyleSheet()` and a `ConnectEvents()` hook. Guidance comments describe where to
add actions, bindings, validators and application logic. Both output files are
staged before replacing existing exports; failed installation attempts restore
previous files or retain backups with a diagnostic. Regeneration is an explicit
new export: preserve your hand-written additions before replacing it. It does not
merge edited source automatically. The build compiles an exported class containing
every registered catalog type.

Select a container subtree, enter a class name in the Widgets dock, and choose
**Add composition to palette**. Its children, constraints, properties and CSS form
a reusable session palette entry. Each insertion clones ordinary registered nodes
with fresh IDs and remapped internal constraints/ID selectors. Instances therefore
remain independent, editable and portable in the existing JSON format, with no
custom runtime loader dependency. **Export selected component** exports just the
selected subtree as the same standalone class pair. Up to 64 session compositions
are retained; save JSON or export source to keep one independently. A persistent
component catalog, linked-instance updates and arbitrary user C++ imports remain
future work.

Runtime construction deliberately leaves stylesheet installation explicit:

```cpp
#include <EStudioUI/UiDocument.h>
#include <EStudioUI/UiContext.h>

EStudioUI::UiBuildResult LoadDashboard(EStudioUI::UiContext& ui,
                                     std::string_view json)
{
    EStudioUI::WidgetRegistry registry;
    auto document = EStudioUI::UiDocument::FromJson(json, registry);
    std::string error;
    if (!ui.SetStyleSheet(document.m_StyleSheet, error))
        throw std::invalid_argument(error);
    return EStudioUI::BuildUiDocument(ui.Root(), document, registry);
}
```

Both paths share explicit `UiWidgetDescriptor` / `UiPropertyDescriptor` metadata.
Applications can register their own factory, content slot, typed property setters
and C++ emitters. Missing types/properties fail with diagnostics; they never
silently become generic panels. Factories build off-tree before attachment, so
validation or factory failure preserves the host. Registry callbacks are trusted
application code, not expressions accepted from a design file.

JSON limits are 4 MiB input, 96 parser nesting levels, 32 widget levels, 4,096
nodes, 256 property overrides per node, 1,024 rules per container and 256 KiB CSS.
Duplicate object keys, duplicate/unsafe IDs, unsupported versions, wrong property
types, unknown fields, invalid dimensions and dangling/cross-parent constraints
reject before construction. No business logic or executable expressions are
loaded. See `UiDocument.h` for the exact current data representation.

## Current adapter coverage

The initial catalog contains 40 actual embeddable types: Widget, Label, Button,
ToolButton, SplitToolButton, TextBox, NumericInput, Slider, CheckBox, RadioButton,
ComboBox, ProgressBar, BusyIndicator, Frame, Separator, GroupBox,
CollapsiblePanel, ScrollArea, ScrollBar, Splitter, StackedWidget, TabWidget,
ToolBar, ListWidget, TreeWidget, TableWidget, ImageView, CanvasWidget,
SketchWidget, OpenGLWidget, PaintWidget, FilePathEditor, FolderPicker, StatusBar,
PropertyGrid, ColorEditor, ColorPicker, UnitEditor, VectorEditor and MenuBar.
They share basic geometry, visibility, help and visual style metadata, with
specific text, toggle, scalar, color or vector adapters where applicable.

This is construction coverage, not complete serialization of every API. A new
ComboBox has no application model, MenuBar/SplitToolButton has no authored command
list, ImageView has no embedded image, and OpenGLWidget has no application render
callback. Tabs/stacks initially expose one default content page. Window,
MainWindow, docking, popup/dialog ownership and native services require document
or application-level adapters rather than arbitrary child factories.

## Missing blocks exposed by this application, in priority order

| Priority | Missing block | Reason and next acceptance target |
| --- | --- | --- |
| 1 | Complete widget adapters and named/multiple slots | Author combo choices, list/tree/table models, tab/stack pages, menu/tool actions, resources, units and command properties without losing information on either output path |
| 2 | Document-safe application services | Integrate the 0.24 DocumentWorkspace close workflow into Creator; shared bounded UndoHistory is now used. Crash recovery, migration policies and workspace preferences remain application work; open already confirms discarding edits and async file completion preserves newer data |
| 3 | Scoped styles and richer visual CSS editing | Keep theme editing inside a preview subtree, distinguish inherited/computed/overridden values, edit pseudo states/parts, gradients, shadows and keyframes without rebuilding controls |
| 4 | Visual hierarchy and constraints | Existing-widget reparent/reorder by drag, multi-selection, relationship handles and visible solver conflict reports; container/sibling snapping guides now ship |
| 5 | Canvas dashboard authoring | Build on existing sparse widget hosts; add visual host placement, document persistence of host bounds, zoom policy and large-scene widget lifecycle/resource budgets |
| 6 | Resource and data/action authoring | Stable image/font references, shared actions, model schemas and application binding hooks; exclude executable logic from untrusted descriptions |
| 7 | Multiline plain/code editor | Replace the temporary CSS line table; add selection, undo, composition and large-document text behavior through the deferred F10b track |
| 8 | Creator-specific integration acceptance | Native input methods, real accessibility tools, mixed-DPI/floating inspectors, physical resize/snap and hardware GPU profiling |

The application intentionally drives these discoveries while useful widgets ship.
It does not pull F10b document editing, a universal reflection system, or F14 module
extraction into this increment. Performance-sensitive property edits preserve
preview widget identity; structure/layout changes rebuild a staged preview. Full
scene diffing and large-design authoring remain explicit future work.

## Application appearance

Creator now starts with the neutral Minimal/System theme. The Appearance dock
selects light/dark or System mode and tunes presets, colors, font, density and
corners. See [THEMES.md](THEMES.md) for the shared framework API and explicit
platform-support limits. Design CSS remains independent of the authoring theme.
