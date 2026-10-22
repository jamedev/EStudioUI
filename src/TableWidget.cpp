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

void TableWidget::SetColumns(std::vector<TableColumn> columns)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
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
    m_HoverRow = m_HoverColumn = m_HoverHeader = -1;
    m_PressedRow = m_PressedColumn = m_PressedHeader = -1;
    SetState(Pressed, false);
    const bool changed = m_CurrentRow != -1;
    m_CurrentRow = m_CurrentColumn = -1;
    SetScrollOffset({});
    Invalidate();
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
    EndEdit(false, false);
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
    Invalidate();
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
    m_Rows.push_back(std::move(cells));
    m_ContentFontSize = -1;
    m_SortColumn = -1;
    Invalidate();
    return GetRowCount() - 1;
}

int TableWidget::GetRowCount() const
{
    VerifyAccess();
    return static_cast<int>(m_Rows.size());
}

int TableWidget::GetColumnCount() const
{
    VerifyAccess();
    return static_cast<int>(m_Columns.size());
}

const std::string& TableWidget::GetCell(int row, int column) const
{
    VerifyAccess();
    if (row < 0 || row >= GetRowCount() || column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table cell.");
    return m_Rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
}

void TableWidget::SetCell(int row, int column, std::string text)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
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
    VerifyAccess();
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
    if (m_CurrentRow == row && m_CurrentColumn == column)
        return;
    m_CurrentRow = row;
    m_CurrentColumn = column;
    RequestRepaint();
    m_CurrentCellChanged.Emit(row, column);
}

std::pair<int, int> TableWidget::GetCurrentCell() const
{
    VerifyAccess();
    return {m_CurrentRow, m_CurrentColumn};
}

void TableWidget::SortByColumn(int column, bool ascending)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
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
    VerifyAccess();
    m_Editable = editable;
    SetState(ReadOnly, !editable);
    if (!editable)
        CancelEdit();
}

void TableWidget::SetSortingEnabled(bool enabled)
{
    VerifyAccess();
    m_SortingEnabled = enabled;
}

void TableWidget::SetColumnWidth(int column, float pixels)
{
    VerifyAccess();
    if (column < 0 || column >= GetColumnCount())
        throw std::out_of_range("Invalid table column.");
    (void)SizeRule::Fixed(pixels);
    m_Widths[static_cast<std::size_t>(column)] = std::max(24.0f, pixels);
    Invalidate();
}

