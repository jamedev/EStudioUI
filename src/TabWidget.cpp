#include <EStudioUI/TabWidget.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/BoxLayout.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/Renderer.h>

#include "OverflowStrip.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
using TabStrip = Detail::OverflowStrip;

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
        {
            return strip->ScrollWheel(delta);
        }
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

    std::function<void()> m_Select, m_RequestClose;
    std::function<void(Point, bool)> m_Drag;
    bool m_DragEnabled = false;

    void SetClosable(bool enabled)
    {
        if (enabled && !m_Close)
        {
            m_Close = &Add<ToolButton>("×");
            m_Close->AddClass("tab-close");
            m_Close->SetAccessibleName("Close document");
            m_Close->SetWidth(SizeRule::Fixed(18));
            m_Close->SetHeight(SizeRule::Fixed(18));
            m_Close->SetStyleProperty(StyleProperty::Padding, Insets{});
            const WidgetRef self(this);
            m_Close->Clicked().Connect(
                [self]
                {
                    if (auto* live = dynamic_cast<TabButton*>(self.Get()))
                    {
                        if (const auto close = live->m_RequestClose)
                        {
                            close();
                        }
                    }
                });
        }
        if (m_Close)
        {
            m_Close->SetVisible(enabled);
        }
        if (m_Closable != enabled)
        {
            m_Closable = enabled;
            Invalidate(Invalidation::Content);
        }
    }

    void SetModified(bool modified)
    {
        if (m_Modified == modified)
        {
            return;
        }
        m_Modified = modified;
        if (modified)
        {
            AddClass("tab-modified");
        }
        else
        {
            RemoveClass("tab-modified");
        }
        SetAccessibleDescription(modified ? "Unsaved changes" : "");
        Invalidate(Invalidation::Content);
    }

    bool IsModified() const
    {
        return m_Modified;
    }

    //! Updates CSS selection without delivering an unrelated toggle signal.
    void SetSelected(bool selected)
    {
        m_Selected = selected;
        SetState(Selected, selected);
    }

  protected:
    float Gutter() const
    {
        return m_Closable ? 36.0f : (m_Modified ? 12.0f : 0.0f);
    }

    Point Measure(FontFace& font) const override
    {
        auto size = ToolButton::Measure(font);
        size.m_X += Gutter();
        return size;
    }

    MeasureResult MeasureConstrained(FontFace& font, const MeasureConstraints& constraints) const override
    {
        auto adjusted = constraints;
        if (adjusted.m_Width)
        {
            adjusted.m_Width = std::max(0.0f, *adjusted.m_Width - Gutter());
        }
        auto result = ToolButton::MeasureConstrained(font, adjusted);
        result.m_Preferred.m_X += Gutter();
        result.m_Minimum.m_X += Gutter();
        return result;
    }

    void OnLayout() override
    {
        if (m_Close && m_Closable)
        {
            const auto content = GetContentBounds();
            m_Close->SetFloatingBounds({content.m_X + std::max(0.0f, content.m_Width - 18),
                                        content.m_Y + (content.m_Height - 18) / 2, 18, 18});
        }
    }

    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override
    {
        if (!m_Closable && !m_Modified)
        {
            ToolButton::Paint(renderer, font, time, opacity);
            return;
        }
        Widget::Paint(renderer, font, time, opacity);
        auto content = GetContentBounds();
        content.m_Width = std::max(0.0f, content.m_Width - Gutter());
        const auto caption = content;
        const auto& lines = Lines(font, std::max(0.0f, content.m_Width - ImageIconExtent()));
        float width = 0;
        for (const auto& line : lines)
        {
            width = std::max(width, line.m_Width);
        }
        const float extent = ImageIconExtent();
        width = std::min(content.m_Width, width + extent);
        float x = content.m_X;
        if (GetStyle().m_TextAlign == "center")
        {
            x += (content.m_Width - width) / 2;
        }
        else if (GetStyle().m_TextAlign == "right")
        {
            x += content.m_Width - width;
        }
        const bool rtl = !lines.empty() && lines.front().m_RightToLeft;
        const auto icon = GetImageIconSize();
        PaintImageIcon(renderer, {rtl ? x + width - icon.m_X : x, content.m_Y, icon.m_X, content.m_Height}, opacity);
        PaintText(renderer, font,
                  {x + (rtl ? 0 : extent), content.m_Y, std::max(0.0f, width - extent), content.m_Height}, opacity);
        content = caption;
        if (m_Modified)
        {
            Style dot;
            dot.m_Background = GetStyle().m_Color;
            dot.m_Radius = 3;
            renderer.DrawBox({content.m_X + content.m_Width + 3, content.m_Y + content.m_Height / 2 - 3, 6, 6}, dot,
                             GetClip(), opacity);
        }
    }

    void OnPointerDown(const PointerEvent& event) override
    {
        m_Origin = GetContext()->ToWindowPoint(*this, event.m_Position);
        m_Dragging = false;
        m_Held = true;
        ToolButton::OnPointerDown(event);
    }

    void OnPointerMove(const PointerEvent& event) override
    {
        ToolButton::OnPointerMove(event);
        if (!m_DragEnabled || !m_Drag || !m_Held)
        {
            return;
        }
        const auto point = GetContext()->ToWindowPoint(*this, event.m_Position);
        if (!m_Dragging && std::hypot(point.m_X - m_Origin.m_X, point.m_Y - m_Origin.m_Y) < 6)
        {
            return;
        }
        const WidgetRef self(this);
        const bool starting = !m_Dragging;
        m_Dragging = true;
        if (starting)
        {
            if (const auto select = m_Select)
            {
                select();
            }
        }
        if (self.Get())
        {
            if (const auto drag = m_Drag)
            {
                drag(point, false);
            }
        }
    }

    void OnPointerUp(const PointerEvent& event) override
    {
        m_Held = false;
        if (!std::exchange(m_Dragging, false))
        {
            ToolButton::OnPointerUp(event);
            return;
        }
        const auto point = GetContext()->ToWindowPoint(*this, event.m_Position);
        ToolButton::OnCaptureLost();
        if (const auto drag = m_Drag)
        {
            drag(point, true);
        }
    }

    void OnCaptureLost() override
    {
        m_Held = false;
        m_Dragging = false;
        ToolButton::OnCaptureLost();
    }

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
        {
            return Trigger() ? SemanticStatus::Succeeded : SemanticStatus::Disabled;
        }
        if (command.m_Action == SemanticAction::AddToSelection)
        {
            return m_Selected ? SemanticStatus::Succeeded : SemanticStatus::InvalidValue;
        }
        if (command.m_Action == SemanticAction::RemoveFromSelection)
        {
            return m_Selected ? SemanticStatus::InvalidValue : SemanticStatus::Succeeded;
        }
        return ToolButton::OnSemanticAction(command);
    }

  private:
    //! Mirrors the tab container's canonical selected-page state.
    bool m_Selected = false;
    bool m_Closable = false, m_Modified = false, m_Dragging = false, m_Held = false;
    Point m_Origin;
    ToolButton* m_Close = nullptr;
};
} // namespace

