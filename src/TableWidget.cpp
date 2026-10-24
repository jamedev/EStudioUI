#include <EStudioUI/TableWidget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/TextBuffer.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
Rect ComposedEditorBounds(Rect cell, Rect viewport)
{
    cell.m_Width = std::min(viewport.m_Width, std::max(cell.m_Width, 340.0f));
    cell.m_X = std::clamp(cell.m_X, viewport.m_X, viewport.m_X + viewport.m_Width - cell.m_Width);
    return cell.Intersect(viewport);
}

class DelegateValidator final : public InputValidator
{
  public:
    DelegateValidator(std::shared_ptr<const ItemDelegate> delegate, std::shared_ptr<ItemModel> model, ItemId item,
                      std::size_t column)
        : m_Delegate(std::move(delegate)), m_Model(std::move(model)), m_Item(item), m_Column(column)
    {
    }

    ValidationState Validate(std::string_view text) const override
    {
        try
        {
            const auto model = m_Model.lock();
            if (!model)
                return ValidationState::Intermediate;
            (void)m_Delegate->ParseEdit(model->GetSnapshot(), m_Item, m_Column, text);
            return ValidationState::Acceptable;
        }
        catch (...)
        {
            return ValidationState::Intermediate;
        }
    }

  private:
    std::shared_ptr<const ItemDelegate> m_Delegate;
    std::weak_ptr<ItemModel> m_Model;
    ItemId m_Item;
    std::size_t m_Column;
};
} // namespace

std::size_t TableWidget::GetViewRowCount() const
{
    return GetModel()->GetSnapshot().GetRowCount();
}

ItemId TableWidget::GetViewItem(std::size_t row) const
{
    return GetModel()->GetSnapshot().GetItem(row);
}

std::optional<std::size_t> TableWidget::GetViewIndex(ItemId item) const
{
    const auto data = GetModel()->GetSnapshot();
    return data.Contains(item) && !data.GetParent(item) ? data.GetPosition(item) : std::nullopt;
}

void TableWidget::OnModelRowsChanged(ModelSnapshot, bool)
{
}

float TableWidget::GetCellIndent(int, int) const
{
    return 0;
}

void TableWidget::PaintCellDecoration(Renderer&, FontFace&, int, int, Rect, const Style&, float) const
{
}

void TableWidget::RefreshViewRows()
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    ++m_SemanticStructureRevision;
    m_ContentFontSize = -1;
    if (m_EditItem)
    {
        const auto row = GetViewIndex(m_EditItem);
        if (row)
        {
            m_EditRow = static_cast<int>(*row);
        }
        else if (!m_Committing)
        {
            EndEdit(false, false);
        }
        else
        {
            // A confirmed write owns its pending draft even if its branch closes.
            if (auto* typed = m_TypedEditor.Get())
            {
                typed->SetVisible(false);
            }
            m_Editor->SetVisible(false);
        }
        if (!self.Get())
        {
            return;
        }
    }
    if (m_View)
    {
        SelectionChanged();
    }
    if (self.Get())
    {
        Invalidate(Invalidation::Content);
    }
}

bool TableWidget::IsEditing() const
{
    VerifyAccess();
    return m_EditRow >= 0;
}

void TableWidget::SetRowsMovable(bool enabled)
{
    VerifyStructuralMutation();
    m_RowsMovable = enabled;
    if (enabled)
    {
        EnsureDragDrop();
    }
    else if (m_DragDrop)
    {
        m_DragDrop->Cancel();
    }
}

void TableWidget::SetRowDropHandler(std::function<void(const ModelDragData&, const ModelDropTarget&)> handler)
{
    VerifyStructuralMutation();
    EnsureDragDrop().SetDropHandler(std::move(handler));
}

Signal<ModelDragData, ModelDropTarget>& TableWidget::RowsDropped()
{
    return EnsureDragDrop().Dropped();
}

Signal<std::string>& TableWidget::DropRejected()
{
    return EnsureDragDrop().Rejected();
}

ItemDragDrop& TableWidget::EnsureDragDrop()
{
    VerifyStructuralMutation();
    if (!m_DragDrop)
    {
        const WidgetRef self(this);
        m_DragDrop = std::make_unique<ItemDragDrop>(
            *this,
            [self]
            {
                auto* owner = dynamic_cast<TableWidget*>(self.Get());
                return owner ? owner->GetModel() : nullptr;
            },
            [self]
            {
                auto* owner = dynamic_cast<TableWidget*>(self.Get());
                const auto selection = owner ? owner->GetSelectionModel() : nullptr;
                if (!selection)
                {
                    return std::vector<ItemId>{};
                }
                const auto selected = selection->GetSnapshot();
                if (selected->m_Items.size() > 4096)
                {
                    throw std::length_error("A drag accepts at most 4096 selected records.");
                }
                return std::vector<ItemId>(selected->m_Items.begin(), selected->m_Items.end());
            },
            [self](Point point) -> std::optional<ModelDropTarget>
            {
                auto* owner = dynamic_cast<TableWidget*>(self.Get());
                return owner && owner->m_RowsMovable ? owner->GetRowDropTarget(point) : std::nullopt;
            });
    }
    return *m_DragDrop;
}

std::optional<ModelDropTarget> TableWidget::GetRowDropTarget(Point point) const
{
    if (!GetModel() || !GetViewport().Contains(point))
    {
        return {};
    }
    const auto viewport = GetViewport();
    const int row = RowAt(point);
    if (row < 0)
    {
        return ModelDropTarget{
            0,
            0,
            {viewport.m_X,
             viewport.m_Y + static_cast<float>(GetRowCount()) * GetStyle().m_RowHeight - GetScrollOffset().m_Y,
             viewport.m_Width, 2},
            false};
    }
    const auto item = GetViewItem(static_cast<std::size_t>(row));
    const auto snapshot = GetModel()->GetRootModel()->GetSnapshot();
    const auto parent = snapshot.GetParent(item);
    const float y = viewport.m_Y + static_cast<float>(row) * GetStyle().m_RowHeight - GetScrollOffset().m_Y;
    if (point.m_Y < y + GetStyle().m_RowHeight * .5f)
    {
        return ModelDropTarget{parent, item, {viewport.m_X, y, viewport.m_Width, 2}, false};
    }
    const auto next = *snapshot.GetPosition(item) + 1;
    return ModelDropTarget{parent,
                           next < snapshot.GetRowCount(parent) ? snapshot.GetItem(next, parent) : 0,
                           {viewport.m_X, y + GetStyle().m_RowHeight, viewport.m_Width, 2},
                           false};
}

TableWidget::TableWidget() : ScrollWidget("tablewidget")
{
    RegisterStylePart("cell", Hover | Pressed | Selected | Alternate,
                      [](const Style& owner, unsigned state)
                      {
                          Style part;
                          part.m_Background = state & Selected    ? owner.m_SelectionBackground
                                              : state & Hover     ? owner.m_HoverBackground
                                              : state & Alternate ? owner.m_AlternateBackground
                                                                  : Color{0, 0, 0, 0};
                          part.m_Color = state & Selected ? owner.m_SelectionColor : owner.m_Color;
                          part.m_FontSize = owner.m_FontSize;
                          part.m_TextAlign = owner.m_TextAlign;
                          part.m_Padding = {0, 8, 0, 8};
                          part.m_GridColor = owner.m_GridColor;
                          return part;
                      });
    RegisterStylePart("header", Hover | Pressed | Selected | Sorted,
                      [](const Style& owner, unsigned)
                      {
                          Style part;
                          part.m_Background = owner.m_HeaderBackground;
                          part.m_Color = owner.m_Color;
                          part.m_FontSize = owner.m_FontSize;
                          part.m_TextAlign = owner.m_TextAlign;
                          part.m_Padding = {0, 8, 0, 8};
                          part.m_GridColor = owner.m_GridColor;
                          return part;
                      });
    m_Editor = &Add<TextBox>();
    m_Editor->AddClass("cell-editor");
    m_Editor->SetVisible(false);
    m_Editor->SetFloatingBounds({});
    const WidgetRef owner(this);
    m_Editor->Submitted().Connect(
        [owner](const std::string&)
        {
            if (auto* live = dynamic_cast<TableWidget*>(owner.Get()))
                live->CommitEdit();
        });
    m_Editor->FocusLost().Connect(
        [owner]
        {
            if (auto* live = dynamic_cast<TableWidget*>(owner.Get()))
                live->EndEdit(true, false);
        });
}

void TableWidget::SetModel(std::shared_ptr<ItemModel> model)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!model)
        throw std::invalid_argument("Table model cannot be empty.");
    const auto data = model->GetSnapshot();
    const bool compatible = m_View && m_View->GetModel()->GetRootModel() == model->GetRootModel();
    auto selection = compatible ? m_View->GetSelectionModel() : std::shared_ptr<SelectionModel>{};
    auto view = std::make_unique<CollectionView>(
        *this, model,
        [](Widget& owner, const ModelChange& change) { static_cast<TableWidget&>(owner).ModelChanged(change); },
        [](Widget& owner) { static_cast<TableWidget&>(owner).SelectionChanged(); }, std::move(selection));
    std::vector<TableColumn> columns;
    for (const auto& column : data.GetColumns())
        columns.push_back({TextBuffer::Sanitize(column.m_Title), SizeRule::Fill(1, 60)});
    const WidgetRef self(this);
    if (!compatible)
        EndEdit(false, false);
    if (!self.Get())
        return;
    ++m_SemanticStructureRevision;
    m_View = std::move(view);
    OnModelRowsChanged(data, true);
    m_LocalProjection.reset();
    m_Columns = std::move(columns);
    SyncColumns(!compatible);
    if (!compatible || m_Widths.size() != m_Columns.size())
        m_Widths.assign(m_Columns.size(), std::nullopt);
    m_Rows.clear();
    m_ContentPolicy = ContentMeasurePolicy::Sample;
    m_ContentFontSize = -1;
    m_HoverRow = m_HoverColumn = m_HoverHeader = -1;
    m_PressedRow = m_PressedColumn = m_PressedHeader = -1;
    if (!compatible)
        SetScrollOffset({});
    Invalidate(Invalidation::Content);
    if (m_EditItem)
    {
        if (!GetViewIndex(m_EditItem) || m_EditColumn >= GetColumnCount())
        {
            EndEdit(false, false);
        }
        else
            m_EditRow = static_cast<int>(*GetViewIndex(m_EditItem));
        if (!self.Get())
            return;
    }
    SelectionChanged();
}

