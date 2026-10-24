#pragma once
#include "Action.h"
#include "Popup.h"
#include "SearchBox.h"

namespace EStudioUI
{
//! Modal searchable commands backed by the application's existing Action instances.
class CommandPalette : public Popup
{
  public:
    //! Creates a hidden command surface.
    CommandPalette();
    //! Creates a root-owned palette following a logical owner's lifetime.
    static CommandPalette& Create(Widget& owner);
    //! Sets at most 4096 commands; refreshes their live eligibility on each search/open.
    void SetActions(std::vector<std::shared_ptr<Action>> actions);
    //! Opens centered without a nested event loop.
    PopupSession Show();
    //! Returns the input field for application shortcuts and styling.
    SearchBox& GetSearchBox() const;
    //! Re-evaluates command captions, visibility and enabled state.
    void Refresh();
    //! Runs the current eligible Action, closing the palette first.
    bool ExecuteCurrent();

  protected:
    //! Navigates results while search retains keyboard focus.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Query input.
    SearchBox* m_Search = nullptr;
    //! Virtualized result list.
    ListWidget* m_List = nullptr;
    //! Application commands retained by the palette.
    std::vector<std::shared_ptr<Action>> m_Actions;
    //! Mapping from visible rows to canonical commands.
    std::vector<std::shared_ptr<Action>> m_Visible;
};
} // namespace EStudioUI
