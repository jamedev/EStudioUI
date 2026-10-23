#pragma once

#include "ToolButton.h"

namespace EStudioUI
{
//! Owns a tab strip and persistent pages; inactive pages never affect layout.
class TabWidget final : public Widget
{
  public:
    //! Creates an initially empty tab container.
    TabWidget();

    //! Appends an owned page and returns its container.
    Widget& AddTab(std::string title);

    //! Removes a page and selects the nearest surviving tab.
    void RemoveTab(int index);

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

    //! Reveals tab selectors reached through keyboard or accessibility focus.
    void OnDescendantFocused(Widget& descendant) override;

  private:
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

    //! Stores the active page index.
    int m_Current = -1;

    //! Delivers page selection changes.
    Signal<int> m_CurrentChanged;
};
} // namespace EStudioUI
