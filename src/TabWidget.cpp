#include <EStudioUI/TabWidget.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/BoxLayout.h>
#include <EStudioUI/ListWidget.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
struct TabStripState
{
    std::uint64_t m_RevealTarget = 0;
    float m_Offset = 0;
};

//! Preserves natural captions, an explicit viewport offset and minimal focus reveal.
class TabStripLayout final : public LayoutStrategy
{
  public:
    explicit TabStripLayout(std::shared_ptr<TabStripState> state) : m_State(std::move(state))
    {
    }

    std::string_view GetName() const override
    {
        return "Scrollable tab strip";
    }

    LayoutPlan Compute(const LayoutContext& context) const override
    {
        auto natural = context;
        natural.m_Constraints.m_Width.reset();
        natural.m_DefiniteWidth = false;
        natural.m_Justification = LayoutJustification::Start;
        auto plan = BoxLayout(Axis::Horizontal).Compute(natural);
        const float width = context.m_Constraints.m_Width.value_or(plan.m_Measure.m_Preferred.m_X);
        float offset = std::clamp(m_State->m_Offset, 0.0f,
                                  std::max(0.0f, plan.m_Measure.m_Preferred.m_X - width));
        for (const auto& slot : plan.m_Slots)
            if (context.m_Children[slot.m_Index].m_Target == m_State->m_RevealTarget)
            {
                const auto& bounds = slot.m_Bounds;
                if (bounds.m_X < offset || bounds.m_Width > width)
                    offset = bounds.m_X;
                else if (bounds.m_X + bounds.m_Width > offset + width)
                    offset = bounds.m_X + bounds.m_Width - width;
                break;
            }
        offset = std::clamp(offset, 0.0f, std::max(0.0f, plan.m_Measure.m_Preferred.m_X - width));
        for (auto& slot : plan.m_Slots)
            slot.m_Bounds.m_X -= offset;
        plan.m_Measure.m_Minimum.m_X = 0;
        return plan;
    }

  private:
    std::shared_ptr<TabStripState> m_State;
};

//! Invalidates its own arrangement when selection moves inside an unchanged viewport.
class TabStrip final : public Widget
{
  public:
    TabStrip() : Widget("tabbar"), m_State(std::make_shared<TabStripState>())
    {
        SetWidth(SizeRule::Fill());
        SetHeight(SizeRule::Content());
        SetLayout(TabStripLayout(m_State));
    }

    void Reveal(std::uint64_t selected)
    {
        if (m_State->m_RevealTarget == selected)
            return;
        m_State->m_Offset = GetOffset();
        m_State->m_RevealTarget = selected;
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
    }

    bool ScrollPage(int direction)
    {
        return ScrollBy(static_cast<float>(direction) * std::max(48.0f, GetContentBounds().m_Width * 0.75f));
    }

    bool ScrollWheel(Point delta)
    {
        const float amount = std::abs(delta.m_X) > std::abs(delta.m_Y) ? delta.m_X : delta.m_Y;
        return ScrollBy(-amount * 48.0f);
    }

  protected:
    bool OnScroll(Point delta, Modifiers) override
    {
        return ScrollWheel(delta);
    }

  private:
    float GetOffset() const
    {
        const auto& children = GetPreparedLayoutReport().m_Children;
        return children.empty() ? 0.0f
                                : std::max(0.0f, children.front().m_Rules.m_Margin.m_Left -
                                                    children.front().m_Slot.m_Bounds.m_X);
    }

    bool ScrollBy(float distance)
    {
        const auto& children = GetPreparedLayoutReport().m_Children;
        if (children.empty())
            return false;
        const float offset = GetOffset();
        const auto& last = children.back();
        const float width = last.m_Slot.m_Bounds.m_X + last.m_Slot.m_Bounds.m_Width +
                            last.m_Rules.m_Margin.m_Right + offset;
        const float next = std::clamp(offset + distance, 0.0f, std::max(0.0f, width - GetContentBounds().m_Width));
        if (next == offset)
            return false;
        m_State->m_RevealTarget = 0;
        m_State->m_Offset = next;
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
        return true;
    }

    std::shared_ptr<TabStripState> m_State;
};

//! Includes navigation controls in the strip's wheel-scrolling region.
class TabNavigation final : public Widget
{
  public:
    TabNavigation() : Widget("tab-navigation")
    {
    }

    void SetStrip(TabStrip& strip)
    {
        m_Strip = WidgetRef(&strip);
    }

  protected:
    bool OnScroll(Point delta, Modifiers) override
    {
        if (auto* strip = dynamic_cast<TabStrip*>(m_Strip.Get()))
            return strip->ScrollWheel(delta);
        return false;
    }

