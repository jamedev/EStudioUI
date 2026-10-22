#include <EStudioUI/Slider.h>
#include <EStudioUI/Renderer.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
float PartDimension(const Length& length, float available, float fallback, float minimum, float maximum)
{
    return std::clamp(length.Resolve(available, fallback), std::min(minimum, available),
                      std::min(std::max(minimum, maximum), available));
}
} // namespace

Slider::Slider(Axis orientation) : Widget("slider"), m_Orientation(orientation)
{
    RegisterStylePart(
        "groove", Horizontal | Vertical,
        [](const Style& owner, unsigned state)
        {
            Style style;
            style.m_Background = owner.m_GridColor;
            style.m_Radius = 2;
            if (state & Vertical)
                style.m_Width = {Length::Unit::Pixels, 4};
            else
                style.m_Height = {Length::Unit::Pixels, 4};
            return style;
        },
        true);
    RegisterStylePart(
        "fill", Horizontal | Vertical,
        [](const Style& owner, unsigned state)
        {
            Style style;
            style.m_Background = owner.m_SelectionBackground;
            style.m_Radius = 2;
            if (state & Vertical)
                style.m_Width = {Length::Unit::Pixels, 4};
            else
                style.m_Height = {Length::Unit::Pixels, 4};
            return style;
        },
        true);
    RegisterStylePart(
        "handle", Horizontal | Vertical,
        [](const Style& owner, unsigned state)
        {
            Style style;
            style.m_Background = owner.m_Color;
            style.m_BorderColor = state & Focus ? owner.m_SelectionBackground : owner.m_BorderColor;
            style.m_BorderWidth = state & Focus ? 2.0f : 1.0f;
            style.m_Radius = 8;
            style.m_Width = style.m_Height = {Length::Unit::Pixels, 16};
            return style;
        },
        true);
    UpdateOrientationState();
}

void Slider::SetRange(double minimum, double maximum)
{
    VerifyAccess();
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
        throw std::invalid_argument("Slider bounds must be finite and ordered.");
    m_Minimum = minimum;
    m_Maximum = maximum;
    RequestRepaint();
    SetValue(m_Value);
}

double Slider::GetMinimum() const
{
    VerifyAccess();
    return m_Minimum;
}

double Slider::GetMaximum() const
{
    VerifyAccess();
    return m_Maximum;
}

void Slider::SetStep(double step)
{
    VerifyAccess();
    if (!std::isfinite(step) || step <= 0)
        throw std::invalid_argument("Slider step must be finite and positive.");
    m_Step = step;
}

double Slider::GetStep() const
{
    VerifyAccess();
    return m_Step;
}

void Slider::SetValue(double value)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
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
    VerifyAccess();
    return m_Value;
}

void Slider::SetOrientation(Axis orientation)
{
    VerifyAccess();
    if (m_Orientation == orientation)
        return;
    m_Orientation = orientation;
    UpdateOrientationState();
    Invalidate();
}

void Slider::UpdateOrientationState()
{
    const bool horizontal = m_Orientation == Axis::Horizontal;
    SetState(Horizontal, horizontal);
    SetState(Vertical, !horizontal);
    const unsigned orientation = horizontal ? Horizontal : Vertical;
    SetPartState("groove", orientation);
    SetPartState("fill", orientation);
    SetPartState("handle", orientation);
}

Axis Slider::GetOrientation() const
{
    VerifyAccess();
    return m_Orientation;
}

Signal<double>& Slider::ValueChanged()
{
    VerifyAccess();
    return m_ValueChanged;
}

Signal<>& Slider::SliderReleased()
{
    VerifyAccess();
    return m_SliderReleased;
}

