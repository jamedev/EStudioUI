#include <EStudioUI/Button.h>
#include <EStudioUI/Renderer.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
Button::Button(std::string text, std::string type) : Label(std::move(text), std::move(type))
{
    RegisterStylePart("image-icon", 0,
                      [](const Style&, unsigned)
                      {
                          Style style;
                          style.m_Width = style.m_Height = {Length::Unit::Pixels, 16};
                          style.m_Gap = 6;
                          return style;
                      });
}

void Button::SetImageIcon(Icon icon)
{
    VerifyStructuralMutation();
    if (m_ImageIcon == icon)
        return;
    const bool changedSize = m_ImageIcon.IsEmpty() != icon.IsEmpty();
    m_ImageIcon = std::move(icon);
    Invalidate(changedSize ? Invalidation::Content : Invalidation::Paint);
}

const Icon& Button::GetImageIcon() const
{
    VerifyAccess();
    return m_ImageIcon;
}

void Button::SetImageIconSize(Point size)
{
    VerifyStructuralMutation();
    if (!std::isfinite(size.m_X) || !std::isfinite(size.m_Y) || size.m_X <= 0 || size.m_Y <= 0 || size.m_X > 1024 ||
        size.m_Y > 1024)
        throw std::invalid_argument("Image icon dimensions must be finite and in (0,1024] logical pixels.");
    if (m_ImageIconSize == size)
        return;
    m_ImageIconSize = size;
    Invalidate(Invalidation::Content);
}

void Button::ClearImageIconSize()
{
    VerifyStructuralMutation();
    if (m_ImageIconSize)
    {
        m_ImageIconSize.reset();
        Invalidate(Invalidation::Content);
    }
}

Point Button::GetImageIconSize() const
{
    VerifyAccess();
    const auto& style = GetPartStyle("image-icon");
    return m_ImageIconSize.value_or(Point{std::clamp(style.m_Width.Resolve(16, 16), 0.0f, 1024.0f),
                                          std::clamp(style.m_Height.Resolve(16, 16), 0.0f, 1024.0f)});
}

float Button::ImageIconExtent() const
{
    return m_ImageIcon.IsEmpty() ? 0
                                 : GetImageIconSize().m_X + (GetText().empty() ? 0 : GetPartStyle("image-icon").m_Gap);
}

Point Button::Measure(FontFace& font) const
{
    auto size = Label::Measure(font);
    if (!m_ImageIcon.IsEmpty())
    {
        size.m_X += ImageIconExtent();
        size.m_Y = std::max(size.m_Y, GetImageIconSize().m_Y);
    }
    return size;
}

MeasureResult Button::MeasureConstrained(FontFace& font, const MeasureConstraints& constraints) const
{
    if (m_ImageIcon.IsEmpty())
        return Label::MeasureConstrained(font, constraints);
    auto textConstraints = constraints;
    if (textConstraints.m_Width)
        textConstraints.m_Width = std::max(0.0f, *textConstraints.m_Width - ImageIconExtent());
    auto result = Label::MeasureConstrained(font, textConstraints);
    const float height = std::max(result.m_Preferred.m_Y, GetImageIconSize().m_Y);
    if (result.m_Baseline && !constraints.m_Height)
        *result.m_Baseline += (height - result.m_Preferred.m_Y) * .5f;
    result.m_Preferred.m_X += ImageIconExtent();
    result.m_Preferred.m_Y = height;
    return result;
}

void Button::PaintImageIcon(Renderer& renderer, Rect slot, float opacity) const
{
    if (m_ImageIcon.IsEmpty() || slot.m_Width <= 0 || slot.m_Height <= 0)
        return;
    const auto size = GetImageIconSize();
    const Rect box{slot.m_X + (slot.m_Width - size.m_X) * .5f, slot.m_Y + (slot.m_Height - size.m_Y) * .5f, size.m_X,
                   size.m_Y};
    const auto& style = GetPartStyle("image-icon");
    renderer.PushState(style.m_Transform.Resolve(box));
    renderer.PushClip(box, style.m_Radius);
    const auto clip = GetClip().Intersect(slot);
    renderer.DrawBox(box, style, clip, opacity * style.m_Opacity);
    const float border = style.m_BorderWidth;
    const Rect content{box.m_X + border + style.m_Padding.m_Left, box.m_Y + border + style.m_Padding.m_Top,
                       std::max(0.0f, box.m_Width - 2 * border - style.m_Padding.m_Left - style.m_Padding.m_Right),
                       std::max(0.0f, box.m_Height - 2 * border - style.m_Padding.m_Top - style.m_Padding.m_Bottom)};
    const auto natural = m_ImageIcon.GetSize();
    const float scale = std::min(content.m_Width / natural.m_X, content.m_Height / natural.m_Y);
    renderer.DrawIcon({content.m_X + (content.m_Width - natural.m_X * scale) * .5f,
                       content.m_Y + (content.m_Height - natural.m_Y * scale) * .5f, natural.m_X * scale,
                       natural.m_Y * scale},
                      m_ImageIcon, clip, opacity * style.m_Opacity);
    renderer.PopState();
}

