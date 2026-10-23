#include <EStudioUI/StyleProperty.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace EStudioUI
{
namespace
{
using Member =
    std::variant<float Style::*, Color Style::*, Insets Style::*, Shadow Style::*, Length Style::*, bool Style::*,
                 std::string Style::*, Gradient Style::*, Transform Style::*, ShadowLayers Style::*, Anchors Style::*>;

struct PropertyInfo
{
    std::string_view m_Name;
    Member m_Member;
};

const auto s_Properties = std::to_array<PropertyInfo>({{"background-color", &Style::m_Background},
                                                       {"color", &Style::m_Color},
                                                       {"border-color", &Style::m_BorderColor},
                                                       {"selection-background-color", &Style::m_SelectionBackground},
                                                       {"selection-color", &Style::m_SelectionColor},
                                                       {"placeholder-color", &Style::m_PlaceholderColor},
                                                       {"caret-color", &Style::m_CaretColor},
                                                       {"border-width", &Style::m_BorderWidth},
                                                       {"border-radius", &Style::m_Radius},
                                                       {"font-size", &Style::m_FontSize},
                                                       {"opacity", &Style::m_Opacity},
                                                       {"padding", &Style::m_Padding},
                                                       {"margin", &Style::m_Margin},
                                                       {"box-shadow", &Style::m_Shadow},
                                                       {"width", &Style::m_Width},
                                                       {"height", &Style::m_Height},
                                                       {"min-width", &Style::m_MinWidth},
                                                       {"min-height", &Style::m_MinHeight},
                                                       {"max-width", &Style::m_MaxWidth},
                                                       {"max-height", &Style::m_MaxHeight},
                                                       {"gap", &Style::m_Gap},
                                                       {"flex-grow", &Style::m_FlexGrow},
                                                       {"flex-direction", &Style::m_Row},
                                                       {"display", &Style::m_Displayed},
                                                       {"align-items", &Style::m_Align},
                                                       {"justify-content", &Style::m_Justify},
                                                       {"text-align", &Style::m_TextAlign},
                                                       {"row-height", &Style::m_RowHeight},
                                                       {"indent-size", &Style::m_Indent},
                                                       {"header-background", &Style::m_HeaderBackground},
                                                       {"alternate-background", &Style::m_AlternateBackground},
                                                       {"hover-background", &Style::m_HoverBackground},
                                                       {"grid-color", &Style::m_GridColor},
                                                       {"translate-x", &Style::m_TranslateX},
                                                       {"translate-y", &Style::m_TranslateY},
                                                       {"background-image", &Style::m_BackgroundImage},
                                                       {"transform", &Style::m_Transform},
                                                       {"shadow-layers", &Style::m_ShadowLayers},
                                                       {"overflow", &Style::m_ClipContent},
                                                       {"content", &Style::m_Decoration},
                                                       {"inset", &Style::m_Anchors},
                                                       {"text-wrap", &Style::m_WordWrap},
                                                       {"font-family", &Style::m_FontFamily},
                                                       {"font-weight", &Style::m_FontWeight},
                                                       {"font-style", &Style::m_FontStyle},
                                                       {"direction", &Style::m_TextDirection},
                                                       {"language", &Style::m_Language},
                                                       {"object-fit", &Style::m_ObjectFit},
                                                       {"object-position-x", &Style::m_ObjectPositionX},
                                                       {"object-position-y", &Style::m_ObjectPositionY}});
static_assert(s_Properties.size() == static_cast<std::size_t>(StyleProperty::Count));

const PropertyInfo& Info(StyleProperty property)
{
    const auto index = static_cast<std::size_t>(property);
    if (index >= s_Properties.size())
        throw std::invalid_argument("Unknown style property.");
    return s_Properties[index];
}

bool ValidFamilies(std::string_view value)
{
    if (value.empty() || value.size() > 1024 || value.find_first_of("\r\n\0", 0, 3) != std::string_view::npos)
        return false;
    char quote = 0;
    std::size_t first = 0, count = 0;
    for (std::size_t i = 0; i <= value.size(); ++i)
    {
        const char c = i == value.size() ? ',' : value[i];
        if ((c == '\'' || c == '"') && (!quote || quote == c))
            quote = quote ? 0 : c;
        else if (c == ',' && !quote)
        {
            const auto family = value.substr(first, i - first);
            if (family.find_first_not_of(" \t\"'") == std::string_view::npos || ++count > 16)
                return false;
            first = i + 1;
        }
    }
    return !quote && count > 0;
}

float Finite(float value, float minimum = 0, float maximum = 1000000)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("Animated style values must be finite.");
    return std::clamp(value, minimum, maximum);
}

Color Sanitize(Color color)
{
    return {Finite(color.m_R, 0, 1), Finite(color.m_G, 0, 1), Finite(color.m_B, 0, 1), Finite(color.m_A, 0, 1)};
}

Insets Sanitize(Insets value)
{
    return {Finite(value.m_Top), Finite(value.m_Right), Finite(value.m_Bottom), Finite(value.m_Left)};
}

Shadow Sanitize(Shadow value)
{
    return {Finite(value.m_X, -1000000), Finite(value.m_Y, -1000000),      Finite(value.m_Blur),
            Sanitize(value.m_Color),     Finite(value.m_Spread, -1000000), value.m_Inset};
}

Gradient Sanitize(Gradient value)
{
    if (value.m_Kind < Gradient::Kind::None || value.m_Kind > Gradient::Kind::Radial || value.m_Stops.size() > 8 ||
        (value.m_Kind != Gradient::Kind::None && value.m_Stops.size() < 2))
        throw std::invalid_argument("Gradients require two to eight stops.");
    value.m_Angle = Finite(value.m_Angle, -1000000);
    value.m_Center = {Finite(value.m_Center.m_X, -1000, 1000), Finite(value.m_Center.m_Y, -1000, 1000)};
    value.m_Radius = {Finite(value.m_Radius.m_X, 0.0001f, 1000), Finite(value.m_Radius.m_Y, 0.0001f, 1000)};
    float previous = -1000000;
    for (auto& stop : value.m_Stops)
    {
        stop.m_Position = std::max(previous, Finite(stop.m_Position, -1000000));
        stop.m_Color = Sanitize(stop.m_Color);
        previous = stop.m_Position;
    }
    return value;
}

Transform Sanitize(Transform value)
{
    if (value.m_Operations.size() > 8)
        throw std::invalid_argument("Transforms support at most eight operations.");
    value.m_Origin = {Finite(value.m_Origin.m_X, -1000, 1000), Finite(value.m_Origin.m_Y, -1000, 1000)};
    for (auto& operation : value.m_Operations)
    {
        if (operation.m_Kind < TransformOperation::Kind::Translate ||
            operation.m_Kind > TransformOperation::Kind::Matrix)
            throw std::invalid_argument("Unknown transform operation.");
        for (auto& component : operation.m_Values)
            component = Finite(component, -1000000);
    }
    return value;
}

ShadowLayers Sanitize(ShadowLayers value)
{
    if (value.m_Layers.size() > 7)
        throw std::invalid_argument("Box shadows support at most eight layers.");
    for (auto& shadow : value.m_Layers)
        shadow = Sanitize(shadow);
    return value;
}

Anchors Sanitize(Anchors value)
{
    for (auto* edge : {&value.m_Top, &value.m_Right, &value.m_Bottom, &value.m_Left})
    {
        if (edge->m_Unit < Length::Unit::Auto || edge->m_Unit > Length::Unit::Percent)
            throw std::invalid_argument("Decoration offsets require auto, pixels, or percentages.");
        edge->m_Value = Finite(edge->m_Value, -1000000);
    }
    return value;
}

Length Sanitize(Length value)
{
    if (value.m_Unit < Length::Unit::Auto || value.m_Unit > Length::Unit::Fraction)
        throw std::invalid_argument("Unknown length unit.");
    value.m_Value = Finite(value.m_Value);
    if (value.m_Unit == Length::Unit::Fraction && value.m_Value <= 0)
        value.m_Value = 0.0001f;
    return value;
}
} // namespace