TabWidget::TabWidget() : Widget("tabwidget")
{
    auto& navigation = Add<TabNavigation>();
    m_Navigation = &navigation;
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
            {
                live->ScrollPage(-1);
            }
        });
    forward.Clicked().Connect(
        [stripRef]
        {
            if (auto* live = dynamic_cast<TabStrip*>(stripRef.Get()))
            {
                live->ScrollPage(1);
            }
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
            {
                live->ShowTabChooser();
            }
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
            {
                return;
            }
            for (std::size_t i = 0; i < tabs->m_Tabs.size(); ++i)
            {
                if (tabs->m_Tabs[i].m_Page.Get() == pageRef.Get())
                {
                    tabs->SetCurrentIndex(static_cast<int>(i));
                    return;
                }
            }
        });
    const auto locate = [owner, pageRef]() -> std::pair<TabWidget*, int>
    {
        auto* tabs = dynamic_cast<TabWidget*>(owner.Get());
        if (tabs)
        {
            for (int index = 0; index < tabs->GetTabCount(); ++index)
            {
                if (&tabs->GetPage(index) == pageRef.Get())
                {
                    return {tabs, index};
                }
            }
        }
        return {nullptr, -1};
    };
    button.m_Select = [locate]
    {
        const auto [tabs, index] = locate();
        if (tabs)
        {
            tabs->SetCurrentIndex(index);
        }
    };
    button.m_RequestClose = [locate]
    {
        const auto [tabs, index] = locate();
        if (tabs)
        {
            tabs->m_CloseRequested.Emit(index);
        }
    };
    button.m_Drag = [owner, pageRef](Point point, bool finished)
    {
        if (auto* tabs = dynamic_cast<TabWidget*>(owner.Get()))
        {
            tabs->DragTab(pageRef, point, finished);
        }
    };
    if (m_TabMenusEnabled)
    {
        ConnectTabContextMenu(pageRef, button);
    }
    m_Tabs.push_back({pageRef, WidgetRef(&button)});
    button.SetClosable(m_Closable);
    button.m_DragEnabled = m_Movable || m_Detachable;
    page.SetVisible(false);
    if (m_Current < 0)
    {
        SetCurrentIndex(0);
    }
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
    {
        m_Pages->RemoveChild(*page);
    }
    if (auto* button = removed.m_Button.Get())
    {
        m_Strip->RemoveChild(*button);
    }
    const int previous = m_Current;
    if (index > previous)
    {
        Invalidate(Invalidation::Content);
        return;
    }
    const int next = m_Tabs.empty() ? -1 : std::min(index < previous ? previous - 1 : previous, GetTabCount() - 1);
    m_Current = -1;
    if (!m_Tabs.empty())
    {
        SetCurrentIndex(std::max(0, next));
    }
    else
    {
        m_CurrentChanged.Emit(-1);
    }
}

