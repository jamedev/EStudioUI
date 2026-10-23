#pragma once

#include "ContextMenu.h"

#include <unordered_map>

namespace EStudioUI
{
//! A persistent command menu row reusing ContextMenu and the shared popup stack.
class MenuBar final : public Widget
{
  public:
    //! Creates an empty horizontal bar; Tab focuses the bar as one navigation stop.
    MenuBar();
    //! Destroys any menu not yet attached to a context.
    ~MenuBar() override;
    //! Appends a persistent top-level menu and returns its ordinary command API.
    ContextMenu& AddMenu(std::string title);
    //! Returns a menu by configured index, throwing for invalid/removed entries.
    ContextMenu& GetMenu(int index) const;
    //! Returns a top-level trigger for caption/enabled/visible customization.
    Button& GetButton(int index) const;
    //! Returns the configured menu count.
    int GetMenuCount() const;
    //! Opens one eligible menu, observing its trigger through layout/resize.
    PopupSession OpenMenu(int index);
    //! Closes this bar's active menu chain.
    void CloseMenu();
    //! Returns the open top-level index, or -1.
    int GetCurrentIndex() const;
    //! Keeps top-level triggers out of ordinary Tab traversal.
    bool IsFocusable() const override;

  protected:
    //! Gives the bar its platform-independent semantic role.
    SemanticInfo DescribeSemantics() const override;
    //! Opens on pointer-down and switches on hover only while a menu is active.
    bool OnInputPreview(const InputEvent& event) override;
    //! Navigates top-level menus and opens them with Down/Enter/Space.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    friend class ContextMenu;
    friend class MenuItem;
    //! Counts source presentations so rebinding/removing one does not disable another shared command.
    void TrackCommand(const std::shared_ptr<Action>& action, bool add);
    //! Retains each shared command once, with its active presentation reference count.
    std::unordered_map<Action*, std::pair<std::shared_ptr<Action>, std::size_t>> m_Commands;
    //! Suppresses registry callbacks while detached menu ownership is being retired.
    bool m_Retiring = false;
    //! Navigates enabled/visible top-level entries without per-frame work.
    void NavigateMenu(int direction, bool open);

    //! Owns a menu until attachment and otherwise keeps only weak identities.
    struct Entry
    {
        //! Observes the ordinary top-level presentation.
        WidgetRef m_Button;
        //! Owns a detached menu until first opening.
        std::unique_ptr<ContextMenu> m_Pending;
        //! Observes a context-owned menu after first opening.
        WidgetRef m_Menu;
    };

    //! Stores at most 128 menu entries.
    std::vector<Entry> m_Menus;
    //! Stores the current keyboard navigation index independently of an open menu.
    int m_FocusedMenu = 0;
};
} // namespace EStudioUI
