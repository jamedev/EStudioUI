#pragma once
#include "ListWidget.h"
#include "Popup.h"
#include "ToolButton.h"

namespace EStudioUI
{
//! A bounded multiple-choice popup sharing canonical model and selection endpoints.
class MultiSelectComboBox : public Widget
{
  public:
    //! Creates an empty multi-choice selector.
    MultiSelectComboBox();
    //! Sets at most 4096 convenience captions, creating an ordinary model on attachment.
    void SetItems(std::vector<std::string> items);
    //! Displays a shared model column with up to 1000000 top-level rows.
    void SetModel(std::shared_ptr<ItemModel> model, std::size_t column = 0);
    //! Returns the canonical model, initializing attached convenience items if needed.
    std::shared_ptr<ItemModel> GetModel();
    //! Returns canonical multiple selection.
    std::shared_ptr<SelectionModel> GetSelectionModel();
    //! Replaces selected identities once; selection is bounded to 128 entries.
    void SetSelectedItems(std::vector<ItemId> items);
    //! Opens the virtualized multiple-choice list without blocking.
    void ShowPopup();
    //! Closes this control's popup.
    void HidePopup();
    //! Reports coherent canonical selected identities.
    Signal<std::vector<ItemId>>& SelectionChanged();

  protected:
    //! Opens on Alt+Down or Space while the selector owns focus.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Initializes deferred convenience records.
    void EnsureModel();
    //! Synchronizes the bounded summary caption.
    void Sync();
    //! Selector button.
    ToolButton* m_Button = nullptr;
    //! Current canonical/projected data.
    std::shared_ptr<ItemModel> m_Model;
    //! Shared canonical selected identities.
    std::shared_ptr<SelectionModel> m_Selection;
    //! Selection observer.
    std::optional<ScopedConnection> m_Connection;
    //! Precommit selection cardinality guard.
    std::optional<ScopedConnection> m_Guard;
    //! Precommit model size/schema guard.
    std::optional<ScopedConnection> m_ModelGuard;
    //! Model observer for current captions.
    std::optional<ScopedConnection> m_ModelConnection;
    //! Deferred convenience captions.
    std::vector<std::string> m_Pending;
    //! Displayed model column.
    std::size_t m_Column = 0;
    //! Logically owned popup.
    WidgetRef m_Popup;
    //! Selection observers.
    Signal<std::vector<ItemId>> m_SelectionChanged;
};
} // namespace EStudioUI