  private:
    WidgetRef m_Strip;
};

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
    auto& navigation = Add<TabNavigation>();
    navigation.SetLayoutDirection(Axis::Horizontal);
    navigation.SetHeight(SizeRule::Content());
    navigation.SetGap(4);
    auto& backward = navigation.Add<ToolButton>("‹");
    backward.AddClass("tab-scroll");
    backward.AddClass("tab-scroll-backward");
    backward.SetAccessibleName("Scroll tabs left");
    backward.SetWidth(SizeRule::Fixed(28));
    backward.SetHeight(SizeRule::Fill());
    auto& strip = navigation.Add<TabStrip>();
    m_Strip = &strip;
    navigation.SetStrip(strip);
    auto& forward = navigation.Add<ToolButton>("›");
    forward.AddClass("tab-scroll");
    forward.AddClass("tab-scroll-forward");
    forward.SetAccessibleName("Scroll tabs right");
    forward.SetWidth(SizeRule::Fixed(28));
    forward.SetHeight(SizeRule::Fill());
    const WidgetRef stripRef(&strip);
    backward.Clicked().Connect(
        [stripRef]
        {
            if (auto* live = dynamic_cast<TabStrip*>(stripRef.Get()))
                live->ScrollPage(-1);
        });
    forward.Clicked().Connect(
        [stripRef]
        {
            if (auto* live = dynamic_cast<TabStrip*>(stripRef.Get()))
                live->ScrollPage(1);
        });
    m_ChooserButton = &navigation.Add<ToolButton>("...");
    m_ChooserButton->AddClass("tab-overflow");
    m_ChooserButton->SetAccessibleName("Choose a tab");
    m_ChooserButton->SetWidth(SizeRule::Fixed(40));
    m_ChooserButton->SetHeight(SizeRule::Fill());
    const WidgetRef self(this);
    m_ChooserButton->Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<TabWidget*>(self.Get()))
                live->ShowTabChooser();
        });
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
    static_cast<TabStrip*>(m_Strip)->Reveal(m_Tabs[static_cast<std::size_t>(index)].m_Button.Get()->m_TargetKey);
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

void TabWidget::OnDescendantFocused(Widget& descendant)
{
    for (const auto& tab : m_Tabs)
        if (tab.m_Button.Get() == &descendant)
        {
            static_cast<TabStrip*>(m_Strip)->Reveal(descendant.m_TargetKey);
            break;
        }
}

void TabWidget::ShowTabChooser()
{
    VerifyStructuralMutation();
    if (!GetContext() || m_Tabs.empty())
        return;
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    auto* list = dynamic_cast<ListWidget*>(m_Chooser.Get());
    if (!list)
    {
        auto popup = std::make_unique<ListWidget>();
        popup->AddClass("tab-chooser");
        popup->SetAccessibleName("Choose a tab");
        popup->SetWidth(SizeRule::Content(0, 360));
        list = &static_cast<ListWidget&>(GetContext()->AddOwnedPopup(std::move(popup), *this));
        m_Chooser = WidgetRef(list);
        const auto choose = [self](int index)
        {
            if (auto* live = dynamic_cast<TabWidget*>(self.Get()))
                live->ChooseTab(index);
        };
        list->ItemClicked().Connect(choose);
        list->Activated().Connect(choose);
    }
    std::vector<std::string> captions;
    captions.reserve(m_Tabs.size());
    m_ChooserPages.clear();
    m_ChooserPages.reserve(m_Tabs.size());
    for (const auto& tab : m_Tabs)
    {
        captions.push_back(static_cast<ToolButton*>(tab.m_Button.Get())->GetText());
        m_ChooserPages.push_back(tab.m_Page);
    }
    const WidgetRef listRef(list);
    const auto current = m_Current;
    list->SetItems(std::move(captions));
    if (!self.Get() || !listRef.Get())
        return;
    list->SetCurrentIndex(current);
    if (!self.Get() || !listRef.Get())
        return;
    PopupOptions options;
    options.m_AnchorWidget = m_ChooserButton;
    options.m_Placement = PopupPlacement::Below;
    GetContext()->ShowPopup(*list, {}, options);
}

void TabWidget::ChooseTab(int index)
{
    const auto lease = RetainOwner();
    if (index < 0 || static_cast<std::size_t>(index) >= m_ChooserPages.size())
        return;
    const WidgetRef self(this);
    const auto page = m_ChooserPages[static_cast<std::size_t>(index)];
    if (auto* chooser = m_Chooser.Get())
        GetContext()->ClosePopup(*chooser, {PopupCloseReason::Accepted, index});
    if (!self.Get())
        return;
    const auto found = std::find_if(m_Tabs.begin(), m_Tabs.end(),
                                    [&](const Tab& tab) { return page.Get() && tab.m_Page.Get() == page.Get(); });
    if (found == m_Tabs.end())
        return;
    const auto button = found->m_Button;
    SetCurrentIndex(static_cast<int>(found - m_Tabs.begin()));
    if (self.Get())
        if (auto* live = button.Get())
            GetContext()->SetFocus(live);
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
