#pragma once

#include "ToolButton.h"
#include "SplitToolButton.h"

namespace EStudioUI
{
//! A horizontal container for commands, separators, and arbitrary child widgets.
class ToolBar : public Widget
{
  public:
    //! Creates a horizontal command strip with content-driven height.
    ToolBar();

    //! Appends a compact command button.
    ToolButton& AddButton(std::string text);

    //! Appends a presentation of one shared action.
    ToolButton& AddAction(std::shared_ptr<Action> action);

    //! Appends a default command button with a popup action chooser.
    SplitToolButton& AddSplitButton(std::string text = "Tools");

    //! Appends a noninteractive vertical separator.
    Widget& AddSeparator();

  protected:
    //! Moves keyboard focus between enabled command buttons with arrow keys.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;
};
} // namespace EStudioUI
