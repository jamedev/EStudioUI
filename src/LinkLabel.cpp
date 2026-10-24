#include <EStudioUI/LinkLabel.h>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
LinkLabel::LinkLabel(std::string text, std::string target) : SelectableLabel(std::move(text))
{
    AddClass("link-label");
    SetTarget(std::move(target));
}

void LinkLabel::SetTarget(std::string target)
{
    VerifyStructuralMutation();
    if (target.size() > 8192)
    {
        throw std::length_error("Link targets are limited to 8192 bytes.");
    }
    m_Target = std::move(target);
    NotifySemanticsChanged();
}

const std::string& LinkLabel::GetTarget() const
{
    VerifyAccess();
    return m_Target;
}

Signal<std::string>& LinkLabel::Activated()
{
    VerifyAccess();
    return m_Activated;
}

void LinkLabel::OnPointerDown(const PointerEvent& event)
{
    m_Press = event.m_Position;
    m_Dragged = event.m_Clicks > 1;
    m_Pressed = true;
    TextBox::OnPointerDown(event);
}

void LinkLabel::OnPointerMove(const PointerEvent& event)
{
    if (m_Pressed && std::hypot(event.m_Position.m_X - m_Press.m_X, event.m_Position.m_Y - m_Press.m_Y) >= 4)
    {
        m_Dragged = true;
    }
    TextBox::OnPointerMove(event);
}

void LinkLabel::OnCaptureLost()
{
    m_Pressed = false;
    TextBox::OnCaptureLost();
}

void LinkLabel::OnPointerUp(const PointerEvent& event)
{
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    const bool click = m_Pressed && !m_Dragged;
    m_Pressed = false;
    TextBox::OnPointerUp(event);
    if (!self.Get())
    {
        return;
    }
    if (click && event.m_InsideClip && GetBounds().Contains(event.m_Position) &&
        std::hypot(event.m_Position.m_X - m_Press.m_X, event.m_Position.m_Y - m_Press.m_Y) < 4)
    {
        const auto target = m_Target;
        m_Activated.Emit(target);
    }
}

void LinkLabel::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (key == Key::Enter && down && !repeat && !modifiers.m_Control && !modifiers.m_Alt)
    {
        const auto target = m_Target;
        m_Activated.Emit(target);
    }
    else
    {
        TextBox::OnKey(key, down, repeat, modifiers);
    }
}

SemanticInfo LinkLabel::DescribeSemantics() const
{
    auto info = TextBox::DescribeSemantics();
    info.m_Role = SemanticRole::Button;
    info.m_Name = GetText();
    info.m_Description = "Link: " + m_Target;
    info.m_Actions = info.m_Actions | SemanticAction::Invoke;
    return info;
}

SemanticStatus LinkLabel::OnSemanticAction(const SemanticCommand& command)
{
    if (command.m_Action == SemanticAction::Invoke)
    {
        const auto target = m_Target;
        m_Activated.Emit(target);
        return SemanticStatus::Succeeded;
    }
    return TextBox::OnSemanticAction(command);
}
} // namespace EStudioUI
