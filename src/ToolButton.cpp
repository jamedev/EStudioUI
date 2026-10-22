#include <EStudioUI/ToolButton.h>

namespace EStudioUI
{
ToolButton::ToolButton(std::string text) : Button(std::move(text), "toolbutton")
{
    const WidgetRef owner(this);
    Clicked().Connect(
        [owner]
        {
            if (auto* live = dynamic_cast<ToolButton*>(owner.Get()); live && live->m_Checkable)
                live->SetChecked(!live->m_Checked);
        });
}

void ToolButton::SetCheckable(bool checkable)
{
    VerifyAccess();
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
} // namespace EStudioUI
