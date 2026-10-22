#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/TextBuffer.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
TreeWidget::TreeWidget() : ScrollWidget("treewidget")
{
    RegisterStylePart("item", Hover | Pressed | Selected | Alternate | Expanded,
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
                          return part;
                      });
    RegisterStylePart("branch", Hover | Pressed | Selected | Alternate | Expanded,
                      [](const Style& owner, unsigned state)
                      {
                          Style part;
                          part.m_Color = state & Selected ? owner.m_SelectionColor : owner.m_Color;
                          part.m_FontSize = owner.m_FontSize;
                          part.m_TextAlign = "center";
                          part.m_Width = {Length::Unit::Pixels, 22};
                          return part;
                      });
}

TreeWidget::NodeId TreeWidget::AddItem(NodeId parent, std::string text)
{
    VerifyStructuralMutation();
    if (parent && !m_Nodes.contains(parent))
        throw std::out_of_range("Invalid tree parent ID.");
    const auto id = m_NextId++;
    m_Nodes.emplace(id, Node{parent, TextBuffer::Sanitize(text), {}, false});
    (parent ? m_Nodes.at(parent).m_Children : m_Roots).push_back(id);
    m_RowsDirty = true;
    Invalidate(Invalidation::Content);
    return id;
}

void TreeWidget::RemoveItem(NodeId item)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
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
    m_Pressed = 0;
    m_PressedBranch = false;
    SetState(Pressed, false);
    Invalidate(Invalidation::Content);
    if (selectionRemoved)
    {
        m_Current = 0;
        m_CurrentChanged.Emit(0);
    }
}

void TreeWidget::Clear()
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    m_Nodes.clear();
    m_Roots.clear();
    m_RowsDirty = true;
    m_Hover = -1;
    m_Pressed = 0;
    m_PressedBranch = false;
    SetState(Pressed, false);
    const bool changed = m_Current != 0;
    m_Current = 0;
    SetScrollOffset({});
    Invalidate(Invalidation::Content);
    if (changed)
        m_CurrentChanged.Emit(0);
}

void TreeWidget::SetItemText(NodeId item, std::string text)
{
    VerifyStructuralMutation();
    auto& caption = m_Nodes.at(item).m_Text;
    text = TextBuffer::Sanitize(text);
    if (caption == text)
        return;
    caption = std::move(text);
    RequestRepaint();
}

const std::string& TreeWidget::GetItemText(NodeId item) const
{
    VerifyAccess();
    return m_Nodes.at(item).m_Text;
}

void TreeWidget::SetExpanded(NodeId item, bool expanded)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    auto& node = m_Nodes.at(item);
    if (node.m_Expanded == expanded)
        return;
    node.m_Expanded = expanded;
    m_RowsDirty = true;
    m_Hover = -1;
    Invalidate(Invalidation::Content);
    m_ExpandedChanged.Emit(item, expanded);
}

bool TreeWidget::IsExpanded(NodeId item) const
{
    VerifyAccess();
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
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    bool revealed = false;
    if (item)
    {
        for (auto parent = m_Nodes.at(item).m_Parent; parent; parent = m_Nodes.at(parent).m_Parent)
        {
            if (!m_Nodes.at(parent).m_Expanded)
            {
                m_Nodes.at(parent).m_Expanded = true;
                m_RowsDirty = true;
                revealed = true;
            }
        }
        EnsureRows();
        const auto found =
            std::find_if(m_Rows.begin(), m_Rows.end(), [&](const auto& row) { return row.m_Id == item; });
        EnsureVisible(
            {0, static_cast<float>(found - m_Rows.begin()) * GetStyle().m_RowHeight, 0, GetStyle().m_RowHeight});
    }
    if (revealed)
        Invalidate(Invalidation::Content);
    if (m_Current == item)
        return;
    m_Current = item;
    RequestRepaint();
    m_CurrentChanged.Emit(item);
}

TreeWidget::NodeId TreeWidget::GetCurrentItem() const
{
    VerifyAccess();
    return m_Current;
}

std::size_t TreeWidget::GetVisibleRowCount() const
{
    VerifyAccess();
    EnsureRows();
    return m_Rows.size();
}

Signal<TreeWidget::NodeId>& TreeWidget::CurrentChanged()
{
    VerifyAccess();
    return m_CurrentChanged;
}

Signal<TreeWidget::NodeId, bool>& TreeWidget::ExpandedChanged()
{
    VerifyAccess();
    return m_ExpandedChanged;
}

Signal<TreeWidget::NodeId>& TreeWidget::Activated()
{
    VerifyAccess();
    return m_Activated;
}