void TabWidget::SetTabBarVisible(bool visible)
{
    VerifyStructuralMutation();
    if (m_TabBarVisible == visible)
    {
        return;
    }
    m_TabBarVisible = visible;
    m_Navigation->SetVisible(visible);
    NotifySemanticsChanged();
}

bool TabWidget::IsTabBarVisible() const
{
    VerifyAccess();
    return m_TabBarVisible;
}

void TabWidget::MoveTab(int from, int to)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    (void)GetPage(from);
    (void)GetPage(to);
    if (from == to)
    {
        return;
    }
    const auto selected = m_Current >= 0 ? m_Tabs[static_cast<std::size_t>(m_Current)].m_Page : WidgetRef{};
    const auto moved = m_Tabs[static_cast<std::size_t>(from)];
    const auto destination = m_Tabs[static_cast<std::size_t>(to)];
    const auto reorder = [](auto& items, std::size_t oldIndex, std::size_t newIndex)
    {
        if (oldIndex < newIndex)
        {
            std::rotate(items.begin() + static_cast<std::ptrdiff_t>(oldIndex),
                        items.begin() + static_cast<std::ptrdiff_t>(oldIndex + 1),
                        items.begin() + static_cast<std::ptrdiff_t>(newIndex + 1));
        }
        else
        {
            std::rotate(items.begin() + static_cast<std::ptrdiff_t>(newIndex),
                        items.begin() + static_cast<std::ptrdiff_t>(oldIndex),
                        items.begin() + static_cast<std::ptrdiff_t>(oldIndex + 1));
        }
    };
    reorder(m_Tabs, static_cast<std::size_t>(from), static_cast<std::size_t>(to));
    const auto reorderOwned = [&](Widget& parent, WidgetRef child, WidgetRef before)
    {
        auto& children = parent.m_Children;
        const auto oldItem = std::find_if(children.begin(), children.end(),
                                          [child](const auto& item) { return item.get() == child.Get(); });
        const auto newItem = std::find_if(children.begin(), children.end(),
                                          [before](const auto& item) { return item.get() == before.Get(); });
        reorder(children, static_cast<std::size_t>(oldItem - children.begin()),
                static_cast<std::size_t>(newItem - children.begin()));
        parent.Invalidate(Invalidation::Structure);
    };
    // Ownership order supplies selector layout and semantic traversal; page identity remains stable.
    reorderOwned(*m_Strip, moved.m_Button, destination.m_Button);
    reorderOwned(*m_Pages, moved.m_Page, destination.m_Page);
    const int previous = m_Current;
    for (std::size_t index = 0; index < m_Tabs.size(); ++index)
    {
        if (m_Tabs[index].m_Page.Get() == selected.Get())
        {
            m_Current = static_cast<int>(index);
        }
    }
    if (m_Current >= 0)
    {
        static_cast<TabStrip*>(m_Strip)->Reveal(
            m_Tabs[static_cast<std::size_t>(m_Current)].m_Button.Get()->m_TargetKey);
    }
    const WidgetRef self(this);
    if (previous != m_Current)
    {
        m_CurrentChanged.Emit(m_Current);
    }
    if (self.Get())
    {
        m_TabsReordered.Emit(from, to);
    }
}

