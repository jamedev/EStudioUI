#include <EStudioUI/ScrollBar.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>

#include "ScrollGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
ScrollBar::ScrollBar(Axis orientation) : Widget("scrollbar"), m_Orientation(orientation)
{
    if (orientation != Axis::Horizontal && orientation != Axis::Vertical)
        throw std::invalid_argument("Invalid scrollbar orientation.");
    RegisterStylePart(
        "scroll-track", Horizontal | Vertical | Hover | Pressed,
        [](const Style& owner, unsigned state)
        {
            Style style;
            style.m_Background = owner.m_GridColor;
            style.m_Radius = 6;
            style.m_Padding = {2, 2, 2, 2};
            if (state & Horizontal)
                style.m_Height = {Length::Unit::Pixels, 14};
            else
                style.m_Width = {Length::Unit::Pixels, 14};
            return style;
        },
        true);
    RegisterStylePart(
        "scroll-thumb", Horizontal | Vertical | Hover | Pressed,
        [](const Style& owner, unsigned state)
        {
            Style style;
            style.m_Background = state & Pressed ? owner.m_SelectionBackground : owner.m_PlaceholderColor;
            style.m_Radius = 5;
            if (state & Horizontal)
                style.m_MinWidth = 20;
            else
                style.m_MinHeight = 20;
            return style;
        },
        true);
    UpdateOrientation();
}

void ScrollBar::SetRange(double minimum, double maximum)
{
    VerifyStructuralMutation();
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
        throw std::invalid_argument("Scrollbar limits must be finite and ordered.");
    if (minimum == m_Minimum && maximum == m_Maximum)
        return;
    m_Minimum = minimum;
    m_Maximum = maximum;
    m_Dragging = false;
    RequestRepaint();
    NotifySemanticsChanged();
    SetValue(m_Value);
}

double ScrollBar::GetMinimum() const
{
    VerifyAccess();
    return m_Minimum;
}

double ScrollBar::GetMaximum() const
{
    VerifyAccess();
    return m_Maximum;
}

void ScrollBar::SetValue(double value)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!std::isfinite(value))
        throw std::invalid_argument("Scrollbar value must be finite.");
    value = std::clamp(value, m_Minimum, m_Maximum);
    if (value == m_Value)
        return;
    m_Value = value;
    RequestRepaint();
    NotifySemanticsChanged();
    PublishValue();
}

void ScrollBar::PublishValue()
{
    if (m_Notifying)
        return;
    const WidgetRef self(this);
    m_Notifying = true;
    try
    {
        unsigned turns = 0;
        while (m_Notified != m_Value)
        {
            if (++turns > 64)
                throw std::logic_error("Scrollbar observers exceeded 64 reentrant changes.");
            m_Notified = m_Value;
            m_ValueChanged.Emit(m_Notified);
            if (!self.Get())
                return;
        }
        m_Notifying = false;
    }
    catch (...)
    {
        if (self.Get())
            m_Notifying = false;
        throw;
    }
}

double ScrollBar::GetValue() const
{
    VerifyAccess();
    return m_Value;
}

void ScrollBar::SetSingleStep(double step)
{
    VerifyStructuralMutation();
    if (!std::isfinite(step) || step <= 0)
        throw std::invalid_argument("Scrollbar single step must be finite and positive.");
    if (m_SingleStep == step)
        return;
    m_SingleStep = step;
    NotifySemanticsChanged();
}

double ScrollBar::GetSingleStep() const
{
    VerifyAccess();
    return m_SingleStep;
}

void ScrollBar::SetPageStep(double step)
{
    VerifyStructuralMutation();
    if (!std::isfinite(step) || step < 0)
        throw std::invalid_argument("Scrollbar page step must be finite and nonnegative.");
    if (m_PageStep == step)
        return;
    m_PageStep = step;
    m_Dragging = false;
    RequestRepaint();
}

double ScrollBar::GetPageStep() const
{
    VerifyAccess();
    return m_PageStep;
}

void ScrollBar::SetOrientation(Axis orientation)
{
    VerifyStructuralMutation();
    if (orientation != Axis::Horizontal && orientation != Axis::Vertical)
        throw std::invalid_argument("Invalid scrollbar orientation.");
    if (m_Orientation == orientation)
        return;
    m_Orientation = orientation;
    m_Dragging = false;
    UpdateOrientation();
    Invalidate(Invalidation::Content);
}

