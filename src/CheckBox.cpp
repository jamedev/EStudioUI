#include <EStudioUI/CheckBox.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/TextBuffer.h>
#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
CheckBox::CheckBox(std::string text, std::string type) : Button(std::move(text), type), m_Radio(type == "radiobutton")
{
    RegisterStylePart(
        "indicator", Selected | Indeterminate,
        [](const Style& owner, unsigned state)
        {
            Style style;
            style.m_Width = style.m_Height = {Length::Unit::Pixels, 20};
            style.m_Radius = 5;
            style.m_BorderWidth = 1;
            style.m_BorderColor = owner.m_PlaceholderColor;
            if (state & (Selected | Indeterminate))
                style.m_Background = {0.15f, 0.48f, 0.95f, 1};
            return style;
        },
        true);
    RegisterStylePart("mark", Selected | Indeterminate,
                      [](const Style&, unsigned)
                      {
                          Style style;
                          style.m_Color = style.m_Background = {1, 1, 1, 1};
                          style.m_Width = {Length::Unit::Pixels, 2};
                          return style;
                      });
    RegisterStylePart("label", 0, [](const Style&, unsigned) { return Style{}; });
    const WidgetRef self(this);
    Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<CheckBox*>(self.Get()); live && !live->GetAction())
                live->SetCheckState(live->NextCheckState());
        });
}

void CheckBox::SetAction(std::shared_ptr<Action> action)
{
    VerifyStructuralMutation();
    if (action && (m_Tristate || !action->GetSnapshot()->m_Checkable))
        throw std::invalid_argument("Checkbox commands require a checkable boolean action and two-state cycling.");
    Button::SetAction(std::move(action));
}

void CheckBox::SetTristate(bool enabled)
{
    VerifyStructuralMutation();
    if (enabled && GetAction())
        throw std::logic_error("Three-state cycling requires standalone checkbox state.");
    m_Tristate = enabled;
}

bool CheckBox::IsTristate() const
{
    VerifyAccess();
    return m_Tristate;
}

CheckState CheckBox::GetCheckState() const
{
    VerifyAccess();
    return m_CheckState;
}

bool CheckBox::IsChecked() const
{
    return GetCheckState() == CheckState::Checked;
}

void CheckBox::SetChecked(bool checked)
{
    SetCheckState(checked ? CheckState::Checked : CheckState::Unchecked);
}

Signal<CheckState>& CheckBox::StateChanged()
{
    VerifyAccess();
    return m_StateChanged;
}

Signal<bool>& CheckBox::Toggled()
{
    VerifyAccess();
    return m_Toggled;
}

void CheckBox::SetCheckState(CheckState state)
{
    VerifyStructuralMutation();
    if (state < CheckState::Unchecked || state > CheckState::Indeterminate)
        throw std::invalid_argument("Invalid checkbox state.");
    if (m_CheckState == state)
        return;
    if (const auto action = GetAction())
    {
        if (state == CheckState::Indeterminate || !action->GetSnapshot()->m_Checkable)
            throw std::logic_error("Checkbox actions require boolean checkable state.");
        action->SetChecked(state == CheckState::Checked);
        return;
    }
    ApplyCheckState(state);
}

void CheckBox::ApplyCheckState(CheckState state, bool notify)
{
    VerifyStructuralMutation();
    if (m_CheckState == state)
        return;
    m_CheckState = state;
    const unsigned bits = state == CheckState::Checked         ? Selected
                          : state == CheckState::Indeterminate ? Indeterminate
                                                               : 0u;
    SetState(Selected, state == CheckState::Checked);
    SetState(Indeterminate, state == CheckState::Indeterminate);
    SetPartState("indicator", bits);
    SetPartState("mark", bits);
    NotifySemanticsChanged();
    if (notify)
        NotifyCheckState();
}