std::vector<float> TableWidget::GetColumnWidths() const
{
    VerifyAccess();
    const auto& cell = GetPartStyle("cell", 0);
    const auto& header = GetPartStyle("header", 0);
    const float cellInset = cell.m_Padding.m_Left + cell.m_Padding.m_Right + 2 * cell.m_BorderWidth;
    const float headerInset = header.m_Padding.m_Left + header.m_Padding.m_Right + 2 * header.m_BorderWidth;
    if (m_ContentFontSize != cell.m_FontSize || m_ContentHeaderFontSize != header.m_FontSize ||
        m_ContentCellInset != cellInset || m_ContentHeaderInset != headerInset ||
        m_ContentWidths.size() != m_Columns.size())
    {
        m_ContentWidths.assign(m_Columns.size(), 0);
        for (std::size_t i = 0; i < m_Columns.size(); ++i)
        {
            if (m_Columns[i].m_Size.m_Mode != SizeRule::Mode::Content || !GetContext())
                continue;
            auto& font = GetContext()->GetFont();
            const float sortWidth =
                std::max(font.Shape(" ↑", header.m_FontSize).m_Width, font.Shape(" ↓", header.m_FontSize).m_Width);
            float width = font.Shape(m_Columns[i].m_Title, header.m_FontSize).m_Width + headerInset + sortWidth;
            for (const auto& row : m_Rows)
                width = std::max(width, font.Shape(row[i], cell.m_FontSize).m_Width + cellInset);
            m_ContentWidths[i] = width;
        }
        m_ContentFontSize = cell.m_FontSize;
        m_ContentHeaderFontSize = header.m_FontSize;
        m_ContentCellInset = cellInset;
        m_ContentHeaderInset = headerInset;
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
    VerifyAccess();
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
    VerifyAccess();
    const auto ownerLease = RetainOwner();
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
    const auto ownerLease = RetainOwner();
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
        for (std::size_t column = 0; column < widths.size(); ++column)
        {
            const Rect cell{x, viewport.m_Y + static_cast<float>(row) * style.m_RowHeight - offset.m_Y, widths[column],
                            style.m_RowHeight};
            x += widths[column];
            if (cell.m_X >= viewport.m_X + viewport.m_Width)
                break;
            if (cell.m_X + cell.m_Width <= viewport.m_X)
                continue;
            unsigned state = row % 2 ? static_cast<unsigned>(Alternate) : 0u;
            if (row == m_CurrentRow && static_cast<int>(column) == m_CurrentColumn)
                state |= Selected;
            if (row == m_HoverRow && static_cast<int>(column) == m_HoverColumn)
                state |= Hover;
            if (row == m_PressedRow && static_cast<int>(column) == m_PressedColumn)
                state |= Pressed;
            const auto& part = GetPartStyle("cell", state);
            if (!part.m_Displayed || part.m_Opacity <= 0)
                continue;
            const auto identity = ((static_cast<std::uint64_t>(row) + 1) << 32) | (column + 1);
            renderer.PushState(part.m_Transform.Resolve(cell));
            renderer.SetWidgetPaint(cell, clip, nullptr, false, 0);
            renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Rows), identity);
            renderer.DrawBox(cell, part, clip, opacity * part.m_Opacity);
            if (part.m_ClipContent)
                renderer.PushClip(cell, part.m_Radius);
            DrawRowText(renderer, font, m_Rows[static_cast<std::size_t>(row)][column], cell, part, cell,
                        opacity * part.m_Opacity);
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
    for (std::size_t i = 0; i < widths.size(); ++i)
    {
        const Rect cell{x, header.m_Y, widths[i], header.m_Height};
        x += widths[i];
        if (cell.m_X >= header.m_X + header.m_Width)
            break;
        if (cell.m_X + cell.m_Width <= header.m_X)
            continue;
        unsigned state = static_cast<int>(i) == m_SortColumn ? static_cast<unsigned>(Sorted) : 0u;
        if (static_cast<int>(i) == m_CurrentColumn)
            state |= Selected;
        if (static_cast<int>(i) == m_HoverHeader)
            state |= Hover;
        if (static_cast<int>(i) == m_PressedHeader)
            state |= Pressed;
        const auto& part = GetPartStyle("header", state);
        if (!part.m_Displayed || part.m_Opacity <= 0)
            continue;
        const auto title = m_Columns[i].m_Title + (state & Sorted ? (m_Ascending ? " ↑" : " ↓") : "");
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
        SetState(Pressed, true);
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
    const int row = RowAt(event.m_Position);
    if (row < 0)
        return;
    m_PressedRow = row;
    m_PressedColumn = column;
    SetState(Pressed, true);
    const WidgetRef self(this);
    SetCurrentCell(row, column);
    if (self.Get() && event.m_Clicks == 2 && row < GetRowCount() && column < GetColumnCount())
        BeginEdit(row, column);
}

void TableWidget::OnPointerMove(const PointerEvent& event)
{
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
    if (m_ResizeColumn >= 0)
        SetColumnWidth(m_ResizeColumn, std::max(24.0f, m_ResizeWidth + event.m_Position.m_X - m_ResizeOrigin));
    else
        ScrollWidget::OnPointerMove(event);
}

void TableWidget::OnPointerUp(const PointerEvent& event)
{
    m_ResizeColumn = -1;
    m_PressedRow = m_PressedColumn = m_PressedHeader = -1;
    SetState(Pressed, false);
    ScrollWidget::OnPointerUp(event);
}

void TableWidget::OnCaptureLost()
{
    m_ResizeColumn = -1;
    m_PressedRow = m_PressedColumn = m_PressedHeader = -1;
    SetState(Pressed, false);
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
