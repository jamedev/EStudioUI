#pragma once
#include "CollectionView.h"
#include "ListWidget.h"
#include "TextBox.h"
#include "ToolButton.h"

namespace EStudioUI
{
//! A single-choice control with an editable field and a virtualized owned popup.
class ComboBox final : public Widget
{
  public:
    //! Creates an empty read-only choice control.
    ComboBox();
    //! Moves convenience captions into a canonical model when attached.
    void SetItems(std::vector<std::string> items);
    //! Displays one model column; requires an attached idle control.
    void SetModel(std::shared_ptr<ItemModel> model, std::size_t column = 0);
    //! Returns the canonical or projected model, or null before detached items attach.
    std::shared_ptr<ItemModel> GetModel() const;
    //! Returns the shared canonical selection endpoint.
    std::shared_ptr<SelectionModel> GetSelectionModel() const;
    //! Shares selection with another compatible view.
    void SetSelectionModel(std::shared_ptr<SelectionModel> selection);
    //! Returns the visible top-level item count.
    int GetItemCount() const;
    //! Selects a visible item; minus one clears selection.
    void SetCurrentIndex(int index);
    //! Returns the selected visible index, or minus one.
    int GetCurrentIndex() const;
    //! Returns the current field text.
    const std::string& GetCurrentText() const;
    //! Enables user text entry without inserting records into the model.
    void SetEditable(bool editable);
    //! Returns the editing policy.
    bool IsEditable() const;
    //! Returns the composed editor for validators, selection and composition.
    TextBox& GetEditor() const;
    //! Opens the asynchronous popup with current selection highlighted.
    void ShowPopup();
    //! Dismisses this control's popup.
    void HidePopup();
    //! Returns whether this popup is currently open.
    bool IsPopupOpen() const;
    //! Replaces default case-folded prefix lookup with an owner-side matching hook.
    void SetCompletionProvider(std::function<std::optional<ItemId>(std::string_view)> provider);
    //! Returns a matching visible model identity without changing selection.
    std::optional<ItemId> FindCompletion(std::string_view prefix) const;
    //! Reports committed selection/index changes.
    Signal<int>& CurrentChanged();
    //! Reports user acceptance of a popup item.
    Signal<int>& Activated();
    //! Reports committed editor text independently of selection.
    Signal<const std::string&>& EditTextChanged();

  protected:
    //! Exposes value and expandable combo semantics.
    SemanticInfo DescribeSemantics() const override;
    //! Applies expand, collapse and editable value actions.
    SemanticStatus OnSemanticAction(const SemanticCommand& command) override;
    //! Handles shared keyboard navigation before the composed editor.
    bool OnInputPreview(const InputEvent& event) override;

  private:
    //! Creates an attached model from detached convenience items.
    void EnsureModel();
    //! Creates the root-owned popup once per context attachment.
    ListWidget* EnsurePopup();
    //! Refreshes the prefix index after one model revision.
    void ModelChanged();
    //! Builds numeric navigation positions from the bounded enabled prefix index.
    void IndexEnabledRows();
    //! Mirrors coherent canonical selection into the field.
    void SyncSelection();
    //! Handles a canonical editor change and updates completion highlight.
    void Edited();
    //! Commits and closes a highlighted item through normal selection.
    void Accept(int index);
    //! Returns a bounded, sorted case-folded prefix index.
    static std::vector<std::pair<std::string, ItemId>> BuildIndex(const ModelSnapshot& snapshot, std::size_t column);
    //! Composes the canonical editor.
    TextBox* m_Editor = nullptr;
    //! Composes the drop-down button.
    ToolButton* m_Arrow = nullptr;
    //! Owns model/selection guards and subscriptions.
    std::unique_ptr<CollectionView> m_View;
    //! Prevalidates model candidates before any commit.
    std::optional<ScopedConnection> m_ModelGuard;
    //! Observes the independently owned virtual popup.
    WidgetRef m_Popup;
    //! Retains detached convenience items until a context is available.
    std::optional<std::vector<std::string>> m_PendingItems;
    //! Retains selection before model attachment.
    int m_DetachedIndex = -1;
    //! Chooses the displayed schema column.
    std::size_t m_Column = 0;
    //! Caches prefix matching without model scans per keystroke/frame.
    std::vector<std::pair<std::string, ItemId>> m_Index;
    //! Keeps disabled-run keyboard navigation logarithmic.
    std::vector<int> m_EnabledRows;
    //! Overrides default prefix matching.
    std::function<std::optional<ItemId>(std::string_view)> m_Completion;
    //! Prevents feedback from model-to-editor updates.
    bool m_Syncing = false;
    //! Enables user text entry.
    bool m_Editable = false;
    //! Remembers the last announced visible index.
    int m_LastIndex = -1;
    //! Remembers the last announced stable identity.
    ItemId m_LastItem = 0;
    //! Expires stale popup completion observers.
    std::uint64_t m_Presentation = 0;
    //! Retains keyboard prefix bytes between nearby input events.
    std::string m_Search;
    //! Expires the keyboard prefix without an idle timer.
    double m_SearchUntil = 0;
    //! Publishes committed selection.
    Signal<int> m_CurrentChanged;
    //! Publishes user acceptance.
    Signal<int> m_Activated;
    //! Publishes owned-local editor captions during delivery.
    Signal<const std::string&> m_EditTextChanged;
};
} // namespace EStudioUI