std::string_view GetStylePropertyName(StyleProperty property)
{
    return Info(property).m_Name;
}

bool IsPaintOnlyProperty(StyleProperty property)
{
    switch (property)
    {
    case StyleProperty::BackgroundImage:
    case StyleProperty::Transform:
    case StyleProperty::ShadowLayers:
    case StyleProperty::ClipContent:
    case StyleProperty::Decoration:
    case StyleProperty::Anchors:
    case StyleProperty::Background:
    case StyleProperty::BorderColor:
    case StyleProperty::SelectionBackground:
    case StyleProperty::SelectionColor:
    case StyleProperty::PlaceholderColor:
    case StyleProperty::CaretColor:
    case StyleProperty::Radius:
    case StyleProperty::Opacity:
    case StyleProperty::BoxShadow:
    case StyleProperty::HeaderBackground:
    case StyleProperty::AlternateBackground:
    case StyleProperty::HoverBackground:
    case StyleProperty::GridColor:
    case StyleProperty::ObjectFit:
    case StyleProperty::ObjectPositionX:
    case StyleProperty::ObjectPositionY:
        return true;
    default:
        return false;
    }
}

StyleValue GetStyleValue(const Style& style, StyleProperty property)
{
    return std::visit([&](auto member) -> StyleValue { return style.*member; }, Info(property).m_Member);
}

