#include "DesignSnap.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
DesignSnapResult SnapDesign(Rect bounds, bool resize, Rect container, const std::vector<DesignReference>& references,
                            const DesignSnapSettings& settings)
{
    const auto valid = [](Rect r)
    {
        return std::isfinite(r.m_X) && std::isfinite(r.m_Y) && std::isfinite(r.m_Width) && std::isfinite(r.m_Height) &&
               r.m_Width >= 0 && r.m_Height >= 0;
    };
    if (!valid(bounds) || !valid(container) || !std::isfinite(settings.m_Distance) || settings.m_Distance < 0 ||
        settings.m_Distance > 100 || !std::isfinite(settings.m_GridSpacing) || settings.m_GridSpacing < 1 ||
        settings.m_GridSpacing > 1000)
    {
        throw std::invalid_argument("Invalid design snapping geometry or settings.");
    }
    DesignSnapResult result{bounds, {}};
    if (!settings.m_Enabled)
    {
        return result;
    }
    for (const bool x : {true, false})
    {
        const float start = x ? bounds.m_X : bounds.m_Y, size = x ? bounds.m_Width : bounds.m_Height;
        const std::array<float, 3> anchors{start, start + size / 2, start + size};
        const std::array<const char*, 3> names =
            x ? std::array{"Left", "CenterX", "Right"} : std::array{"Top", "CenterY", "Bottom"};
        float best = settings.m_Distance + 1, delta = 0;
        std::optional<DesignGuide> guide;
        if (settings.m_Grid)
        {
            const float origin = x ? container.m_X : container.m_Y;
            const float coordinate = resize ? anchors[2] : anchors[0];
            const float target =
                origin + std::round((coordinate - origin) / settings.m_GridSpacing) * settings.m_GridSpacing;
            if (std::abs(target - coordinate) <= settings.m_Distance)
            {
                best = std::abs(target - coordinate);
                delta = target - coordinate;
                guide = DesignGuide{x, target, {}};
                guide->m_Rule.m_Target.clear();
            }
        }
        const auto consider = [&](const DesignReference& reference)
        {
            if (!valid(reference.m_Bounds))
            {
                throw std::invalid_argument("Invalid sibling snapping bounds.");
            }
            const float position = x ? reference.m_Bounds.m_X : reference.m_Bounds.m_Y;
            const float extent = x ? reference.m_Bounds.m_Width : reference.m_Bounds.m_Height;
            const std::array<float, 3> targets{position, position + extent / 2, position + extent};
            for (std::size_t source = resize ? 2 : 0; source < 3; ++source)
            {
                for (std::size_t target = 0; target < 3; ++target)
                {
                    const float change = targets[target] - anchors[source];
                    if (std::abs(change) <= settings.m_Distance && std::abs(change) <= best &&
                        (!resize || size + change >= 24))
                    {
                        best = std::abs(change);
                        delta = change;
                        guide = DesignGuide{x, targets[target], {}};
                        guide->m_Rule.m_Anchor = names[source];
                        guide->m_Rule.m_Target = reference.m_Id;
                        guide->m_Rule.m_TargetAnchor = names[target];
                    }
                }
            }
        };
        consider({"parent", container});
        for (const auto& reference : references)
        {
            consider(reference);
        }
        if (guide)
        {
            if (x)
            {
                if (resize)
                {
                    result.m_Bounds.m_Width += delta;
                }
                else
                {
                    result.m_Bounds.m_X += delta;
                }
            }
            else
            {
                if (resize)
                {
                    result.m_Bounds.m_Height += delta;
                }
                else
                {
                    result.m_Bounds.m_Y += delta;
                }
            }
            result.m_Guides.push_back(*guide);
        }
    }
    return result;
}
} // namespace EStudioUI
