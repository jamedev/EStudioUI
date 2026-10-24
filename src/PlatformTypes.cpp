#include <EStudioUI/PlatformTypes.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
bool Finite(Point point)
{
    return std::isfinite(point.m_X) && std::isfinite(point.m_Y);
}

Point Checked(double x, double y)
{
    const double maximum = std::numeric_limits<float>::max();
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > maximum || std::abs(y) > maximum)
        throw std::invalid_argument("Platform coordinates must remain finite.");
    return {static_cast<float>(x), static_cast<float>(y)};
}
} // namespace

PlatformSupport PlatformCapabilities::Get(PlatformFeature feature) const
{
    if (feature < PlatformFeature::ClipboardText || feature >= PlatformFeature::Count)
        throw std::invalid_argument("Invalid platform feature.");
    return m_Features[static_cast<std::size_t>(feature)];
}

void PlatformCapabilities::Set(PlatformFeature feature, PlatformSupport support)
{
    (void)Get(feature);
    if (support < PlatformSupport::Unsupported || support > PlatformSupport::Native)
        throw std::invalid_argument("Invalid platform support kind.");
    m_Features[static_cast<std::size_t>(feature)] = support;
}

PlatformResult::operator bool() const noexcept
{
    return m_Status == PlatformStatus::Succeeded;
}

void WindowIcon::Validate() const
{
    if (m_Width < 1 || m_Height < 1 || m_Width > 1024 || m_Height > 1024 ||
        m_Pixels.size() != static_cast<std::size_t>(m_Width) * static_cast<std::size_t>(m_Height) * 4)
        throw std::invalid_argument("A window icon requires complete RGBA8 pixels with dimensions from 1 to 1024.");
}

void WindowMetrics::Validate() const
{
    if (!Finite(m_LogicalSize) || !Finite(m_FramebufferSize) || !Finite(m_ContentScale) || !Finite(m_InputScale) ||
        !Finite(m_PixelScale) || !Finite(m_ScreenOrigin) || m_LogicalSize.m_X < 0 || m_LogicalSize.m_Y < 0 ||
        m_FramebufferSize.m_X < 0 || m_FramebufferSize.m_Y < 0 || m_ContentScale.m_X <= 0 || m_ContentScale.m_Y <= 0 ||
        m_InputScale.m_X <= 0 || m_InputScale.m_Y <= 0 || m_PixelScale.m_X <= 0 || m_PixelScale.m_Y <= 0)
        throw std::invalid_argument("Window metrics require finite sizes and positive scales.");
}

void SystemAppearance::Validate() const
{
    if (m_Support < PlatformSupport::Unsupported || m_Support > PlatformSupport::Native)
        throw std::invalid_argument("Invalid appearance capability.");
    for (const auto color : {m_Accent, m_Foreground, m_Background})
        for (const float channel : {color.m_R, color.m_G, color.m_B, color.m_A})
            if (!std::isfinite(channel) || channel < 0 || channel > 1)
                throw std::invalid_argument("Appearance colors require normalized finite channels.");
}

Point WindowMetrics::FromNative(Point point) const
{
    Validate();
    return Checked(static_cast<double>(point.m_X) / m_InputScale.m_X,
                   static_cast<double>(point.m_Y) / m_InputScale.m_Y);
}

Point WindowMetrics::ToScreen(Point point) const
{
    Validate();
    if (!m_HasScreenCoordinates)
        throw std::logic_error("Physical desktop coordinates are unavailable on this platform adapter.");
    return Checked(m_ScreenOrigin.m_X + static_cast<double>(point.m_X) * m_PixelScale.m_X,
                   m_ScreenOrigin.m_Y + static_cast<double>(point.m_Y) * m_PixelScale.m_Y);
}

Point WindowMetrics::FromScreen(Point point) const
{
    Validate();
    if (!m_HasScreenCoordinates)
        throw std::logic_error("Physical desktop coordinates are unavailable on this platform adapter.");
    return Checked((static_cast<double>(point.m_X) - m_ScreenOrigin.m_X) / m_PixelScale.m_X,
                   (static_cast<double>(point.m_Y) - m_ScreenOrigin.m_Y) / m_PixelScale.m_Y);
}

Rect WindowMetrics::ToScreen(Rect bounds) const
{
    const auto from = ToScreen(Point{bounds.m_X, bounds.m_Y});
    const auto size = Checked(static_cast<double>(bounds.m_Width) * m_PixelScale.m_X,
                              static_cast<double>(bounds.m_Height) * m_PixelScale.m_Y);
    if (size.m_X < 0 || size.m_Y < 0)
        throw std::invalid_argument("Screen bounds cannot have negative dimensions.");
    return {from.m_X, from.m_Y, size.m_X, size.m_Y};
}
} // namespace EStudioUI
