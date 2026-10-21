#pragma once

#include "ToolButton.h"

namespace EStudioUI
{
//! A horizontal container for commands, separators, and arbitrary child widgets.
class ToolBar final : public Widget
{
  public:
    //! Creates a horizontal command strip with content-driven height.
    ToolBar();

    //! Appends a compact command button.
    ToolButton& AddButton(std::string text);

    //! Appends a noninteractive vertical separator.
    Widget& AddSeparator();

  protected:
    //! Moves keyboard focus between enabled command buttons with arrow keys.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;
};
} // namespace EStudioUI
