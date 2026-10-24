#include <EStudioUI/RangeSlider.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "PropertyNotification.h"

namespace EStudioUI
{
RangeSlider::RangeSlider(Axis orientation) : Widget("rangeslider")
{
    for (const auto* name : {"groove", "fill", "lower-handle", "upper-handle"})
    {
        RegisterStylePart(
            name, Hover | Focus | Pressed | Horizontal | Vertical | Selected,
            [part = std::string(name)](const Style& owner, unsigned state)
            {
                Style style;
                style.m_Background = part == "groove" ? owner.m_GridColor
                                     : part == "fill" ? owner.m_SelectionBackground
                                                      : owner.m_Color;
                style.m_Radius = part == "groove" || part == "fill" ? 2.0f : 8.0f;
                style.m_Width =
                    style.m_Height = {Length::Unit::Pixels, part == "groove" || part == "fill" ? 4.0f : 16.0f};
                if (state & Focus)
                {
                    style.m_BorderWidth = 2;
                    style.m_BorderColor = owner.m_SelectionBackground;
                }
                return style;
            },
            true);
    }
    SetOrientation(orientation);
}

void RangeSlider::SetRange(double minimum, double maximum)
{
    VerifyStructuralMutation();
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
    {
        throw std::invalid_argument("Range slider bounds must be finite and ordered.");
    }
    m_Minimum = minimum;
    m_Maximum = maximum;
    NotifySemanticsChanged();
    RequestRepaint();
    SetValues(m_Values);
}

double RangeSlider::GetMinimum() const
{
    VerifyAccess();
    return m_Minimum;
}

double RangeSlider::GetMaximum() const
{
    VerifyAccess();
    return m_Maximum;
}

void RangeSlider::SetValues(RangeValues values)
{
    VerifyStructuralMutation();
    if (!std::isfinite(values.m_Lower) || !std::isfinite(values.m_Upper) || values.m_Lower > values.m_Upper)
    {
        throw std::invalid_argument("Range slider endpoints must be finite and ordered.");
    }
    values.m_Lower = std::clamp(values.m_Lower, m_Minimum, m_Maximum);
    values.m_Upper = std::clamp(values.m_Upper, m_Minimum, m_Maximum);
    if (values == m_Values)
    {
        return;
    }
    m_Values = values;
    RequestRepaint();
    NotifySemanticsChanged();
    const auto lease = RetainOwner();
    Detail::EmitPropertyNotification(*this, m_NotificationDepth, m_ValuesChanged, values);
}

RangeValues RangeSlider::GetValues() const
{
    VerifyAccess();
    return m_Values;
}

void RangeSlider::SetStep(double step)
{
    VerifyStructuralMutation();
    if (!std::isfinite(step) || step <= 0)
    {
        throw std::invalid_argument("Range slider step must be finite and positive.");
    }
    m_Step = step;
    NotifySemanticsChanged();
}

void RangeSlider::SetOrientation(Axis orientation)
{
    VerifyStructuralMutation();
    if (orientation != Axis::Horizontal && orientation != Axis::Vertical)
    {
        throw std::invalid_argument("Invalid slider orientation.");
    }
    m_Orientation = orientation;
    SetState(Horizontal, orientation == Axis::Horizontal);
    SetState(Vertical, orientation == Axis::Vertical);
    UpdateParts();
    Invalidate(Invalidation::Content);
}

Signal<RangeValues>& RangeSlider::ValuesChanged()
{
    VerifyAccess();
    return m_ValuesChanged;
}

Signal<>& RangeSlider::SliderReleased()
{
    VerifyAccess();
    return m_Released;
}

bool RangeSlider::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Point RangeSlider::Measure(FontFace&) const
{
    return m_Orientation == Axis::Horizontal ? Point{180, 28} : Point{28, 180};
}

double RangeSlider::Fraction(double value) const
{
    if (m_Minimum == m_Maximum)
    {
        return 0;
    }
    const auto span = m_Maximum - m_Minimum;
    return std::clamp(std::isfinite(span) ? (value - m_Minimum) / span
                                          : (value * 0.5 - m_Minimum * 0.5) / (m_Maximum * 0.5 - m_Minimum * 0.5),
                      0.0, 1.0);
}

Rect RangeSlider::PartBounds(int handle) const
{
    const auto b = GetContentBounds();
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const auto& style = GetPartStyle(handle < 0 ? "groove" : handle == 0 ? "lower-handle" : "upper-handle");
    const float width = std::clamp(style.m_Width.Resolve(b.m_Width, 16), 0.0f, std::max(0.0f, b.m_Width));
    const float height = std::clamp(style.m_Height.Resolve(b.m_Height, 16), 0.0f, std::max(0.0f, b.m_Height));
    const auto& lower = GetPartStyle("lower-handle");
    const auto& upper = GetPartStyle("upper-handle");
    const float available = std::max(0.0f, horizontal ? b.m_Width : b.m_Height);
    const float extent =
        std::max(horizontal ? lower.m_Width.Resolve(available, 16) : lower.m_Height.Resolve(available, 16),
                 horizontal ? upper.m_Width.Resolve(available, 16) : upper.m_Height.Resolve(available, 16));
    const float inset = std::clamp(extent * .5f, 0.0f, available * .5f);
    if (handle < 0)
    {
        return horizontal ? Rect{b.m_X + inset, b.m_Y + (b.m_Height - height) * 0.5f,
                                 std::max(0.0f, b.m_Width - 2 * inset), height}
                          : Rect{b.m_X + (b.m_Width - width) * 0.5f, b.m_Y + inset, width,
                                 std::max(0.0f, b.m_Height - 2 * inset)};
    }
    const auto fraction = static_cast<float>(Fraction(handle == 0 ? m_Values.m_Lower : m_Values.m_Upper));
    return horizontal ? Rect{b.m_X + inset + std::max(0.0f, b.m_Width - 2 * inset) * fraction - width * 0.5f,
                             b.m_Y + (b.m_Height - height) * 0.5f, width, height}
                      : Rect{b.m_X + (b.m_Width - width) * 0.5f,
                             b.m_Y + inset + std::max(0.0f, b.m_Height - 2 * inset) * (1 - fraction) - height * 0.5f,
                             width, height};
}

void RangeSlider::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto clip = GetClip().Intersect(GetBounds());
    const auto track = PartBounds(-1);
    auto fill = track;
    const auto low = static_cast<float>(Fraction(m_Values.m_Lower)),
               high = static_cast<float>(Fraction(m_Values.m_Upper));
    if (m_Orientation == Axis::Horizontal)
    {
        fill.m_X += track.m_Width * low;
        fill.m_Width *= high - low;
    }
    else
    {
        fill.m_Y += track.m_Height * (1 - high);
        fill.m_Height *= high - low;
    }
    const auto draw = [&](std::string_view name, Rect bounds)
    {
        const auto& style = GetPartStyle(name);
        if (style.m_Displayed)
        {
            renderer.PushState(style.m_Transform.Resolve(bounds));
            renderer.DrawBox(bounds, style, clip, opacity * style.m_Opacity);
            renderer.PopState();
        }
    };
    draw("groove", track);
    draw("fill", fill);
    for (const int i : {1 - m_Active, m_Active})
    {
        draw(i == 0 ? "lower-handle" : "upper-handle", PartBounds(i));
    }
}

