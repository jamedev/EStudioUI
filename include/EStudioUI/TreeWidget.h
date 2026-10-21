#pragma once

#include "ScrollWidget.h"

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

    //! Creates an empty hierarchy.
    TreeWidget();

    //! Appends a child of parent; zero appends a top-level item.
    NodeId AddItem(NodeId parent, std::string text);

    //! Removes an item and all its descendants without reusing IDs.
    void RemoveItem(NodeId item);

    //! Removes the complete model and clears selection.
    void Clear();

    //! Replaces a caption without changing node identity.
    void SetItemText(NodeId item, std::string text);

    //! Returns a node's caption, throwing for an invalid ID.
    const std::string& GetItemText(NodeId item) const;

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

    //! Implements arrows, page navigation, Home/End, and activation.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
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
    };

    //! Caches a visible row and its nesting depth.
    struct Row
    {
        //! Stable model identity.
        NodeId m_Id = 0;

        //! Indentation depth below the invisible root.
        unsigned m_Depth = 0;
    };

    //! Rebuilds the flattened hierarchy only after structural/expansion changes.
    void EnsureRows() const;

    //! Converts a pointer position to a flattened row index.
    int RowAt(Point position) const;

    //! Stores nodes independently of rendering widgets.
    std::unordered_map<NodeId, Node> m_Nodes;

    //! Stores top-level nodes in insertion order.
    std::vector<NodeId> m_Roots;

    //! Generates never-reused stable IDs.
    NodeId m_NextId = 1;

    //! Stores the selected ID.
    NodeId m_Current = 0;

    //! Stores the pointer's flattened row index.
    int m_Hover = -1;

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