void ScrollBar::UpdateOrientation()
{
    SetState(Horizontal, m_Orientation == Axis::Horizontal);
    SetState(Vertical, m_Orientation == Axis::Vertical);
}

Axis ScrollBar::GetOrientation() const
{
    VerifyAccess();
    return m_Orientation;
}

unsigned ScrollBar::PartState(bool thumb) const
{
    unsigned state = m_Orientation == Axis::Horizontal ? Horizontal : Vertical;
    if (m_Dragging)
        state |= Pressed;
    if (IsHovered() && GetContext())
        if (const auto pointer = GetContext()->GetPointerPosition())
        {
            const auto point = GetContext()->ToLayoutPoint(*this, *pointer);
            if (!thumb || GetThumbBounds().Contains(point))
                state |= Hover;
        }
    return state;
}

Rect ScrollBar::GetTrackBounds() const
{
    VerifyAccess();
    return GetPartStyle("scroll-track", m_Orientation == Axis::Horizontal ? Horizontal : Vertical).m_Displayed
               ? GetContentBounds()
               : Rect{};
}

Rect ScrollBar::TrackContent() const
{
    return Detail::ScrollTrackContent(GetTrackBounds(), GetPartStyle("scroll-track", PartState(false)));
}

double ScrollBar::VisibleFraction() const
{
    if (m_Minimum == m_Maximum)
        return 1;
    const double scale = std::max({std::abs(m_Minimum), std::abs(m_Maximum), m_PageStep});
    const double page = m_PageStep / scale;
    return page / (m_Maximum / scale - m_Minimum / scale + page);
}

Rect ScrollBar::GetThumbBounds() const
{
    VerifyAccess();
    const bool horizontal = m_Orientation == Axis::Horizontal;
    unsigned state = (horizontal ? Horizontal : Vertical) | (m_Dragging ? Pressed : 0u);
    const auto track = TrackContent();
    const auto fraction = Detail::ScrollFraction(m_Value, m_Minimum, m_Maximum);
    const auto neutral =
        Detail::ScrollThumb(track, GetPartStyle("scroll-thumb", state), horizontal, VisibleFraction(), fraction);
    // Resolve hover from the neutral geometry once; hover dimensions cannot recursively select themselves.
    if (IsHovered() && GetContext())
        if (const auto pointer = GetContext()->GetPointerPosition();
            pointer && neutral.Contains(GetContext()->ToLayoutPoint(*this, *pointer)))
            state |= Hover;
    return Detail::ScrollThumb(track, GetPartStyle("scroll-thumb", state), horizontal, VisibleFraction(), fraction);
}

Signal<double>& ScrollBar::ValueChanged()
{
    VerifyAccess();
    return m_ValueChanged;
}

bool ScrollBar::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Point ScrollBar::Measure(FontFace&) const
{
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const auto& style = GetPartStyle("scroll-track", horizontal ? Horizontal : Vertical);
    const auto& thumb = GetPartStyle("scroll-thumb", horizontal ? Horizontal : Vertical);
    const float thickness = horizontal
                                ? std::clamp(style.m_Height.Resolve(14, 14), style.m_MinHeight, style.m_MaxHeight)
                                : std::clamp(style.m_Width.Resolve(14, 14), style.m_MinWidth, style.m_MaxWidth);
    const float along = horizontal ? thumb.m_MinWidth + style.m_Padding.m_Left + style.m_Padding.m_Right
                                   : thumb.m_MinHeight + style.m_Padding.m_Top + style.m_Padding.m_Bottom;
    return horizontal ? Point{std::max(120.0f, along), thickness} : Point{thickness, std::max(120.0f, along)};
}

void ScrollBar::Paint(Renderer& renderer, FontFace&, double, float opacity) const
{
    renderer.DrawBox(GetBounds(), GetStyle(), GetClip(), opacity);
    for (bool thumb : {false, true})
    {
        const auto bounds = thumb ? GetThumbBounds() : GetTrackBounds();
        const auto& style = GetPartStyle(thumb ? "scroll-thumb" : "scroll-track", PartState(thumb));
        if (!style.m_Displayed || bounds.m_Width <= 0 || bounds.m_Height <= 0)
            continue;
        renderer.PushState(style.m_Transform.Resolve(bounds));
        renderer.SetWidgetPaint(bounds, GetClip(), nullptr, false, 0);
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Value), thumb ? 1 : 0);
        renderer.DrawBox(bounds, style, GetClip(), opacity * style.m_Opacity);
        renderer.PopState();
    }
}