void RangeSlider::UpdateParts()
{
    const auto orientation = m_Orientation == Axis::Horizontal ? Horizontal : Vertical;
    SetPartState("groove", orientation);
    SetPartState("fill", orientation);
    for (const int i : {0, 1})
    {
        SetPartState(i == 0 ? "lower-handle" : "upper-handle",
                     orientation | (i == m_Active ? Selected : 0u) | (i == m_Active && m_Dragging ? Pressed : 0u));
    }
}

void RangeSlider::Change(double value)
{
    auto values = m_Values;
    if (m_Active == 0)
    {
        values.m_Lower = std::clamp(value, m_Minimum, values.m_Upper);
    }
    else
    {
        values.m_Upper = std::clamp(value, values.m_Lower, m_Maximum);
    }
    SetValues(values);
}

void RangeSlider::FromPointer(Point point)
{
    const auto track = PartBounds(-1);
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const auto span = horizontal ? track.m_Width : track.m_Height;
    if (span <= 0)
    {
        return;
    }
    const double fraction = std::clamp(
        static_cast<double>(horizontal ? (point.m_X - track.m_X) / span : 1 - (point.m_Y - track.m_Y) / span), 0.0,
        1.0);
    auto value = std::lerp(m_Minimum, m_Maximum, fraction);
    const auto steps = (value - m_Minimum) / m_Step, snapped = m_Minimum + std::round(steps) * m_Step;
    if (fraction != 0 && fraction != 1 && std::isfinite(snapped))
    {
        value = snapped;
    }
    if (m_ChoosingHandle && value != m_Values.m_Lower)
    {
        m_Active = value > m_Values.m_Lower ? 1 : 0;
        m_ChoosingHandle = false;
        UpdateParts();
    }
    Change(value);
}

void RangeSlider::OnPointerDown(const PointerEvent& event)
{
    const auto a = PartBounds(0), b = PartBounds(1);
    const auto distance = [&](Rect r)
    {
        return std::hypot(event.m_Position.m_X - r.m_X - r.m_Width * 0.5f,
                          event.m_Position.m_Y - r.m_Y - r.m_Height * 0.5f);
    };
    if (distance(a) != distance(b))
    {
        m_Active = distance(a) < distance(b) ? 0 : 1;
    }
    m_ChoosingHandle = m_Values.m_Lower == m_Values.m_Upper;
    m_Dragging = true;
    UpdateParts();
    RequestRepaint();
    FromPointer(event.m_Position);
}

void RangeSlider::OnPointerMove(const PointerEvent& event)
{
    if (m_Dragging)
    {
        FromPointer(event.m_Position);
    }
}

