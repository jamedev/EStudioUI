#include <EStudioUI/StatusBar.h>

namespace EStudioUI
{
StatusBar::StatusBar() : Widget("statusbar")
{
    SetLayoutDirection(Axis::Horizontal);
    SetHeight(SizeRule::Content());
    m_Message = &Add<Label>();
    m_Message->AddClass("status-message");
    m_Message->SetWidth(SizeRule::Fill());
    m_Permanent = &Add<Widget>("status-permanent");
    m_Permanent->SetLayoutDirection(Axis::Horizontal);
    m_Permanent->SetWidth(SizeRule::Content());
    m_Permanent->SetHeight(SizeRule::Fill());
}

void StatusBar::SetMessage(std::string message)
{
    VerifyStructuralMutation();
    m_Message->SetText(std::move(message));
}

const std::string& StatusBar::GetMessage() const
{
    VerifyAccess();
    return m_Message->GetText();
}

void StatusBar::ClearMessage()
{
    SetMessage({});
}

Widget& StatusBar::Permanent() const
{
    VerifyAccess();
    return *m_Permanent;
}
} // namespace EStudioUI