void SetStyleValue(Style& style, StyleProperty property, const StyleValue& value)
{
    std::visit(
        [&](auto member)
        {
            using Value = std::remove_cvref_t<decltype(style.*member)>;
            const auto* typed = std::get_if<Value>(&value);
            if (!typed)
                throw std::invalid_argument("Incorrect value type for " + std::string(GetStylePropertyName(property)));
            if constexpr (std::is_same_v<Value, float>)
            {
                const float minimum = property == StyleProperty::FontWeight  ? 1.0f
                                      : property == StyleProperty::RowHeight ? 12.0f
                                      : property == StyleProperty::TranslateX || property == StyleProperty::TranslateY
                                          ? -1000000.0f
                                          : 0.0f;
                const float maximum = property == StyleProperty::FontWeight ? 1000.0f
                                      : property == StyleProperty::Opacity ||
                                              property == StyleProperty::ObjectPositionX ||
                                              property == StyleProperty::ObjectPositionY
                                          ? 1.0f
                                      : property == StyleProperty::FontSize ? 512.0f
                                                                            : 1000000.0f;
                style.*member = Finite(*typed, minimum, maximum);
            }
            else if constexpr (std::is_same_v<Value, std::string>)
            {
                const bool valid =
                    property == StyleProperty::FontFamily ? ValidFamilies(*typed)
                    : property == StyleProperty::FontStyle
                        ? (*typed == "normal" || *typed == "italic" || *typed == "oblique")
                    : property == StyleProperty::TextDirection
                        ? (*typed == "auto" || *typed == "ltr" || *typed == "rtl")
                    : property == StyleProperty::Language
                        ? (!typed->empty() && typed->size() <= 63 &&
                           std::all_of(typed->begin(), typed->end(),
                                       [](unsigned char c)
                                       {
                                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                  (c >= '0' && c <= '9') || c == '-';
                                       }))
                    : property == StyleProperty::ObjectFit
                        ? (*typed == "contain" || *typed == "cover" || *typed == "fill" || *typed == "none" ||
                           *typed == "scale-down")
                    : property == StyleProperty::Align ? (*typed == "stretch" || *typed == "start" ||
                                                          *typed == "center" || *typed == "end" || *typed == "baseline")
                    : property == StyleProperty::Justify
                        ? (*typed == "start" || *typed == "center" || *typed == "end" || *typed == "space-between")
                        : (*typed == "left" || *typed == "center" || *typed == "right");
                if (!valid)
                    throw std::invalid_argument("Unsupported discrete style value.");
                style.*member = *typed;
            }
            else if constexpr (std::is_same_v<Value, bool>)
                style.*member = *typed;
            else
                style.*member = Sanitize(*typed);
        },
        Info(property).m_Member);
}
} // namespace EStudioUI

namespace EStudioUI
{
std::vector<StyleProperty> FindStyleProperties(std::string_view name)
{
    std::vector<StyleProperty> result;
    for (int i = 0; i < static_cast<int>(StyleProperty::Count); ++i)
    {
        const auto property = static_cast<StyleProperty>(i);
        if (name == "all" || GetStylePropertyName(property) == name)
            result.push_back(property);
    }
    if (name == "background")
        result = {StyleProperty::Background, StyleProperty::BackgroundImage};
    else if (name == "box-shadow")
        result = {StyleProperty::BoxShadow, StyleProperty::ShadowLayers};
    else if (name == "object-position")
        result = {StyleProperty::ObjectPositionX, StyleProperty::ObjectPositionY};
    else if (name == "border")
        result = {StyleProperty::BorderWidth, StyleProperty::BorderColor};
    else if (name == "transform-origin")
        result = {StyleProperty::Transform};
    else if (name == "flex")
        result = {StyleProperty::FlexGrow};
    else if (name == "left" || name == "right" || name == "top" || name == "bottom")
        result = {StyleProperty::Anchors};
    return result;
}
} // namespace EStudioUI