std::shared_ptr<ItemModel> TableWidget::GetModel() const
{
    VerifyAccess();
    return m_View ? m_View->GetModel() : nullptr;
}

void TableWidget::SetSelectionModel(std::shared_ptr<SelectionModel> selection)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!m_View)
        throw std::logic_error("Attach a shared table model before assigning shared selection.");
    m_View->SetSelectionModel(std::move(selection));
    SelectionChanged();
}

std::shared_ptr<SelectionModel> TableWidget::GetSelectionModel() const
{
    VerifyAccess();
    return m_View ? m_View->GetSelectionModel() : nullptr;
}

ItemId TableWidget::GetCurrentItemId() const
{
    VerifyAccess();
    return m_View ? m_View->GetSelectionModel()->GetCurrent() : 0;
}

void TableWidget::SetCurrentItemId(ItemId item, std::size_t column)
{
    VerifyStructuralMutation();
    if (!m_View)
        throw std::logic_error("Stable ID selection requires a shared table model.");
    const auto selection = m_View->GetSelectionModel();
    selection->Select(item, column);
}

void TableWidget::SetContentMeasurePolicy(ContentMeasurePolicy policy, std::size_t sampleCount)
{
    VerifyStructuralMutation();
    if (!sampleCount)
        throw std::invalid_argument("Content measurement sample count must be positive.");
    m_ContentPolicy = policy;
    m_ContentSampleCount = sampleCount;
    m_ContentFontSize = -1;
    Invalidate(Invalidation::Arrange | Invalidation::Paint);
}

void TableWidget::FitColumnsToContents()
{
    VerifyStructuralMutation();
    m_FitAll = true;
    m_ContentFontSize = -1;
    Invalidate(Invalidation::Arrange | Invalidation::Paint);
}

void TableWidget::SetItemDelegate(std::size_t column, std::shared_ptr<const ItemDelegate> delegate)
{
    VerifyStructuralMutation();
    if (column >= m_Columns.size())
        throw std::out_of_range("Delegate requires an existing table column.");
    m_Delegates.resize(m_Columns.size());
    m_Delegates[column] = std::move(delegate);
    m_ContentFontSize = -1;
    Invalidate(Invalidation::Arrange | Invalidation::Paint);
}

std::shared_ptr<const ItemDelegate> TableWidget::GetItemDelegate(std::size_t column) const
{
    VerifyAccess();
    if (column >= m_Columns.size())
        throw std::out_of_range("Delegate requires an existing table column.");
    static const auto standard = std::make_shared<const ItemDelegate>();
    return column < m_Delegates.size() && m_Delegates[column] ? m_Delegates[column] : standard;
}

const std::string& TableWidget::GetEditError() const
{
    VerifyAccess();
    return m_EditError;
}

Signal<const std::string&>& TableWidget::EditRejected()
{
    VerifyAccess();
    return m_EditRejected;
}

void TableWidget::SelectionChanged()
{
    const auto data = m_View->GetModel()->GetSnapshot();
    const auto selection = m_View->GetSelectionModel();
    const auto item = selection->GetCurrent();
    const auto position = GetViewIndex(item);
    const int row = position ? static_cast<int>(*position) : -1;
    const int column = row < 0 || data.GetColumns().empty()
                           ? -1
                           : static_cast<int>(std::min(selection->GetColumn(), data.GetColumns().size() - 1));
    const bool changed = row != m_CurrentRow || column != m_CurrentColumn;
    m_CurrentRow = row;
    m_CurrentColumn = column;
    NotifySemanticsChanged();
    RequestRepaint();
    if (changed)
        m_CurrentCellChanged.Emit(row, column);
}

void TableWidget::ModelChanged(const ModelChange& change)
{
    if (!change.m_Reset && !change.m_Structure && change.m_Fields.empty())
    {
        NotifySemanticsChanged();
        return;
    }

    const auto lease = RetainOwner();
    OnModelRowsChanged(change.m_After, change.m_Reset || change.m_Structure);
    if (change.m_Reset || change.m_Structure)
        ++m_SemanticStructureRevision;
    const WidgetRef self(this);
    const auto model = m_View->GetModel();
    if (change.m_Reset)
    {
        for (std::size_t column = 0; column < m_Delegates.size(); ++column)
            if (column >= change.m_Before.GetColumns().size() || column >= change.m_After.GetColumns().size() ||
                change.m_Before.GetColumns()[column].m_Title != change.m_After.GetColumns()[column].m_Title ||
                change.m_Before.GetColumns()[column].m_ValueType != change.m_After.GetColumns()[column].m_ValueType)
                m_Delegates[column].reset();
        std::vector<TableColumn> columns;
        for (std::size_t i = 0; i < change.m_After.GetColumns().size(); ++i)
            columns.push_back({TextBuffer::Sanitize(change.m_After.GetColumns()[i].m_Title),
                               i < m_Columns.size() ? m_Columns[i].m_Size : SizeRule::Fill(1, 60)});
        m_Columns = std::move(columns);
        SyncColumns();
        m_Widths.resize(m_Columns.size());
    }
    if (m_EditItem && !m_Committing)
    {
        if (!GetViewIndex(m_EditItem) || m_EditColumn >= GetColumnCount())
        {
            EndEdit(false, false);
        }
        else
            m_EditRow = static_cast<int>(*GetViewIndex(m_EditItem));
        if (!self.Get())
            return;
    }
    bool sizing = change.m_Reset || change.m_Structure;
    for (const auto& field : change.m_Fields)
        if (field.m_Column < m_Columns.size() && m_Columns[field.m_Column].m_Size.m_Mode == SizeRule::Mode::Content)
            sizing = true;
    if (sizing)
    {
        m_ContentFontSize = -1;
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
    }
    else
        RequestRepaint();
    m_HoverRow = m_HoverColumn = m_PressedRow = m_PressedColumn = -1;
    SelectionChanged();
    if (!self.Get() || !m_View || m_View->GetModel() != model || !m_CellChanged.HasConnections())
        return;
    for (const auto& field : change.m_Fields)
    {
        if (field.m_Role != ModelRole::Edit || !change.m_After.Contains(field.m_Item) || !GetViewIndex(field.m_Item) ||
            field.m_Column >= m_Columns.size())
        {
            continue;
        }
        const auto position = GetViewIndex(field.m_Item);
        if (position)
            m_CellChanged.Emit(static_cast<int>(*position), static_cast<int>(field.m_Column));
        if (!self.Get() || !m_View || m_View->GetModel() != model)
            return;
    }
}

void TableWidget::SelectCell(int row, int column, Modifiers modifiers)
{
    if (!m_View)
    {
        SetCurrentCell(row, column);
        return;
    }
    const auto data = m_View->GetModel()->GetSnapshot();
    const auto item = GetViewItem(static_cast<std::size_t>(row));
    const auto enabled = data.GetValue(item, static_cast<std::size_t>(column), ModelRole::Enabled);
    if (std::holds_alternative<bool>(enabled) && !std::get<bool>(enabled))
        return;
    std::vector<ItemId> order;
    if (modifiers.m_Shift && m_View->GetSelectionModel()->GetMode() == SelectionMode::Multiple)
        for (std::size_t index = 0; index < GetViewRowCount(); ++index)
        {
            order.push_back(GetViewItem(index));
        }
    const auto bounds = GetCellBounds(row, column);
    const auto viewport = GetViewport();
    const auto offset = GetScrollOffset();
    EnsureVisible({bounds.m_X - viewport.m_X + offset.m_X, static_cast<float>(row) * GetStyle().m_RowHeight,
                   bounds.m_Width, GetStyle().m_RowHeight});
    m_View->Select(item, static_cast<std::size_t>(column), modifiers, order);
}

void TableWidget::SetColumns(std::vector<TableColumn> columns)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    for (auto& column : columns)
    {
        column.m_Size.Validate();
        column.m_Title = TextBuffer::Sanitize(column.m_Title);
    }
    const WidgetRef lifetime(this);
    EndEdit(false, false);
    if (!lifetime.Get())
        return;
    m_View.reset();
    m_LocalProjection.reset();
    m_ContentPolicy = ContentMeasurePolicy::Exact;
    m_Columns = std::move(columns);
    SyncColumns();
    m_Widths.assign(m_Columns.size(), std::nullopt);
    m_Rows.clear();
    m_SemanticRowIds.clear();
    m_SemanticRowIndices.clear();
    m_SemanticGeneration = AllocateItemId();
    ++m_SemanticStructureRevision;
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    m_ResizeColumn = -1;
    m_HoverRow = m_HoverColumn = m_HoverHeader = -1;
    m_PressedRow = m_PressedColumn = m_PressedHeader = -1;
    SetState(Pressed, false);
    const bool changed = m_CurrentRow != -1;
    m_CurrentRow = m_CurrentColumn = -1;
    SetScrollOffset({});
    Invalidate(Invalidation::Content);
    if (changed)
        m_CurrentCellChanged.Emit(-1, -1);
}

