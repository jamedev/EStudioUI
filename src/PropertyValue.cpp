#include <EStudioUI/PropertyValue.h>
#include <EStudioUI/TextBuffer.h>

#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
PropertyDefinition::PropertyDefinition() = default;

PropertyDefinition::PropertyDefinition(std::string id, std::string label, std::string category, PropertyKind kind,
                                       PropertyValue initial)
    : m_Id(std::move(id)), m_Label(std::move(label)), m_Category(std::move(category)), m_Kind(kind),
      m_Default(std::move(initial))
{
}

void ValidateColor(Color color)
{
    for (const float component : {color.m_R, color.m_G, color.m_B, color.m_A})
    {
        if (!std::isfinite(component) || component < 0 || component > 1)
        {
            throw std::invalid_argument("Color components must be finite and in [0,1].");
        }
    }
}

void ValidateUnits(const std::vector<UnitDefinition>& units)
{
    if (units.size() > 64)
    {
        throw std::length_error("An editor supports at most 64 display units.");
    }
    std::set<std::string> symbols;
    for (const auto& unit : units)
    {
        if (unit.m_Symbol.size() > 64 || unit.m_Dimension.size() > 128 || unit.m_Dimension.empty() ||
            !symbols.insert(unit.m_Symbol).second || unit.m_Dimension != units.front().m_Dimension ||
            !std::isfinite(unit.m_Scale) || unit.m_Scale < 1e-12 || unit.m_Scale > 1e12 ||
            !std::isfinite(unit.m_Offset) || std::abs(unit.m_Offset) > 1e12)
        {
            throw std::invalid_argument("Display units require distinct symbols and compatible finite conversions.");
        }
    }
}

void ValidatePropertyDefinition(const PropertyDefinition& property)
{
    if (static_cast<unsigned>(property.m_Kind) > static_cast<unsigned>(PropertyKind::Vector) || property.m_Id.empty() ||
        property.m_Id.size() > 128 || property.m_Label.size() > 4096 || property.m_Category.size() > 256 ||
        property.m_Help.size() > 4096 || !std::isfinite(property.m_Minimum) || !std::isfinite(property.m_Maximum) ||
        property.m_Minimum < -1e12 || property.m_Maximum > 1e12 || property.m_Minimum > property.m_Maximum ||
        !std::isfinite(property.m_Step) || property.m_Step <= 0 || property.m_Decimals < 0 ||
        property.m_Decimals > 12 || property.m_Choices.size() > 1024)
    {
        throw std::invalid_argument("Invalid property metadata: " + property.m_Id);
    }
    std::set<std::string> choices;
    for (const auto& choice : property.m_Choices)
    {
        if (choice.size() > 65536 || TextBuffer::Sanitize(choice) != choice || !choices.insert(choice).second)
        {
            throw std::invalid_argument("Property choices require distinct bounded single-line UTF-8 values.");
        }
    }
    ValidateUnits(property.m_Units);
    ValidatePropertyValue(property, property.m_Default);
}

