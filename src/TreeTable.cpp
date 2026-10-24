#include <EStudioUI/TreeTable.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>

namespace EStudioUI
{
TreeTable::TreeTable()
{
    AddClass("tree-table");
    RegisterStylePart("branch", Hover | Pressed | Selected | Expanded,
                      [](const Style& owner, unsigned)
                      {
                          Style part;
                          part.m_Background = {0, 0, 0, 0};
                          part.m_Color = owner.m_Color;
                          part.m_FontSize = owner.m_FontSize;
                          return part;
                      });
}

void TreeTable::OnModelRowsChanged(ModelSnapshot snapshot, bool structure)
{
    if (structure)
    {
        std::erase_if(m_Expanded, [&](ItemId item) { return !snapshot.Contains(item); });
        m_Rows.Reset(snapshot, m_Expanded);
    }
    else
    {
        m_Rows.UpdateSnapshot(snapshot);
    }
}

std::size_t TreeTable::GetViewRowCount() const
{
    return m_Rows.GetCount();
}

ItemId TreeTable::GetViewItem(std::size_t row) const
{
    return m_Rows.GetRow(row).m_Item;
}

std::optional<std::size_t> TreeTable::GetViewIndex(ItemId item) const
{
    return m_Rows.GetIndex(item);
}

std::size_t TreeTable::GetVisibleRowCount() const
{
    VerifyAccess();
    return m_Rows.GetCount();
}

void TreeTable::SetExpanded(ItemId item, bool expanded)
{
    VerifyStructuralMutation();
    const auto model = GetModel();
    if (!model || !model->GetSnapshot().Contains(item))
    {
        throw std::invalid_argument("Tree expansion requires an existing canonical record.");
    }
    if (!model->GetSnapshot().GetRowCount(item))
    {
        return;
    }
    const bool changed = expanded ? m_Expanded.insert(item).second : m_Expanded.erase(item) != 0;
    if (!changed)
    {
        return;
    }
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    m_Rows.Reset(model->GetSnapshot(), m_Expanded);
    RefreshViewRows();
    if (self.Get())
    {
        m_ExpandedChanged.Emit(item, expanded);
    }
}

bool TreeTable::IsExpanded(ItemId item) const
{
    VerifyAccess();
    return m_Expanded.contains(item);
}

Signal<ItemId, bool>& TreeTable::ExpandedChanged()
{
    VerifyAccess();
    return m_ExpandedChanged;
}

void TreeTable::RevealItem(ItemId item, std::size_t column)
{
    VerifyStructuralMutation();
    const auto model = GetModel();
    if (!model || !model->GetSnapshot().Contains(item))
    {
        throw std::invalid_argument("Cannot reveal an absent tree record.");
    }
    const auto snapshot = model->GetSnapshot();
    std::vector<ItemId> parents;
    for (auto parent = snapshot.GetParent(item); parent; parent = snapshot.GetParent(parent))
    {
        parents.push_back(parent);
    }
    const WidgetRef self(this);
    for (auto position = parents.rbegin(); position != parents.rend(); ++position)
    {
        SetExpanded(*position, true);
        if (!self.Get())
        {
            return;
        }
    }
    SetCurrentItemId(item, column);
    if (const auto row = m_Rows.GetIndex(item))
    {
        const auto bounds = GetCellBounds(static_cast<int>(*row), static_cast<int>(column));
        const auto viewport = GetViewport();
        const auto offset = GetScrollOffset();
        EnsureVisible({bounds.m_X - viewport.m_X + offset.m_X, static_cast<float>(*row) * GetStyle().m_RowHeight,
                       bounds.m_Width, bounds.m_Height});
    }
}

float TreeTable::GetCellIndent(int row, int column) const
{
    return column || !GetModel() ? 0
                                 : static_cast<float>(m_Rows.GetRow(static_cast<std::size_t>(row)).m_Depth) * 18 + 20;
}

void TreeTable::PaintCellDecoration(Renderer& renderer, FontFace& font, int row, int column, Rect cell,
                                    const Style& style, float opacity) const
{
    if (column || !GetModel())
    {
        return;
    }
    const auto item = GetViewItem(static_cast<std::size_t>(row));
    if (!GetModel()->GetSnapshot().GetRowCount(item))
    {
        return;
    }
    const float indent = GetCellIndent(row, column);
    const Rect indicator{cell.m_X + indent - 18, cell.m_Y, 18, cell.m_Height};
    const auto& branch = GetPartStyle("branch", IsExpanded(item) ? static_cast<unsigned>(Expanded) : 0u);
    renderer.DrawBox(indicator, branch, GetClip().Intersect(cell), opacity * branch.m_Opacity);
    DrawRowText(renderer, font, IsExpanded(item) ? "−" : "+", indicator, branch, cell, opacity * style.m_Opacity);
}

std::optional<ModelDropTarget> TreeTable::GetRowDropTarget(Point point) const
{
    const auto row = RowAt(point);
    if (row >= 0 && GetModel() && GetViewport().Contains(point))
    {
        const auto viewport = GetViewport();
        const float y = viewport.m_Y + static_cast<float>(row) * GetStyle().m_RowHeight - GetScrollOffset().m_Y;
        const float fraction = (point.m_Y - y) / GetStyle().m_RowHeight;
        if (fraction >= .25f && fraction <= .75f)
        {
            return ModelDropTarget{GetViewItem(static_cast<std::size_t>(row)),
                                   0,
                                   {viewport.m_X, y, viewport.m_Width, GetStyle().m_RowHeight},
                                   true};
        }
    }
    return TableWidget::GetRowDropTarget(point);
}

void TreeTable::OnPointerDown(const PointerEvent& event)
{
    const int row = RowAt(event.m_Position);
    if (row >= 0 && ColumnAt(event.m_Position) == 0 && GetModel())
    {
        const auto cell = GetCellBounds(row, 0);
        const auto indent = GetCellIndent(row, 0);
        if (event.m_Position.m_X >= cell.m_X + indent - 18 && event.m_Position.m_X < cell.m_X + indent)
        {
            const auto item = GetViewItem(static_cast<std::size_t>(row));
            if (GetModel()->GetSnapshot().GetRowCount(item))
            {
                SetExpanded(item, !IsExpanded(item));
                return;
            }
        }
    }
    TableWidget::OnPointerDown(event);
}

void TreeTable::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    const auto model = GetModel();
    const auto item = GetCurrentItemId();
    if (down && model && item && GetCurrentCell().second == 0 && !IsEditing() && !modifiers.m_Control &&
        !modifiers.m_Alt && (key == Key::Left || key == Key::Right))
    {
        const auto snapshot = model->GetSnapshot();
        if (key == Key::Right)
        {
            if (snapshot.GetRowCount(item))
            {
                if (!IsExpanded(item))
                {
                    SetExpanded(item, true);
                }
                else
                {
                    RevealItem(snapshot.GetItem(0, item));
                }
            }
        }
        else if (IsExpanded(item))
        {
            SetExpanded(item, false);
        }
        else if (const auto parent = snapshot.GetParent(item))
        {
            RevealItem(parent);
        }
        return;
    }
    TableWidget::OnKey(key, down, repeat, modifiers);
}

std::optional<SemanticVirtualNode> TreeTable::DescribeSemanticChild(SemanticKey key) const
{
    auto node = TableWidget::DescribeSemanticChild(key);
    if (node && !key.m_Part && GetModel() && GetModel()->GetSnapshot().Contains(key.m_Item) &&
        GetModel()->GetSnapshot().GetRowCount(key.m_Item))
    {
        node->m_Info.m_Expandable = true;
        node->m_Info.m_Expanded = IsExpanded(key.m_Item);
        node->m_Info.m_Actions = node->m_Info.m_Actions | SemanticAction::Expand | SemanticAction::Collapse;
    }
    return node;
}

SemanticStatus TreeTable::OnSemanticChildAction(SemanticKey key, const SemanticCommand& command)
{
    if (!key.m_Part && (command.m_Action == SemanticAction::Expand || command.m_Action == SemanticAction::Collapse))
    {
        if (!GetModel() || !GetModel()->GetSnapshot().Contains(key.m_Item))
        {
            return SemanticStatus::Unavailable;
        }
        SetExpanded(key.m_Item, command.m_Action == SemanticAction::Expand);
        return SemanticStatus::Succeeded;
    }
    return TableWidget::OnSemanticChildAction(key, command);
}
} // namespace EStudioUI
