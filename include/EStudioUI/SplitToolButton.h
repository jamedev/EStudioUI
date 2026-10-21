#pragma once

#include "ContextMenu.h"

#include <limits>

namespace EStudioUI
{
//! A composed command button with a remembered default action and an owned popup chooser.
class SplitToolButton : public Widget
{
  public:
    //! Creates an empty tool chooser with a fallback caption.
    explicit SplitToolButton(std::string text = "Tools");

    //! Adds an action; choosing it also makes it the default command.
    ToolButton& AddAction(std::string text);

    //! Returns the number of commands in the chooser.
    std::size_t GetActionCount() const;

    //! Returns an action, throwing out_of_range for an invalid index.
    ToolButton& GetAction(std::size_t index);

    //! Changes the default action without activating it.
    void SetDefaultAction(std::size_t index);

    //! Returns the default index, or size_t max when no command exists.
    std::size_t GetDefaultActionIndex() const;

    //! Activates the enabled default command, including its normal Clicked listeners.
    void TriggerDefault();

    //! Opens the command popup below the chooser when attached to a UI context.
    void OpenMenu();

    //! Returns whether the popup is currently open.
    bool IsMenuOpen() const;

    //! Returns the primary command button for composition and focus integration.
    ToolButton& GetPrimaryButton();

    //! Reports an activated command index after updating the default caption.
    Signal<std::size_t>& Triggered();

  protected:
    //! Supports Alt+Down and Down while one of the composed buttons owns focus.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Returns the popup before or after attaching it to the UI root.
    ContextMenu* Menu() const;

    //! Commits an action choice before notifying clients.
    void Choose(std::size_t index);

    //! Observes the primary command button owned by this widget.
    ToolButton* m_Primary = nullptr;

    //! Observes the popup arrow owned by this widget.
    ToolButton* m_Arrow = nullptr;

    //! Owns the menu until its first open attaches it to a context.
    std::unique_ptr<ContextMenu> m_PendingMenu;

    //! Observes the context-managed popup after attachment.
    WidgetRef m_Menu;

    //! Observes stable command objects owned by the popup.
    std::vector<WidgetRef> m_Actions;

    //! Stores the remembered action without executing it.
    std::size_t m_Default = std::numeric_limits<std::size_t>::max();

    //! Delivers activated command indices.
    Signal<std::size_t> m_Triggered;
};
} // namespace EStudioUI
