#include <EStudioUI/Slider.h>
#include <EStudioUI/Renderer.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
Color Faded(Color color, float opacity)
{
    color.m_A *= opacity;
    return color;
}
} // namespace

Slider::Slider(Axis orientation) : Widget("slider"), m_Orientation(orientation)
{
}

void Slider::SetRange(double minimum, double maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
        throw std::invalid_argument("Slider bounds must be finite and ordered.");
    m_Minimum = minimum;
    m_Maximum = maximum;
    RequestRepaint();
    SetValue(m_Value);
}

double Slider::GetMinimum() const
{
    return m_Minimum;
}

double Slider::GetMaximum() const
{
    return m_Maximum;
}

void Slider::SetStep(double step)
{
    if (!std::isfinite(step) || step <= 0)
        throw std::invalid_argument("Slider step must be finite and positive.");
    m_Step = step;
}

double Slider::GetStep() const
{
    return m_Step;
}

void Slider::SetValue(double value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("Slider value must be finite.");
    value = std::clamp(value, m_Minimum, m_Maximum);
    if (value == m_Value)
        return;
    m_Value = value;
    RequestRepaint();
    m_ValueChanged.Emit(value);
}

double Slider::GetValue() const
{
    return m_Value;
}

void Slider::SetOrientation(Axis orientation)
{
    if (m_Orientation == orientation)
        return;
    m_Orientation = orientation;
    Invalidate();
}

Axis Slider::GetOrientation() const
{
    return m_Orientation;
}

Signal<double>& Slider::ValueChanged()
{
    return m_ValueChanged;
}

Signal<>& Slider::SliderReleased()
{
    return m_SliderReleased;
}

bool Slider::IsFocusable() const
{
    return true;
}

Point Slider::Measure(FontFace&) const
{
    return m_Orientation == Axis::Horizontal ? Point{160, 24} : Point{24, 160};
}

double Slider::Fraction() const
{
    if (m_Minimum == m_Maximum || m_Value == m_Minimum)
        return 0;
    if (m_Value == m_Maximum)
        return 1;
    const double range = m_Maximum - m_Minimum;
    const double fraction = std::isfinite(range)
                                ? (m_Value - m_Minimum) / range
                                : (m_Value * 0.5 - m_Minimum * 0.5) / (m_Maximum * 0.5 - m_Minimum * 0.5);
    return std::clamp(fraction, 0.0, 1.0);
}

void Slider::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto content = GetContentBounds();
    const auto clip = content.Intersect(GetClip());
    if (clip.m_Width <= 0 || clip.m_Height <= 0)
        return;
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const float cross = horizontal ? content.m_Height : content.m_Width;
    const float along = horizontal ? content.m_Width : content.m_Height;
    const float radius = std::max(0.0f, std::min({8.0f, cross * 0.5f, along * 0.5f}));
    const Point from = horizontal
                           ? Point{content.m_X + radius, content.m_Y + content.m_Height * 0.5f}
                           : Point{content.m_X + content.m_Width * 0.5f, content.m_Y + content.m_Height - radius};
    const Point to =
        horizontal ? Point{content.m_X + content.m_Width - radius, from.m_Y} : Point{from.m_X, content.m_Y + radius};
    const float fraction = static_cast<float>(Fraction());
    const Point handle{std::lerp(from.m_X, to.m_X, fraction), std::lerp(from.m_Y, to.m_Y, fraction)};
    renderer.DrawLine(from, to, Faded(GetStyle().m_GridColor, opacity), 4, clip);
    renderer.DrawLine(from, handle, Faded(GetStyle().m_SelectionBackground, opacity), 4, clip);
    Style style;
    style.m_Background = GetStyle().m_Color;
    style.m_BorderColor = IsFocused() ? GetStyle().m_SelectionBackground : GetStyle().m_BorderColor;
    style.m_BorderWidth = IsFocused() ? 2.0f : 1.0f;
    renderer.DrawCircle(handle, radius, style, clip, opacity);
}

void Slider::SetFromPointer(Point point)
{
    const auto content = GetContentBounds();
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const double cross = horizontal ? content.m_Height : content.m_Width;
    const double along = horizontal ? content.m_Width : content.m_Height;
    const double radius = std::max(0.0, std::min({8.0, cross * 0.5, along * 0.5}));
    if (along <= 2 * radius || m_Minimum == m_Maximum)
        return;
    double fraction = horizontal ? (point.m_X - content.m_X - radius) / (along - 2 * radius)
                                 : (content.m_Y + content.m_Height - radius - point.m_Y) / (along - 2 * radius);
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (fraction == 0 || fraction == 1)
    {
        SetValue(fraction == 0 ? m_Minimum : m_Maximum);
        return;
    }
    const double value = std::lerp(m_Minimum, m_Maximum, fraction);
    const double steps = (value - m_Minimum) / m_Step;
    const double snapped = m_Minimum + std::round(steps) * m_Step;
    if (std::isfinite(snapped))
    {
        SetValue(snapped);
        return;
    }
    // Opposite large bounds may overflow their difference or the snapped offset.
    // Quantize a normalized distance in that case, keeping endpoint interpolation finite.
    const double span = m_Maximum * 0.5 - m_Minimum * 0.5;
    const double stepFraction = (m_Step * 0.5) / span;
    const double normalizedSteps = fraction / stepFraction;
    if (stepFraction > 0 && std::isfinite(normalizedSteps))
        SetValue(std::lerp(m_Minimum, m_Maximum, std::clamp(std::round(normalizedSteps) * stepFraction, 0.0, 1.0)));
    else
        SetValue(value);
}

void Slider::StepBy(int steps)
{
    const double distance = m_Step * std::abs(static_cast<double>(steps));
    if (steps > 0)
        SetValue(!std::isfinite(distance) || m_Value >= m_Maximum - distance ? m_Maximum : m_Value + distance);
    else if (steps < 0)
        SetValue(!std::isfinite(distance) || m_Value <= m_Minimum + distance ? m_Minimum : m_Value - distance);
}

void Slider::OnPointerDown(const PointerEvent& event)
{
    m_Dragging = true;
    SetState(Pressed, true);
    SetFromPointer(event.m_Position);
}

void Slider::OnPointerMove(const PointerEvent& event)
{
    if (m_Dragging)
        SetFromPointer(event.m_Position);
}

void Slider::OnPointerUp(const PointerEvent& event)
{
    if (!m_Dragging)
        return;
    m_Dragging = false;
    SetState(Pressed, false);
    const WidgetRef lifetime(this);
    SetFromPointer(event.m_Position);
    if (lifetime.Get())
        m_SliderReleased.Emit();
}

void Slider::OnCaptureLost()
{
    m_Dragging = false;
    SetState(Pressed, false);
}

void Slider::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt)
        return;
    if (key == Key::Right || key == Key::Up)
        StepBy(1);
    else if (key == Key::Left || key == Key::Down)
        StepBy(-1);
    else if (key == Key::PageUp)
        StepBy(10);
    else if (key == Key::PageDown)
        StepBy(-10);
    else if (key == Key::Home)
        SetValue(m_Minimum);
    else if (key == Key::End)
        SetValue(m_Maximum);
}

bool Slider::OnScroll(Point delta, Modifiers)
{
    if (!IsFocused() || !std::isfinite(delta.m_Y) || delta.m_Y == 0)
        return false;
    StepBy(delta.m_Y > 0 ? 1 : -1);
    return true;
}
} // namespace EStudioUI
