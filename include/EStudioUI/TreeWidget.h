#pragma once

#include "ScrollWidget.h"
#include "CollectionView.h"
#include "TreeRowMap.h"
#include "ItemDelegate.h"
#include "ItemDragDrop.h"

#include <cstdint>
#include <unordered_map>

namespace EStudioUI
{
//! A virtualized hierarchy with stable node IDs and keyboard expansion/navigation.
class TreeWidget final : public ScrollWidget
{
  public:
    //! Stable node identity; zero identifies the invisible root.
    using NodeId = std::uint64_t;

    //! Enables captured canonical row moves without creating per-record widgets.
    void SetRowsMovable(bool enabled);

    //! Installs an owner-side custom drop adapter for copy/transfer policies.
    void SetRowDropHandler(std::function<void(const ModelDragData&, const ModelDropTarget&)> handler);

    //! Reports accepted stable-ID drops after canonical publication.
    Signal<ModelDragData, ModelDropTarget>& RowsDropped();

    //! Reports rejected stale/cyclic/read-only drops without changing source data.
    Signal<std::string>& DropRejected();

    //! Creates an empty hierarchy.
    TreeWidget();

    //! Displays a shared hierarchical snapshot while retaining view-local expansion.
    void SetModel(std::shared_ptr<ItemModel> model, std::size_t column = 0);

    //! Returns the external model, or empty for convenience-owned nodes.
    std::shared_ptr<ItemModel> GetModel() const;

    //! Shares canonical selection with compatible list/table/tree views.
    void SetSelectionModel(std::shared_ptr<SelectionModel> selection);

    //! Returns canonical selection when externally modeled.
    std::shared_ptr<SelectionModel> GetSelectionModel() const;

    //! Installs an immutable GPU presentation policy; null restores the default.
    void SetItemDelegate(std::shared_ptr<const ItemDelegate> delegate);

    //! Appends a child of parent; zero appends a top-level item.
    NodeId AddItem(NodeId parent, std::string text);

    //! Removes an item and all its descendants without reusing IDs.
    void RemoveItem(NodeId item);

    //! Removes the complete model and clears selection.
    void Clear();

    //! Replaces a caption without changing node identity.
    void SetItemText(NodeId item, std::string text);

    //! Returns a node's caption, throwing for an invalid ID.
    std::string GetItemText(NodeId item) const;

    //! Expands or collapses a node; hidden selected IDs remain stable.
    void SetExpanded(NodeId item, bool expanded);

    //! Returns the stored expansion state.
    bool IsExpanded(NodeId item) const;

    //! Selects an item, reveals its ancestors, and scrolls it into view; zero clears.
    void SetCurrentItem(NodeId item);

    //! Returns the selected node ID, or zero.
    NodeId GetCurrentItem() const;

    //! Returns the count of currently expanded visible rows.
    std::size_t GetVisibleRowCount() const;

    //! Reports committed selection changes.
    Signal<NodeId>& CurrentChanged();

    //! Reports explicit expansion changes.
    Signal<NodeId, bool>& ExpandedChanged();

    //! Reports Enter or double-click activation.
    Signal<NodeId>& Activated();

    //! Participates in Tab navigation.
    bool IsFocusable() const override;

  protected:
    //! Describes selection and logical structure without materializing row widgets.
    SemanticInfo DescribeSemantics() const override;

    //! Returns a direct logical child count.
    std::size_t GetSemanticChildCount(SemanticKey parent) const override;

    //! Resolves a logical child by position without scanning earlier records.
    std::optional<SemanticKey> GetSemanticChildAt(SemanticKey parent, std::size_t index) const override;

    //! Describes a stable live item using prepared geometry.
    std::optional<SemanticVirtualNode> DescribeSemanticChild(SemanticKey key) const override;

    //! Resolves a stable item's current sibling position.
    std::optional<std::size_t> GetSemanticChildIndex(SemanticKey key) const override;

    //! Applies selection, expansion, realization, or editing through canonical paths.
    SemanticStatus OnSemanticChildAction(SemanticKey key, const SemanticCommand& command) override;

    //! Resolves the logical item under one layout-coordinate point.
    std::optional<SemanticKey> SemanticHitTest(Point layoutPoint) const override;

    //! Returns the canonical selected set size without copying it.
    std::size_t GetSemanticSelectionCount() const override;

    //! Returns a bounded canonical selection page, including temporarily filtered items.
    std::vector<SemanticKey> GetSemanticSelection(std::size_t offset, std::size_t limit) const override;

    //! Returns the current stable item for logical keyboard focus.
    std::optional<SemanticKey> GetSemanticFocus() const override;

    //! Supplies a preferred viewport size independent of model size.
    Point Measure(FontFace& font) const override;

    //! Computes the virtual visible-row height.
    Point GetVirtualSize() const override;

    //! Paints viewport rows and expansion indicators.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

    //! Selects, toggles an expansion indicator, or begins a scrollbar drag.
    void OnPointerDown(const PointerEvent& event) override;

    //! Updates row hover and scrollbar dragging.
    void OnPointerMove(const PointerEvent& event) override;

    //! Releases a pressed row or expansion branch.
    void OnPointerUp(const PointerEvent& event) override;

    //! Clears cancelled row and branch presses.
    void OnCaptureLost() override;

