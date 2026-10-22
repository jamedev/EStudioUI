#pragma once

#include "ToolButton.h"
#include "PopupSession.h"

namespace EStudioUI
{
//! An owned popup command menu with dismissal, focus restoration, and keyboard navigation.
class ContextMenu final : public Widget
{
  public:
    //! Creates an initially hidden popup; add it directly to UiContext::Root.
    ContextMenu();

    //! Releases any submenus that have not yet been attached to a context.
    ~ContextMenu() override;

    //! Appends an action whose Clicked signal fires after the popup closes.
    ToolButton& AddAction(std::string text, bool enabled = true);

    //! Appends a shared command presentation that executes after menu-chain dismissal.
    ToolButton& AddAction(std::shared_ptr<Action> action);

    //! Appends a lazily attached submenu with pointer and Right/Left keyboard navigation.
    ContextMenu& AddMenu(std::string text);

    //! Appends a noninteractive horizontal separator.
    void AddSeparator();

    //! Opens at a window-coordinate anchor, clamped to the visible window.
    PopupSession Open(Point position, Widget* parentPopup = nullptr);

    //! Dismisses this menu if it is the active popup.
    void Close();

    //! Returns whether this menu currently owns the popup layer.
    bool IsOpen() const;

    //! Allows a newly opened menu to receive arrow-key commands.
    bool IsFocusable() const override;

  protected:
    //! Moves among enabled actions and activates the first item on Enter.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Attaches and opens a submenu below the current presentation.
    void OpenSubmenu(std::size_t index);

    //! Dismisses this menu and its menu ancestors while preserving an enclosing modal surface.
    void CloseMenuChain();

    //! Keeps submenu layout/ownership separate from the parent menu's item list.
    struct Submenu
    {
        //! Observes the ordinary button opening this child menu.
        WidgetRef m_Button;
        //! Owns the child menu until first presentation.
        std::unique_ptr<ContextMenu> m_Pending;
        //! Observes the context-owned menu after attachment.
        WidgetRef m_Menu;
    };

    //! Owns or observes child menus without extending parent/context lifetime.
    std::vector<Submenu> m_Submenus;
};
} // namespace EStudioUI
