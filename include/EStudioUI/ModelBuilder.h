#pragma once

#include "ModelSnapshot.h"

namespace EStudioUI
{
//! Builds owned flat or hierarchical snapshots on any thread; structural rebuilding is explicit linear work.
class ModelBuilder
{
  public:
    //! Starts an empty dataset with a typed schema.
    explicit ModelBuilder(std::vector<ModelColumn> columns);

    //! Copies an existing snapshot for an explicit structural batch.
    explicit ModelBuilder(const ModelSnapshot& snapshot);

    //! Adds an owned row; zero allocates a fresh ID and an explicit ID must be unique.
    ItemId Add(std::vector<ModelValue> values, ItemId parent = 0, ItemId item = 0);

    //! Removes an item and its descendants in one structural operation.
    void Remove(ItemId item);

    //! Moves a record and its subtree before a sibling, or to the end when before is zero.
    void Move(ItemId item, ItemId parent, ItemId before = 0);

    //! Validates and moves all data into an immutable snapshot.
    ModelSnapshot Build() &&;

  private:
    //! Stores mutable records only while building an unpublished snapshot.
    std::vector<std::vector<ModelValue>> m_Rows;

    //! Stores the field schema.
    std::vector<ModelColumn> m_Columns;

    //! Stores explicit stable item identities.
    std::vector<ItemId> m_Items;

    //! Stores parent identities in record order.
    std::vector<ItemId> m_Parents;

    //! Retains the original storage adapter for typed structural edits.
    std::optional<ModelSnapshot> m_Origin;

    //! Suppresses publication when a structural operation leaves the original ordering unchanged.
    bool m_Changed = true;
};
} // namespace EStudioUI
