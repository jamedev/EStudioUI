#include <EStudioUI/Measure.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
void MeasureConstraints::Validate() const
{
    for (const auto value : {m_Width, m_Height})
        if (value && (!std::isfinite(*value) || *value < 0))
            throw std::invalid_argument("Measurement dimensions must be finite and nonnegative.");
}

void MeasureResult::Validate() const
{
    for (const float value : {m_Minimum.m_X, m_Minimum.m_Y, m_Preferred.m_X, m_Preferred.m_Y})
        if (!std::isfinite(value) || value < 0)
            throw std::invalid_argument("Measured sizes must be finite and nonnegative.");
    if (m_Minimum.m_X > m_Preferred.m_X || m_Minimum.m_Y > m_Preferred.m_Y ||
        (m_Baseline && !std::isfinite(*m_Baseline)))
        throw std::invalid_argument("A measured minimum cannot exceed its preferred size; baselines must be finite.");
}
} // namespace EStudioUI
