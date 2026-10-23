# Property editing

The 0.23 increment adds `CollapsiblePanel`, `PropertyModel`, `PropertyGrid`,
`ColorEditor` / `ColorPicker`, `UnitEditor`, and `VectorEditor`. These are ordinary
EStudioUI widgets, usable in dock panels, dialogs, application forms or custom
compositions. See [CREATOR.md](CREATOR.md) for their first application consumer.

## Small common path

```cpp
#include <EStudioUI/PropertyGrid.h>
#include <EStudioUI/UiContext.h>

void AddInspector(EStudioUI::UiContext& ui)
{
    using namespace EStudioUI;
    std::vector<PropertyDefinition> properties{
        {"name", "Name", "General", PropertyKind::String, std::string("Part")},
        {"visible", "Visible", "General", PropertyKind::Boolean, true},
        {"position", "Position", "Geometry", PropertyKind::Vector, VectorValue{0, 0, 0}},
        {"color", "Color", "Appearance", PropertyKind::Color, Color{0.2f, 0.6f, 1, 1}}
    };
    PropertyDefinition length("length", "Length", "Geometry", PropertyKind::Number, 0.0254);
    length.m_Minimum = 0;
    length.m_Maximum = 100;
    length.m_Units = {{"m", "length", 1, 0}, {"mm", "length", 0.001, 0},
                      {"in", "length", 0.0254, 0}};
    properties.push_back(std::move(length));
    auto model = std::make_shared<PropertyModel>(ui.States(), std::move(properties));
    auto& inspector = ui.Root().Add<PropertyGrid>();
    inspector.SetHeight(SizeRule::Fill());
    inspector.SetModel(model);
    model->SetValue("length", 0.0508);
}
```

The schema has stable IDs and immutable type/range/category metadata. Application
values live in canonical `State<std::vector<PropertyValue>>`, exposed by
`PropertyModel::Values()`. Use its validators and StateStore transactions for
application rules and coherent multiple-property changes. Wrong types, nonfinite
numbers, incorrect vector shapes and invalid colors reject before publication.
Readonly metadata prevents inspector editing while allowing application updates.
Choice properties hold strings selected from their declared options.

`PropertyGrid` paints visible rows through the existing GPU renderer and shared
bounded text caches. It does not create one widget per property. At most one typed
editor subtree is active; selection uses stable property IDs, keyboard navigation,
and shared draggable scrollbars. Category collapse retires the active editor but
retains canonical values. Rejected edits restore canonical values and expose
`GetEditError()` / `EditRejected()`.

Metadata is bounded to 16,384 properties. Schema/category rebuilds and complete
State candidate validation are explicit full-sheet operations. Ordinary painting
visits the viewport rows. This is an inspector for moderate property sets, not a
replacement for external model paging. `SetEditorFactory` lets an application
replace an editor policy using composition or a derived built-in editor; its
callback receives owned candidate values. Custom editors that do not derive from
a recognized control must manage their own external-value synchronization.

## Independent editors

`CollapsiblePanel("Geometry").Content()` retains ordinary children while its
keyboard-operable header expands/collapses the section. `IsExpanded()` reports
the configured state even when an ancestor is hidden.

`ColorEditor` combines a checkerboard swatch and exact CSS hexadecimal input with
an anchored asynchronous `ColorPicker`. The picker supports RGB or HSV channels,
alpha, sliders, numeric values and a small palette. Canonical values are always
finite normalized RGBA. Hexadecimal input accepts `#RGB`, `#RGBA`, `#RRGGBB` and
`#RRGGBBAA`; invalid intermediate text preserves the last committed color. Hex
formatting quantizes display to eight bits; programmatic RGBA values retain their
float precision. Picker changes are live edits, and Escape dismisses the popover.

`UnitEditor` stores a finite canonical scalar. Display conversion is
`canonical = displayed * scale + offset`; switching units does not emit a value
change or modify canonical precision. Unit lists must have distinct symbols,
compatible dimension IDs and finite positive bounded conversions. This supports
offset units such as Celsius as well as CAD lengths. Explicit ranges use canonical
units and must contain the current value; an invalid range rejects instead of
silently changing application data. `SetPrecision` controls display stepping and
decimals. It does not introduce physical-unit semantics into logical CSS pixels.

`VectorEditor(2/3/4)` composes labeled numeric components and emits complete vector
snapshots. `SetValue` validates all components before changing the canonical tuple.
`Component(index)` exposes an ordinary `NumericInput` for further composition.
Matrices, coordinate frames and mixed/multiple-selection editing remain future
extensions rather than alternate representations hidden in this API.

`BindValue(UnitEditor&, State<double>&)`,
`BindValue(VectorEditor&, State<VectorValue>&)` and
`BindColor(ColorEditor&, State<Color>&)` use the existing binding validation,
rollback, receiver lifetime and error policies. State values are canonical units,
complete vectors and normalized colors.

## Styling

Load `assets/property-editors.css` after the application theme, or supply ordinary
CSS selectors for these types. `PropertyGrid::row` supports `:selected`,
`:alternate`, `:read-only` and `:invalid`; `::category` supports `:expanded`.
Composite editor children use ordinary selectors (`.property-editor`,
`.unit-choice`, `vector-component`, `vector-label`, `color-channel`,
`color-palette`, `.color-popover`, `.section-header`, `section-content`).
Existing gradients, borders, shadows, transforms, transitions and animations use
the same compiled CSS and GPU mechanisms as other controls. CSS is not parsed
while painting rows or interpolating values.

The first editor policies do not include expression evaluation, unit expressions,
color-management profiles, matrix editors or a universal C++ reflection system.
