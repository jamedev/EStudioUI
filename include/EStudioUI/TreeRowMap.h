#pragma once

#include "ModelSnapshot.h"

#include <unordered_map>
#include <unordered_set>

namespace EStudioUI
{
//! Maps expanded hierarchy rows using branch counts, without enumerating collapsed or flat record ranges.
class TreeRowMap
{
  public:
    //! Describes one visible record and its indentation depth.
    struct Row
    {
        //! Stable canonical identity.
        ItemId m_Item = 0;

        //! Number of visible ancestors.
        unsigned m_Depth = 0;
    };

    //! Rebuilds branch prefixes in work proportional to expanded identities, independent of flat row count.
    void Reset(ModelSnapshot snapshot, const std::unordered_set<ItemId>& expanded);

    //! Replaces field-only storage while retaining the unchanged hierarchy prefixes.
    void UpdateSnapshot(ModelSnapshot snapshot);

    //! Returns visible row count without enumerating record values.
    std::size_t GetCount() const;

    //! Resolves one visible row through expanded branch prefixes.
    Row GetRow(std::size_t row) const;

    //! Resolves a visible record's flattened position, or empty if filtered/collapsed.
    std::optional<std::size_t> GetIndex(ItemId item) const;

  private:
    //! Describes an expanded immediate child's additional visible rows.
    struct Branch
    {
        //! Identity of the expanded child.
        ItemId m_Item = 0;

        //! Position among its parent's immediate children.
        std::size_t m_Position = 0;

        //! Number of preceding visible rows before this child.
        std::size_t m_Start = 0;

        //! Additional rows contributed by descendants.
        std::size_t m_Extra = 0;
    };

    //! Stores aggregate and ordered expanded children for one parent.
    struct Group
    {
        //! Immediate source child count.
        std::size_t m_Children = 0;

        //! Total visible rows below this parent.
        std::size_t m_Total = 0;

        //! Expanded children in sibling order with visible prefixes.
        std::vector<Branch> m_Branches;
    };

    //! Shares immutable source values and identity accessors.
    ModelSnapshot m_Snapshot;

    //! Stores metadata for the invisible root and expanded parents only.
    std::unordered_map<ItemId, Group> m_Groups;
};
} // namespace EStudioUI
