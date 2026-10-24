#pragma once

#include "ToolButton.h"

namespace EStudioUI
{
//! Owns scrollable tab navigation and persistent pages; inactive pages never affect layout.
class TabWidget final : public Widget
{
  public:
    //! Creates an initially empty tab container.
    TabWidget();

    //! Appends an owned page and returns its container.
    Widget& AddTab(std::string title);

    //! Removes a page and selects the nearest surviving tab.
    void RemoveTab(int index);

    //! Shows or hides the selector row while preserving pages and current selection.
    void SetTabBarVisible(bool visible);

    //! Returns whether the selector row is configured visible.
    bool IsTabBarVisible() const;

    //! Reorders persistent pages/selectors while preserving the selected page identity.
    void MoveTab(int from, int to);

    //! Returns a page by index, throwing for an invalid index.
    Widget& GetPage(int index) const;

    //! Returns the tab count.
    int GetTabCount() const;

    //! Selects a page; invalid indices throw and preserve current state.
    void SetCurrentIndex(int index);

    //! Returns the selected index, or -1 when empty.
    int GetCurrentIndex() const;

    //! Changes a tab caption without recreating its page.
    void SetTabText(int index, std::string title);

    //! Returns the persistent selector for styling, focus and accessible interaction.
    ToolButton& GetTabButton(int index) const;

    //! Enables explicit close buttons; requests leave removal to the document/application policy.
    void SetTabsClosable(bool enabled);
    //! Enables captured pointer reordering of stable persistent pages.
    void SetTabsMovable(bool enabled);
    //! Enables drag-out detach requests without changing page ownership automatically.
    void SetTabsDetachable(bool enabled);
    //! Shows a GPU indicator and accessible description without changing the stored title.
    void SetTabModified(int index, bool modified);
    //! Returns whether a selector displays unsaved changes.
    bool IsTabModified(int index) const;
    //! Requests application approval before removing a page.
    Signal<int>& CloseRequested();
    //! Reports committed reordering independently of selection changes.
    Signal<int, int>& TabsReordered();
    //! Reports a released drag-out in window logical coordinates.
    Signal<int, Point>& DetachRequested();
    //! Reports a selector context menu with its stable current index and window point.
    Signal<int, Point>& TabContextMenuRequested();

    //! Reports a committed page selection.
    Signal<int>& CurrentChanged();

  protected:
    //! Reports required single selection independently of tab-strip composition.
    SemanticInfo DescribeSemantics() const override;

    //! Returns whether one live tab is currently selected.
    std::size_t GetSemanticSelectionCount() const override;

    //! Resolves the selected actual tab button without scanning all pages.
    std::vector<WidgetRef> GetSemanticSelectedWidgets(std::size_t offset, std::size_t limit) const override;

    //! Selects adjacent tabs when an arrow key is pressed on the tab strip.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Minimally reveals focused selectors without displacing already visible tabs.
    void OnDescendantFocused(Widget& descendant) override;

  private:
    //! Resolves one captured selector through its stable page identity.
    void DragTab(WidgetRef page, Point point, bool finished);
    //! Updates opt-in selector behavior without rebuilding pages.
    void ConfigureSelectors();
    //! Installs opt-in selector menus while preserving ordinary parent menu bubbling.
    void ConnectTabContextMenu(WidgetRef page, ToolButton& button);

    //! Opens the bounded, scrollable chooser for every persistent page.
    void ShowTabChooser();

    //! Commits a chooser row after dismissing its presentation.
    void ChooseTab(int index);

    //! Observes an owned page and its corresponding selector button.
    struct Tab
    {
        //! Observes the persistent page container.
        WidgetRef m_Page;

        //! Observes the matching tab button.
        WidgetRef m_Button;
    };

    //! Observes the owned complete selector/navigation row.
    Widget* m_Navigation = nullptr;

    //! Configured row visibility independent of hidden ancestors and CSS.
    bool m_TabBarVisible = true;

    //! Owns selectors through the widget tree.
    Widget* m_Strip = nullptr;

    //! Opens a tab list even when the horizontal selectors are clipped.
    ToolButton* m_ChooserButton = nullptr;

    //! Observes the lazily created, root-owned virtual tab list.
    WidgetRef m_Chooser;

    //! Resolves a chooser snapshot through persistent pages when tab indices change.
    std::vector<WidgetRef> m_ChooserPages;

    //! Owns page containers through the widget tree.
    Widget* m_Pages = nullptr;

    //! Maps stable page identities to tab selectors.
    std::vector<Tab> m_Tabs;

    //! Keeps generic tabs behavior unchanged unless explicitly enabled.
    bool m_Closable = false, m_Movable = false, m_Detachable = false;
    //! Enables lazy menu routing for both existing and subsequently added selectors.
    bool m_TabMenusEnabled = false;
    //! Reports policy-controlled tab closure.
    Signal<int> m_CloseRequested;
    //! Reports stable-page reorder operations.
    Signal<int, int> m_TabsReordered;
    //! Reports released drag-out requests.
    Signal<int, Point> m_DetachRequested;
    //! Reports selector context-menu requests.
    Signal<int, Point> m_TabContextMenuRequested;

    //! Stores the active page index.
    int m_Current = -1;

    //! Delivers page selection changes.
    Signal<int> m_CurrentChanged;
};
} // namespace EStudioUI