void TableWidget::SetRows(std::vector<std::vector<std::string>> rows)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    for (auto& row : rows)
    {
        if (row.size() != m_Columns.size())
            throw std::invalid_argument("Every table row must match the column count.");
        for (auto& cell : row)
            cell = TextBuffer::Sanitize(cell);
    }
    const WidgetRef lifetime(this);
    EndEdit(false, false);
    if (!lifetime.Get())
        return;
    m_View.reset();
    m_LocalProjection.reset();
    m_ContentPolicy = ContentMeasurePolicy::Exact;
    std::vector<ItemId> semanticIds;
    std::unordered_map<ItemId, std::size_t> semanticIndices;
    semanticIds.reserve(rows.size());
    semanticIndices.reserve(rows.size());
    for (std::size_t row = 0; row < rows.size(); ++row)
    {
        const auto id = AllocateItemId();
        semanticIds.push_back(id);
        semanticIndices.emplace(id, row);
    }
    m_SemanticGeneration = AllocateItemId();
    ++m_SemanticStructureRevision;
    m_SemanticRowIds = std::move(semanticIds);
    m_SemanticRowIndices = std::move(semanticIndices);
    m_Rows = std::move(rows);
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    m_ResizeColumn = -1;
    m_HoverRow = m_HoverColumn = m_HoverHeader = -1;
    m_PressedRow = m_PressedColumn = m_PressedHeader = -1;
    SetState(Pressed, false);
    const bool changed = m_CurrentRow != -1;
    m_CurrentRow = m_CurrentColumn = -1;
    SetScrollOffset({});
    Invalidate(Invalidation::Content);
    if (changed)
        m_CurrentCellChanged.Emit(-1, -1);
}

int TableWidget::AddRow(std::vector<std::string> cells)
{
    VerifyStructuralMutation();
    if (cells.size() != m_Columns.size())
        throw std::invalid_argument("Every table row must match the column count.");
    for (auto& cell : cells)
        cell = TextBuffer::Sanitize(cell);
    if (m_View)
    {
        const auto model = m_View->GetModel();
        const auto data = model->GetSnapshot();
        std::vector<ModelValue> values;
        for (std::size_t i = 0; i < cells.size(); ++i)
            values.push_back(ParseModelValue(data.GetColumns()[i], cells[i]));
        const auto index = static_cast<int>(data.GetRowCount());
        model->Restructure([values = std::move(values)](ModelBuilder& builder) { builder.Add(values); });
        return index;
    }
    if (m_Rows.size() == m_Rows.capacity())
        m_Rows.reserve(std::max<std::size_t>(8, m_Rows.size() * 2));
    if (m_SemanticRowIds.size() == m_SemanticRowIds.capacity())
        m_SemanticRowIds.reserve(std::max<std::size_t>(8, m_SemanticRowIds.size() * 2));
    const auto id = AllocateItemId();
    m_SemanticRowIndices.emplace(id, m_Rows.size());
    m_SemanticRowIds.push_back(id);
    ++m_SemanticStructureRevision;
    m_Rows.push_back(std::move(cells));
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    Invalidate(Invalidation::Content);
    return GetRowCount() - 1;
}

int TableWidget::GetRowCount() const
{
    VerifyAccess();
    return m_View ? static_cast<int>(GetViewRowCount()) : static_cast<int>(m_Rows.size());
}

int TableWidget::GetColumnCount() const
{
    VerifyAccess();
    return static_cast<int>(m_Columns.size());
}

std::string TableWidget::GetCell(int row, int column) const
{
    VerifyAccess();
    if (row < 0 || row >= GetRowCount() || column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table cell.");
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        return TextBuffer::Sanitize(
            GetItemDelegate(static_cast<std::size_t>(column))
                ->GetText(data, GetViewItem(static_cast<std::size_t>(row)), static_cast<std::size_t>(column)));
    }
    return m_Rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
}

void TableWidget::SetCell(int row, int column, std::string text)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    (void)GetCell(row, column);
    text = TextBuffer::Sanitize(text);
    if (m_View)
    {
        const auto model = m_View->GetModel();
        const auto item = GetViewItem(static_cast<std::size_t>(row));
        model->SetText(item, static_cast<std::size_t>(column), text);
        return;
    }
    auto& cell = m_Rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    if (cell == text)
        return;
    cell = std::move(text);
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    Invalidate(Invalidation::Content);
    m_CellChanged.Emit(row, column);
}

void TableWidget::SetCurrentCell(int row, int column)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (row != -1 || column != -1)
        (void)GetCell(row, column);
    if (row >= 0)
    {
        const auto bounds = GetCellBounds(row, column);
        const auto viewport = GetViewport();
        const auto offset = GetScrollOffset();
        EnsureVisible({bounds.m_X - viewport.m_X + offset.m_X, static_cast<float>(row) * GetStyle().m_RowHeight,
                       bounds.m_Width, GetStyle().m_RowHeight});
    }
    if (m_View)
    {
        const auto selection = m_View->GetSelectionModel();
        const auto data = m_View->GetModel()->GetSnapshot();
        selection->Select(row < 0 ? 0 : GetViewItem(static_cast<std::size_t>(row)),
                          column < 0 ? 0 : static_cast<std::size_t>(column));
        return;
    }
    if (m_CurrentRow == row && m_CurrentColumn == column)
        return;
    m_CurrentRow = row;
    m_CurrentColumn = column;
    NotifySemanticsChanged();
    RequestRepaint();
    m_CurrentCellChanged.Emit(row, column);
}

std::pair<int, int> TableWidget::GetCurrentCell() const
{
    VerifyAccess();
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        const auto selection = m_View->GetSelectionModel();
        const auto item = selection->GetCurrent();
        const auto position = GetViewIndex(item);
        if (!position || data.GetColumns().empty())
            return {-1, -1};
        return {static_cast<int>(*position),
                static_cast<int>(std::min(selection->GetColumn(), data.GetColumns().size() - 1))};
    }
    return {m_CurrentRow, m_CurrentColumn};
}

void TableWidget::SortByColumn(int column, bool ascending)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table sort column.");
    if (m_View)
    {
        const WidgetRef self(this);
        if (!m_LocalProjection)
        {
            const auto projection = SortFilterModel::Create(m_View->GetModel());
            SetModel(projection);
            if (!self.Get())
                return;
            m_LocalProjection = projection;
        }
        m_SortColumn = column;
        m_Ascending = ascending;
        const auto projection = m_LocalProjection;
        projection->SetSort(static_cast<std::size_t>(column), ascending);
        return;
    }
    const WidgetRef lifetime(this);
    EndEdit(false, false);
    if (!lifetime.Get())
        return;
    std::vector<std::size_t> order(m_Rows.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](auto a, auto b)
                     {
                         const auto& first = m_Rows[a][static_cast<std::size_t>(column)];
                         const auto& second = m_Rows[b][static_cast<std::size_t>(column)];
                         return ascending ? first < second : first > second;
                     });
    const int previous = m_CurrentRow;
    std::vector<std::vector<std::string>> sorted;
    std::vector<ItemId> sortedIds;
    sortedIds.reserve(m_Rows.size());
    sorted.reserve(m_Rows.size());
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        if (static_cast<int>(order[i]) == previous)
            m_CurrentRow = static_cast<int>(i);
        sortedIds.push_back(m_SemanticRowIds[order[i]]);
        m_SemanticRowIndices[sortedIds.back()] = i;
        sorted.push_back(std::move(m_Rows[order[i]]));
    }
    m_Rows = std::move(sorted);
    m_SemanticRowIds = std::move(sortedIds);
    ++m_SemanticStructureRevision;
    m_SortColumn = column;
    m_Ascending = ascending;
    NotifySemanticsChanged();
    RequestRepaint();
    if (previous != m_CurrentRow)
        m_CurrentCellChanged.Emit(m_CurrentRow, m_CurrentColumn);
}

void TableWidget::SetEditable(bool editable)
{
    VerifyStructuralMutation();
    if (m_Editable == editable && (editable || m_EditRow < 0))
        return;
    m_Editable = editable;
    NotifySemanticsChanged();
    SetState(ReadOnly, !editable);
    if (!editable)
        CancelEdit();
}

void TableWidget::SetSortingEnabled(bool enabled)
{
    VerifyAccess();
    if (m_SortingEnabled == enabled)
        return;
    m_SortingEnabled = enabled;
}