bool TreeWidget::IsFocusable() const
{
    VerifyAccess();
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

unsigned TreeWidget::ItemState(int index, int hover) const
{
    const auto& row = m_Rows[static_cast<std::size_t>(index)];
    unsigned state = row.m_Id == m_Current ? static_cast<unsigned>(Selected) : 0u;
    if (index % 2)
        state |= Alternate;
    if (m_Nodes.at(row.m_Id).m_Expanded)
        state |= Expanded;
    if (hover == index)
        state |= Hover;
    if (row.m_Id == m_Pressed)
        state |= Pressed;
    return state;
}

Rect TreeWidget::BranchBounds(const Row& row, Rect bounds, unsigned state) const
{
    // Hover/pressed styles decorate the allocated branch without moving its hit target.
    const auto& part = GetPartStyle("branch", state & (Selected | Alternate | Expanded));
    const float indent = static_cast<float>(row.m_Depth) * GetStyle().m_Indent;
    const float available = std::max(0.0f, bounds.m_Width - indent);
    const float width = std::clamp(part.m_Width.Resolve(available, 22), part.m_MinWidth, part.m_MaxWidth);
    return {bounds.m_X + indent, bounds.m_Y, std::min(width, available), bounds.m_Height};
}

void TreeWidget::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    EnsureRows();
    const auto viewport = GetViewport();
    const auto clip = GetClip().Intersect(viewport);
    const auto& style = GetStyle();
    const auto offset = GetScrollOffset().m_Y;
    const auto pointer = IsHovered() && GetContext() ? GetContext()->GetPointerPosition() : std::optional<Point>{};
    const Point position = pointer ? GetContext()->ToLayoutPoint(*this, *pointer) : m_HoverPosition;
    m_Hover = IsHovered() ? RowAt(position) : -1;
    m_HoverBranch = false;
    const int first = std::max(0, static_cast<int>(offset / style.m_RowHeight));
    const int last = std::min(static_cast<int>(m_Rows.size()),
                              static_cast<int>(std::ceil((offset + viewport.m_Height) / style.m_RowHeight)));
    renderer.PushState();
    if (style.m_ClipContent)
        renderer.PushClip(GetBounds(), style.m_Radius);
    renderer.PushClip(clip);
    for (int i = first; i < last; ++i)
    {
        const auto row = m_Rows[static_cast<std::size_t>(i)];
        const auto& node = m_Nodes.at(row.m_Id);
        const Rect bounds{viewport.m_X, viewport.m_Y + static_cast<float>(i) * style.m_RowHeight - offset,
                          viewport.m_Width, style.m_RowHeight};
        const unsigned state = ItemState(i, m_Hover);
        const auto& item = GetPartStyle("item", state);
        if (!item.m_Displayed || item.m_Opacity <= 0)
            continue;
        const auto indicator = BranchBounds(row, bounds, state);
        renderer.PushState(item.m_Transform.Resolve(bounds));
        renderer.SetWidgetPaint(bounds, clip, nullptr, false, 0);
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Nodes), row.m_Id);
        renderer.DrawBox(bounds, item, clip, opacity * item.m_Opacity);
        if (item.m_ClipContent)
            renderer.PushClip(bounds, item.m_Radius);
        const Rect textBounds{indicator.m_X + indicator.m_Width, bounds.m_Y,
                              std::max(0.0f, bounds.m_X + bounds.m_Width - indicator.m_X - indicator.m_Width),
                              bounds.m_Height};
        DrawRowText(renderer, font, node.m_Text, textBounds, item, textBounds, opacity * item.m_Opacity);
        if (!node.m_Children.empty())
        {
            unsigned branchState = state;
            if (!indicator.Contains(position))
                branchState &= ~Hover;
            if (branchState & Hover)
                m_HoverBranch = true;
            if (!m_PressedBranch)
                branchState &= ~Pressed;
            const auto& branch = GetPartStyle("branch", branchState);
            renderer.PushState(branch.m_Transform.Resolve(indicator));
            renderer.SetWidgetPaint(indicator, clip, nullptr, false, 0);
            renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Roots), row.m_Id);
            const float branchOpacity = opacity * item.m_Opacity * branch.m_Opacity;
            if (branch.m_Displayed && branch.m_Opacity > 0)
                renderer.DrawBox(indicator, branch, clip, branchOpacity);
            if (branch.m_ClipContent)
                renderer.PushClip(indicator, branch.m_Radius);
            DrawRowText(renderer, font, node.m_Expanded ? "−" : "+", indicator, branch, indicator, branchOpacity);
            renderer.PopState();
        }
        renderer.PopState();
    }
    renderer.PopState();
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
    const auto viewport = GetViewport();
    const Rect bounds{viewport.m_X,
                      viewport.m_Y + static_cast<float>(index) * GetStyle().m_RowHeight - GetScrollOffset().m_Y,
                      viewport.m_Width, GetStyle().m_RowHeight};
    m_Pressed = row.m_Id;
    m_PressedBranch = !m_Nodes.at(row.m_Id).m_Children.empty() &&
                      BranchBounds(row, bounds, ItemState(index, index)).Contains(event.m_Position);
    const bool toggle = !m_Nodes.at(row.m_Id).m_Children.empty() && (m_PressedBranch || event.m_Clicks == 2);
    SetState(Pressed, true);
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
    m_HoverPosition = event.m_Position;
    const int index = RowAt(event.m_Position);
    bool branch = false;
    if (index >= 0)
    {
        const auto& row = m_Rows[static_cast<std::size_t>(index)];
        const auto viewport = GetViewport();
        const Rect bounds{viewport.m_X,
                          viewport.m_Y + static_cast<float>(index) * GetStyle().m_RowHeight - GetScrollOffset().m_Y,
                          viewport.m_Width, GetStyle().m_RowHeight};
        branch = !m_Nodes.at(row.m_Id).m_Children.empty() &&
                 BranchBounds(row, bounds, ItemState(index, index)).Contains(event.m_Position);
    }
    if (index != m_Hover || branch != m_HoverBranch)
    {
        m_Hover = index;
        m_HoverBranch = branch;
        RequestRepaint();
    }
}

void TreeWidget::OnPointerUp(const PointerEvent& event)
{
    m_Pressed = 0;
    m_PressedBranch = false;
    SetState(Pressed, false);
    ScrollWidget::OnPointerUp(event);
}

void TreeWidget::OnCaptureLost()
{
    m_Pressed = 0;
    m_PressedBranch = false;
    SetState(Pressed, false);
    ScrollWidget::OnCaptureLost();
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
