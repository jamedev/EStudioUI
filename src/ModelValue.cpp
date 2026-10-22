#include <EStudioUI/ModelValue.h>

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
std::string FormatModelValue(const ModelValue& value)
{
    return std::visit(
        [](const auto& item) -> std::string
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::string>)
                return item;
            else if constexpr (std::is_same_v<T, bool>)
                return item ? "true" : "false";
            else if constexpr (std::is_arithmetic_v<T>)
            {
                char buffer[64];
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), item);
                if (result.ec != std::errc{})
                    throw std::runtime_error("Model numeric formatting failed.");
                return {buffer, result.ptr};
            }
            else
                return {};
        },
        value);
}

void ValidateModelValue(const ModelColumn& column, const ModelValue& value)
{
    if (column.m_ValueType != ModelValueType::Any && value.index() != static_cast<std::size_t>(column.m_ValueType))
        throw std::invalid_argument("Model value does not match its column's declared type.");
    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        throw std::invalid_argument("Model numeric values must be finite.");
    if (const auto* color = std::get_if<Color>(&value);
        color && (!std::isfinite(color->m_R) || !std::isfinite(color->m_G) || !std::isfinite(color->m_B) ||
                  !std::isfinite(color->m_A)))
        throw std::invalid_argument("Model colors must have finite components.");
    if (column.m_Validate)
        column.m_Validate(value);
}

ModelValue ParseModelValue(const ModelColumn& column, std::string_view text)
{
    ModelValue value;
    if (column.m_Parse)
        value = column.m_Parse(text);
    else if (column.m_ValueType == ModelValueType::Integer || column.m_ValueType == ModelValueType::Real)
    {
        if (text.empty())
            throw std::invalid_argument("A numeric model edit cannot be empty.");
        if (text.front() == '+')
            text.remove_prefix(1);
        if (text.empty())
            throw std::invalid_argument("A numeric model edit requires digits after its sign.");
        if (column.m_ValueType == ModelValueType::Integer)
        {
            std::int64_t number = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
                throw std::invalid_argument("Expected an exact integer model value.");
            value = number;
        }
        else
        {
            double number = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
                throw std::invalid_argument("Expected an exact numeric model value.");
            value = number;
        }
    }
    else if (column.m_ValueType == ModelValueType::Boolean)
    {
        if (text != "true" && text != "false" && text != "1" && text != "0")
            throw std::invalid_argument("Expected true, false, 1, or 0.");
        value = text == "true" || text == "1";
    }
    else
        value = std::string(text);
    ValidateModelValue(column, value);
    return value;
}

int CompareModelValues(const ModelValue& left, const ModelValue& right)
{
    const auto numeric = [](const ModelValue& value) { return value.index() >= 1 && value.index() <= 3; };
    if (numeric(left) && numeric(right))
    {
        const auto integer = [](const ModelValue& value)
        {
            return value.index() == 1 ? static_cast<std::int64_t>(std::get<bool>(value))
                                      : std::get<std::int64_t>(value);
        };
        const auto compareIntegerDouble = [](std::int64_t a, double b)
        {
            if (!std::isfinite(b))
                throw std::invalid_argument("Cannot sort nonfinite model numbers.");
            if (b >= 9223372036854775808.0)
                return -1;
            if (b < -9223372036854775808.0)
                return 1;
            const auto truncated = static_cast<std::int64_t>(b);
            if (a != truncated)
                return a < truncated ? -1 : 1;
            const auto exact = static_cast<double>(truncated);
            return exact < b ? -1 : exact > b ? 1 : 0;
        };
        if (left.index() != 3 && right.index() != 3)
        {
            const auto a = integer(left), b = integer(right);
            return a < b ? -1 : a > b ? 1 : 0;
        }
        if (left.index() != 3)
            return compareIntegerDouble(integer(left), std::get<double>(right));
        if (right.index() != 3)
            return -compareIntegerDouble(integer(right), std::get<double>(left));
        const auto a = std::get<double>(left), b = std::get<double>(right);
        if (!std::isfinite(a) || !std::isfinite(b))
            throw std::invalid_argument("Cannot sort nonfinite model numbers.");
        return a < b ? -1 : a > b ? 1 : 0;
    }
    if (left.index() != right.index())
        return left.index() < right.index() ? -1 : 1;
    if (const auto* a = std::get_if<std::string>(&left))
        return a->compare(std::get<std::string>(right));
    return 0;
}
} // namespace EStudioUI
