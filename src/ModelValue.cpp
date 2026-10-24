#include <EStudioUI/ModelValue.h>

#include <charconv>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
std::string EncodeModelBlob(const ModelBlob& value)
{
    if (value.size() > 16 * 1024 * 1024)
    {
        throw std::length_error("A binary editor accepts at most 16 MiB.");
    }
    constexpr char digits[] = "0123456789ABCDEF";
    std::string text("0x");
    text.reserve(value.size() * 2 + 2);
    for (const auto byte : value)
    {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 15]);
    }
    return text;
}

ModelBlob DecodeModelBlob(std::string_view text)
{
    if (text.starts_with("0x") || text.starts_with("0X"))
    {
        text.remove_prefix(2);
    }
    if (text.size() % 2 || text.size() > 32 * 1024 * 1024)
    {
        throw std::invalid_argument("Binary edits require pairs of hexadecimal digits, up to 16 MiB.");
    }
    const auto digit = [](char value) -> unsigned
    {
        if (value >= '0' && value <= '9')
        {
            return static_cast<unsigned>(value - '0');
        }
        if (value >= 'a' && value <= 'f')
        {
            return static_cast<unsigned>(value - 'a' + 10);
        }
        if (value >= 'A' && value <= 'F')
        {
            return static_cast<unsigned>(value - 'A' + 10);
        }
        throw std::invalid_argument("Invalid hexadecimal digit.");
    };
    ModelBlob result;
    result.reserve(text.size() / 2);
    for (std::size_t offset = 0; offset < text.size(); offset += 2)
    {
        result.push_back(static_cast<std::uint8_t>((digit(text[offset]) << 4) | digit(text[offset + 1])));
    }
    return result;
}

std::string FormatModelValue(const ModelValue& value)
{
    return std::visit(
        [](const auto& item) -> std::string
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::string>)
                return item;
            else if constexpr (std::is_same_v<T, Color>)
            {
                constexpr char digits[] = "0123456789ABCDEF";
                std::string text("#");
                for (const auto channel : {item.m_R, item.m_G, item.m_B, item.m_A})
                {
                    const auto byte = static_cast<unsigned>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255));
                    text.push_back(digits[byte >> 4]);
                    text.push_back(digits[byte & 15]);
                }
                return text;
            }
            else if constexpr (std::is_same_v<T, ModelBlob>)
            {
                const ModelBlob prefix(
                    item.begin(), item.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(32, item.size())));
                return "BLOB (" + std::to_string(item.size()) + " bytes) " + EncodeModelBlob(prefix) +
                       (item.size() > 32 ? "..." : "");
            }
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
    if (!(column.m_Nullable && std::holds_alternative<std::monostate>(value)) &&
        column.m_ValueType != ModelValueType::Any && value.index() != static_cast<std::size_t>(column.m_ValueType))
    {
        throw std::invalid_argument("Model value does not match its column's declared type.");
    }
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
    else if (column.m_ValueType == ModelValueType::Blob)
    {
        value = DecodeModelBlob(text);
    }
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
    if (const auto* a = std::get_if<ModelBlob>(&left))
    {
        const auto& b = std::get<ModelBlob>(right);
        return *a < b ? -1 : *a > b ? 1 : 0;
    }
    return 0;
}
} // namespace EStudioUI
