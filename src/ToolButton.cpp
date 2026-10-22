#include <EStudioUI/ToolButton.h>

namespace EStudioUI
{
ToolButton::ToolButton(std::string text) : Button(std::move(text), "toolbutton")
{
    const WidgetRef owner(this);
    Clicked().Connect(
        [owner]
        {
            if (auto* live = dynamic_cast<ToolButton*>(owner.Get()); live && live->m_Checkable && !live->GetAction())
                live->SetChecked(!live->m_Checked);
        });
}

void ToolButton::SetCheckable(bool checkable)
{
    VerifyAccess();
    if (const auto action = GetAction(); action && action->GetSnapshot()->m_Checkable != checkable)
    {
        action->SetCheckable(checkable);
        return;
    }
    if (m_Checkable == checkable)
        return;
    m_Checkable = checkable;
}

bool ToolButton::IsCheckable() const
{
    VerifyAccess();
    return m_Checkable;
}

void ToolButton::SetChecked(bool checked)
{
    VerifyAccess();
    if (const auto action = GetAction(); action && action->GetSnapshot()->m_Checked != checked)
    {
        action->SetChecked(checked);
        return;
    }
    const auto ownerLease = RetainOwner();
    if (m_Checked == checked)
        return;
    m_Checked = checked;
    SetState(Selected, checked);
    m_Toggled.Emit(checked);
}

bool ToolButton::IsChecked() const
{
    VerifyAccess();
    return m_Checked;
}

Signal<bool>& ToolButton::Toggled()
{
    VerifyAccess();
    return m_Toggled;
}

void ToolButton::OnActionChanged()
{
    Button::OnActionChanged();
    const auto state = GetAction()->GetSnapshot();
    m_Checkable = state->m_Checkable;
    SetChecked(state->m_Checked);
}
} // namespace EStudioUI
