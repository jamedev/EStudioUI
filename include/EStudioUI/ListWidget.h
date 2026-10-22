#pragma once

#include "ScrollWidget.h"
#include "CollectionView.h"
#include "ItemDelegate.h"

namespace EStudioUI
{
//! A single-selection UTF-8 list that paints only rows intersecting its viewport.
class ListWidget final : public ScrollWidget
{
  public:
    //! Creates an empty, focusable list.
    ListWidget();

    //! Displays a shared model column; the target must already belong to an idle UiContext.
    void SetModel(std::shared_ptr<ItemModel> model, std::size_t column = 0);

    //! Returns the external shared model, or an empty pointer for convenience-owned items.
    std::shared_ptr<ItemModel> GetModel() const;

    //! Shares selection with another view of the same canonical model.
    void SetSelectionModel(std::shared_ptr<SelectionModel> selection);

    //! Returns shared selection, or an empty pointer for convenience-owned items.
    std::shared_ptr<SelectionModel> GetSelectionModel() const;

    //! Installs an immutable GPU item presentation policy; null restores the default.
    void SetItemDelegate(std::shared_ptr<const ItemDelegate> delegate);

    //! Returns the current canonical ID, including a selected item hidden by filtering.
    ItemId GetCurrentItemId() const;

    //! Selects a canonical identity through shared selection.
    void SetCurrentItemId(ItemId item);

    //! Replaces all items and clears the selection and scroll position.
    void SetItems(std::vector<std::string> items);

    //! Appends one item and returns its index.
    int AddItem(std::string text);

    //! Removes one item and updates or clears the current selection.
    void RemoveItem(int index);

    //! Returns a caption by index, throwing for invalid indices.
    std::string GetItem(int index) const;

    //! Returns the item count.
    int GetItemCount() const;

    //! Selects an item and scrolls it into view; -1 clears selection.
    void SetCurrentIndex(int index);

    //! Returns the current index, or -1.
    int GetCurrentIndex() const;

    //! Exposes committed selection changes.
    Signal<int>& CurrentChanged();

    //! Exposes Enter and double-click activation.
    Signal<int>& Activated();

    //! Participates in Tab navigation.
    bool IsFocusable() const override;

  protected:
    //! Supplies a bounded preferred viewport size, independent of item count.
    Point Measure(FontFace& font) const override;

    //! Returns virtual row dimensions.
    Point GetVirtualSize() const override;

    //! Paints visible rows, selection, and scrollbars.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

    //! Selects a row or starts a scrollbar interaction.
    void OnPointerDown(const PointerEvent& event) override;

    //! Updates row hover or the active scrollbar drag.
    void OnPointerMove(const PointerEvent& event) override;

    //! Releases the active row press and scrollbar capture.
    void OnPointerUp(const PointerEvent& event) override;

    //! Clears a cancelled row press.
    void OnCaptureLost() override;

    //! Navigates, activates, and copies the current item.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Selects the next item matching a committed UTF-8 prefix.
    void OnText(std::string_view text) override;

  private:
    //! Applies selection modifiers in this view's visible order.
    void SelectIndex(int index, Modifiers modifiers);

    //! Updates geometry or painting after a coherent shared-model publication.
    void ModelChanged(const ModelChange& change);

    //! Refreshes local current-row coordinates after shared selection changes.
    void SelectionChanged();

    //! Owns lifetime-bound shared data and selection subscriptions when externally modeled.
    std::unique_ptr<CollectionView> m_View;

    //! Owns immutable custom model presentation without allocating row widgets.
    std::shared_ptr<const ItemDelegate> m_Delegate = std::make_shared<const ItemDelegate>();

    //! Selects the model field displayed by list rows.
    std::size_t m_ModelColumn = 0;

    //! Tests semantic row interaction without changing selection.
    bool ItemEnabled(int index) const;

    //! Converts a window-coordinate point into a visible item index.
    int ItemAt(Point position) const;

    //! Returns the cached style-state key for a visible row.
    unsigned ItemState(int index, int hover) const;

    //! Stores UTF-8 captions independently of widget children.
    std::vector<std::string> m_Items;

    //! Stores the selected row.
    int m_Current = -1;

    //! Stores the row under the pointer.
    mutable int m_Hover = -1;

    //! Remembers layout coordinates so hover follows rows while scrolling.
    Point m_HoverPosition;

    //! Stores the captured pressed row.
    int m_Pressed = -1;

    //! Reports committed selection changes.
    Signal<int> m_CurrentChanged;

    //! Reports explicit activation.
    Signal<int> m_Activated;
};
} // namespace EStudioUI
