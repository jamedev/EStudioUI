#include <EStudioUI/Layout.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
//! Fixed and content bases are preserved. Fill items start at their minimum;
//! excess space is repeatedly shared until no item has spare capacity.
LayoutResult Layout::Distribute(float available, const std::vector<LayoutItem>& items)
{
    if (!std::isfinite(available))
        throw std::invalid_argument("Layout space must be finite.");
    available = std::max(0.0f, available);
    LayoutResult result;
    float used = 0;
    for (const auto& item : items)
    {
        if (!std::isfinite(item.m_Basis) || !std::isfinite(item.m_Minimum) || !std::isfinite(item.m_Maximum) ||
            !std::isfinite(item.m_Weight) || item.m_Basis < 0 || item.m_Minimum < 0 ||
            item.m_Maximum < item.m_Minimum || item.m_Weight < 0)
            throw std::invalid_argument("Invalid layout item.");
        const float size = std::clamp(item.m_Basis, item.m_Minimum, item.m_Maximum);
        result.m_Sizes.push_back(size);
        used += size;
    }
    result.m_Overflow = std::max(0.0f, used - available);
    float remaining = std::max(0.0f, available - used);
    for (std::size_t pass = 0; pass <= items.size() && remaining > 0.001f; ++pass)
    {
        float weight = 0;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (result.m_Sizes[i] + 0.001f < items[i].m_Maximum)
                weight += items[i].m_Weight;
        if (weight <= 0)
            break;
        float distributed = 0;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            if (item.m_Weight <= 0 || item.m_Maximum - result.m_Sizes[i] <= 0.001f)
                continue;
            const float addition = std::min(item.m_Maximum - result.m_Sizes[i], remaining * item.m_Weight / weight);
            result.m_Sizes[i] += addition;
            distributed += addition;
        }
        remaining = std::max(0.0f, remaining - distributed);
        if (distributed < 0.0001f)
            break;
    }
    result.m_Unused = remaining;
    return result;
}
} // namespace EStudioUI
