#pragma once

#include "SemanticTypes.h"
#include "Widget.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace EStudioUI
{
class UiContext;

//! Owner-side, on-demand semantics; indexes actual widgets once and never expands collection rows.
class SemanticTree
{
  public:
    //! Releases the index without retaining widgets or their native owner.
    ~SemanticTree();
    //! Semantic trees remain attached to one context.
    SemanticTree(const SemanticTree&) = delete;
    //! Semantic trees remain attached to one context.
    SemanticTree& operator=(const SemanticTree&) = delete;
    //! Returns a cheap owner-side invalidation generation without traversing widgets.
    std::uint64_t GetRevision() const;
    //! Returns the root's stable identity.
    SemanticId GetRoot() const;
    //! Returns a stable identity for an attached widget, or zero for a foreign widget.
    SemanticId GetId(const Widget& widget) const;
    //! Queries one current node; geometry reflects the last explicit UI preparation.
    std::optional<SemanticNode> GetNode(SemanticId id) const;
    //! Returns at most 256 logical child identities, without invoking paint or layout.
    std::vector<SemanticId> GetChildren(SemanticId id, std::size_t offset = 0, std::size_t limit = 256) const;
    //! Returns the number of logical children without allocating virtual row nodes.
    std::size_t GetChildCount(SemanticId id) const;
    //! Resolves one child directly by its sibling position.
    SemanticId GetChildAt(SemanticId id, std::size_t index) const;
    //! Resolves an adjacent sibling without scanning preceding virtual rows.
    SemanticId GetSibling(SemanticId id, bool next) const;
    //! Returns a cheap logical selected-item count for a selection container.
    std::size_t GetSelectionCount(SemanticId id) const;
    //! Returns at most 256 selected identities, retaining the control's canonical selection policy.
    std::vector<SemanticId> GetSelection(SemanticId id, std::size_t offset = 0, std::size_t limit = 256) const;
    //! Resolves one logical table cell without materializing other rows or columns.
    SemanticId GetGridItem(SemanticId id, std::size_t row, std::size_t column) const;
    //! Returns a weak actual widget only; virtual nodes never masquerade as widgets.
    WidgetRef GetWidget(SemanticId id) const;
    //! Returns the current focused node, or zero.
    SemanticId GetFocus() const;
    //! Uses the shared transformed input geometry and modal boundary.
    SemanticId HitTest(Point windowPoint) const;
    //! Performs a validated interaction through the control's canonical implementation.
    SemanticStatus Perform(SemanticId id, const SemanticCommand& command);

  private:
    friend class UiContext;
    friend class Widget;
    //! Restores the semantic inspection phase after extension hooks.
    struct QueryScope;
    //! Creates the lazy actual-widget index on the UI owner.
    explicit SemanticTree(UiContext& context);
    //! Adds one attached widget, without recursively scanning unrelated subtrees.
    void Register(Widget& widget);
    //! Expires one stable node before widget destruction or detachment.
    void Unregister(const Widget& widget);
    //! Rejects native queries while staged state or an unsafe frame traversal is active.
    void VerifyQuery() const;
    //! Tests popup visibility independently of physical root ownership.
    bool IsVisible(const Widget& widget) const;
    //! Tests the active modal input boundary.
    bool IsAllowed(const Widget& widget) const;
    //! Records a possible semantic change without allocating or visiting widgets.
    void Changed();
    //! Changes only on actual widget registration or removal.
    std::uint64_t m_StructureRevision = 1;
    //! Tracks changes only after the semantic service has been requested.
    std::uint64_t m_Revision = 1;
    //! Observes the containing context, which owns this tree.
    UiContext& m_Context;
    //! Contains weak references only to actual widget instances.
    std::unordered_map<SemanticId, WidgetRef> m_Widgets{};
};
} // namespace EStudioUI
