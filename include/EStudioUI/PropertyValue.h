#pragma once

#include "Types.h"

#include <string>
#include <variant>
#include <vector>

namespace EStudioUI
{
//! A two-, three-, or four-component editable vector.
using VectorValue = std::vector<double>;

//! Owned values shared by inspectors, UI documents and application state.
using PropertyValue = std::variant<bool, double, std::string, Color, VectorValue>;

//! One affine conversion from display units to a canonical dimensional value.
struct UnitDefinition
{
    //! Short display suffix.
    std::string m_Symbol;

    //! Dimension identity; conversion across dimensions is rejected.
    std::string m_Dimension;

    //! Canonical value = displayed value * scale + offset.
    double m_Scale = 1;

    //! Canonical offset, useful for temperature units.
    double m_Offset = 0;

    //! Compares complete unit definitions.
    bool operator==(const UnitDefinition&) const = default;
};

//! Supported built-in editor policies.
enum class PropertyKind
{
    Boolean,
    Number,
    String,
    Choice,
    Color,
    Vector
};

//! Immutable metadata for one typed property.
struct PropertyDefinition
{
    //! Creates an empty string-property definition.
    PropertyDefinition();

    //! Creates a typed definition with safe range and presentation defaults.
    PropertyDefinition(std::string id, std::string label, std::string category, PropertyKind kind,
                       PropertyValue initial);

    //! Stable identity, independent of row order or caption.
    std::string m_Id;

    //! Visible caption.
    std::string m_Label;

    //! Collapsible category; empty means no category header.
    std::string m_Category;

    //! Editor policy and value representation.
    PropertyKind m_Kind = PropertyKind::String;

    //! Initial canonical value.
    PropertyValue m_Default = std::string{};

    //! Inclusive canonical scalar/component minimum.
    double m_Minimum = -1e12;

    //! Inclusive canonical scalar/component maximum.
    double m_Maximum = 1e12;

    //! Positive display increment.
    double m_Step = 1;

    //! Display precision, from zero through twelve.
    int m_Decimals = 3;

    //! Allowed string choices for choice editors.
    std::vector<std::string> m_Choices;

    //! Compatible display units for scalar editors.
    std::vector<UnitDefinition> m_Units;

    //! Prevents editing in the inspector; application state remains writable.
    bool m_ReadOnly = false;

    //! Optional help text.
    std::string m_Help;
};

//! Validates finite normalized RGBA data without clamping.
void ValidateColor(Color color);

//! Validates bounded compatible units, rejecting duplicates and unsafe conversions.
void ValidateUnits(const std::vector<UnitDefinition>& units);

//! Validates immutable schema metadata, conversions and its initial value.
void ValidatePropertyDefinition(const PropertyDefinition& definition);

//! Validates a value against the complete immutable editor contract.
void ValidatePropertyValue(const PropertyDefinition& definition, const PropertyValue& value);

//! Formats an owned value for a virtual inspector row.
std::string FormatPropertyValue(const PropertyValue& value);

//! Formats normalized RGBA as #RRGGBBAA.
std::string ColorToHex(Color color);

//! Accepts exact #RGB, #RGBA, #RRGGBB or #RRGGBBAA input.
Color ColorFromHex(std::string_view text);
} // namespace EStudioUI