void TableWidget::SetColumnWidth(int column, float pixels)
{
    VerifyAccess();
    if (column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table column.");
    (void)SizeRule::Fixed(pixels);
    pixels = std::max(24.0f, pixels);
    auto& width = m_Widths[static_cast<std::size_t>(column)];
    if (width == pixels)
        return;
    width = pixels;
    Invalidate(Invalidation::Content);
}

void TableWidget::SetColumnSize(int column, SizeRule size)
{
    VerifyStructuralMutation();
    if (column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table column size contract.");
    size.Validate();
    m_Columns[static_cast<std::size_t>(column)].m_Size = size;
    m_Widths[static_cast<std::size_t>(column)].reset();
    m_ContentFontSize = -1;
    Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
}

void TableWidget::SyncColumns(bool reset)
{
    if (reset || m_ColumnOrder.size() != m_Columns.size())
    {
        m_ColumnOrder.resize(m_Columns.size());
        std::iota(m_ColumnOrder.begin(), m_ColumnOrder.end(), std::size_t{0});
        m_ColumnVisible.assign(m_Columns.size(), true);
    }
}

std::vector<std::size_t> TableWidget::GetColumnOrder(bool visibleOnly) const
{
    VerifyAccess();
    auto order = m_ColumnOrder;
    if (visibleOnly)
    {
        std::erase_if(order, [&](std::size_t column) { return !m_ColumnVisible[column]; });
    }
    return order;
}

void TableWidget::SetColumnOrder(std::vector<std::size_t> order)
{
    VerifyStructuralMutation();
    if (order.size() != m_Columns.size())
    {
        throw std::invalid_argument("Column order requires a complete schema permutation.");
    }
    std::vector<bool> seen(m_Columns.size(), false);
    for (auto column : order)
    {
        if (column >= seen.size() || seen[column])
        {
            throw std::invalid_argument("Column order contains a duplicate or unknown schema column.");
        }
        seen[column] = true;
    }
    if (order == m_ColumnOrder)
    {
        return;
    }
    m_ColumnOrder = std::move(order);
    ++m_SemanticStructureRevision;
    Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
}

void TableWidget::MoveColumn(std::size_t column, std::size_t visualIndex)
{
    VerifyStructuralMutation();
    if (column >= m_Columns.size() || visualIndex >= m_Columns.size())
    {
        throw std::out_of_range("Invalid visual column move.");
    }
    auto order = m_ColumnOrder;
    std::erase(order, column);
    order.insert(order.begin() + static_cast<std::ptrdiff_t>(visualIndex), column);
    SetColumnOrder(std::move(order));
}

void TableWidget::SetColumnVisible(std::size_t column, bool visible)
{
    VerifyStructuralMutation();
    if (column >= m_Columns.size())
    {
        throw std::out_of_range("Unknown table column.");
    }
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    if (m_ColumnVisible[column] == visible)
    {
        return;
    }
    if (!visible && std::count(m_ColumnVisible.begin(), m_ColumnVisible.end(), true) == 1)
    {
        throw std::logic_error("Keep at least one table column visible.");
    }
    if (!visible && m_EditColumn == static_cast<int>(column))
    {
        EndEdit(true, false);
        if (!self.Get())
        {
            return;
        }
        if (m_EditRow >= 0)
        {
            throw std::logic_error("Finish the cell edit before hiding its column.");
        }
    }
    m_ColumnVisible[column] = visible;
    if (!visible && m_CurrentColumn == static_cast<int>(column))
    {
        const auto order = GetColumnOrder(true);
        if (!order.empty() && m_CurrentRow >= 0)
        {
            SetCurrentCell(m_CurrentRow, static_cast<int>(order.front()));
        }
    }
    if (!self.Get())
    {
        return;
    }
    ++m_SemanticStructureRevision;
    m_ContentFontSize = -1;
    Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
}

bool TableWidget::IsColumnVisible(std::size_t column) const
{
    VerifyAccess();
    return m_ColumnVisible.at(column);
}

void TableWidget::SetColumnsMovable(bool enabled)
{
    VerifyStructuralMutation();
    m_ColumnsMovable = enabled;
}

Signal<std::size_t, bool, bool>& TableWidget::SortRequested()
{
    VerifyAccess();
    return m_SortRequested;
}

void TableWidget::SetSortIndicators(std::vector<ModelSort> sort)
{
    VerifyStructuralMutation();
    if (sort.size() > 16)
    {
        throw std::length_error("A table displays at most 16 sort keys.");
    }
    for (const auto& key : sort)
    {
        if (key.m_Column >= m_Columns.size())
        {
            throw std::out_of_range("Unknown header sort column.");
        }
    }
    m_SortIndicators = std::move(sort);
    RequestRepaint();
}

float TableWidget::ColumnOffset(std::size_t column, const std::vector<float>& widths) const
{
    float offset = 0;
    for (const auto current : m_ColumnOrder)
    {
        if (current == column)
        {
            return offset;
        }
        offset += widths[current];
    }
    throw std::out_of_range("Unknown table column offset.");
}

std::vector<float> TableWidget::GetColumnWidths() const
{
    VerifyAccess();
    const auto& cell = GetPartStyle("cell", 0);
    const auto& header = GetPartStyle("header", 0);
    const float cellInset = cell.m_Padding.m_Left + cell.m_Padding.m_Right + 2 * cell.m_BorderWidth;
    const float headerInset = header.m_Padding.m_Left + header.m_Padding.m_Right + 2 * header.m_BorderWidth;
    const auto rowHeight = std::max(1.0f, GetStyle().m_RowHeight);
    const auto viewportHeight = GetViewport().m_Height;
    const auto verticalExtent = static_cast<float>(GetRowCount()) * rowHeight;
    const auto offsetY = std::clamp(GetStoredScrollOffset().m_Y, 0.0f, std::max(0.0f, verticalExtent - viewportHeight));
    const auto visibleFirst = static_cast<std::size_t>(offsetY / rowHeight);
    const auto visibleCount = static_cast<std::size_t>(std::ceil(viewportHeight / rowHeight)) + 1;
    if (m_ContentFontSize != cell.m_FontSize || m_ContentHeaderFontSize != header.m_FontSize ||
        m_ContentCellInset != cellInset || m_ContentHeaderInset != headerInset ||
        m_ContentWidths.size() != m_Columns.size() ||
        (m_ContentPolicy == ContentMeasurePolicy::Visible &&
         (m_ContentFirstVisible != visibleFirst || m_ContentVisibleCount != visibleCount)))
    {
        m_ContentWidths.assign(m_Columns.size(), 0);
        for (std::size_t i = 0; i < m_Columns.size(); ++i)
        {
            if (!m_ColumnVisible[i] || m_Columns[i].m_Size.m_Mode != SizeRule::Mode::Content || !GetContext())
            {
                continue;
            }
            auto& font = GetContext()->GetFont();
            const float sortWidth = std::max(font.Shape(" ↑", header).m_Width, font.Shape(" ↓", header).m_Width);
            float width = font.Shape(m_Columns[i].m_Title, header).m_Width + headerInset + sortWidth;
            const auto data = m_View ? m_View->GetModel()->GetSnapshot() : ModelSnapshot{};
            if (m_View && data.GetColumns()[i].m_WidthHint)
                width = std::max(width, *data.GetColumns()[i].m_WidthHint + cellInset);
            const auto policy = m_FitAll ? ContentMeasurePolicy::Exact : m_ContentPolicy;
            const auto count = static_cast<std::size_t>(GetRowCount());
            std::size_t first = 0, end = count;
            if (policy == ContentMeasurePolicy::Hints)
                end = 0;
            else if (policy == ContentMeasurePolicy::Sample)
                end = std::min(count, m_ContentSampleCount);
            else if (policy == ContentMeasurePolicy::Visible)
            {
                first = std::min(count, visibleFirst);
                end = std::min(count, first + visibleCount);
            }
            for (auto row = first; row < end; ++row)
                width = std::max(width, font.Shape(GetCell(static_cast<int>(row), static_cast<int>(i)), cell).m_Width +
                                            cellInset + GetCellIndent(static_cast<int>(row), static_cast<int>(i)));
            m_ContentWidths[i] = width;
        }
        m_FitAll = false;
        m_ContentFirstVisible = visibleFirst;
        m_ContentVisibleCount = visibleCount;
        m_ContentFontSize = cell.m_FontSize;
        m_ContentHeaderFontSize = header.m_FontSize;
        m_ContentCellInset = cellInset;
        m_ContentHeaderInset = headerInset;
    }
    std::vector<LayoutItem> items;
    for (std::size_t i = 0; i < m_Columns.size(); ++i)
    {
        const auto rule = !m_ColumnVisible[i] ? SizeRule::Fixed(0)
                          : m_Widths[i]       ? SizeRule::Fixed(*m_Widths[i])
                                              : m_Columns[i].m_Size;
        const float basis = rule.m_Mode == SizeRule::Mode::Fixed  ? rule.m_Value
                            : rule.m_Mode == SizeRule::Mode::Fill ? rule.m_Minimum
                                                                  : m_ContentWidths[i];
        items.push_back(
            {basis, rule.m_Minimum, rule.m_Maximum, rule.m_Mode == SizeRule::Mode::Fill ? rule.m_Value : 0});
    }
    return Layout::Distribute(GetViewport().m_Width, items).m_Sizes;
}

Rect TableWidget::GetCellBounds(int row, int column) const
{
    VerifyAccess();
    (void)GetCell(row, column);
    const auto widths = GetColumnWidths();
    const auto viewport = GetViewport();
    const auto offset = GetScrollOffset();
    const float x = viewport.m_X - offset.m_X + ColumnOffset(static_cast<std::size_t>(column), widths);
    return {x, viewport.m_Y + static_cast<float>(row) * GetStyle().m_RowHeight - offset.m_Y,
            widths[static_cast<std::size_t>(column)], GetStyle().m_RowHeight};
}

void TableWidget::BeginEdit(int row, int column)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (m_Committing)
    {
        return;
    }
    if (column >= 0 && static_cast<std::size_t>(column) < m_ColumnVisible.size() &&
        !m_ColumnVisible[static_cast<std::size_t>(column)])
    {
        return;
    }
    (void)GetCell(row, column);
    if (!m_Editable || !IsEnabled() || !GetContext())
        return;
    // Diagnose nested transaction/frame use before changing selection or editor state.
    GetContext()->Prepare(GetContext()->Root().GetBounds().m_Width, GetContext()->Root().GetBounds().m_Height);
    const WidgetRef self(this);
    const auto model = m_View ? m_View->GetModel() : nullptr;
    const auto data = model ? model->GetSnapshot() : ModelSnapshot{};
    const auto item = model ? GetViewItem(static_cast<std::size_t>(row)) : 0;
    if (model && !data.GetColumns().at(static_cast<std::size_t>(column)).m_Editable)
        return;
    if (model)
    {
        const auto enabled = data.GetValue(item, static_cast<std::size_t>(column), ModelRole::Enabled);
        if (std::holds_alternative<bool>(enabled) && !std::get<bool>(enabled))
            return;
    }
    SetCurrentCell(row, column);
    if (!self.Get() || row >= GetRowCount() || column >= GetColumnCount())
        return;
    m_EditRow = row;
    m_EditColumn = column;
    m_EditItem = item;
    m_EditError.clear();
    ++m_EditSerial;
    if (auto* previous = m_TypedEditor.Get())
    {
        RemoveChild(*previous);
    }
    m_TypedEditor = {};
    if (model)
    {
        std::unique_ptr<CellEditor> composed;
        try
        {
            composed = GetItemDelegate(static_cast<std::size_t>(column))
                           ->CreateEditor(data, item, static_cast<std::size_t>(column));
        }
        catch (const std::exception& error)
        {
            if (!self.Get())
            {
                return;
            }
            m_EditRow = m_EditColumn = -1;
            m_EditItem = 0;
            m_EditError = error.what();
            const auto message = m_EditError;
            m_EditRejected.Emit(message);
            return;
        }
        if (!self.Get())
        {
            return;
        }
        if (composed)
        {
            auto& typed = static_cast<CellEditor&>(AddChild(std::move(composed)));
            m_TypedEditor = WidgetRef(&typed);
            m_EditOriginal = data.GetValue(item, static_cast<std::size_t>(column));
            m_Editor->SetVisible(false);
            typed.SetFloatingBounds(ComposedEditorBounds(GetCellBounds(m_EditRow, m_EditColumn), GetViewport()));
            typed.CommitRequested().Connect(
                [self]
                {
                    if (auto* table = dynamic_cast<TableWidget*>(self.Get()))
                    {
                        table->CommitEdit();
                    }
                });
            typed.CancelRequested().Connect(
                [self]
                {
                    if (auto* table = dynamic_cast<TableWidget*>(self.Get()))
                    {
                        table->CancelEdit();
                    }
                });
            GetContext()->Prepare(GetContext()->Root().GetBounds().m_Width, GetContext()->Root().GetBounds().m_Height);
            GetContext()->SetFocus(&typed.FocusTarget());
            return;
        }
    }
    std::string text = GetCell(row, column);
    if (model)
    {
        const auto delegate = GetItemDelegate(static_cast<std::size_t>(column));
        m_EditOriginal = data.GetValue(item, static_cast<std::size_t>(column));
        text = delegate->GetEditorText(data, item, static_cast<std::size_t>(column));
        m_Editor->SetValidator(
            std::make_shared<DelegateValidator>(delegate, model, item, static_cast<std::size_t>(column)));
    }
    else
        m_Editor->SetValidator({});
    if (!self.Get())
        return;
    if (!m_Editor->CanSetText(text))
    {
        m_EditRow = m_EditColumn = -1;
        m_EditItem = 0;
        m_EditError = "The cell value cannot be represented exactly by the table editor.";
        const auto error = m_EditError;
        m_EditRejected.Emit(error);
        return;
    }
    m_Editor->SetText(text);
    if (!self.Get())
        return;
    m_Editor->SetVisible(true);
    m_Editor->SetFloatingBounds(GetCellBounds(m_EditRow, m_EditColumn).Intersect(GetViewport()));
    GetContext()->Prepare(GetContext()->Root().GetBounds().m_Width, GetContext()->Root().GetBounds().m_Height);
    GetContext()->SetFocus(m_Editor);
    if (self.Get())
        m_Editor->SelectAll();
}

Widget* TableWidget::GetCellEditor() const
{
    VerifyAccess();
    return m_EditRow < 0 ? nullptr : m_TypedEditor.Get() ? m_TypedEditor.Get() : m_Editor;
}

void TableWidget::FinishEdit(ModelEditResult result, bool restoreFocus, std::uint64_t serial)
{
    if (serial != m_EditSerial || m_EditRow < 0)
    {
        return;
    }
    const auto lease = RetainOwner();
    m_Committing = false;
    if (auto* typed = m_TypedEditor.Get())
    {
        typed->SetEnabled(true);
    }
    m_Editor->SetEnabled(true);
    if (result.m_Status != ModelEditStatus::Committed)
    {
        m_EditError = result.m_Message.empty() ? "The model rejected the cell edit." : result.m_Message;
        const auto error = m_EditError;
        m_EditRejected.Emit(error);
        return;
    }
    const WidgetRef self(this);
    m_EditError.clear();
    m_EditRow = m_EditColumn = -1;
    m_EditItem = 0;
    ++m_EditSerial;
    m_Editor->SetVisible(false);
    if (!self.Get())
    {
        return;
    }
    if (auto* typed = m_TypedEditor.Get())
    {
        typed->SetVisible(false);
    }
    if (!self.Get())
    {
        return;
    }
    if (restoreFocus && GetContext())
    {
        GetContext()->SetFocus(this);
    }
    if (self.Get())
    {
        Invalidate(Invalidation::Arrange | Invalidation::Paint);
    }
}

void TableWidget::EndEdit(bool commit, bool restoreFocus)
{
    const auto lease = RetainOwner();
    if (m_EditRow < 0 || m_Committing)
    {
        return;
    }
    const int row = m_EditRow, column = m_EditColumn;
    const auto serial = m_EditSerial;
    const WidgetRef self(this);
    const auto model = m_View ? m_View->GetModel() : nullptr;
    if (commit && model)
    {
        try
        {
            const auto canonical = model->GetRootModel()->GetSnapshot();
            if (!canonical.Contains(m_EditItem) || static_cast<std::size_t>(column) >= canonical.GetColumns().size())
            {
                throw std::logic_error("The edited record or column was removed.");
            }
            const auto delegate = GetItemDelegate(static_cast<std::size_t>(column));
            auto value =
                m_TypedEditor.Get()
                    ? static_cast<CellEditor*>(m_TypedEditor.Get())->GetValue()
                    : delegate->ParseEdit(canonical, m_EditItem, static_cast<std::size_t>(column), m_Editor->GetText());
            if (!self.Get())
            {
                return;
            }
            const auto current = canonical.GetValue(m_EditItem, static_cast<std::size_t>(column));
            if (current != m_EditOriginal && current != value)
            {
                throw std::logic_error("The cell changed externally while editing; cancel and reopen the editor.");
            }
            m_Committing = true;
            m_EditSession = model->RequestEdit({{m_EditItem, static_cast<std::size_t>(column), std::move(value)}});
            if (!self.Get())
            {
                return;
            }
            if (m_EditSession.IsPending())
            {
                if (auto* typed = m_TypedEditor.Get())
                {
                    typed->SetEnabled(false);
                }
                m_Editor->SetEnabled(false);
            }
            m_EditSession.OnFinished(
                [self, serial, restoreFocus](ModelEditResult result)
                {
                    if (auto* table = dynamic_cast<TableWidget*>(self.Get()))
                    {
                        table->FinishEdit(std::move(result), restoreFocus, serial);
                    }
                });
        }
        catch (const std::exception& error)
        {
            if (self.Get())
            {
                FinishEdit({ModelEditStatus::Failed, error.what()}, false, serial);
            }
        }
        return;
    }
    const auto text = m_Editor->GetText();
    FinishEdit({ModelEditStatus::Committed, {}}, restoreFocus, serial);
    if (self.Get() && commit && !model && row < GetRowCount() && column < GetColumnCount())
    {
        SetCell(row, column, text);
    }
}

void TableWidget::CommitEdit()
{
    VerifyAccess();
    EndEdit(true, true);
}

void TableWidget::CancelEdit()
{
    VerifyAccess();
    EndEdit(false, true);
}

Signal<int, int>& TableWidget::CellChanged()
{
    VerifyAccess();
    return m_CellChanged;
}

Signal<int, int>& TableWidget::CurrentCellChanged()
{
    VerifyAccess();
    return m_CurrentCellChanged;
}

bool TableWidget::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Rect TableWidget::GetViewport() const
{
    VerifyAccess();
    auto viewport = ScrollWidget::GetViewport();
    viewport.m_Y += GetStyle().m_RowHeight;
    viewport.m_Height = std::max(0.0f, viewport.m_Height - GetStyle().m_RowHeight);
    return viewport;
}

Point TableWidget::Measure(FontFace&) const
{
    return {320, 220};
}

Point TableWidget::GetVirtualSize() const
{
    const auto widths = GetColumnWidths();
    return {std::accumulate(widths.begin(), widths.end(), 0.0f),
            static_cast<float>(GetRowCount()) * GetStyle().m_RowHeight};
}

int TableWidget::ColumnAt(Point point) const
{
    if (point.m_X < GetViewport().m_X || point.m_X >= GetViewport().m_X + GetViewport().m_Width)
        return -1;
    const auto widths = GetColumnWidths();
    float x = GetViewport().m_X - GetScrollOffset().m_X;
    for (const auto i : m_ColumnOrder)
    {
        if (point.m_X >= x && point.m_X < x + widths[i])
            return static_cast<int>(i);
        x += widths[i];
    }
    return -1;
}

int TableWidget::RowAt(Point point) const
{
    const auto viewport = GetViewport();
    if (!viewport.Contains(point))
        return -1;
    const int row = static_cast<int>((point.m_Y - viewport.m_Y + GetScrollOffset().m_Y) / GetStyle().m_RowHeight);
    return row < GetRowCount() ? row : -1;
}

int TableWidget::HeaderAt(Point point) const
{
    const auto viewport = GetViewport();
    if (point.m_Y < GetContentBounds().m_Y || point.m_Y >= viewport.m_Y)
        return -1;
    return ColumnAt(point);
}

void TableWidget::OnLayout()
{
    const auto widths = GetColumnWidths();
    m_SemanticColumns = GetColumnOrder(true);
    m_SemanticColumnOffsets.assign(1, 0);
    for (const auto column : m_SemanticColumns)
    {
        m_SemanticColumnOffsets.push_back(m_SemanticColumnOffsets.back() + widths[column]);
    }
    if (m_EditRow >= 0 && m_EditRow < GetRowCount() && m_EditColumn < GetColumnCount())
    {
        const auto bounds = GetCellBounds(m_EditRow, m_EditColumn).Intersect(GetViewport()).Intersect(GetClip());
        if (auto* typed = m_TypedEditor.Get())
        {
            typed->SetFloatingBounds(
                ComposedEditorBounds(GetCellBounds(m_EditRow, m_EditColumn), GetViewport()).Intersect(GetClip()));
        }
        m_Editor->SetFloatingBounds(bounds);
    }
}

void TableWidget::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto& style = GetStyle();
    const auto viewport = GetViewport();
    const auto clip = GetClip().Intersect(viewport);
    const auto widths = GetColumnWidths();
    const auto offset = GetScrollOffset();
    const int first = std::max(0, static_cast<int>(offset.m_Y / style.m_RowHeight));
    const int last =
        std::min(GetRowCount(), static_cast<int>(std::ceil((offset.m_Y + viewport.m_Height) / style.m_RowHeight)));
    const auto pointer = IsHovered() && GetContext() ? GetContext()->GetPointerPosition() : std::optional<Point>{};
    const Point position = pointer ? GetContext()->ToLayoutPoint(*this, *pointer) : m_HoverPosition;
    m_HoverRow = IsHovered() ? RowAt(position) : -1;
    m_HoverColumn = m_HoverRow >= 0 ? ColumnAt(position) : -1;
    m_HoverHeader = IsHovered() ? HeaderAt(position) : -1;
    renderer.PushState();
    if (style.m_ClipContent)
        renderer.PushClip(GetBounds(), style.m_Radius);
    renderer.PushState();
    renderer.PushClip(clip);
    for (int row = first; row < last; ++row)
    {
        float x = viewport.m_X - offset.m_X;
        for (const auto column : m_ColumnOrder)
        {
            const Rect cell{x, viewport.m_Y + static_cast<float>(row) * style.m_RowHeight - offset.m_Y, widths[column],
                            style.m_RowHeight};
            x += widths[column];
            if (widths[column] <= 0)
            {
                continue;
            }
            if (cell.m_X >= viewport.m_X + viewport.m_Width)
                break;
            if (cell.m_X + cell.m_Width <= viewport.m_X)
                continue;
            unsigned state = row % 2 ? static_cast<unsigned>(Alternate) : 0u;
            const bool selected =
                m_View ? m_View->GetSelectionModel()->IsSelected(GetViewItem(static_cast<std::size_t>(row))) &&
                             (m_View->GetSelectionModel()->GetMode() == SelectionMode::Multiple ||
                              static_cast<int>(column) == m_CurrentColumn)
                       : row == m_CurrentRow && static_cast<int>(column) == m_CurrentColumn;
            if (selected)
                state |= Selected;
            if (row == m_HoverRow && static_cast<int>(column) == m_HoverColumn)
                state |= Hover;
            if (row == m_PressedRow && static_cast<int>(column) == m_PressedColumn)
                state |= Pressed;
            const auto& cssPart = GetPartStyle("cell", state);
            const auto data = m_View ? m_View->GetModel()->GetSnapshot() : ModelSnapshot{};
            const auto item = m_View ? GetViewItem(static_cast<std::size_t>(row)) : 0;
            const auto delegate = m_View ? GetItemDelegate(column) : std::shared_ptr<const ItemDelegate>{};
            const auto styled =
                m_View ? delegate->GetStyle(cssPart, data, item, column, (state & Selected) != 0) : Style{};
            const auto& part = m_View ? styled : cssPart;
            if (!part.m_Displayed || part.m_Opacity <= 0)
                continue;
            const auto identity = ((static_cast<std::uint64_t>(row) + 1) << 32) | (column + 1);
            renderer.PushState(part.m_Transform.Resolve(cell));
            renderer.SetWidgetPaint(cell, clip, nullptr, false, 0);
            renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Rows), identity);
            renderer.DrawBox(cell, part, clip, opacity * part.m_Opacity);
            if (part.m_ClipContent)
                renderer.PushClip(cell, part.m_Radius);
            PaintCellDecoration(renderer, font, row, static_cast<int>(column), cell, part, opacity * part.m_Opacity);
            auto textCell = cell;
            const float indent = std::min(textCell.m_Width, GetCellIndent(row, static_cast<int>(column)));
            textCell.m_X += indent;
            textCell.m_Width -= indent;
            if (!delegate ||
                !delegate->Paint(renderer, font, data, item, column, textCell, part, clip, opacity * part.m_Opacity))
            {
                DrawRowText(renderer, font, GetCell(row, static_cast<int>(column)), textCell, part, cell,
                            opacity * part.m_Opacity);
            }
            auto grid = part.m_GridColor;
            grid.m_A *= opacity * part.m_Opacity;
            renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Widths), identity);
            renderer.DrawSolid({cell.m_X + cell.m_Width - 1, cell.m_Y, 1, cell.m_Height}, grid, clip);
            renderer.DrawSolid({cell.m_X, cell.m_Y + cell.m_Height - 1, cell.m_Width, 1}, grid, clip);
            renderer.PopState();
        }
    }
    renderer.PopState();
    const Rect header{viewport.m_X, GetContentBounds().m_Y, viewport.m_Width, style.m_RowHeight};
    const auto headerClip = GetClip().Intersect(header);
    renderer.PushState();
    renderer.PushClip(headerClip);
    float x = header.m_X - offset.m_X;
    for (const auto i : m_ColumnOrder)
    {
        const Rect cell{x, header.m_Y, widths[i], header.m_Height};
        x += widths[i];
        if (widths[i] <= 0)
        {
            continue;
        }
        if (cell.m_X >= header.m_X + header.m_Width)
            break;
        if (cell.m_X + cell.m_Width <= header.m_X)
            continue;
        const auto sort = std::find_if(m_SortIndicators.begin(), m_SortIndicators.end(),
                                       [&](const ModelSort& key) { return key.m_Column == i; });
        unsigned state =
            static_cast<int>(i) == m_SortColumn || sort != m_SortIndicators.end() ? static_cast<unsigned>(Sorted) : 0u;
        if (static_cast<int>(i) == m_CurrentColumn)
            state |= Selected;
        if (static_cast<int>(i) == m_HoverHeader)
            state |= Hover;
        if (static_cast<int>(i) == m_PressedHeader)
            state |= Pressed;
        const auto& part = GetPartStyle("header", state);
        if (!part.m_Displayed || part.m_Opacity <= 0)
            continue;
        const auto title =
            m_Columns[i].m_Title +
            (state & Sorted ? ((sort == m_SortIndicators.end() ? m_Ascending : sort->m_Ascending) ? " ↑" : " ↓") : "") +
            (sort != m_SortIndicators.end() && m_SortIndicators.size() > 1
                 ? " " + std::to_string(static_cast<std::size_t>(sort - m_SortIndicators.begin()) + 1)
                 : "");
        renderer.PushState(part.m_Transform.Resolve(cell));
        renderer.SetWidgetPaint(cell, headerClip, nullptr, false, 0);
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Columns), i + 1);
        renderer.DrawBox(cell, part, headerClip, opacity * part.m_Opacity);
        if (part.m_ClipContent)
            renderer.PushClip(cell, part.m_Radius);
        DrawRowText(renderer, font, title, cell, part, cell, opacity * part.m_Opacity);
        auto grid = part.m_GridColor;
        grid.m_A *= opacity * part.m_Opacity;
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Widths), i + 1);
        renderer.DrawSolid({cell.m_X + cell.m_Width - 1, header.m_Y, 1, header.m_Height}, grid, headerClip);
        renderer.PopState();
    }
    // Fill any header space left by capped/fixed columns without compositing fills twice.
    if (x < header.m_X + header.m_Width)
    {
        const Rect remainder{std::max(x, header.m_X), header.m_Y, header.m_X + header.m_Width - std::max(x, header.m_X),
                             header.m_Height};
        const auto& part = GetPartStyle("header", 0);
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_ContentHeaderFontSize));
        if (part.m_Displayed && part.m_Opacity > 0)
            renderer.DrawBox(remainder, part, headerClip, opacity * part.m_Opacity);
    }
    renderer.PopState();
    renderer.PopState();
    PaintScrollBars(renderer, opacity);
    if (m_DragDrop)
    {
        m_DragDrop->Paint(renderer, opacity);
    }
}