void ScrollBar::OnPointerDown(const PointerEvent& event)
{
    const auto thumb = GetThumbBounds();
    if (m_Minimum == m_Maximum || !GetTrackBounds().Contains(event.m_Position) || thumb.m_Width <= 0 ||
        thumb.m_Height <= 0)
        return;
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const float position = horizontal ? event.m_Position.m_X : event.m_Position.m_Y;
    if (thumb.Contains(event.m_Position))
    {
        m_DragOrigin = position;
        m_FractionOrigin = Detail::ScrollFraction(m_Value, m_Minimum, m_Maximum);
        m_Dragging = true;
        RequestRepaint();
    }
    else
        StepBy(position < (horizontal ? thumb.m_X : thumb.m_Y) ? -m_PageStep : m_PageStep);
}

void ScrollBar::OnPointerMove(const PointerEvent& event)
{
    const unsigned state = PartState(true);
    if (m_HoverState != state)
    {
        m_HoverState = state;
        RequestRepaint();
    }
    if (!m_Dragging)
        return;
    const bool horizontal = m_Orientation == Axis::Horizontal;
    const double fraction = Detail::ScrollDragFraction(m_FractionOrigin, m_DragOrigin,
                                                       horizontal ? event.m_Position.m_X : event.m_Position.m_Y,
                                                       TrackContent(), GetThumbBounds(), horizontal);
    SetValue(std::lerp(m_Minimum, m_Maximum, fraction));
}

void ScrollBar::OnPointerUp(const PointerEvent&)
{
    m_Dragging = false;
    RequestRepaint();
}

void ScrollBar::OnCaptureLost()
{
    m_Dragging = false;
    RequestRepaint();
}

void ScrollBar::StepBy(double amount)
{
    SetValue(std::clamp(m_Value + amount, m_Minimum, m_Maximum));
}

void ScrollBar::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt)
        return;
    if (key == Key::Home)
        SetValue(m_Minimum);
    else if (key == Key::End)
        SetValue(m_Maximum);
    else if (key == Key::PageUp || key == Key::PageDown)
        StepBy(key == Key::PageUp ? -m_PageStep : m_PageStep);
    else if ((m_Orientation == Axis::Horizontal && (key == Key::Left || key == Key::Right)) ||
             (m_Orientation == Axis::Vertical && (key == Key::Up || key == Key::Down)))
        StepBy(key == Key::Left || key == Key::Up ? -m_SingleStep : m_SingleStep);
}

bool ScrollBar::OnScroll(Point delta, Modifiers modifiers)
{
    const double amount = m_Orientation == Axis::Horizontal && delta.m_X != 0 ? delta.m_X : delta.m_Y;
    if (modifiers.m_Control || modifiers.m_Alt || amount == 0 || m_Minimum == m_Maximum ||
        (amount > 0 && m_Value == m_Minimum) || (amount < 0 && m_Value == m_Maximum))
        return false;
    StepBy(-std::clamp(amount, -100.0, 100.0) * m_SingleStep * 3);
    return true;
}

SemanticInfo ScrollBar::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::ScrollBar;
    info.m_Minimum = m_Minimum;
    info.m_Maximum = m_Maximum;
    info.m_Number = m_Value;
    info.m_Step = m_SingleStep;
    info.m_Actions = SemanticAction::SetNumber;
    return info;
}

SemanticStatus ScrollBar::OnSemanticAction(const SemanticCommand& command)
{
    if (command.m_Action != SemanticAction::SetNumber)
        return Widget::OnSemanticAction(command);
    if (!std::isfinite(command.m_Number) || command.m_Number < m_Minimum || command.m_Number > m_Maximum)
        return SemanticStatus::InvalidValue;
    SetValue(command.m_Number);
    return SemanticStatus::Succeeded;
}
} // namespace EStudioUI