Widget& TabWidget::GetPage(int index) const
{
    VerifyAccess();
    if (index < 0 || index >= GetTabCount())
    {
        throw std::out_of_range("Invalid tab index.");
    }
    auto* page = m_Tabs[static_cast<std::size_t>(index)].m_Page.Get();
    if (!page)
    {
        throw std::logic_error("Tab pages must be removed with RemoveTab.");
    }
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
    {
        return;
    }
    m_Current = index;
    for (std::size_t i = 0; i < m_Tabs.size(); ++i)
    {
        if (auto* page = m_Tabs[i].m_Page.Get())
        {
            page->SetVisible(static_cast<int>(i) == index);
        }
        if (auto* button = m_Tabs[i].m_Button.Get())
        {
            static_cast<TabButton*>(button)->SetSelected(static_cast<int>(i) == index);
            if (static_cast<int>(i) == index)
            {
                button->AddClass("selected");
            }
            else
            {
                button->RemoveClass("selected");
            }
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
    {
        button->SetText(std::move(title));
    }
}

Signal<int>& TabWidget::CurrentChanged()
{
    VerifyAccess();
    return m_CurrentChanged;
}

ToolButton& TabWidget::GetTabButton(int index) const
{
    (void)GetPage(index);
    return *static_cast<ToolButton*>(m_Tabs[static_cast<std::size_t>(index)].m_Button.Get());
}

void TabWidget::ConfigureSelectors()
{
    for (const auto& tab : m_Tabs)
    {
        auto* button = static_cast<TabButton*>(tab.m_Button.Get());
        button->SetClosable(m_Closable);
        button->m_DragEnabled = m_Movable || m_Detachable;
    }
}

void TabWidget::SetTabsClosable(bool enabled)
{
    VerifyStructuralMutation();
    m_Closable = enabled;
    ConfigureSelectors();
}

void TabWidget::SetTabsMovable(bool enabled)
{
    VerifyStructuralMutation();
    m_Movable = enabled;
    ConfigureSelectors();
}

void TabWidget::SetTabsDetachable(bool enabled)
{
    VerifyStructuralMutation();
    m_Detachable = enabled;
    ConfigureSelectors();
}

void TabWidget::SetTabModified(int index, bool modified)
{
    VerifyStructuralMutation();
    static_cast<TabButton&>(GetTabButton(index)).SetModified(modified);
}

bool TabWidget::IsTabModified(int index) const
{
    return static_cast<TabButton&>(GetTabButton(index)).IsModified();
}

Signal<int>& TabWidget::CloseRequested()
{
    VerifyAccess();
    return m_CloseRequested;
}

Signal<int, int>& TabWidget::TabsReordered()
{
    VerifyAccess();
    return m_TabsReordered;
}

Signal<int, Point>& TabWidget::DetachRequested()
{
    VerifyAccess();
    return m_DetachRequested;
}

void TabWidget::ConnectTabContextMenu(WidgetRef page, ToolButton& button)
{
    const WidgetRef owner(this);
    button.ContextMenuRequested().Connect(
        [owner, page](Point point)
        {
            if (auto* tabs = dynamic_cast<TabWidget*>(owner.Get()))
            {
                for (int index = 0; index < tabs->GetTabCount(); ++index)
                {
                    if (&tabs->GetPage(index) == page.Get())
                    {
                        tabs->m_TabContextMenuRequested.Emit(index, point);
                        return;
                    }
                }
            }
        });
}

Signal<int, Point>& TabWidget::TabContextMenuRequested()
{
    VerifyStructuralMutation();
    if (!m_TabMenusEnabled)
    {
        m_TabMenusEnabled = true;
        for (const auto& tab : m_Tabs)
        {
            ConnectTabContextMenu(tab.m_Page, *static_cast<ToolButton*>(tab.m_Button.Get()));
        }
    }
    return m_TabContextMenuRequested;
}

void TabWidget::DragTab(WidgetRef page, Point point, bool finished)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    int index = -1;
    for (int i = 0; i < GetTabCount(); ++i)
    {
        if (&GetPage(i) == page.Get())
        {
            index = i;
        }
    }
    if (index < 0)
    {
        return;
    }
    const auto local = GetContext()->ToLayoutPoint(*m_Navigation, point);
    const auto bounds = m_Navigation->GetBounds();
    const Rect nearby{bounds.m_X - 24, bounds.m_Y - 24, bounds.m_Width + 48, bounds.m_Height + 48};
    if (!nearby.Contains(local))
    {
        if (finished && m_Detachable)
        {
            m_DetachRequested.Emit(index, point);
        }
        return;
    }
    if (!m_Movable)
    {
        return;
    }
    int destination = index;
    for (int i = 0; i < GetTabCount(); ++i)
    {
        const auto& button = GetTabButton(i);
        const auto hit = GetContext()->ToLayoutPoint(button, point);
        if (button.GetBounds().Intersect(button.GetClip()).Contains(hit))
        {
            destination = i;
            break;
        }
    }
    if (destination != index)
    {
        MoveTab(index, destination);
    }
}

void TabWidget::OnDescendantFocused(Widget& descendant)
{
    for (const auto& tab : m_Tabs)
    {
        if (tab.m_Button.Get() == &descendant)
        {
            static_cast<TabStrip*>(m_Strip)->Reveal(descendant.m_TargetKey);
            break;
        }
    }
}

void TabWidget::ShowTabChooser()
{
    VerifyStructuralMutation();
    if (!GetContext() || m_Tabs.empty())
    {
        return;
    }
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
            {
                live->ChooseTab(index);
            }
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
    {
        return;
    }
    list->SetCurrentIndex(current);
    if (!self.Get() || !listRef.Get())
    {
        return;
    }
    PopupOptions options;
    options.m_AnchorWidget = m_ChooserButton;
    options.m_Placement = PopupPlacement::Below;
    GetContext()->ShowPopup(*list, {}, options);
}

void TabWidget::ChooseTab(int index)
{
    const auto lease = RetainOwner();
    if (index < 0 || static_cast<std::size_t>(index) >= m_ChooserPages.size())
    {
        return;
    }
    const WidgetRef self(this);
    const auto page = m_ChooserPages[static_cast<std::size_t>(index)];
    if (auto* chooser = m_Chooser.Get())
    {
        GetContext()->ClosePopup(*chooser, {PopupCloseReason::Accepted, index});
    }
    if (!self.Get())
    {
        return;
    }
    const auto found = std::find_if(m_Tabs.begin(), m_Tabs.end(),
                                    [&](const Tab& tab) { return page.Get() && tab.m_Page.Get() == page.Get(); });
    if (found == m_Tabs.end())
    {
        return;
    }
    const auto button = found->m_Button;
    SetCurrentIndex(static_cast<int>(found - m_Tabs.begin()));
    if (self.Get())
    {
        if (auto* live = button.Get())
        {
            GetContext()->SetFocus(live);
        }
    }
}

bool TabWidget::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Alt || m_Tabs.empty())
    {
        return false;
    }
    int next = -1;
    if (key == Key::Tab && modifiers.m_Control)
    {
        next = (m_Current + (modifiers.m_Shift ? -1 : 1) + GetTabCount()) % GetTabCount();
    }
    else
    {
        if (key != Key::Left && key != Key::Right && key != Key::Home && key != Key::End)
        {
            return false;
        }
        const auto found = std::find_if(m_Tabs.begin(), m_Tabs.end(), [&](const auto& tab)
                                        { return tab.m_Button.Get() == GetContext()->GetFocus(); });
        if (found == m_Tabs.end())
        {
            return false;
        }
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
    {
        context->SetFocus(live);
    }
    return true;
}

SemanticInfo TabWidget::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    if (!m_TabBarVisible)
    {
        info.m_Role = SemanticRole::Group;
    }
    info.m_SelectionRequired = m_TabBarVisible && !m_Tabs.empty();
    return info;
}

std::size_t TabWidget::GetSemanticSelectionCount() const
{
    return m_TabBarVisible && m_Current >= 0 && static_cast<std::size_t>(m_Current) < m_Tabs.size() &&
                   m_Tabs[static_cast<std::size_t>(m_Current)].m_Button.Get()
               ? 1
               : 0;
}

std::vector<WidgetRef> TabWidget::GetSemanticSelectedWidgets(std::size_t offset, std::size_t limit) const
{
    if (offset || !limit || !GetSemanticSelectionCount())
    {
        return {};
    }
    return {m_Tabs[static_cast<std::size_t>(m_Current)].m_Button};
}

} // namespace EStudioUI