void ValidatePropertyValue(const PropertyDefinition& definition, const PropertyValue& value)
{
    const auto scalar = [&](double number)
    {
        if (!std::isfinite(number) || number < definition.m_Minimum || number > definition.m_Maximum)
        {
            throw std::invalid_argument("Property '" + definition.m_Id + "' is outside its finite range.");
        }
    };
    const auto mismatch = [&]
    { throw std::invalid_argument("Incorrect value type for property '" + definition.m_Id + "'."); };
    switch (definition.m_Kind)
    {
    case PropertyKind::Boolean:
        if (!std::holds_alternative<bool>(value))
        {
            mismatch();
        }
        break;
    case PropertyKind::Number:
        if (!std::holds_alternative<double>(value))
        {
            mismatch();
        }
        scalar(std::get<double>(value));
        break;
    case PropertyKind::Choice:
    case PropertyKind::String:
        if (!std::holds_alternative<std::string>(value))
        {
            mismatch();
        }
        if (std::get<std::string>(value).size() > 65536 ||
            TextBuffer::Sanitize(std::get<std::string>(value)) != std::get<std::string>(value))
        {
            throw std::invalid_argument("Property strings require exact single-line UTF-8 within 64 KiB.");
        }
        if (definition.m_Kind == PropertyKind::Choice &&
            std::find(definition.m_Choices.begin(), definition.m_Choices.end(), std::get<std::string>(value)) ==
                definition.m_Choices.end())
        {
            throw std::invalid_argument("Property choice is not in its declared options.");
        }
        break;
    case PropertyKind::Color:
        if (!std::holds_alternative<Color>(value))
        {
            mismatch();
        }
        ValidateColor(std::get<Color>(value));
        break;
    case PropertyKind::Vector:
        if (!std::holds_alternative<VectorValue>(value))
        {
            mismatch();
        }
        if (std::get<VectorValue>(value).size() < 2 || std::get<VectorValue>(value).size() > 4 ||
            std::get<VectorValue>(value).size() != std::get<VectorValue>(definition.m_Default).size())
        {
            throw std::invalid_argument("Vector component count must match its two-to-four component schema.");
        }
        for (const double component : std::get<VectorValue>(value))
        {
            scalar(component);
        }
        break;
    }
}

std::string ColorToHex(Color color)
{
    ValidateColor(color);
    std::ostringstream output;
    output << '#' << std::hex << std::uppercase << std::setfill('0');
    for (const float component : {color.m_R, color.m_G, color.m_B, color.m_A})
    {
        output << std::setw(2) << static_cast<unsigned>(std::lround(component * 255));
    }
    return output.str();
}

Color ColorFromHex(std::string_view text)
{
    if (text.empty() || text.front() != '#' ||
        (text.size() != 4 && text.size() != 5 && text.size() != 7 && text.size() != 9))
    {
        throw std::invalid_argument("Use #RGB, #RGBA, #RRGGBB or #RRGGBBAA.");
    }
    const auto nibble = [](char digit) -> int
    {
        if (digit >= '0' && digit <= '9')
        {
            return digit - '0';
        }
        if (digit >= 'a' && digit <= 'f')
        {
            return digit - 'a' + 10;
        }
        if (digit >= 'A' && digit <= 'F')
        {
            return digit - 'A' + 10;
        }
        throw std::invalid_argument("Invalid hexadecimal color digit.");
    };
    const bool shortForm = text.size() < 6;
    float channels[4]{0, 0, 0, 1};
    const std::size_t count = shortForm ? text.size() - 1 : (text.size() - 1) / 2;
    for (std::size_t channel = 0; channel < count; ++channel)
    {
        const auto start = 1 + channel * (shortForm ? 1 : 2);
        const int value = shortForm ? nibble(text[start]) * 17 : nibble(text[start]) * 16 + nibble(text[start + 1]);
        channels[channel] = static_cast<float>(value) / 255;
    }
    return {channels[0], channels[1], channels[2], channels[3]};
}

std::string FormatPropertyValue(const PropertyValue& value)
{
    return std::visit(
        [](const auto& data) -> std::string
        {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, std::string>)
            {
                return data;
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return data ? "True" : "False";
            }
            else if constexpr (std::is_same_v<T, Color>)
            {
                return ColorToHex(data);
            }
            else
            {
                std::ostringstream output;
                output.imbue(std::locale::classic());
                output << std::setprecision(8);
                if constexpr (std::is_same_v<T, double>)
                {
                    output << data;
                }
                else
                {
                    for (std::size_t index = 0; index < data.size(); ++index)
                    {
                        if (index)
                        {
                            output << ", ";
                        }
                        output << data[index];
                    }
                }
                return output.str();
            }
        },
        value);
}
} // namespace EStudioUI
