#include <EStudioUI/ToolButton.h>

namespace EStudioUI
{
ToolButton::ToolButton(std::string text) : Button(std::move(text), "toolbutton")
{
    Clicked().Connect(
        [this]
        {
            if (m_Checkable)
                SetChecked(!m_Checked);
        });
}

void ToolButton::SetCheckable(bool checkable)
{
    m_Checkable = checkable;
}

bool ToolButton::IsCheckable() const
{
    return m_Checkable;
}

void ToolButton::SetChecked(bool checked)
{
    if (m_Checked == checked)
        return;
    m_Checked = checked;
    SetState(Selected, checked);
    m_Toggled.Emit(checked);
}

bool ToolButton::IsChecked() const
{
    return m_Checked;
}

Signal<bool>& ToolButton::Toggled()
{
    return m_Toggled;
}
} // namespace EStudioUI
