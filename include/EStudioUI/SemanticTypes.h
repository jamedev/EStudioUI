#pragma once

#include "Types.h"

#include <cstdint>
#include <optional>
#include <string>

namespace EStudioUI
{
//! Lifetime-unique identity, independent of allocation addresses and CSS identifiers.
using SemanticId = std::uint64_t;

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
    Canvas
};

//! Supported interactions; advertised only when implemented by a semantic control.
enum class SemanticAction : unsigned
{
    None = 0,
    Focus = 1,
    Invoke = 2,
    Toggle = 4,
    SetText = 8,
    SetNumber = 16
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
};

//! A queried widget state with owned strings and no borrowed widget pointers.
struct SemanticNode
{
    //! Lifetime-unique node identity.
    SemanticId m_Id = 0;
    //! Logical parent identity; zero for the root.
    SemanticId m_Parent = 0;
    //! Control metadata and supported interactions.
    SemanticInfo m_Info{};
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
