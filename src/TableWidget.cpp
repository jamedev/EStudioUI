#include <EStudioUI/TableWidget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/TextBuffer.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace EStudioUI
{
TableWidget::TableWidget() : ScrollWidget("tablewidget")
{
    m_Editor = &Add<TextBox>();
    m_Editor->AddClass("cell-editor");
    m_Editor->SetVisible(false);
    m_Editor->SetFloatingBounds({});
    m_Editor->Submitted().Connect([this](const std::string&) { CommitEdit(); });
    m_Editor->FocusLost().Connect([this] { EndEdit(true, false); });
}

void TableWidget::SetColumns(std::vector<TableColumn> columns)
{
    for (auto& column : columns)
    {
        column.m_Size.Validate();
        column.m_Title = TextBuffer::Sanitize(column.m_Title);
    }
    EndEdit(false, false);
    m_Columns = std::move(columns);
    m_Widths.assign(m_Columns.size(), std::nullopt);
    m_Rows.clear();
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    m_ResizeColumn = -1;
    const bool changed = m_CurrentRow != -1;
    m_CurrentRow = m_CurrentColumn = -1;
    SetScrollOffset({});
    Invalidate();
    if (changed)
        m_CurrentCellChanged.Emit(-1, -1);
}

void TableWidget::SetRows(std::vector<std::vector<std::string>> rows)
{
    for (auto& row : rows)
    {
        if (row.size() != m_Columns.size())
            throw std::invalid_argument("Every table row must match the column count.");
        for (auto& cell : row)
            cell = TextBuffer::Sanitize(cell);
    }
    EndEdit(false, false);
    m_Rows = std::move(rows);
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    m_ResizeColumn = -1;
    const bool changed = m_CurrentRow != -1;
    m_CurrentRow = m_CurrentColumn = -1;
    SetScrollOffset({});
    Invalidate();
    if (changed)
        m_CurrentCellChanged.Emit(-1, -1);
}

int TableWidget::AddRow(std::vector<std::string> cells)
{
    if (cells.size() != m_Columns.size())
        throw std::invalid_argument("Every table row must match the column count.");
    for (auto& cell : cells)
        cell = TextBuffer::Sanitize(cell);
    m_Rows.push_back(std::move(cells));
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    Invalidate();
    return GetRowCount() - 1;
}

int TableWidget::GetRowCount() const
{
    return static_cast<int>(m_Rows.size());
}

int TableWidget::GetColumnCount() const
{
    return static_cast<int>(m_Columns.size());
}

const std::string& TableWidget::GetCell(int row, int column) const
{
    if (row < 0 || row >= GetRowCount() || column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table cell.");
    return m_Rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
}

void TableWidget::SetCell(int row, int column, std::string text)
{
    (void)GetCell(row, column);
    text = TextBuffer::Sanitize(text);
    auto& cell = m_Rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    if (cell == text)
        return;
    cell = std::move(text);
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    Invalidate();
    m_CellChanged.Emit(row, column);
}

void TableWidget::SetCurrentCell(int row, int column)
{
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
    if (m_CurrentRow == row && m_CurrentColumn == column)
        return;
    m_CurrentRow = row;
    m_CurrentColumn = column;
    RequestRepaint();
    m_CurrentCellChanged.Emit(row, column);
}

std::pair<int, int> TableWidget::GetCurrentCell() const
{
    return {m_CurrentRow, m_CurrentColumn};
}

void TableWidget::SortByColumn(int column, bool ascending)
{
    if (column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table sort column.");
    EndEdit(false, false);
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
    sorted.reserve(m_Rows.size());
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        if (static_cast<int>(order[i]) == previous)
            m_CurrentRow = static_cast<int>(i);
        sorted.push_back(std::move(m_Rows[order[i]]));
    }
    m_Rows = std::move(sorted);
    m_SortColumn = column;
    m_Ascending = ascending;
    Invalidate();
    if (previous != m_CurrentRow)
        m_CurrentCellChanged.Emit(m_CurrentRow, m_CurrentColumn);
}

void TableWidget::SetEditable(bool editable)
{
    m_Editable = editable;
    if (!editable)
        CancelEdit();
}

void TableWidget::SetSortingEnabled(bool enabled)
{
    m_SortingEnabled = enabled;
}

void TableWidget::SetColumnWidth(int column, float pixels)
{
    if (column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table column.");
    (void)SizeRule::Fixed(pixels);
    m_Widths[static_cast<std::size_t>(column)] = std::max(24.0f, pixels);
    Invalidate();
}

std::vector<float> TableWidget::GetColumnWidths() const
{
    if (m_ContentFontSize != GetStyle().m_FontSize || m_ContentWidths.size() != m_Columns.size())
    {
        m_ContentWidths.assign(m_Columns.size(), 0);
        for (std::size_t i = 0; i < m_Columns.size(); ++i)
        {
            if (m_Columns[i].m_Size.m_Mode != SizeRule::Mode::Content || !GetContext())
                continue;
            auto& font = GetContext()->GetFont();
            float width = font.Shape(m_Columns[i].m_Title, GetStyle().m_FontSize).m_Width + 32;
            for (const auto& row : m_Rows)
                width = std::max(width, font.Shape(row[i], GetStyle().m_FontSize).m_Width + 16);
            m_ContentWidths[i] = width;
        }
        m_ContentFontSize = GetStyle().m_FontSize;
    }
    std::vector<LayoutItem> items;
    for (std::size_t i = 0; i < m_Columns.size(); ++i)
    {
        const auto rule = m_Widths[i] ? SizeRule::Fixed(*m_Widths[i]) : m_Columns[i].m_Size;
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
    (void)GetCell(row, column);
    const auto widths = GetColumnWidths();
    const auto viewport = GetViewport();
    const auto offset = GetScrollOffset();
    float x = viewport.m_X - offset.m_X;
    for (int i = 0; i < column; ++i)
        x += widths[static_cast<std::size_t>(i)];
    return {x, viewport.m_Y + static_cast<float>(row) * GetStyle().m_RowHeight - offset.m_Y,
            widths[static_cast<std::size_t>(column)], GetStyle().m_RowHeight};
}

void TableWidget::BeginEdit(int row, int column)
{
    (void)GetCell(row, column);
    if (!m_Editable || !IsEnabled() || !GetContext())
        return;
    const WidgetRef self(this);
    SetCurrentCell(row, column);
    if (!self.Get() || row >= GetRowCount() || column >= GetColumnCount())
        return;
    m_EditRow = row;
    m_EditColumn = column;
    m_Editor->SetText(GetCell(row, column));
    if (!self.Get())
        return;
    m_Editor->SetVisible(true);
    m_Editor->SetFloatingBounds(GetCellBounds(row, column).Intersect(GetViewport()));
    GetContext()->Prepare(GetContext()->Root().GetBounds().m_Width, GetContext()->Root().GetBounds().m_Height);
    GetContext()->SetFocus(m_Editor);
    if (self.Get())
        m_Editor->SelectAll();
}

void TableWidget::EndEdit(bool commit, bool restoreFocus)
{
    if (m_EditRow < 0)
        return;
    const int row = m_EditRow;
    const int column = m_EditColumn;
    const std::string text = m_Editor->GetText();
    m_EditRow = m_EditColumn = -1;
    m_Editor->SetVisible(false);
    const WidgetRef self(this);
    if (restoreFocus && GetContext())
        GetContext()->SetFocus(this);
    if (!self.Get())
        return;
    Invalidate();
    if (commit && row < GetRowCount() && column < GetColumnCount())
        SetCell(row, column, text);
}

void TableWidget::CommitEdit()
{
    EndEdit(true, true);
}

void TableWidget::CancelEdit()
{
    EndEdit(false, true);
}

Signal<int, int>& TableWidget::CellChanged()
{
    return m_CellChanged;
}

Signal<int, int>& TableWidget::CurrentCellChanged()
{
    return m_CurrentCellChanged;
}

bool TableWidget::IsFocusable() const
{
    return true;
}

Rect TableWidget::GetViewport() const
{
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
            static_cast<float>(m_Rows.size()) * GetStyle().m_RowHeight};
}

int TableWidget::ColumnAt(Point point) const
{
    if (point.m_X < GetViewport().m_X || point.m_X >= GetViewport().m_X + GetViewport().m_Width)
        return -1;
    const auto widths = GetColumnWidths();
    float x = GetViewport().m_X - GetScrollOffset().m_X;
    for (std::size_t i = 0; i < widths.size(); ++i)
    {
        if (point.m_X >= x && point.m_X < x + widths[i])
            return static_cast<int>(i);
        x += widths[i];
    }
    return -1;
}

void TableWidget::OnLayout()
{
    if (m_EditRow >= 0)
        m_Editor->SetFloatingBounds(
            GetCellBounds(m_EditRow, m_EditColumn).Intersect(GetViewport()).Intersect(GetClip()));
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
    auto grid = style.m_GridColor;
    grid.m_A *= opacity;
    for (int row = first; row < last; ++row)
    {
        float x = viewport.m_X - offset.m_X;
        for (std::size_t column = 0; column < widths.size(); ++column)
        {
            const Rect cell{x, viewport.m_Y + static_cast<float>(row) * style.m_RowHeight - offset.m_Y, widths[column],
                            style.m_RowHeight};
            const bool selected = row == m_CurrentRow && static_cast<int>(column) == m_CurrentColumn;
            auto background = selected  ? style.m_SelectionBackground
                              : row % 2 ? style.m_AlternateBackground
                                        : Color{0, 0, 0, 0};
            background.m_A *= opacity;
            renderer.DrawSolid(cell, background, clip);
            auto color = selected ? style.m_SelectionColor : style.m_Color;
            color.m_A *= opacity;
            DrawRowText(renderer, font, m_Rows[static_cast<std::size_t>(row)][column], cell, color, clip);
            renderer.DrawSolid({cell.m_X + cell.m_Width - 1, cell.m_Y, 1, cell.m_Height}, grid, clip);
            renderer.DrawSolid({cell.m_X, cell.m_Y + cell.m_Height - 1, cell.m_Width, 1}, grid, clip);
            x += widths[column];
        }
    }
    const Rect header{viewport.m_X, GetContentBounds().m_Y, viewport.m_Width, style.m_RowHeight};
    const auto headerClip = GetClip().Intersect(header);
    auto background = style.m_HeaderBackground;
    background.m_A *= opacity;
    renderer.DrawSolid(header, background, headerClip);
    float x = header.m_X - offset.m_X;
    auto color = style.m_Color;
    color.m_A *= opacity;
    for (std::size_t i = 0; i < widths.size(); ++i)
    {
        const Rect cell{x, header.m_Y, widths[i], header.m_Height};
        const auto title =
            m_Columns[i].m_Title + (static_cast<int>(i) == m_SortColumn ? (m_Ascending ? " ↑" : " ↓") : "");
        DrawRowText(renderer, font, title, cell, color, headerClip);
        renderer.DrawSolid({x + widths[i] - 1, header.m_Y, 1, header.m_Height}, grid, headerClip);
        x += widths[i];
    }
    PaintScrollBars(renderer, opacity);
}

void TableWidget::OnPointerDown(const PointerEvent& event)
{
    if (HandleScrollPointerDown(event))
        return;
    const int column = ColumnAt(event.m_Position);
    if (column < 0)
        return;
    if (event.m_Position.m_Y >= GetContentBounds().m_Y && event.m_Position.m_Y < GetViewport().m_Y)
    {
        const auto widths = GetColumnWidths();
        float x = GetViewport().m_X - GetScrollOffset().m_X;
        for (std::size_t i = 0; i < widths.size(); ++i)
        {
            x += widths[i];
            if (std::abs(event.m_Position.m_X - x) <= 4)
            {
                m_ResizeColumn = static_cast<int>(i);
                m_ResizeOrigin = event.m_Position.m_X;
                m_ResizeWidth = widths[i];
                return;
            }
        }
        if (m_SortingEnabled)
            SortByColumn(column, m_SortColumn != column || !m_Ascending);
        return;
    }
    if (!GetViewport().Contains(event.m_Position))
        return;
    const int row =
        static_cast<int>((event.m_Position.m_Y - GetViewport().m_Y + GetScrollOffset().m_Y) / GetStyle().m_RowHeight);
    if (row >= GetRowCount())
        return;
    const WidgetRef self(this);
    SetCurrentCell(row, column);
    if (self.Get() && event.m_Clicks == 2 && row < GetRowCount() && column < GetColumnCount())
        BeginEdit(row, column);
}

void TableWidget::OnPointerMove(const PointerEvent& event)
{
    if (m_ResizeColumn >= 0)
        SetColumnWidth(m_ResizeColumn, std::max(24.0f, m_ResizeWidth + event.m_Position.m_X - m_ResizeOrigin));
    else
        ScrollWidget::OnPointerMove(event);
}

void TableWidget::OnPointerUp(const PointerEvent& event)
{
    m_ResizeColumn = -1;
    ScrollWidget::OnPointerUp(event);
}

void TableWidget::OnCaptureLost()
{
    m_ResizeColumn = -1;
    ScrollWidget::OnCaptureLost();
}

void TableWidget::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || m_Rows.empty() || m_Columns.empty())
        return;
    int row = std::max(0, m_CurrentRow);
    int column = std::max(0, m_CurrentColumn);
    if (key == Key::F2 || key == Key::Enter)
    {
        BeginEdit(row, column);
        return;
    }
    if (modifiers.m_Control && key == Key::C)
    {
        GetContext()->SetClipboardText(GetCell(row, column));
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
        column = GetColumnCount() - 1;
        if (modifiers.m_Control)
            row = GetRowCount() - 1;
    }
    else if (key == Key::PageDown)
        row += std::max(1, static_cast<int>(GetViewport().m_Height / GetStyle().m_RowHeight));
    else if (key == Key::PageUp)
        row -= std::max(1, static_cast<int>(GetViewport().m_Height / GetStyle().m_RowHeight));
    else
        return;
    SetCurrentCell(std::clamp(row, 0, GetRowCount() - 1), std::clamp(column, 0, GetColumnCount() - 1));
}

bool TableWidget::OnKeyPreview(Key key, bool down, bool, Modifiers)
{
    if (!down || m_EditRow < 0 || GetContext()->GetFocus() != m_Editor)
        return false;
    if (key == Key::Escape)
    {
        CancelEdit();
        return true;
    }
    if (key == Key::Tab)
    {
        CommitEdit();
        return false;
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
} // namespace EStudioUI
