#include "ScrollGeometry.h"

#include <algorithm>
#include <cmath>

namespace EStudioUI::Detail
{
namespace
{
float Dimension(const Length& length, float available, float fallback, float minimum, float maximum)
{
    minimum = std::max(0.0f, minimum);
    maximum = std::max(minimum, maximum);
    return std::min(available, std::clamp(length.Resolve(available, fallback), minimum, maximum));
}
} // namespace

Rect ScrollTrackContent(Rect bounds, const Style& style)
{
    const float border = std::max(0.0f, style.m_BorderWidth);
    const float left = std::min(bounds.m_Width, border + style.m_Padding.m_Left);
    const float top = std::min(bounds.m_Height, border + style.m_Padding.m_Top);
    return {bounds.m_X + left, bounds.m_Y + top,
            std::max(0.0f, bounds.m_Width - left - border - style.m_Padding.m_Right),
            std::max(0.0f, bounds.m_Height - top - border - style.m_Padding.m_Bottom)};
}

Rect ScrollThumb(Rect track, const Style& style, bool horizontal, double visibleFraction, double fraction)
{
    if (!style.m_Displayed || track.m_Width <= 0 || track.m_Height <= 0)
        return {};
    const float length = horizontal ? track.m_Width : track.m_Height;
    const float ideal = length * static_cast<float>(std::clamp(visibleFraction, 0.0, 1.0));
    const float along = horizontal ? Dimension(style.m_Width, length, ideal, style.m_MinWidth, style.m_MaxWidth)
                                   : Dimension(style.m_Height, length, ideal, style.m_MinHeight, style.m_MaxHeight);
    const float cross =
        horizontal ? Dimension(style.m_Height, track.m_Height, track.m_Height, style.m_MinHeight, style.m_MaxHeight)
                   : Dimension(style.m_Width, track.m_Width, track.m_Width, style.m_MinWidth, style.m_MaxWidth);
    const float position = static_cast<float>(std::clamp(fraction, 0.0, 1.0)) * (length - along);
    return horizontal ? Rect{track.m_X + position, track.m_Y + (track.m_Height - cross) * .5f, along, cross}
                      : Rect{track.m_X + (track.m_Width - cross) * .5f, track.m_Y + position, cross, along};
}

double ScrollFraction(double value, double minimum, double maximum)
{
    if (minimum == maximum || value <= minimum)
        return 0;
    if (value >= maximum)
        return 1;
    const double span = maximum - minimum;
    return std::clamp(std::isfinite(span) ? (value - minimum) / span
                                          : (value * .5 - minimum * .5) / (maximum * .5 - minimum * .5),
                      0.0, 1.0);
}

double ScrollDragFraction(double origin, float start, float current, Rect track, Rect thumb, bool horizontal)
{
    const double travel = horizontal ? track.m_Width - thumb.m_Width : track.m_Height - thumb.m_Height;
    if (travel <= 0)
        return std::clamp(origin, 0.0, 1.0);
    return std::clamp(origin + (static_cast<double>(current) - start) / travel, 0.0, 1.0);
}
} // namespace EStudioUI::Detail
