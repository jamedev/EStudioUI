#include <EStudioUI/SizeRule.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void ValidateLimits(float minimum, float maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < 0 || maximum < minimum || maximum > 1000000)
        throw std::invalid_argument("Size limits must satisfy 0 <= minimum <= maximum <= 1000000.");
}
} // namespace

SizeRule SizeRule::Fixed(float pixels)
{
    ValidateLimits(pixels, pixels);
    return {Mode::Fixed, pixels, pixels, pixels};
}

SizeRule SizeRule::Content(float minimum, float maximum)
{
    ValidateLimits(minimum, maximum);
    return {Mode::Content, 0, minimum, maximum};
}

SizeRule SizeRule::Fill(float weight, float minimum, float maximum)
{
    ValidateLimits(minimum, maximum);
    if (!std::isfinite(weight) || weight <= 0 || weight > 1000000)
        throw std::invalid_argument("A Fill weight must be finite and positive.");
    return {Mode::Fill, weight, minimum, maximum};
}

void SizeRule::Validate() const
{
    ValidateLimits(m_Minimum, m_Maximum);
    if (!std::isfinite(m_Value) || m_Value < 0 || m_Value > 1000000 || (m_Mode == Mode::Fill && m_Value <= 0) ||
        (m_Mode == Mode::Fixed && (m_Minimum != m_Value || m_Maximum != m_Value)) ||
        (m_Mode != Mode::Fixed && m_Mode != Mode::Content && m_Mode != Mode::Fill))
        throw std::invalid_argument("Invalid size contract.");
}
} // namespace EStudioUI