bool Slider::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Point Slider::Measure(FontFace&) const
{
    const auto& handle = GetPartStyle("handle");
    const auto& groove = GetPartStyle("groove");
    const auto& fill = GetPartStyle("fill");
    const auto intrinsic = [](const Length& length, float fallback, float minimum, float maximum)
    { return std::clamp(length.Resolve(fallback, fallback), minimum, std::max(minimum, maximum)); };
    const float width = intrinsic(handle.m_Width, 16, handle.m_MinWidth, handle.m_MaxWidth);
    const float height = intrinsic(handle.m_Height, 16, handle.m_MinHeight, handle.m_MaxHeight);
    if (m_Orientation == Axis::Horizontal)
        return {std::max(160.0f, width + intrinsic(groove.m_Width, 0, groove.m_MinWidth, groove.m_MaxWidth)),
                std::max({24.0f, height, intrinsic(groove.m_Height, 4, groove.m_MinHeight, groove.m_MaxHeight),
                          intrinsic(fill.m_Height, 4, fill.m_MinHeight, fill.m_MaxHeight)})};
    return {std::max({24.0f, width, intrinsic(groove.m_Width, 4, groove.m_MinWidth, groove.m_MaxWidth),
                      intrinsic(fill.m_Width, 4, fill.m_MinWidth, fill.m_MaxWidth)}),
            std::max(160.0f, height + intrinsic(groove.m_Height, 0, groove.m_MinHeight, groove.m_MaxHeight))};
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

Slider::Geometry Slider::GetGeometry() const
{
    const auto content = GetContentBounds();
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const auto& groove = GetPartStyle("groove");
    const auto& fill = GetPartStyle("fill");
    const auto& handle = GetPartStyle("handle");
    const float width = std::max(0.0f, content.m_Width);
    const float height = std::max(0.0f, content.m_Height);
    const float handleWidth = PartDimension(handle.m_Width, width, 16, handle.m_MinWidth, handle.m_MaxWidth);
    const float handleHeight = PartDimension(handle.m_Height, height, 16, handle.m_MinHeight, handle.m_MaxHeight);
    const float available = horizontal ? width - handleWidth : height - handleHeight;
    const float along =
        horizontal ? PartDimension(groove.m_Width, available, available, groove.m_MinWidth, groove.m_MaxWidth)
                   : PartDimension(groove.m_Height, available, available, groove.m_MinHeight, groove.m_MaxHeight);
    const float cross = horizontal ? PartDimension(groove.m_Height, height, 4, groove.m_MinHeight, groove.m_MaxHeight)
                                   : PartDimension(groove.m_Width, width, 4, groove.m_MinWidth, groove.m_MaxWidth);
    const float fillCross = horizontal ? PartDimension(fill.m_Height, height, cross, fill.m_MinHeight, fill.m_MaxHeight)
                                       : PartDimension(fill.m_Width, width, cross, fill.m_MinWidth, fill.m_MaxWidth);
    const Point center{content.m_X + width * 0.5f, content.m_Y + height * 0.5f};
    Geometry geometry;
    geometry.m_From =
        horizontal ? Point{center.m_X - along * 0.5f, center.m_Y} : Point{center.m_X, center.m_Y + along * 0.5f};
    geometry.m_To =
        horizontal ? Point{center.m_X + along * 0.5f, center.m_Y} : Point{center.m_X, center.m_Y - along * 0.5f};
    const float fraction = static_cast<float>(Fraction());
    const Point thumb{std::lerp(geometry.m_From.m_X, geometry.m_To.m_X, fraction),
                      std::lerp(geometry.m_From.m_Y, geometry.m_To.m_Y, fraction)};
    geometry.m_Handle = {thumb.m_X - handleWidth * 0.5f, thumb.m_Y - handleHeight * 0.5f, handleWidth, handleHeight};
    geometry.m_Groove = horizontal
                            ? Rect{geometry.m_From.m_X - cross * 0.5f, center.m_Y - cross * 0.5f, along + cross, cross}
                            : Rect{center.m_X - cross * 0.5f, geometry.m_To.m_Y - cross * 0.5f, cross, along + cross};
    geometry.m_Fill = horizontal ? Rect{geometry.m_From.m_X - fillCross * 0.5f, center.m_Y - fillCross * 0.5f,
                                        along * fraction + fillCross, fillCross}
                                 : Rect{center.m_X - fillCross * 0.5f, thumb.m_Y - fillCross * 0.5f, fillCross,
                                        along * fraction + fillCross};
    return geometry;
}

void Slider::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto clip = GetBounds().Intersect(GetClip());
    if (clip.m_Width <= 0 || clip.m_Height <= 0)
        return;
    const auto geometry = GetGeometry();
    renderer.PushState();
    if (GetStyle().m_ClipContent)
        renderer.PushClip(GetBounds(), GetStyle().m_Radius);
    const auto draw = [&](std::string_view name, Rect bounds)
    {
        const auto& style = GetPartStyle(name);
        if (!style.m_Displayed || style.m_Opacity <= 0)
            return;
        renderer.PushState(style.m_Transform.Resolve(bounds));
        renderer.DrawBox(bounds, style, clip, opacity * style.m_Opacity);
        renderer.PopState();
    };
    draw("groove", geometry.m_Groove);
    draw("fill", geometry.m_Fill);
    draw("handle", geometry.m_Handle);
    renderer.PopState();
}

void Slider::SetFromPointer(Point point)
{
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const auto geometry = GetGeometry();
    const double along = horizontal ? geometry.m_To.m_X - geometry.m_From.m_X : geometry.m_From.m_Y - geometry.m_To.m_Y;
    if (along <= 0 || m_Minimum == m_Maximum)
        return;
    double fraction =
        horizontal ? (point.m_X - geometry.m_From.m_X) / along : (geometry.m_From.m_Y - point.m_Y) / along;
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