void TableWidget::OnPointerDown(const PointerEvent& event)
{
    if (HandleScrollPointerDown(event))
        return;
    const int column = ColumnAt(event.m_Position);
    if (column < 0)
        return;
    if (HeaderAt(event.m_Position) >= 0)
    {
        m_PressedHeader = column;
        m_HeaderOrigin = event.m_Position;
        m_HeaderDragging = false;
        SetState(Pressed, true);
        const auto widths = GetColumnWidths();
        float x = GetViewport().m_X - GetScrollOffset().m_X;
        for (const auto i : m_ColumnOrder)
        {
            x += widths[i];
            if (widths[i] <= 0)
            {
                continue;
            }
            if (std::abs(event.m_Position.m_X - x) <= 4)
            {
                m_ResizeColumn = static_cast<int>(i);
                m_ResizeOrigin = event.m_Position.m_X;
                m_ResizeWidth = widths[i];
                return;
            }
        }
        if (m_SortingEnabled && !m_ColumnsMovable)
        {
            const bool ascending = m_SortColumn != column || !m_Ascending;
            if (m_SortRequested.HasConnections())
            {
                m_SortRequested.Emit(static_cast<std::size_t>(column), ascending, event.m_Modifiers.m_Shift);
            }
            else
            {
                SortByColumn(column, ascending);
            }
        }
        return;
    }
    const int row = RowAt(event.m_Position);
    if (row < 0)
        return;
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        const auto enabled = data.GetValue(GetViewItem(static_cast<std::size_t>(row)), static_cast<std::size_t>(column),
                                           ModelRole::Enabled);
        if (std::holds_alternative<bool>(enabled) && !std::get<bool>(enabled))
            return;
    }
    m_PressedRow = row;
    m_PressedColumn = column;
    SetState(Pressed, true);
    const WidgetRef self(this);
    const bool retainSelection = m_RowsMovable && m_View && !event.m_Modifiers.m_Control &&
                                 !event.m_Modifiers.m_Shift &&
                                 m_View->GetSelectionModel()->IsSelected(GetViewItem(static_cast<std::size_t>(row)));
    m_RetainedRowSelection = retainSelection;
    if (!retainSelection)
    {
        SelectCell(row, column, event.m_Modifiers);
    }
    if (self.Get() && event.m_Clicks == 2 && row < GetRowCount() && column < GetColumnCount())
        BeginEdit(row, column);
    if (self.Get() && m_RowsMovable && m_View && !IsEditing())
    {
        EnsureDragDrop().Press(GetViewItem(static_cast<std::size_t>(row)),
                               GetContext()->ToWindowPoint(*this, event.m_Position));
    }
}

