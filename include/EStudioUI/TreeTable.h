#pragma once

#include "TableWidget.h"
#include "TreeRowMap.h"
#include <unordered_set>

namespace EStudioUI
{
//! Virtual hierarchy with the table's shared columns, selection, editors and GPU styling.
class TreeTable : public TableWidget
{
  public:
    //! Creates an empty hierarchy table with column zero as its branch column.
    TreeTable();

    //! Changes one view-local expansion state without copying source records.
    void SetExpanded(ItemId item, bool expanded);

    //! Returns the stored expansion state for an existing identity.
    bool IsExpanded(ItemId item) const;

    //! Returns the visible count using expanded-branch prefixes.
    std::size_t GetVisibleRowCount() const;

    //! Reveals an existing record's ancestors and selects its schema column.
    void RevealItem(ItemId item, std::size_t column = 0);

    //! Reports committed expansion state independently of model contents.
    Signal<ItemId, bool>& ExpandedChanged();

  protected:
    //! Accepts between-sibling moves or drops into a stable hierarchy parent.
    std::optional<ModelDropTarget> GetRowDropTarget(Point point) const override;

    //! Resolves the branch-count row map without walking collapsed subtrees.
    std::size_t GetViewRowCount() const override;

    //! Resolves a flattened visible row to its canonical identity.
    ItemId GetViewItem(std::size_t row) const override;

    //! Resolves a visible canonical identity to a flattened row.
    std::optional<std::size_t> GetViewIndex(ItemId item) const override;

    //! Retains unchanged branch prefixes for field-only publications.
    void OnModelRowsChanged(ModelSnapshot snapshot, bool structure) override;

    //! Reserves branch/indent space in schema column zero only.
    float GetCellIndent(int row, int column) const override;

    //! Paints the expandable branch using a compiled CSS part.
    void PaintCellDecoration(Renderer& renderer, FontFace& font, int row, int column, Rect cell, const Style& style,
                             float opacity) const override;

    //! Routes disclosure clicks before ordinary cell selection/editing.
    void OnPointerDown(const PointerEvent& event) override;

    //! Supports left/right expansion, collapse and ancestor/child navigation.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Adds accessible expansion state/actions to the stable row node.
    std::optional<SemanticVirtualNode> DescribeSemanticChild(SemanticKey key) const override;

    //! Reuses the same expansion path for assistive technology.
    SemanticStatus OnSemanticChildAction(SemanticKey key, const SemanticCommand& command) override;

  private:
    //! Owns only expanded identities, never row widgets.
    std::unordered_set<ItemId> m_Expanded;

    //! Caches branch prefixes proportional to expansion, independent of collapsed row count.
    TreeRowMap m_Rows;

    //! Publishes view-local expansion changes.
    Signal<ItemId, bool> m_ExpandedChanged;
};
} // namespace EStudioUI