void Button::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    if (m_ImageIcon.IsEmpty())
    {
        Label::Paint(renderer, font, time, opacity);
        return;
    }
    Widget::Paint(renderer, font, time, opacity);
    const auto content = GetContentBounds();
    const auto& lines = Lines(font, std::max(0.0f, content.m_Width - ImageIconExtent()));
    float width = 0;
    for (const auto& line : lines)
        width = std::max(width, line.m_Width);
    const float extent = ImageIconExtent();
    width = std::min(content.m_Width, width + extent);
    float x = content.m_X;
    if (GetStyle().m_TextAlign == "center")
        x += (content.m_Width - width) * .5f;
    else if (GetStyle().m_TextAlign == "right")
        x += content.m_Width - width;
    const bool rtl = !lines.empty() && lines.front().m_RightToLeft;
    const auto iconSize = GetImageIconSize();
    PaintImageIcon(renderer, {rtl ? x + width - iconSize.m_X : x, content.m_Y, iconSize.m_X, content.m_Height},
                   opacity);
    PaintText(renderer, font, {x + (rtl ? 0 : extent), content.m_Y, std::max(0.0f, width - extent), content.m_Height},
              opacity);
}

bool Button::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Signal<>& Button::Clicked()
{
    VerifyAccess();
    return m_Clicked;
}

void Button::SetAction(std::shared_ptr<Action> action)
{
    VerifyStructuralMutation();
    if (action == m_Action)
        return;
    if (action)
        (void)action->GetSnapshot();
    m_ActionStop.reset();
    m_ActionConnection.reset();
    RemoveAction(m_Action);
    m_Action = std::move(action);
    if (!m_Action)
        return;
    AddAction(m_Action);
    const WidgetRef reference(this);
    m_ActionConnection.emplace(m_Action->Properties().Subscribe(
        [reference](const StateChange<ActionState>&)
        {
            if (auto* live = dynamic_cast<Button*>(reference.Get()))
                live->OnActionChanged();
        }));
    m_ActionStop = std::make_unique<std::stop_callback<std::function<void()>>>(m_Action->GetStopToken(),
                                                                               [reference]
                                                                               {
                                                                                   if (auto* live = reference.Get())
                                                                                       live->SetEnabled(false);
                                                                               });
    OnActionChanged();
}

std::shared_ptr<Action> Button::GetAction() const
{
    VerifyAccess();
    return m_Action;
}

void Button::OnActionChanged()
{
    const auto state = m_Action->GetSnapshot();
    SetText(state->m_Text);
    SetEnabled(state->m_Enabled);
    SetVisible(state->m_Visible);
    SetState(Selected, state->m_Checked);
}

void Button::Activate()
{
    const auto action = m_Action;
    const WidgetRef reference(this);
    m_Clicked.Emit();
    if (reference.Get() && action)
        action->Trigger();
}

bool Button::Trigger()
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (!IsEnabled() || !IsVisible())
        return false;
    Activate();
    return true;
}

void Button::OnPointerDown(const PointerEvent&)
{
    m_PointerDown = true;
    SetState(Pressed, true);
}

void Button::OnPointerMove(const PointerEvent& event)
{
    if (m_PointerDown)
        SetState(Pressed, event.m_InsideClip && ContainsRounded(GetBounds(), GetStyle().m_Radius, event.m_Position));
}

void Button::OnPointerUp(const PointerEvent& event)
{
    const bool activate = m_PointerDown && IsEnabled() && event.m_InsideClip &&
                          ContainsRounded(GetBounds(), GetStyle().m_Radius, event.m_Position);
    m_PointerDown = false;
    SetState(Pressed, false);
    if (activate)
        Activate();
}

void Button::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (key == Key::Space && !down)
    {
        const bool activate = m_SpaceDown && !modifiers.m_Control && !modifiers.m_Alt;
        m_SpaceDown = false;
        SetState(Pressed, m_PointerDown);
        if (activate)
            Activate();
        return;
    }
    if (modifiers.m_Control || modifiers.m_Alt)
        return;
    if (key == Key::Enter && down && !repeat)
        Activate();
    else if (key == Key::Space)
    {
        if (down && !repeat)
        {
            m_SpaceDown = true;
            SetState(Pressed, true);
        }
    }
}

void Button::OnFocus(bool focused)
{
    if (!focused)
        OnCaptureLost();
}

void Button::OnCaptureLost()
{
    m_SpaceDown = false;
    m_PointerDown = false;
    SetState(Pressed, false);
}
} // namespace EStudioUI
