#include <EStudioUI/GroupBox.h>

namespace EStudioUI
{
GroupBox::GroupBox(std::string title) : Widget("groupbox")
{
    m_Title = &Add<Label>(title, "group-title");
    m_Check = &Add<CheckBox>(std::move(title));
    m_Check->AddClass("group-title");
    m_Check->SetChecked(true);
    m_Check->SetVisible(false);
    m_Content = &Add<Widget>("group-content");
    m_Content->SetHeight(SizeRule::Fill());
    const WidgetRef self(this);
    m_Check->Toggled().Connect(
        [self](bool checked)
        {
            if (auto* live = dynamic_cast<GroupBox*>(self.Get()))
            {
                live->m_Content->SetEnabled(!live->m_Checkable || checked);
                if (self.Get())
                    live->NotifySemanticsChanged();
            }
        });
}

Widget& GroupBox::Content() const
{
    VerifyAccess();
    return *m_Content;
}

void GroupBox::SetTitle(std::string title)
{
    VerifyStructuralMutation();
    m_Title->SetText(title);
    m_Check->SetText(std::move(title));
    NotifySemanticsChanged();
}

void GroupBox::SetCheckable(bool checkable)
{
    VerifyStructuralMutation();
    m_Checkable = checkable;
    m_Title->SetVisible(!checkable);
    m_Check->SetVisible(checkable);
    m_Content->SetEnabled(!checkable || m_Check->IsChecked());
    NotifySemanticsChanged();
}

bool GroupBox::IsCheckable() const
{
    VerifyAccess();
    return m_Checkable;
}

void GroupBox::SetChecked(bool checked)
{
    VerifyStructuralMutation();
    m_Check->SetChecked(checked);
}

bool GroupBox::IsChecked() const
{
    VerifyAccess();
    return m_Check->IsChecked();
}

Signal<bool>& GroupBox::Toggled()
{
    VerifyAccess();
    return m_Check->Toggled();
}

SemanticInfo GroupBox::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Name = m_Title->GetText();
    info.m_Checked = IsChecked();
    return info;
}
} // namespace EStudioUI
