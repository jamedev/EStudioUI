#include <EStudioUI/TabWidget.h>
#include <EStudioUI/UiContext.h>

#include <stdexcept>

namespace EStudioUI
{
namespace
{
//! A tab command publishes selection without an independent toggle action.
class TabButton final : public ToolButton
{
  public:
    //! Creates a normal tool button with a stable styling hook.
    explicit TabButton(std::string text) : ToolButton(std::move(text))
    {
        AddClass("tab-button");
    }

    //! Updates CSS selection without delivering an unrelated toggle signal.
    void SetSelected(bool selected)
    {
        m_Selected = selected;
        SetState(Selected, selected);
    }

  protected:
    //! Presents an actual selector as a selectable tab rather than a generic command button.
    SemanticInfo DescribeSemantics() const override
    {
        auto info = ToolButton::DescribeSemantics();
        info.m_Role = SemanticRole::Tab;
        info.m_Selected = m_Selected;
        info.m_Actions = SemanticAction::Select | SemanticAction::AddToSelection | SemanticAction::RemoveFromSelection;
        return info;
    }

    //! Reuses the same command path as pointer and keyboard tab selection.
    SemanticStatus OnSemanticAction(const SemanticCommand& command) override
    {
        if (command.m_Action == SemanticAction::Select)
            return Trigger() ? SemanticStatus::Succeeded : SemanticStatus::Disabled;
        if (command.m_Action == SemanticAction::AddToSelection)
            return m_Selected ? SemanticStatus::Succeeded : SemanticStatus::InvalidValue;
        if (command.m_Action == SemanticAction::RemoveFromSelection)
            return m_Selected ? SemanticStatus::InvalidValue : SemanticStatus::Succeeded;
        return ToolButton::OnSemanticAction(command);
    }

  private:
    //! Mirrors the tab container's canonical selected-page state.
    bool m_Selected = false;
};
} // namespace

TabWidget::TabWidget() : Widget("tabwidget")
{
    m_Strip = &Add<Widget>("tabbar");
    m_Strip->SetLayoutDirection(Axis::Horizontal);
    m_Strip->SetHeight(SizeRule::Content());
    m_Pages = &Add<Widget>("tabpages");
    m_Pages->SetHeight(SizeRule::Fill());
}

Widget& TabWidget::AddTab(std::string title)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    auto& page = m_Pages->Add<Widget>("tabpage");
    page.SetHeight(SizeRule::Fill());
    auto& button = m_Strip->Add<TabButton>(std::move(title));
    const WidgetRef pageRef(&page);
    const WidgetRef owner(this);
    button.Clicked().Connect(
        [owner, pageRef]
        {
            auto* tabs = dynamic_cast<TabWidget*>(owner.Get());
            if (!tabs)
                return;
            for (std::size_t i = 0; i < tabs->m_Tabs.size(); ++i)
                if (tabs->m_Tabs[i].m_Page.Get() == pageRef.Get())
                {
                    tabs->SetCurrentIndex(static_cast<int>(i));
                    return;
                }
        });
    m_Tabs.push_back({pageRef, WidgetRef(&button)});
    page.SetVisible(false);
    if (m_Current < 0)
        SetCurrentIndex(0);
    return page;
}

void TabWidget::RemoveTab(int index)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    (void)GetPage(index);
    const auto removed = m_Tabs[static_cast<std::size_t>(index)];
    m_Tabs.erase(m_Tabs.begin() + index);
    if (auto* page = removed.m_Page.Get())
        m_Pages->RemoveChild(*page);
    if (auto* button = removed.m_Button.Get())
        m_Strip->RemoveChild(*button);
    const int previous = m_Current;
    if (index > previous)
    {
        Invalidate(Invalidation::Content);
        return;
    }
    const int next = m_Tabs.empty() ? -1 : std::min(index < previous ? previous - 1 : previous, GetTabCount() - 1);
    m_Current = -1;
    if (!m_Tabs.empty())
        SetCurrentIndex(std::max(0, next));
    else
        m_CurrentChanged.Emit(-1);
}

Widget& TabWidget::GetPage(int index) const
{
    VerifyAccess();
    if (index < 0 || index >= GetTabCount())
        throw std::out_of_range("Invalid tab index.");
    auto* page = m_Tabs[static_cast<std::size_t>(index)].m_Page.Get();
    if (!page)
        throw std::logic_error("Tab pages must be removed with RemoveTab.");
    return *page;
}

int TabWidget::GetTabCount() const
{
    VerifyAccess();
    return static_cast<int>(m_Tabs.size());
}

void TabWidget::SetCurrentIndex(int index)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    (void)GetPage(index);
    if (m_Current == index)
        return;
    m_Current = index;
    for (std::size_t i = 0; i < m_Tabs.size(); ++i)
    {
        if (auto* page = m_Tabs[i].m_Page.Get())
            page->SetVisible(static_cast<int>(i) == index);
        if (auto* button = m_Tabs[i].m_Button.Get())
        {
            static_cast<TabButton*>(button)->SetSelected(static_cast<int>(i) == index);
            if (static_cast<int>(i) == index)
                button->AddClass("selected");
            else
                button->RemoveClass("selected");
        }
    }
    Invalidate(Invalidation::Content);
    m_CurrentChanged.Emit(index);
}

int TabWidget::GetCurrentIndex() const
{
    VerifyAccess();
    return m_Current;
}

void TabWidget::SetTabText(int index, std::string title)
{
    VerifyAccess();
    (void)GetPage(index);
    if (auto* button = dynamic_cast<ToolButton*>(m_Tabs[static_cast<std::size_t>(index)].m_Button.Get()))
        button->SetText(std::move(title));
}

Signal<int>& TabWidget::CurrentChanged()
{
    VerifyAccess();
    return m_CurrentChanged;
}

bool TabWidget::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Alt || m_Tabs.empty())
        return false;
    int next = -1;
    if (key == Key::Tab && modifiers.m_Control)
        next = (m_Current + (modifiers.m_Shift ? -1 : 1) + GetTabCount()) % GetTabCount();
    else
    {
        if (key != Key::Left && key != Key::Right && key != Key::Home && key != Key::End)
            return false;
        const auto found = std::find_if(m_Tabs.begin(), m_Tabs.end(), [&](const auto& tab)
                                        { return tab.m_Button.Get() == GetContext()->GetFocus(); });
        if (found == m_Tabs.end())
            return false;
        next = key == Key::Home ? 0
               : key == Key::End
                   ? GetTabCount() - 1
                   : (static_cast<int>(found - m_Tabs.begin()) + (key == Key::Right ? 1 : -1) + GetTabCount()) %
                         GetTabCount();
    }
    const auto button = m_Tabs[static_cast<std::size_t>(next)].m_Button;
    auto* context = GetContext();
    SetCurrentIndex(next);
    if (auto* live = button.Get())
        context->SetFocus(live);
    return true;
}

SemanticInfo TabWidget::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_SelectionRequired = !m_Tabs.empty();
    return info;
}

std::size_t TabWidget::GetSemanticSelectionCount() const
{
    return m_Current >= 0 && static_cast<std::size_t>(m_Current) < m_Tabs.size() &&
                   m_Tabs[static_cast<std::size_t>(m_Current)].m_Button.Get()
               ? 1
               : 0;
}

std::vector<WidgetRef> TabWidget::GetSemanticSelectedWidgets(std::size_t offset, std::size_t limit) const
{
    if (offset || !limit || !GetSemanticSelectionCount())
        return {};
    return {m_Tabs[static_cast<std::size_t>(m_Current)].m_Button};
}

} // namespace EStudioUI
