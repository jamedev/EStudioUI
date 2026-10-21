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
    //! Selects adjacent tabs when an arrow key is pressed on the tab strip.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
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
