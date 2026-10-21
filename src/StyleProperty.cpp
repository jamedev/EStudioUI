#include <EStudioUI/StyleProperty.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace EStudioUI
{
namespace
{
using Member = std::variant<float Style::*, Color Style::*, Insets Style::*, Shadow Style::*, Length Style::*,
                            bool Style::*, std::string Style::*>;

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
                                                       {"translate-y", &Style::m_TranslateY}});
static_assert(s_Properties.size() == static_cast<std::size_t>(StyleProperty::Count));

const PropertyInfo& Info(StyleProperty property)
{
    const auto index = static_cast<std::size_t>(property);
    if (index >= s_Properties.size())
        throw std::invalid_argument("Unknown style property.");
    return s_Properties[index];
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
    return {Finite(value.m_X, -1000000), Finite(value.m_Y, -1000000), Finite(value.m_Blur), Sanitize(value.m_Color)};
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
                const float minimum = property == StyleProperty::RowHeight ? 12.0f
                                      : property == StyleProperty::TranslateX || property == StyleProperty::TranslateY
                                          ? -1000000.0f
                                          : 0.0f;
                const float maximum = property == StyleProperty::Opacity    ? 1.0f
                                      : property == StyleProperty::FontSize ? 512.0f
                                                                            : 1000000.0f;
                style.*member = Finite(*typed, minimum, maximum);
            }
            else if constexpr (std::is_same_v<Value, std::string>)
            {
                const bool valid =
                    property == StyleProperty::Align
                        ? (*typed == "stretch" || *typed == "start" || *typed == "center" || *typed == "end")
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