void RangeSlider::OnPointerUp(const PointerEvent& event)
{
    if (!m_Dragging)
    {
        return;
    }
    m_Dragging = false;
    UpdateParts();
    const WidgetRef self(this);
    FromPointer(event.m_Position);
    if (auto* live = dynamic_cast<RangeSlider*>(self.Get()))
    {
        live->RequestRepaint();
        live->m_Released.Emit();
    }
}

void RangeSlider::OnCaptureLost()
{
    m_Dragging = false;
    UpdateParts();
    RequestRepaint();
}

bool RangeSlider::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (key == Key::Tab && down && !modifiers.m_Control &&
        ((m_Active == 0 && !modifiers.m_Shift) || (m_Active == 1 && modifiers.m_Shift)))
    {
        m_Active = 1 - m_Active;
        UpdateParts();
        RequestRepaint();
        NotifySemanticsChanged();
        return true;
    }
    if (key == Key::Tab && down)
    {
        m_Active = modifiers.m_Shift ? 1 : 0;
        UpdateParts();
    }
    return false;
}

void RangeSlider::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt)
    {
        return;
    }
    const double value = m_Active == 0 ? m_Values.m_Lower : m_Values.m_Upper;
    const double distance = m_Step * ((key == Key::PageUp || key == Key::PageDown) ? 10 : 1);
    if (key == Key::Home)
    {
        Change(m_Minimum);
    }
    else if (key == Key::End)
    {
        Change(m_Maximum);
    }
    else if (key == Key::Right || key == Key::Up || key == Key::PageUp)
    {
        Change(!std::isfinite(distance) || value >= m_Maximum - distance ? m_Maximum : value + distance);
    }
    else if (key == Key::Left || key == Key::Down || key == Key::PageDown)
    {
        Change(!std::isfinite(distance) || value <= m_Minimum + distance ? m_Minimum : value - distance);
    }
}

SemanticInfo RangeSlider::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Name = "Range";
    return info;
}

std::size_t RangeSlider::GetSemanticChildCount(SemanticKey parent) const
{
    return parent == SemanticKey{} ? 2 : 0;
}

std::optional<SemanticKey> RangeSlider::GetSemanticChildAt(SemanticKey parent, std::size_t index) const
{
    return parent == SemanticKey{} && index < 2 ? std::optional<SemanticKey>{{index + 1, 0, 0}} : std::nullopt;
}

std::optional<std::size_t> RangeSlider::GetSemanticChildIndex(SemanticKey key) const
{
    return key.m_Item >= 1 && key.m_Item <= 2 && key.m_Part == 0 && key.m_Generation == 0
               ? std::optional<std::size_t>{key.m_Item - 1}
               : std::nullopt;
}

std::optional<SemanticVirtualNode> RangeSlider::DescribeSemanticChild(SemanticKey key) const
{
    const auto index = GetSemanticChildIndex(key);
    if (!index)
    {
        return std::nullopt;
    }
    SemanticVirtualNode node;
    node.m_Info.m_Role = SemanticRole::Slider;
    node.m_Info.m_Name = *index == 0 ? "Lower endpoint" : "Upper endpoint";
    node.m_Info.m_Number = *index == 0 ? m_Values.m_Lower : m_Values.m_Upper;
    node.m_Info.m_Minimum = *index == 0 ? m_Minimum : m_Values.m_Lower;
    node.m_Info.m_Maximum = *index == 0 ? m_Values.m_Upper : m_Maximum;
    node.m_Info.m_Step = m_Step;
    node.m_Info.m_Actions = SemanticAction::Focus | SemanticAction::SetNumber;
    node.m_Bounds = PartBounds(static_cast<int>(*index));
    node.m_Focused = IsFocused() && m_Active == static_cast<int>(*index);
    return node;
}

std::optional<SemanticKey> RangeSlider::GetSemanticFocus() const
{
    return SemanticKey{static_cast<std::uint64_t>(m_Active) + 1, 0, 0};
}

SemanticStatus RangeSlider::OnSemanticChildAction(SemanticKey key, const SemanticCommand& command)
{
    const auto index = GetSemanticChildIndex(key);
    if (!index)
    {
        return SemanticStatus::Unavailable;
    }
    if (command.m_Action != SemanticAction::Focus && command.m_Action != SemanticAction::SetNumber)
    {
        return SemanticStatus::Unsupported;
    }
    if (command.m_Action == SemanticAction::SetNumber && !std::isfinite(command.m_Number))
    {
        return SemanticStatus::InvalidValue;
    }
    m_Active = static_cast<int>(*index);
    UpdateParts();
    NotifySemanticsChanged();
    RequestRepaint();
    if (command.m_Action == SemanticAction::SetNumber)
    {
        Change(command.m_Number);
    }
    else if (GetContext())
    {
        GetContext()->SetFocus(this);
    }
    return SemanticStatus::Succeeded;
}
} // namespace EStudioUI
