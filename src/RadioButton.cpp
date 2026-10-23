#include <EStudioUI/RadioButton.h>
#include <EStudioUI/UiContext.h>
#include <stdexcept>

namespace EStudioUI
{
RadioButton::RadioButton(std::string text, std::shared_ptr<RadioGroup> group) : CheckBox(std::move(text), "radiobutton")
{
    SetGroup(std::move(group));
}

RadioButton::~RadioButton()
{
    if (m_Group)
        m_Group->Remove(*this);
}

void RadioButton::SetGroup(std::shared_ptr<RadioGroup> group)
{
    VerifyStructuralMutation();
    if (group == m_Group)
        return;
    if (group)
        group->Add(*this);
    if (m_Group)
        m_Group->Remove(*this);
    m_Group = std::move(group);
    if (m_Group && IsChecked())
        m_Group->Select(this);
}

std::shared_ptr<RadioGroup> RadioButton::GetGroup() const
{
    VerifyAccess();
    return m_Group;
}

void RadioButton::SetAction(std::shared_ptr<Action> action)
{
    VerifyStructuralMutation();
    if (action)
        throw std::invalid_argument("Radio buttons use RadioGroup rather than independent toggle actions.");
    Button::SetAction({});
}

void RadioButton::SetCheckState(CheckState state)
{
    VerifyStructuralMutation();
    if (state != CheckState::Checked && state != CheckState::Unchecked)
        throw std::invalid_argument("Radio buttons have two states.");
    const auto group = m_Group;
    if (!group)
    {
        ApplyCheckState(state);
        return;
    }
    if (state == CheckState::Checked)
        group->Select(this);
    else if (group->GetCheckedButton() == this)
        group->Select(nullptr);
}

CheckState RadioButton::NextCheckState() const
{
    return CheckState::Checked;
}

SemanticInfo RadioButton::DescribeSemantics() const
{
    auto info = CheckBox::DescribeSemantics();
    info.m_Role = SemanticRole::RadioButton;
    info.m_Selected = IsChecked();
    info.m_SelectionRequired = m_Group && m_Group->IsSelectionRequired();
    info.m_Actions = SemanticAction::Select | SemanticAction::AddToSelection | SemanticAction::RemoveFromSelection;
    return info;
}

SemanticStatus RadioButton::OnSemanticAction(const SemanticCommand& command)
{
    if (command.m_Action == SemanticAction::Select || command.m_Action == SemanticAction::AddToSelection)
    {
        SetChecked(true);
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::RemoveFromSelection)
    {
        if (IsChecked() && m_Group && m_Group->IsSelectionRequired())
            return SemanticStatus::InvalidValue;
        SetChecked(false);
        return SemanticStatus::Succeeded;
    }
    return SemanticStatus::Unsupported;
}

void RadioButton::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (down && !modifiers.m_Control && !modifiers.m_Alt && m_Group &&
        (key == Key::Left || key == Key::Right || key == Key::Up || key == Key::Down))
    {
        const auto group = m_Group;
        group->Move(*this, key == Key::Left || key == Key::Up);
        return;
    }
    CheckBox::OnKey(key, down, repeat, modifiers);
}
} // namespace EStudioUI
