#include <EStudioUI/InputValidator.h>

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
bool Digit(char value)
{
    return value >= '0' && value <= '9';
}

ValidationState DecimalSyntax(std::string_view text)
{
    if (text.empty())
        return ValidationState::Intermediate;
    std::size_t index = 0;
    if (text[index] == '+' || text[index] == '-')
        ++index;
    if (index == text.size())
        return ValidationState::Intermediate;
    bool digits = false;
    while (index < text.size() && Digit(text[index]))
    {
        digits = true;
        ++index;
    }
    if (index < text.size() && text[index] == '.')
    {
        ++index;
        while (index < text.size() && Digit(text[index]))
        {
            digits = true;
            ++index;
        }
    }
    if (!digits)
        return index == text.size() ? ValidationState::Intermediate : ValidationState::Invalid;
    if (index < text.size() && (text[index] == 'e' || text[index] == 'E'))
    {
        ++index;
        if (index < text.size() && (text[index] == '+' || text[index] == '-'))
            ++index;
        if (index == text.size())
            return ValidationState::Intermediate;
        const auto exponentStart = index;
        while (index < text.size() && Digit(text[index]))
            ++index;
        if (index == exponentStart)
            return ValidationState::Invalid;
    }
    return index == text.size() ? ValidationState::Acceptable : ValidationState::Invalid;
}
} // namespace

IntegerValidator::IntegerValidator(std::int64_t minimum, std::int64_t maximum) : m_Minimum(minimum), m_Maximum(maximum)
{
    if (minimum > maximum)
        throw std::invalid_argument("Integer validator bounds are inverted.");
}

ValidationState IntegerValidator::Validate(std::string_view text) const
{
    if (text.empty() || text == "+" || text == "-")
        return ValidationState::Intermediate;
    auto input = text;
    if (input.front() == '+')
        input.remove_prefix(1);
    std::size_t index = text.front() == '-' || text.front() == '+' ? 1 : 0;
    for (; index < text.size(); ++index)
        if (!Digit(text[index]))
            return ValidationState::Invalid;
    std::int64_t value = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    if (parsed.ec == std::errc::result_out_of_range)
        return ValidationState::Intermediate;
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size())
        return ValidationState::Invalid;
    return value >= m_Minimum && value <= m_Maximum ? ValidationState::Acceptable : ValidationState::Intermediate;
}

DoubleValidator::DoubleValidator(double minimum, double maximum) : m_Minimum(minimum), m_Maximum(maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
        throw std::invalid_argument("Numeric validator bounds must be finite and ordered.");
}

bool DoubleValidator::TryParse(std::string_view text, double& value)
{
    if (DecimalSyntax(text) != ValidationState::Acceptable)
        return false;
    if (text.front() == '+')
        text.remove_prefix(1);
    double candidate = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), candidate, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(candidate))
        return false;
    value = candidate;
    return true;
}

ValidationState DoubleValidator::Validate(std::string_view text) const
{
    const auto syntax = DecimalSyntax(text);
    if (syntax != ValidationState::Acceptable)
        return syntax;
    double value = 0;
    if (!TryParse(text, value))
        return ValidationState::Invalid;
    return value >= m_Minimum && value <= m_Maximum ? ValidationState::Acceptable : ValidationState::Intermediate;
}
} // namespace EStudioUI