void CheckBox::NotifyCheckState()
{
    if (m_Notifying || m_Notified == m_CheckState)
        return;
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    m_Notifying = true;
    try
    {
        unsigned turns = 0;
        while (m_Notified != m_CheckState)
        {
            if (++turns > 64)
                throw std::logic_error("Checkbox observers exceeded 64 reentrant changes.");
            const auto previous = m_Notified, current = m_CheckState;
            m_Notified = current;
            m_StateChanged.Emit(current);
            if (!self.Get())
                return;
            if (current == m_CheckState && (previous == CheckState::Checked) != (current == CheckState::Checked))
                m_Toggled.Emit(current == CheckState::Checked);
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

CheckState CheckBox::NextCheckState() const
{
    if (m_Tristate)
        return m_CheckState == CheckState::Unchecked ? CheckState::Checked
               : m_CheckState == CheckState::Checked ? CheckState::Indeterminate
                                                     : CheckState::Unchecked;
    return IsChecked() ? CheckState::Unchecked : CheckState::Checked;
}

void CheckBox::OnActionChanged()
{
    const WidgetRef self(this);
    Button::OnActionChanged();
    if (self.Get())
    {
        const auto state = GetAction()->GetSnapshot();
        if (!state->m_Checkable)
            SetEnabled(false);
        if (self.Get())
            ApplyCheckState(state->m_Checked ? CheckState::Checked : CheckState::Unchecked);
    }
}

SemanticInfo CheckBox::DescribeSemantics() const
{
    auto info = Button::DescribeSemantics();
    info.m_Role = SemanticRole::CheckBox;
    info.m_Checked = IsChecked();
    info.m_Indeterminate = m_CheckState == CheckState::Indeterminate;
    info.m_Actions = SemanticAction::Toggle;
    return info;
}

void CheckBox::OnFontsChanged() noexcept
{
    Label::OnFontsChanged();
    m_Caption = {};
}

const TextLayout& CheckBox::Caption(FontFace& font) const
{
    const auto& style = GetPartStyle("label");
    if (m_CaptionText != GetText() || !font.Matches(m_Caption, style))
    {
        m_CaptionText = GetText();
        m_Caption = font.Shape(TextBuffer::Sanitize(m_CaptionText), style);
    }
    return m_Caption;
}

Point CheckBox::Measure(FontFace& font) const
{
    const auto& caption = Caption(font);
    const auto& indicator = GetPartStyle("indicator");
    return {indicator.m_Width.Resolve(20, 20) + 10 + caption.m_Width,
            std::max(indicator.m_Height.Resolve(20, 20), caption.m_Ascender + caption.m_Descender)};
}

MeasureResult CheckBox::MeasureConstrained(FontFace& font, const MeasureConstraints&) const
{
    const auto size = Measure(font);
    const auto& text = Caption(font);
    return {{}, size, (size.m_Y - text.m_Ascender - text.m_Descender) * 0.5f + text.m_Ascender, false};
}

void CheckBox::Paint(Renderer& renderer, FontFace& font, double, float opacity) const
{
    renderer.DrawBox(GetBounds(), GetStyle(), GetClip(), opacity);
    const auto content = GetContentBounds();
    const auto& indicator = GetPartStyle("indicator");
    const auto& caption = Caption(font);
    const bool rtl = caption.m_RightToLeft;
    const float width = std::max(0.0f, indicator.m_Width.Resolve(content.m_Width, 20)),
                height = std::max(0.0f, indicator.m_Height.Resolve(content.m_Height, 20));
    const Rect box{rtl ? content.m_X + content.m_Width - width : content.m_X,
                   content.m_Y + (content.m_Height - height) * 0.5f, width, height};
    renderer.PushState(indicator.m_Transform.Resolve(box));
    renderer.DrawBox(box, indicator, GetClip(), opacity * indicator.m_Opacity);
    renderer.PopState();
    if (m_CheckState != CheckState::Unchecked)
    {
        const auto& mark = GetPartStyle("mark");
        auto color = mark.m_Color;
        color.m_A *= opacity * mark.m_Opacity;
        renderer.PushState(mark.m_Transform.Resolve(box));
        const float stroke = std::max(0.5f, mark.m_Width.Resolve(width, 2));
        if (m_Radio)
            renderer.DrawCircle({box.m_X + width * 0.5f, box.m_Y + height * 0.5f}, std::min(width, height) * 0.23f,
                                mark, GetClip(), opacity * mark.m_Opacity);
        else if (m_CheckState == CheckState::Indeterminate)
            renderer.DrawLine({box.m_X + width * 0.25f, box.m_Y + height * 0.5f},
                              {box.m_X + width * 0.75f, box.m_Y + height * 0.5f}, color, stroke, GetClip());
        else
        {
            renderer.DrawLine({box.m_X + width * 0.23f, box.m_Y + height * 0.52f},
                              {box.m_X + width * 0.44f, box.m_Y + height * 0.72f}, color, stroke, GetClip());
            renderer.DrawLine({box.m_X + width * 0.44f, box.m_Y + height * 0.72f},
                              {box.m_X + width * 0.8f, box.m_Y + height * 0.28f}, color, stroke, GetClip());
        }
        renderer.PopState();
    }
    const auto& label = GetPartStyle("label");
    auto color = label.m_Color;
    color.m_A *= opacity * label.m_Opacity;
    const Rect text{content.m_X + (rtl ? 0 : width + 10), content.m_Y, std::max(0.0f, content.m_Width - width - 10),
                    content.m_Height};
    renderer.PushState(label.m_Transform.Resolve(text));
    renderer.DrawText(
        caption,
        {rtl ? text.m_X + text.m_Width - caption.m_Width : text.m_X,
         text.m_Y + (text.m_Height - caption.m_Ascender - caption.m_Descender) * 0.5f + caption.m_Ascender},
        color, GetClip().Intersect(text));
    renderer.PopState();
}
} // namespace EStudioUI
