#pragma once

#include "SelectionModel.h"
#include "Widget.h"

namespace EStudioUI
{
//! Composes lifetime/phase-safe model and selection subscriptions into custom collection widgets.
class CollectionView
{
  public:
    //! Connects a shared model to an attached idle widget without transferring widget ownership.
    CollectionView(Widget& owner, std::shared_ptr<ItemModel> model,
                   std::function<void(Widget&, const ModelChange&)> changed,
                   std::function<void(Widget&)> selectionChanged, std::shared_ptr<SelectionModel> selection = {});

    //! Disconnects all view guards and notifications on the owner.
    ~CollectionView();

    //! Returns the shared model after owner/lifetime checks.
    std::shared_ptr<ItemModel> GetModel() const;

    //! Returns the shared canonical selection.
    std::shared_ptr<SelectionModel> GetSelectionModel() const;

    //! Replaces selection with one from the same canonical model domain.
    void SetSelectionModel(std::shared_ptr<SelectionModel> selection);

    //! Applies pointer/keyboard selection modifiers using this view's visible range order.
    void Select(ItemId item, std::size_t column, Modifiers modifiers, std::span<const ItemId> order = {});

  private:
    //! Observes the widget without extending its lifetime.
    WidgetRef m_Owner;

    //! Shares canonical records or a projection's identity mapping.
    std::shared_ptr<ItemModel> m_Model;

    //! Shares selection independently of this view's scrolling and expansion.
    std::shared_ptr<SelectionModel> m_Selection;

    //! Calls the live widget after selection commits.
    std::function<void(Widget&)> m_SelectionChanged;

    //! Retires data/phase subscriptions before releasing the model.
    std::vector<ScopedConnection> m_Connections;

    //! Retires selection/phase subscriptions when selection is replaced.
    std::vector<ScopedConnection> m_SelectionConnections;
};
} // namespace EStudioUI
