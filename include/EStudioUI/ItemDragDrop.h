#pragma once

#include "ItemModel.h"
#include "Widget.h"

namespace EStudioUI
{
class Renderer;

//! Owns a bounded immutable drag payload without retaining source widgets.
struct ModelDragData
{

    //! Identifies the canonical source independently of its views.
    std::uint64_t m_Model = 0, m_Revision = 0;

    //! Retains immutable values for application-defined copy/drop adapters.
    ModelSnapshot m_Snapshot;

    //! Retains selected canonical identities, never transient row positions.
    std::vector<ItemId> m_Items;
};

//! Describes a stable insertion point and optional GPU preview bounds.
struct ModelDropTarget
{

    //! Inserts under this parent, or the invisible root.
    ItemId m_Parent = 0;

    //! Inserts before this sibling, or appends when zero.
    ItemId m_Before = 0;

    //! Describes an insertion line or a child-target outline in target layout coordinates.
    Rect m_Bounds;

    //! Selects a child-target outline instead of a sibling insertion line.
    bool m_AsChild = false;
};

//! Atomically moves a bounded forest, preserving IDs and rejecting stale/cyclic targets.
void MoveModelItems(std::shared_ptr<ItemModel> model, const std::vector<ItemId>& items, ItemId parent = 0,
                    ItemId before = 0, std::optional<std::uint64_t> expectedRevision = {});

//! Reuses captured input and GPU previews across compatible data views in one UI context.
class ItemDragDrop
{
  public:
    //! Registers a weak view endpoint; target queries use layout coordinates and no source I/O.
    ItemDragDrop(Widget& owner, std::function<std::shared_ptr<ItemModel>()> model,
                 std::function<std::vector<ItemId>()> selection,
                 std::function<std::optional<ModelDropTarget>(Point)> target);

    //! Retires the endpoint and any preview without retaining the view.

    ~ItemDragDrop();

    //! Arms one potential drag in window logical coordinates.
    void Press(ItemId item, Point windowPoint);

    //! Advances captured motion; false leaves ordinary view/scroll handling unchanged.
    bool Move(Point windowPoint);

    //! Applies a validated drop after release, or returns false for a click.
    bool Release(Point windowPoint);

    //! Cancels capture-local data without changing the model.
    void Cancel();

    //! Paints this endpoint's active insertion preview using the ordinary renderer.
    void Paint(Renderer& renderer, float opacity) const;

    //! Overrides the default same-model move with an application-defined drop adapter.
    void SetDropHandler(std::function<void(const ModelDragData&, const ModelDropTarget&)> handler);

    //! Reports accepted drops without exposing borrowed widgets.
    Signal<ModelDragData, ModelDropTarget>& Dropped();

    //! Reports rejected drops while retaining canonical data.
    Signal<std::string>& Rejected();

  private:
    //! Owns weak registry/capture state independently of input callbacks.
    struct Data;

    //! Retains controller metadata until in-flight callbacks finish.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
