#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/TextBuffer.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
TreeWidget::TreeWidget() : ScrollWidget("treewidget")
{
}

TreeWidget::NodeId TreeWidget::AddItem(NodeId parent, std::string text)
{
    if (parent && !m_Nodes.contains(parent))
        throw std::out_of_range("Invalid tree parent ID.");
    const auto id = m_NextId++;
    m_Nodes.emplace(id, Node{parent, TextBuffer::Sanitize(text), {}, false});
    (parent ? m_Nodes.at(parent).m_Children : m_Roots).push_back(id);
    m_RowsDirty = true;
    Invalidate();
    return id;
}

void TreeWidget::RemoveItem(NodeId item)
{
    const auto parent = m_Nodes.at(item).m_Parent;
    std::erase(parent ? m_Nodes.at(parent).m_Children : m_Roots, item);
    std::vector<NodeId> pending{item};
    bool selectionRemoved = false;
    while (!pending.empty())
    {
        const auto id = pending.back();
        pending.pop_back();
        auto found = m_Nodes.find(id);
        pending.insert(pending.end(), found->second.m_Children.begin(), found->second.m_Children.end());
        selectionRemoved = selectionRemoved || m_Current == id;
        m_Nodes.erase(found);
    }
    m_RowsDirty = true;
    m_Hover = -1;
    Invalidate();
    if (selectionRemoved)
    {
        m_Current = 0;
        m_CurrentChanged.Emit(0);
    }
}

void TreeWidget::Clear()
{
    m_Nodes.clear();
    m_Roots.clear();
    m_RowsDirty = true;
    m_Hover = -1;
    const bool changed = m_Current != 0;
    m_Current = 0;
    SetScrollOffset({});
    Invalidate();
    if (changed)
        m_CurrentChanged.Emit(0);
}

void TreeWidget::SetItemText(NodeId item, std::string text)
{
    m_Nodes.at(item).m_Text = TextBuffer::Sanitize(text);
    RequestRepaint();
}

const std::string& TreeWidget::GetItemText(NodeId item) const
{
    return m_Nodes.at(item).m_Text;
}

void TreeWidget::SetExpanded(NodeId item, bool expanded)
{
    auto& node = m_Nodes.at(item);
    if (node.m_Expanded == expanded)
        return;
    node.m_Expanded = expanded;
    m_RowsDirty = true;
    m_Hover = -1;
    Invalidate();
    m_ExpandedChanged.Emit(item, expanded);
}

bool TreeWidget::IsExpanded(NodeId item) const
{
    return m_Nodes.at(item).m_Expanded;
}

void TreeWidget::EnsureRows() const
{
    if (!m_RowsDirty)
        return;
    m_Rows.clear();
    std::vector<Row> pending;
    for (auto it = m_Roots.rbegin(); it != m_Roots.rend(); ++it)
        pending.push_back({*it, 0});
    while (!pending.empty())
    {
        const auto row = pending.back();
        pending.pop_back();
        m_Rows.push_back(row);
        const auto& node = m_Nodes.at(row.m_Id);
        if (node.m_Expanded)
            for (auto it = node.m_Children.rbegin(); it != node.m_Children.rend(); ++it)
                pending.push_back({*it, row.m_Depth + 1});
    }
    m_RowsDirty = false;
}

void TreeWidget::SetCurrentItem(NodeId item)
{
    if (item)
    {
        for (auto parent = m_Nodes.at(item).m_Parent; parent; parent = m_Nodes.at(parent).m_Parent)
        {
            m_Nodes.at(parent).m_Expanded = true;
            m_RowsDirty = true;
        }
        EnsureRows();
        const auto found =
            std::find_if(m_Rows.begin(), m_Rows.end(), [&](const auto& row) { return row.m_Id == item; });
        EnsureVisible(
            {0, static_cast<float>(found - m_Rows.begin()) * GetStyle().m_RowHeight, 0, GetStyle().m_RowHeight});
    }
    if (m_Current == item)
        return;
    m_Current = item;
    Invalidate();
    m_CurrentChanged.Emit(item);
}

TreeWidget::NodeId TreeWidget::GetCurrentItem() const
{
    return m_Current;
}

std::size_t TreeWidget::GetVisibleRowCount() const
{
    EnsureRows();
    return m_Rows.size();
}

Signal<TreeWidget::NodeId>& TreeWidget::CurrentChanged()
{
    return m_CurrentChanged;
}

Signal<TreeWidget::NodeId, bool>& TreeWidget::ExpandedChanged()
{
    return m_ExpandedChanged;
}

Signal<TreeWidget::NodeId>& TreeWidget::Activated()
{
    return m_Activated;
}

bool TreeWidget::IsFocusable() const
{
    return true;
}

Point TreeWidget::Measure(FontFace&) const
{
    return {220, 180};
}

Point TreeWidget::GetVirtualSize() const
{
    EnsureRows();
    return {GetViewport().m_Width, static_cast<float>(m_Rows.size()) * GetStyle().m_RowHeight};
}