void TableWidget::OnPointerMove(const PointerEvent& event)
{
    if (m_DragDrop && m_DragDrop->Move(GetContext()->ToWindowPoint(*this, event.m_Position)))
    {
        return;
    }
    m_HoverPosition = event.m_Position;
    const int row = RowAt(event.m_Position);
    const int column = row >= 0 ? ColumnAt(event.m_Position) : -1;
    const int header = HeaderAt(event.m_Position);
    if (row != m_HoverRow || column != m_HoverColumn || header != m_HoverHeader)
    {
        m_HoverRow = row;
        m_HoverColumn = column;
        m_HoverHeader = header;
        RequestRepaint();
    }
    if (m_ColumnsMovable && m_PressedHeader >= 0 && m_ResizeColumn < 0 &&
        (m_HeaderDragging ||
         std::hypot(event.m_Position.m_X - m_HeaderOrigin.m_X, event.m_Position.m_Y - m_HeaderOrigin.m_Y) >= 6))
    {
        m_HeaderDragging = true;
        const int target = ColumnAt(event.m_Position);
        if (target >= 0)
        {
            const auto position =
                std::find(m_ColumnOrder.begin(), m_ColumnOrder.end(), static_cast<std::size_t>(target));
            MoveColumn(static_cast<std::size_t>(m_PressedHeader),
                       static_cast<std::size_t>(position - m_ColumnOrder.begin()));
        }
    }
    if (m_ResizeColumn >= 0)
        SetColumnWidth(m_ResizeColumn, std::max(24.0f, m_ResizeWidth + event.m_Position.m_X - m_ResizeOrigin));
    else
        ScrollWidget::OnPointerMove(event);
}