    //! Implements arrows, page navigation, Home/End, and activation.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Resolves an insertion against the existing visible-row mapping.
    std::optional<ModelDropTarget> GetRowDropTarget(Point point) const;

    //! Constructs/registers a weak drag endpoint only when explicitly requested.
    ItemDragDrop& EnsureDragDrop();

    //! Owns opt-in capture/preview metadata, independent of source records.
    std::unique_ptr<ItemDragDrop> m_DragDrop;

    //! Enables pointer drag handling without changing default legacy gestures.
    bool m_RowsMovable = false;

    //! Defers collapsing an already selected multi-row set until a click is confirmed.
    bool m_RetainedRowSelection = false;

    //! Returns a stable identity in the current model namespace.
    SemanticKey SemanticItemKey(NodeId item) const;

    //! Rejects keys from unrelated models or deleted records.
    bool SemanticKeyValid(SemanticKey key) const;

    //! Expires convenience dataset keys when unrelated data is attached.
    std::uint64_t m_SemanticGeneration = AllocateItemId();

    //! Changes only on logical structure and expansion publications.
    std::uint64_t m_SemanticStructureRevision = 1;

    //! Maps prepared convenience rows into geometry without query-time flattening.
    mutable std::unordered_map<NodeId, std::size_t> m_SemanticVisibleIndices;

    //! Owns one node's data and stable child handles.
    struct Node
    {
        //! Observes the parent ID, or zero for a top-level node.
        NodeId m_Parent = 0;

        //! Stores the UTF-8 caption.
        std::string m_Text;

        //! Stores children in display order.
        std::vector<NodeId> m_Children;

        //! Stores the expansion preference.
        bool m_Expanded = false;

        //! Current position under its direct logical parent.
        std::size_t m_Position = 0;
    };

    //! Caches a visible row and its nesting depth.
    struct Row
    {
        //! Stable model identity.
        NodeId m_Id = 0;

        //! Indentation depth below the invisible root.
        unsigned m_Depth = 0;
    };

    //! Applies pointer/keyboard modifiers using stable IDs and visible order.
    void SelectItem(NodeId item, Modifiers modifiers);

    //! Updates branch counts and local coordinates after model publication.
    void ModelChanged(const ModelChange& change);

    //! Refreshes current canonical identity after shared selection commits.
    void SelectionChanged();

    //! Resolves one visible row through the external prefix map or convenience cache.
    Row VisibleRow(std::size_t index) const;

    //! Returns a visible record position without scanning external record ranges.
    std::optional<std::size_t> VisibleIndex(NodeId item) const;

    //! Tests semantic interaction policy without enumerating descendants.
    bool ItemEnabled(NodeId item) const;

    //! Tests identity existence in the currently displayed model.
    bool HasItem(NodeId item) const;

    //! Tests immediate child availability without fetching child values.
    bool HasChildren(NodeId item) const;

    //! Returns one record's parent ID.
    NodeId ParentOf(NodeId item) const;

    //! Returns the first immediate child.
    NodeId FirstChild(NodeId item) const;

    //! Owns external model and selection subscriptions.
    std::unique_ptr<CollectionView> m_View;

    //! Selects the displayed schema column.
    std::size_t m_ModelColumn = 0;

    //! Stores view-local expansion preferences, including filtered-out records.
    std::unordered_set<NodeId> m_ExpandedItems;

    //! Maps expanded external branches without flattening all records.
    mutable TreeRowMap m_RowMap;

    //! Owns immutable optional GPU item presentation.
    std::shared_ptr<const ItemDelegate> m_Delegate = std::make_shared<const ItemDelegate>();

    //! Rebuilds the flattened hierarchy only after structural/expansion changes.
    void EnsureRows() const;

    //! Converts a pointer position to a flattened row index.
    int RowAt(Point position) const;

    //! Returns the cached style-state key for a visible row.
    unsigned ItemState(int index, int hover) const;

    //! Returns a branch allocation using state-independent hover/press metrics.
    Rect BranchBounds(const Row& row, Rect bounds, unsigned state) const;

    //! Stores nodes independently of rendering widgets.
    std::unordered_map<NodeId, Node> m_Nodes;

    //! Stores top-level nodes in insertion order.
    std::vector<NodeId> m_Roots;

    //! Generates never-reused stable IDs.
    NodeId m_NextId = 1;

    //! Stores the selected ID.
    NodeId m_Current = 0;

    //! Stores the pointer's flattened row index.
    mutable int m_Hover = -1;

    //! Remembers layout coordinates so hover follows rows while scrolling.
    Point m_HoverPosition;

    //! Records whether the last pointer position is over an expansion branch.
    mutable bool m_HoverBranch = false;

    //! Stores the stable node ID whose row is captured.
    NodeId m_Pressed = 0;

    //! Records whether capture began over an expansion branch.
    bool m_PressedBranch = false;

    //! Caches the expanded traversal.
    mutable std::vector<Row> m_Rows;

    //! Marks the traversal as stale.
    mutable bool m_RowsDirty = true;

    //! Delivers committed selection changes.
    Signal<NodeId> m_CurrentChanged;

    //! Delivers expansion changes.
    Signal<NodeId, bool> m_ExpandedChanged;

    //! Delivers explicit activation.
    Signal<NodeId> m_Activated;
};
} // namespace EStudioUI