int TreeWidget::RowAt(Point position) const
{
    if (!GetViewport().Contains(position))
        return -1;
    EnsureRows();
    const auto row =
        static_cast<int>((position.m_Y - GetViewport().m_Y + GetScrollOffset().m_Y) / GetStyle().m_RowHeight);
    return row < static_cast<int>(m_Rows.size()) ? row : -1;
}

void TreeWidget::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    EnsureRows();
    const auto viewport = GetViewport();
    const auto clip = GetClip().Intersect(viewport);
    const auto& style = GetStyle();
    const auto offset = GetScrollOffset().m_Y;
    const int first = std::max(0, static_cast<int>(offset / style.m_RowHeight));
    const int last = std::min(static_cast<int>(m_Rows.size()),
                              static_cast<int>(std::ceil((offset + viewport.m_Height) / style.m_RowHeight)));
    for (int i = first; i < last; ++i)
    {
        const auto row = m_Rows[static_cast<std::size_t>(i)];
        const auto& node = m_Nodes.at(row.m_Id);
        Rect bounds{viewport.m_X, viewport.m_Y + static_cast<float>(i) * style.m_RowHeight - offset, viewport.m_Width,
                    style.m_RowHeight};
        auto background = row.m_Id == m_Current         ? style.m_SelectionBackground
                          : IsHovered() && i == m_Hover ? style.m_HoverBackground
                          : i % 2                       ? style.m_AlternateBackground
                                                        : Color{0, 0, 0, 0};
        background.m_A *= opacity;
        renderer.DrawSolid(bounds, background, clip);
        auto color = row.m_Id == m_Current ? style.m_SelectionColor : style.m_Color;
        color.m_A *= opacity;
        const float indent = static_cast<float>(row.m_Depth) * style.m_Indent;
        Rect indicator{bounds.m_X + indent, bounds.m_Y, 22, bounds.m_Height};
        if (!node.m_Children.empty())
            DrawRowText(renderer, font, node.m_Expanded ? "−" : "+", indicator, color, clip);
        bounds.m_X += indent + 22;
        bounds.m_Width = std::max(0.0f, bounds.m_Width - indent - 22);
        DrawRowText(renderer, font, node.m_Text, bounds, color, clip);
    }
    PaintScrollBars(renderer, opacity);
}

void TreeWidget::OnPointerDown(const PointerEvent& event)
{
    if (HandleScrollPointerDown(event))
        return;
    const int index = RowAt(event.m_Position);
    if (index < 0)
        return;
    const auto row = m_Rows[static_cast<std::size_t>(index)];
    const bool toggle =
        !m_Nodes.at(row.m_Id).m_Children.empty() &&
        (event.m_Position.m_X < GetViewport().m_X + static_cast<float>(row.m_Depth) * GetStyle().m_Indent + 22 ||
         event.m_Clicks == 2);
    const WidgetRef self(this);
    SetCurrentItem(row.m_Id);
    if (!self.Get() || !m_Nodes.contains(row.m_Id))
        return;
    if (toggle && m_Nodes.contains(row.m_Id))
        SetExpanded(row.m_Id, !IsExpanded(row.m_Id));
    else if (event.m_Clicks == 2)
        m_Activated.Emit(row.m_Id);
}

void TreeWidget::OnPointerMove(const PointerEvent& event)
{
    ScrollWidget::OnPointerMove(event);
    const int row = RowAt(event.m_Position);
    if (row != m_Hover)
    {
        m_Hover = row;
        RequestRepaint();
    }
}

void TreeWidget::OnKey(Key key, bool down, bool, Modifiers)
{
    if (!down)
        return;
    EnsureRows();
    if (m_Rows.empty())
        return;
    const auto found =
        std::find_if(m_Rows.begin(), m_Rows.end(), [&](const auto& row) { return row.m_Id == m_Current; });
    int index = found == m_Rows.end() ? 0 : static_cast<int>(found - m_Rows.begin());
    const auto id = m_Rows[static_cast<std::size_t>(index)].m_Id;
    const auto& node = m_Nodes.at(id);
    if (key == Key::Right)
    {
        if (!node.m_Children.empty() && !node.m_Expanded)
            SetExpanded(id, true);
        else if (!node.m_Children.empty())
            SetCurrentItem(node.m_Children.front());
        return;
    }
    if (key == Key::Left)
    {
        if (node.m_Expanded)
            SetExpanded(id, false);
        else if (node.m_Parent)
            SetCurrentItem(node.m_Parent);
        return;
    }
    if (key == Key::Enter)
    {
        m_Activated.Emit(id);
        return;
    }
    const int page = std::max(1, static_cast<int>(GetViewport().m_Height / GetStyle().m_RowHeight));
    if (key == Key::Down)
        index += found == m_Rows.end() ? 0 : 1;
    else if (key == Key::Up)
        --index;
    else if (key == Key::Home)
        index = 0;
    else if (key == Key::End)
        index = static_cast<int>(m_Rows.size()) - 1;
    else if (key == Key::PageDown)
        index += page;
    else if (key == Key::PageUp)
        index -= page;
    else
        return;
    SetCurrentItem(m_Rows[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(m_Rows.size()) - 1))].m_Id);
}
} // namespace EStudioUI
