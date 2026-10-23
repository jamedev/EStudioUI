#include "PropertyNotification.h"
#include <EStudioUI/CollapsiblePanel.h>
#include <EStudioUI/Renderer.h>

namespace EStudioUI
{
namespace
{
class SectionHeader final : public ToolButton
{
  public:
    explicit SectionHeader(std::string title) : ToolButton(std::move(title))
    {
        AddClass("section-header");
        SetStyleProperty(StyleProperty::Padding, Insets{6, 8, 6, 24});
    }

  protected:
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override
    {
        ToolButton::Paint(renderer, font, time, opacity);
        const auto bounds = GetBounds();
        const Point center{bounds.m_X + 12, bounds.m_Y + bounds.m_Height / 2};
        auto color = GetStyle().m_Color;
        color.m_A *= opacity;
        if (IsChecked())
        {
            renderer.DrawLine({center.m_X - 4, center.m_Y - 2}, {center.m_X, center.m_Y + 2}, color, 1.5f, GetClip());
            renderer.DrawLine({center.m_X, center.m_Y + 2}, {center.m_X + 4, center.m_Y - 2}, color, 1.5f, GetClip());
        }
        else
        {
            renderer.DrawLine({center.m_X - 2, center.m_Y - 4}, {center.m_X + 2, center.m_Y}, color, 1.5f, GetClip());
            renderer.DrawLine({center.m_X + 2, center.m_Y}, {center.m_X - 2, center.m_Y + 4}, color, 1.5f, GetClip());
        }
    }
};
} // namespace

CollapsiblePanel::CollapsiblePanel(std::string title) : Widget("collapsiblepanel"), m_Title(std::move(title))
{
    SetHeight(SizeRule::Content());
    SetGap(0);
    m_Header = &Add<SectionHeader>(m_Title);
    m_Header->SetWidth(SizeRule::Fill());
    m_Header->SetCheckable(true);
    m_Header->SetChecked(true);
    m_Content = &Add<Widget>("section-content");
    m_Content->SetHeight(SizeRule::Content());
    SetState(Expanded, true);
    const WidgetRef self(this);
    m_Header->Toggled().Connect(
        [self](bool expanded)
        {
            if (auto* live = dynamic_cast<CollapsiblePanel*>(self.Get()))
            {
                live->SetExpanded(expanded);
            }
        });
}

Widget& CollapsiblePanel::Content() const
{
    VerifyAccess();
    return *m_Content;
}

void CollapsiblePanel::SetTitle(std::string title)
{
    VerifyStructuralMutation();
    m_Title = std::move(title);
    m_Header->SetText(m_Title);
}

const std::string& CollapsiblePanel::GetTitle() const
{
    VerifyAccess();
    return m_Title;
}

void CollapsiblePanel::SetExpanded(bool expanded)
{
    VerifyStructuralMutation();
    if (expanded == m_Expanded)
    {
        return;
    }
    const WidgetRef self(this);
    m_Expanded = expanded;
    SetState(Expanded, expanded);
    NotifySemanticsChanged();
    m_Content->SetVisible(expanded);
    if (!self.Get() || m_Expanded != expanded)
    {
        return;
    }
    m_Header->SetChecked(expanded);
    if (self.Get() && m_Expanded == expanded)
    {
        Detail::EmitPropertyNotification(*this, m_NotificationDepth, m_ExpandedChanged, expanded);
    }
}

bool CollapsiblePanel::IsExpanded() const
{
    VerifyAccess();
    return m_Expanded;
}

Signal<bool>& CollapsiblePanel::ExpandedChanged()
{
    VerifyAccess();
    return m_ExpandedChanged;
}

SemanticInfo CollapsiblePanel::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Name = m_Title;
    info.m_Expandable = true;
    info.m_Expanded = m_Expanded;
    info.m_Actions = info.m_Actions | SemanticAction::Expand | SemanticAction::Collapse;
    return info;
}

SemanticStatus CollapsiblePanel::OnSemanticAction(const SemanticCommand& command)
{
    if (command.m_Action == SemanticAction::Expand || command.m_Action == SemanticAction::Collapse)
    {
        SetExpanded(command.m_Action == SemanticAction::Expand);
        return SemanticStatus::Succeeded;
    }
    return Widget::OnSemanticAction(command);
}
} // namespace EStudioUI
