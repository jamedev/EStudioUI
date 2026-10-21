#pragma once

#include "ToolButton.h"

namespace EStudioUI
{
//! An owned popup command menu with dismissal, focus restoration, and keyboard navigation.
class ContextMenu final : public Widget
{
  public:
    //! Creates an initially hidden popup; add it directly to UiContext::Root.
    ContextMenu();

    //! Appends an action whose Clicked signal fires after the popup closes.
    ToolButton& AddAction(std::string text, bool enabled = true);

    //! Appends a noninteractive horizontal separator.
    void AddSeparator();

    //! Opens at a window-coordinate anchor, clamped to the visible window.
    void Open(Point position);

    //! Dismisses this menu if it is the active popup.
    void Close();

    //! Returns whether this menu currently owns the popup layer.
    bool IsOpen() const;

    //! Allows a newly opened menu to receive arrow-key commands.
    bool IsFocusable() const override;

  protected:
    //! Moves among enabled actions and activates the first item on Enter.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;
};
} // namespace EStudioUI