void TableWidget::OnPointerUp(const PointerEvent& event)
{
    const WidgetRef self(this);
    const bool dragged = m_DragDrop && m_DragDrop->Release(GetContext()->ToWindowPoint(*this, event.m_Position));
    if (!self.Get())
    {
        return;
    }
    if (!dragged && m_RetainedRowSelection && m_PressedRow >= 0 && RowAt(event.m_Position) == m_PressedRow)
    {
        SelectCell(m_PressedRow, m_PressedColumn, {});
    }
    if (!self.Get())
    {
        return;
    }
    const int header = m_PressedHeader;
    const bool sort = m_ColumnsMovable && !m_HeaderDragging && m_ResizeColumn < 0 && header >= 0 &&
                      HeaderAt(event.m_Position) == header && m_SortingEnabled;
    m_HeaderDragging = false;
    m_ResizeColumn = -1;
    m_PressedRow = m_PressedColumn = m_PressedHeader = -1;
    SetState(Pressed, false);
    ScrollWidget::OnPointerUp(event);
    if (sort)
    {
        const auto indicated = std::find_if(m_SortIndicators.begin(), m_SortIndicators.end(), [&](const ModelSort& key)
                                            { return key.m_Column == static_cast<std::size_t>(header); });
        const bool ascending =
            indicated != m_SortIndicators.end() ? !indicated->m_Ascending : m_SortColumn != header || !m_Ascending;
        if (m_SortRequested.HasConnections())
        {
            m_SortRequested.Emit(static_cast<std::size_t>(header), ascending, event.m_Modifiers.m_Shift);
        }
        else
        {
            SortByColumn(header, ascending);
        }
    }
}

void TableWidget::OnCaptureLost()
{
    if (m_DragDrop)
    {
        m_DragDrop->Cancel();
    }
    m_HeaderDragging = false;
    m_ResizeColumn = -1;
    m_PressedRow = m_PressedColumn = m_PressedHeader = -1;
    SetState(Pressed, false);
    ScrollWidget::OnCaptureLost();
}

void TableWidget::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || GetRowCount() == 0 || m_Columns.empty())
        return;
    int row = std::max(0, m_CurrentRow);
    const auto visible = GetColumnOrder(true);
    if (visible.empty())
    {
        return;
    }
    const auto foundColumn =
        std::find(visible.begin(), visible.end(), static_cast<std::size_t>(std::max(0, m_CurrentColumn)));
    int column = foundColumn == visible.end() ? 0 : static_cast<int>(foundColumn - visible.begin());
    if (key == Key::F2 || key == Key::Enter)
    {
        BeginEdit(row, static_cast<int>(visible[static_cast<std::size_t>(column)]));
        return;
    }
    if (modifiers.m_Control && key == Key::C)
    {
        GetContext()->SetClipboardText(GetCell(row, static_cast<int>(visible[static_cast<std::size_t>(column)])));
        return;
    }
    if (key == Key::Left)
        --column;
    else if (key == Key::Right)
        ++column;
    else if (key == Key::Up)
        --row;
    else if (key == Key::Down)
        row += m_CurrentRow < 0 ? 0 : 1;
    else if (key == Key::Home)
    {
        column = 0;
        if (modifiers.m_Control)
            row = 0;
    }
    else if (key == Key::End)
    {
        column = static_cast<int>(visible.size()) - 1;
        if (modifiers.m_Control)
            row = GetRowCount() - 1;
    }
    else if (key == Key::PageDown)
        row += std::max(1, static_cast<int>(GetViewport().m_Height / GetStyle().m_RowHeight));
    else if (key == Key::PageUp)
        row -= std::max(1, static_cast<int>(GetViewport().m_Height / GetStyle().m_RowHeight));
    else
        return;
    SelectCell(std::clamp(row, 0, GetRowCount() - 1),
               static_cast<int>(
                   visible[static_cast<std::size_t>(std::clamp(column, 0, static_cast<int>(visible.size()) - 1))]),
               modifiers);
}

bool TableWidget::OnKeyPreview(Key key, bool down, bool, Modifiers)
{
    if (!down || m_EditRow < 0 || (!m_TypedEditor.Get() && GetContext()->GetFocus() != m_Editor))
    {
        return false;
    }
    if (key == Key::Escape)
    {
        CancelEdit();
        return true;
    }
    if (key == Key::Tab || (m_TypedEditor.Get() && key == Key::Enter))
    {
        const WidgetRef self(this);
        CommitEdit();
        return !self.Get() || m_EditRow >= 0;
    }
    return false;
}

void TableWidget::OnText(std::string_view text)
{
    if (!m_Editable || text.empty() || m_CurrentRow < 0)
        return;
    const WidgetRef self(this);
    BeginEdit(m_CurrentRow, m_CurrentColumn);
    if (self.Get() && m_EditRow >= 0)
        GetContext()->TextInput(text);
}

bool TableWidget::OnScroll(Point delta, Modifiers modifiers)
{
    const WidgetRef self(this);
    EndEdit(true, true);
    if (!self.Get())
        return true;
    return ScrollWidget::OnScroll(delta, modifiers);
}

SemanticKey TableWidget::SemanticRowKey(std::size_t row, std::uint64_t part) const
{
    if (m_View)
        return {GetViewItem(row), part, m_View->GetModel()->GetRootModel()->GetIdentity()};
    return {m_SemanticRowIds.at(row), part, m_SemanticGeneration};
}

std::optional<std::size_t> TableWidget::SemanticRowIndex(SemanticKey key) const
{
    if (!key.m_Item || key.m_Part > m_Columns.size())
        return {};
    if (m_View)
    {
        const auto model = m_View->GetModel();
        const auto data = model->GetSnapshot();
        if (key.m_Generation != model->GetRootModel()->GetIdentity() || !data.Contains(key.m_Item))
        {
            return {};
        }
        return GetViewIndex(key.m_Item);
    }
    const auto found = m_SemanticRowIndices.find(key.m_Item);
    return key.m_Generation == m_SemanticGeneration && found != m_SemanticRowIndices.end()
               ? std::optional<std::size_t>(found->second)
               : std::nullopt;
}

SemanticInfo TableWidget::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_RowCount = static_cast<std::size_t>(GetRowCount());
    info.m_ColumnCount = GetColumnOrder(true).size();
    info.m_StructureRevision = m_SemanticStructureRevision;
    info.m_MultiSelectable = m_View && m_View->GetSelectionModel()->GetMode() == SelectionMode::Multiple;
    return info;
}

std::size_t TableWidget::GetSemanticChildCount(SemanticKey parent) const
{
    if (!parent.m_Item)
        return static_cast<std::size_t>(GetRowCount());
    return !parent.m_Part && DescribeSemanticChild(parent) ? GetColumnOrder(true).size() : 0;
}

std::optional<SemanticKey> TableWidget::GetSemanticChildAt(SemanticKey parent, std::size_t index) const
{
    if (index >= GetSemanticChildCount(parent))
        return {};
    if (!parent.m_Item)
        return SemanticRowKey(index);
    parent.m_Part = GetColumnOrder(true).at(index) + 1;
    return parent;
}

