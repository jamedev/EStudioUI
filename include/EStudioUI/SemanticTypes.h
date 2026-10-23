#pragma once

#include "Types.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace EStudioUI
{
//! Stable virtual child coordinates; zero identifies the containing real widget.
struct SemanticKey
{
    //! Model or scene identity, independent of the current row position.
    std::uint64_t m_Item = 0;
    //! Sub-item identity; table cells use the schema column plus one.
    std::uint64_t m_Part = 0;
    //! Dataset namespace, changed when a view is attached to unrelated data.
    std::uint64_t m_Generation = 0;
    //! Compares every identity coordinate.
    bool operator==(const SemanticKey&) const = default;
};

//! Collision-free identity without allocating a persistent node for every model record.
struct SemanticId
{
    //! Lifetime-unique identity of the containing actual widget.
    std::uint64_t m_Widget = 0;
    //! Stable virtual child coordinates; zero identifies the widget itself.
    SemanticKey m_Key{};
    //! Creates the empty identity.
    SemanticId() = default;

    //! Converts a real widget identity, including zero, without a virtual child.
    SemanticId(std::uint64_t widget) : m_Widget(widget)
    {
    }

    //! Creates a virtual identity in one actual widget's namespace.
    SemanticId(std::uint64_t widget, SemanticKey key) : m_Widget(widget), m_Key(key)
    {
    }

    //! Tests whether an actual containing widget is identified.
    explicit operator bool() const noexcept
    {
        return m_Widget != 0;
    }

    //! Compares owner, record, sub-item and dataset namespace.
    bool operator==(const SemanticId&) const = default;
};

//! Logical control families, independent of GPU paint records.
enum class SemanticRole
{
    Group,
    Window,
    Text,
    Button,
    CheckBox,
    TextBox,
    Slider,
    SpinBox,
    TabList,
    Tab,
    ToolBar,
    List,
    Tree,
    Table,
    Menu,
    MenuItem,
    Canvas,
    ListItem,
    TreeItem,
    Row,
    Cell,
    CanvasItem,
    RadioButton,
    ComboBox,
    ProgressBar,
    Splitter,
    Separator,
    ToolTip,
    MenuBar,
    Dialog,
    Image,
    ScrollBar
};

//! Supported interactions; advertised only when implemented by a semantic control.
enum class SemanticAction : unsigned
{
    None = 0,
    Focus = 1,
    Invoke = 2,
    Toggle = 4,
    SetText = 8,
    SetNumber = 16,
    Select = 32,
    AddToSelection = 64,
    RemoveFromSelection = 128,
    Expand = 256,
    Collapse = 512,
    ScrollIntoView = 1024
};

//! Combines supported interaction flags.
SemanticAction operator|(SemanticAction left, SemanticAction right) noexcept;

//! Tests whether an interaction is advertised.
bool HasSemanticAction(SemanticAction actions, SemanticAction action) noexcept;

//! Owned control metadata supplied by built-in or custom widgets.
struct SemanticInfo
{
    //! Logical control family.
    SemanticRole m_Role = SemanticRole::Group;
    //! Human-readable accessible name.
    std::string m_Name{};
    //! Additional accessible explanation.
    std::string m_Description{};
    //! Current textual value, independent of the name.
    std::string m_Value{};
    //! Optional current numeric value.
    std::optional<double> m_Number{};
    //! Optional inclusive numeric lower bound.
    std::optional<double> m_Minimum{};
    //! Optional inclusive numeric upper bound.
    std::optional<double> m_Maximum{};
    //! Optional positive numeric increment.
    std::optional<double> m_Step{};
    //! Supported canonical interactions.
    SemanticAction m_Actions = SemanticAction::None;
    //! Prevents value editing while preserving reading and selection.
    bool m_ReadOnly = false;
    //! Reports a persistent toggle state.
    bool m_Checked = false;
    //! Reports a third toggle state distinct from checked and unchecked.
    bool m_Indeterminate = false;
    //! Reports whether this item belongs to its container's selection.
    bool m_Selected = false;
    //! Reports support for simultaneous item selection.
    bool m_MultiSelectable = false;
    //! Reports a container policy requiring at least one selection.
    bool m_SelectionRequired = false;
    //! Reports that this item owns an expandable branch.
    bool m_Expandable = false;
    //! Reports the branch's current expansion state.
    bool m_Expanded = false;
    //! Total logical rows for a grid container, or empty for other controls.
    std::optional<std::size_t> m_RowCount{};
    //! Total schema columns for a grid container.
    std::optional<std::size_t> m_ColumnCount{};
    //! Current zero-based grid row for a cell.
    std::optional<std::size_t> m_Row{};
    //! Current zero-based schema column for a cell.
    std::optional<std::size_t> m_Column{};
    //! Changes only when logical children or their order change.
    std::uint64_t m_StructureRevision = 0;
};

//! On-demand child description; bounds use the owner's prepared layout coordinates.
struct SemanticVirtualNode
{
    //! Owned role, value, action and selection metadata.
    SemanticInfo m_Info{};
    //! Logical virtual parent; zero addresses the containing actual widget.
    SemanticKey m_Parent{};
    //! Logical rectangle clipped to the control's content viewport.
    Rect m_Bounds{};
    //! Per-item interaction availability combined with the owner's effective state.
    bool m_Enabled = true;
    //! Whether the item can receive logical focus through its containing control.
    bool m_Focusable = true;
    //! Whether the item is the containing control's current logical focus.
    bool m_Focused = false;
};

//! A queried widget state with owned strings and no borrowed widget pointers.
struct SemanticNode
{
    //! Lifetime-unique node identity.
    SemanticId m_Id = 0;
    //! Logical parent identity; zero for the root.
    SemanticId m_Parent = 0;
    //! Containing selection control for a virtual selectable child.
    SemanticId m_SelectionContainer = 0;
    //! Containing grid for a virtual table cell.
    SemanticId m_GridContainer = 0;
    //! Control metadata and supported interactions.
    SemanticInfo m_Info{};
    //! Actual-widget attachment revision, separate from a virtual collection's local structure revision.
    std::uint64_t m_TreeStructureRevision = 0;
    //! CSS type supplied as the native class name.
    std::string m_ClassName{};
    //! CSS identifier supplied as the automation identifier.
    std::string m_AutomationId{};
    //! Transformed, ancestor-clipped bounds in logical window coordinates.
    Rect m_Bounds{};
    //! Physical desktop bounds when the adapter supplies desktop coordinates.
    std::optional<Rect> m_ScreenBounds{};
    //! Effective interaction availability, including modal barriers.
    bool m_Enabled = false;
    //! Effective programmatic/CSS and popup visibility.
    bool m_Visible = false;
    //! Keyboard navigation eligibility.
    bool m_Focusable = false;
    //! Actual keyboard focus state.
    bool m_Focused = false;
    //! Reports hidden or completely clipped geometry.
    bool m_Offscreen = true;
};

//! Outcomes of canonical accessibility interactions.
enum class SemanticStatus
{
    Succeeded,
    Unavailable,
    Disabled,
    Unsupported,
    InvalidValue
};

//! Owned interaction request; unused value fields remain empty.
struct SemanticCommand
{
    //! One supported interaction, never a combination.
    SemanticAction m_Action = SemanticAction::None;
    //! Replacement text for SetText.
    std::string m_Text{};
    //! Replacement value for SetNumber.
    double m_Number = 0;
};
} // namespace EStudioUI

namespace std
{
//! Hashes complete semantic identities while equality retains collision-free coordinates.
template <> struct hash<EStudioUI::SemanticId>
{
    //! Combines the actual owner and every virtual child coordinate.
    std::size_t operator()(const EStudioUI::SemanticId& id) const noexcept
    {
        auto value = std::hash<std::uint64_t>{}(id.m_Widget);
        for (const auto part : {id.m_Key.m_Item, id.m_Key.m_Part, id.m_Key.m_Generation})
            value ^= std::hash<std::uint64_t>{}(part) + 0x9e3779b9U + (value << 6) + (value >> 2);
        return value;
    }
};
} // namespace std
