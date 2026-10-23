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

    //! Appends a custom command widget inside the scrollable region.
    template <typename T, typename... Args> T& Add(Args&&... args)
    {
        return Content().Add<T>(std::forward<Args>(args)...);
    }

    //! Adopts an existing custom command inside the scrollable region.
    Widget& AddChild(std::unique_ptr<Widget> child);

    //! Removes a command without exposing internal navigation ownership.
    bool RemoveChild(Widget& child);

    //! Returns the clipped command container for custom toolbar widgets.
    Widget& Content() const;

    //! Scrolls the visible command region by a signed logical-pixel distance.
    void ScrollBy(float distance);

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

    //! Routes wheels on the navigation commands into the same strip.
    bool OnScroll(Point delta, Modifiers modifiers) override;

  private:
    //! Observes the clipped command viewport owned by the toolbar.
    Widget* m_Content = nullptr;
};
} // namespace EStudioUI