std::optional<SemanticVirtualNode> TableWidget::DescribeSemanticChild(SemanticKey key) const
{
    const auto row = SemanticRowIndex(key);
    const auto canonical = m_View ? m_View->GetModel()->GetRootModel()->GetSnapshot() : ModelSnapshot{};
    if (!row && (!m_View || key.m_Generation != m_View->GetModel()->GetRootModel()->GetIdentity() ||
                 !canonical.Contains(key.m_Item)))
        return {};
    if (key.m_Part > m_Columns.size() || (key.m_Part && !m_ColumnVisible[static_cast<std::size_t>(key.m_Part - 1)]))
    {
        return {};
    }
    SemanticVirtualNode node;
    auto& info = node.m_Info;
    info.m_Role = key.m_Part ? SemanticRole::Cell : SemanticRole::Row;
    info.m_StructureRevision = m_SemanticStructureRevision;
    node.m_Focusable = key.m_Part != 0;
    if (key.m_Part)
    {
        const auto column = static_cast<std::size_t>(key.m_Part - 1);
        node.m_Parent = {key.m_Item, 0, key.m_Generation};
        const auto visible = GetColumnOrder(true);
        info.m_Column = static_cast<std::size_t>(std::find(visible.begin(), visible.end(), column) - visible.begin());
        if (row)
            info.m_Row = *row;
        info.m_Value = row ? GetCell(static_cast<int>(*row), static_cast<int>(column))
                           : TextBuffer::Sanitize(GetItemDelegate(column)->GetText(canonical, key.m_Item, column));
        info.m_Name = m_Columns[column].m_Title + ": " + info.m_Value;
        const auto selection = m_View ? m_View->GetSelectionModel() : nullptr;
        const auto selectedColumn =
            selection ? selection->GetColumn() : static_cast<std::size_t>(std::max(0, m_CurrentColumn));
        info.m_Selected = column == selectedColumn && (selection ? selection->IsSelected(key.m_Item)
                                                                 : row && static_cast<int>(*row) == m_CurrentRow);
        node.m_Focused =
            column == selectedColumn && (selection ? selection->GetCurrent() == key.m_Item : info.m_Selected);
        info.m_Actions = SemanticAction::Select | SemanticAction::AddToSelection | SemanticAction::RemoveFromSelection |
                         SemanticAction::ScrollIntoView;
        info.m_ReadOnly = !m_Editable || (m_View && !canonical.GetColumns().at(column).m_Editable);
        if (!info.m_ReadOnly)
            info.m_Actions = info.m_Actions | SemanticAction::SetText;
        if (m_View)
        {
            const auto enabled = canonical.GetValue(key.m_Item, column, ModelRole::Enabled);
            node.m_Enabled = !std::holds_alternative<bool>(enabled) || std::get<bool>(enabled);
        }
    }
    else
        info.m_Name = row ? "Row " + std::to_string(*row + 1) : "Filtered row";
    if (row)
    {
        const auto viewport = GetViewport();
        const auto offset = GetStoredScrollOffset();
        float left = viewport.m_X;
        float width = viewport.m_Width;
        if (key.m_Part && m_SemanticColumnOffsets.size() == m_SemanticColumns.size() + 1)
        {
            const auto schemaColumn = static_cast<std::size_t>(key.m_Part - 1);
            const auto column =
                static_cast<std::size_t>(std::find(m_SemanticColumns.begin(), m_SemanticColumns.end(), schemaColumn) -
                                         m_SemanticColumns.begin());
            if (column >= m_SemanticColumns.size())
            {
                return {};
            }
            left = viewport.m_X + m_SemanticColumnOffsets[column] - offset.m_X;
            width = m_SemanticColumnOffsets[column + 1] - m_SemanticColumnOffsets[column];
        }
        else if (key.m_Part)
            width = 0;
        node.m_Bounds = Rect{left, viewport.m_Y + static_cast<float>(*row) * GetStyle().m_RowHeight - offset.m_Y, width,
                             GetStyle().m_RowHeight}
                            .Intersect(viewport);
    }
    return node;
}

std::optional<std::size_t> TableWidget::GetSemanticChildIndex(SemanticKey key) const
{
    if (!DescribeSemanticChild(key))
        return {};
    if (!key.m_Part)
    {
        return SemanticRowIndex(key);
    }
    const auto visible = GetColumnOrder(true);
    return static_cast<std::size_t>(
        std::find(visible.begin(), visible.end(), static_cast<std::size_t>(key.m_Part - 1)) - visible.begin());
}

SemanticStatus TableWidget::OnSemanticChildAction(SemanticKey key, const SemanticCommand& command)
{
    const auto row = SemanticRowIndex(key);
    if (!key.m_Part || key.m_Part > m_Columns.size())
        return SemanticStatus::Unsupported;
    const auto column = static_cast<std::size_t>(key.m_Part - 1);
    if (command.m_Action == SemanticAction::ScrollIntoView)
    {
        if (!row || m_SemanticColumnOffsets.size() != m_SemanticColumns.size() + 1)
        {
            return SemanticStatus::Unavailable;
        }
        const auto visual = static_cast<std::size_t>(
            std::find(m_SemanticColumns.begin(), m_SemanticColumns.end(), column) - m_SemanticColumns.begin());
        if (visual >= m_SemanticColumns.size())
        {
            return SemanticStatus::Unavailable;
        }
        EnsureVisible({m_SemanticColumnOffsets[visual], static_cast<float>(*row) * GetStyle().m_RowHeight,
                       m_SemanticColumnOffsets[visual + 1] - m_SemanticColumnOffsets[visual], GetStyle().m_RowHeight});
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::SetText)
    {
        if (TextBuffer::Sanitize(command.m_Text) != command.m_Text)
            return SemanticStatus::InvalidValue;
        try
        {
            if (m_View)
            {
                const auto model = m_View->GetModel()->GetRootModel();
                const auto value =
                    GetItemDelegate(column)->ParseEdit(model->GetSnapshot(), key.m_Item, column, command.m_Text);
                const auto result = model->RequestEdit({{key.m_Item, column, value}});
                if (!result.IsPending())
                {
                    if (result.GetResult()->m_Status != ModelEditStatus::Committed)
                    {
                        return SemanticStatus::InvalidValue;
                    }
                }
                else
                {
                    const WidgetRef self(this);
                    result.OnFinished(
                        [self](ModelEditResult outcome)
                        {
                            if (auto* live = dynamic_cast<TableWidget*>(self.Get());
                                live && outcome.m_Status != ModelEditStatus::Committed)
                            {
                                live->m_EditError = std::move(outcome.m_Message);
                                live->m_EditRejected.Emit(live->m_EditError);
                            }
                        });
                }
            }
            else if (row)
                SetCell(static_cast<int>(*row), static_cast<int>(column), command.m_Text);
            else
                return SemanticStatus::Unavailable;
        }
        catch (const std::exception&)
        {
            return SemanticStatus::InvalidValue;
        }
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::Select || command.m_Action == SemanticAction::Focus ||
        command.m_Action == SemanticAction::AddToSelection)
    {
        if (m_View)
        {
            const auto selection = m_View->GetSelectionModel();
            if (command.m_Action == SemanticAction::AddToSelection && selection->GetMode() == SelectionMode::Single &&
                !selection->GetSnapshot()->m_Items.empty() && !selection->IsSelected(key.m_Item))
                return SemanticStatus::InvalidValue;
            selection->Select(key.m_Item, column,
                              command.m_Action == SemanticAction::AddToSelection ? SelectionAction::Add
                                                                                 : SelectionAction::Replace);
        }
        else if (row)
        {
            if (command.m_Action == SemanticAction::AddToSelection && m_CurrentRow >= 0 &&
                (static_cast<int>(*row) != m_CurrentRow || static_cast<int>(column) != m_CurrentColumn))
                return SemanticStatus::InvalidValue;
            SetCurrentCell(static_cast<int>(*row), static_cast<int>(column));
        }
        else
            return SemanticStatus::Unavailable;
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::RemoveFromSelection)
    {
        if (m_View)
        {
            const auto selection = m_View->GetSelectionModel();
            if (selection->IsSelected(key.m_Item))
                selection->Select(key.m_Item, column, SelectionAction::Toggle);
        }
        else if (row && static_cast<int>(*row) == m_CurrentRow && static_cast<int>(column) == m_CurrentColumn)
            SetCurrentCell(-1, -1);
        return SemanticStatus::Succeeded;
    }
    return SemanticStatus::Unsupported;
}

std::optional<SemanticKey> TableWidget::SemanticHitTest(Point point) const
{
    const auto viewport = GetViewport();
    if (!viewport.Contains(point) || m_SemanticColumnOffsets.size() != m_SemanticColumns.size() + 1)
    {
        return {};
    }
    const auto offset = GetStoredScrollOffset();
    const auto row =
        static_cast<std::size_t>(std::max(0.0f, point.m_Y - viewport.m_Y + offset.m_Y) / GetStyle().m_RowHeight);
    if (row >= static_cast<std::size_t>(GetRowCount()))
        return {};
    const auto x = point.m_X - viewport.m_X + offset.m_X;
    const auto end = std::upper_bound(m_SemanticColumnOffsets.begin(), m_SemanticColumnOffsets.end(), x);
    if (end == m_SemanticColumnOffsets.begin() || end == m_SemanticColumnOffsets.end())
        return {};
    const auto visual = static_cast<std::size_t>(end - m_SemanticColumnOffsets.begin() - 1);
    return SemanticRowKey(row, static_cast<std::uint64_t>(m_SemanticColumns.at(visual) + 1));
}

std::size_t TableWidget::GetSemanticSelectionCount() const
{
    return m_View ? m_View->GetSelectionModel()->GetSnapshot()->m_Items.size() : m_CurrentRow >= 0 ? 1 : 0;
}

std::vector<SemanticKey> TableWidget::GetSemanticSelection(std::size_t offset, std::size_t limit) const
{
    std::vector<SemanticKey> result;
    if (!m_View)
    {
        if (!offset && limit && m_CurrentRow >= 0 && m_CurrentColumn >= 0)
            result.push_back(SemanticRowKey(static_cast<std::size_t>(m_CurrentRow),
                                            static_cast<std::uint64_t>(m_CurrentColumn + 1)));
        return result;
    }
    const auto selection = m_View->GetSelectionModel()->GetSnapshot();
    if (selection->m_Column >= m_Columns.size())
        return result;
    auto item = selection->m_Items.begin();
    std::advance(item, std::min(offset, selection->m_Items.size()));
    for (; item != selection->m_Items.end() && result.size() < limit; ++item)
        result.push_back({*item, selection->m_Column + 1, m_View->GetModel()->GetRootModel()->GetIdentity()});
    return result;
}

std::optional<SemanticKey> TableWidget::GetSemanticGridItem(std::size_t row, std::size_t column) const
{
    const auto visible = GetColumnOrder(true);
    return row < static_cast<std::size_t>(GetRowCount()) && column < visible.size()
               ? std::optional<SemanticKey>(SemanticRowKey(row, visible[column] + 1))
               : std::nullopt;
}

std::optional<SemanticKey> TableWidget::GetSemanticFocus() const
{
    const auto cell = GetCurrentCell();
    if (cell.first < 0 || cell.second < 0)
        return {};
    return SemanticRowKey(static_cast<std::size_t>(cell.first), static_cast<std::uint64_t>(cell.second + 1));
}

} // namespace EStudioUI
