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

void TreeWidget::SetModel(std::shared_ptr<ItemModel> model, std::size_t column)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!model || column >= model->GetSnapshot().GetColumns().size())
        throw std::invalid_argument("Tree model requires an existing display column.");
    const bool compatible = m_View && m_View->GetModel()->GetRootModel() == model->GetRootModel();
    auto selection = compatible ? m_View->GetSelectionModel() : std::shared_ptr<SelectionModel>{};
    auto view = std::make_unique<CollectionView>(
        *this, model,
        [](Widget& owner, const ModelChange& change) { static_cast<TreeWidget&>(owner).ModelChanged(change); },
        [](Widget& owner) { static_cast<TreeWidget&>(owner).SelectionChanged(); }, std::move(selection));
    ++m_SemanticStructureRevision;
    m_View = std::move(view);
    m_ModelColumn = column;
    m_Nodes.clear();
    m_Roots.clear();
    m_Rows.clear();
    m_RowsDirty = true;
    if (!compatible)
    {
        m_ExpandedItems.clear();
        SetScrollOffset({});
    }
    Invalidate(Invalidation::Content);
    SelectionChanged();
}

std::shared_ptr<ItemModel> TreeWidget::GetModel() const
{
    VerifyAccess();
    return m_View ? m_View->GetModel() : nullptr;
}

void TreeWidget::SetSelectionModel(std::shared_ptr<SelectionModel> selection)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!m_View)
        throw std::logic_error("Attach a shared tree model before assigning shared selection.");
    m_View->SetSelectionModel(std::move(selection));
    SelectionChanged();
}

std::shared_ptr<SelectionModel> TreeWidget::GetSelectionModel() const
{
    VerifyAccess();
    return m_View ? m_View->GetSelectionModel() : nullptr;
}

void TreeWidget::SetItemDelegate(std::shared_ptr<const ItemDelegate> delegate)
{
    VerifyStructuralMutation();
    m_Delegate = delegate ? std::move(delegate) : std::make_shared<const ItemDelegate>();
    NotifySemanticsChanged();
    RequestRepaint();
}

void TreeWidget::SelectionChanged()
{
    const auto next = m_View->GetSelectionModel()->GetCurrent();
    const bool changed = m_Current != next;
    m_Current = next;
    NotifySemanticsChanged();
    RequestRepaint();
    if (changed)
        m_CurrentChanged.Emit(next);
}

void TreeWidget::ModelChanged(const ModelChange& change)
{
    if (!change.m_Reset && !change.m_Structure && change.m_Fields.empty())
    {
        m_RowMap.UpdateSnapshot(change.m_After);
        NotifySemanticsChanged();
        return;
    }

    const auto lease = RetainOwner();
    if (change.m_Reset || change.m_Structure)
    {
        ++m_SemanticStructureRevision;
        const auto canonical = m_View->GetModel()->GetRootModel()->GetSnapshot();
        std::erase_if(m_ExpandedItems, [&](ItemId item) { return !canonical.Contains(item); });
        if (m_ModelColumn >= change.m_After.GetColumns().size())
            m_ModelColumn = 0;
        m_RowsDirty = true;
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
    }
    else
    {
        m_RowMap.UpdateSnapshot(change.m_After);
        RequestRepaint();
    }
    m_Hover = -1;
    m_Pressed = 0;
    SelectionChanged();
}

TreeWidget::Row TreeWidget::VisibleRow(std::size_t index) const
{
    EnsureRows();
    if (m_View)
    {
        const auto row = m_RowMap.GetRow(index);
        return {row.m_Item, row.m_Depth};
    }
    return m_Rows.at(index);
}

std::optional<std::size_t> TreeWidget::VisibleIndex(NodeId item) const
{
    EnsureRows();
    if (m_View)
        return m_RowMap.GetIndex(item);
    const auto found = m_SemanticVisibleIndices.find(item);
    return found == m_SemanticVisibleIndices.end() ? std::nullopt : std::optional<std::size_t>(found->second);
}

bool TreeWidget::ItemEnabled(NodeId item) const
{
    if (!m_View)
        return true;
    const auto enabled = m_View->GetModel()->GetSnapshot().GetValue(item, m_ModelColumn, ModelRole::Enabled);
    return !std::holds_alternative<bool>(enabled) || std::get<bool>(enabled);
}

bool TreeWidget::HasItem(NodeId item) const
{
    return m_View ? m_View->GetModel()->GetSnapshot().Contains(item) : m_Nodes.contains(item);
}

bool TreeWidget::HasChildren(NodeId item) const
{
    return m_View ? m_View->GetModel()->GetSnapshot().GetRowCount(item) != 0 : !m_Nodes.at(item).m_Children.empty();
}

TreeWidget::NodeId TreeWidget::ParentOf(NodeId item) const
{
    return m_View ? m_View->GetModel()->GetSnapshot().GetParent(item) : m_Nodes.at(item).m_Parent;
}

TreeWidget::NodeId TreeWidget::FirstChild(NodeId item) const
{
    return m_View ? m_View->GetModel()->GetSnapshot().GetItem(0, item) : m_Nodes.at(item).m_Children.front();
}

void TreeWidget::SelectItem(NodeId item, Modifiers modifiers)
{
    if (!m_View)
    {
        SetCurrentItem(item);
        return;
    }
    const auto data = m_View->GetModel()->GetSnapshot();
    const auto enabled = data.GetValue(item, m_ModelColumn, ModelRole::Enabled);
    if (std::holds_alternative<bool>(enabled) && !std::get<bool>(enabled))
        return;
    std::vector<ItemId> order;
    if (modifiers.m_Shift && m_View->GetSelectionModel()->GetMode() == SelectionMode::Multiple)
        for (std::size_t i = 0; i < GetVisibleRowCount(); ++i)
            order.push_back(VisibleRow(i).m_Id);
    m_View->Select(item, m_ModelColumn, modifiers, order);
}

TreeWidget::NodeId TreeWidget::AddItem(NodeId parent, std::string text)
{
    VerifyStructuralMutation();
    if (m_View)
    {
        const auto model = m_View->GetModel();
        if (model->GetSnapshot().GetColumns().size() != 1)
            throw std::logic_error(
                "AddItem on a shared tree requires one text field; use typed model publication for records.");
        ItemId item = 0;
        model->Restructure([&](ModelBuilder& builder) { item = builder.Add({TextBuffer::Sanitize(text)}, parent); });
        return item;
    }
    if (parent && !m_Nodes.contains(parent))
        throw std::out_of_range("Invalid tree parent ID.");
    const auto id = m_NextId++;
    m_Nodes.emplace(id, Node{parent, TextBuffer::Sanitize(text), {}, false});
    auto& siblings = parent ? m_Nodes.at(parent).m_Children : m_Roots;
    m_Nodes.at(id).m_Position = siblings.size();
    siblings.push_back(id);
    ++m_SemanticStructureRevision;
    m_RowsDirty = true;
    Invalidate(Invalidation::Content);
    return id;
}

void TreeWidget::RemoveItem(NodeId item)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (m_View)
    {
        const auto model = m_View->GetModel();
        model->Restructure([item](ModelBuilder& builder) { builder.Remove(item); });
        return;
    }
    const auto parent = m_Nodes.at(item).m_Parent;
    auto& siblings = parent ? m_Nodes.at(parent).m_Children : m_Roots;
    std::erase(siblings, item);
    for (std::size_t index = 0; index < siblings.size(); ++index)
        m_Nodes.at(siblings[index]).m_Position = index;
    ++m_SemanticStructureRevision;
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
    if (m_View)
    {
        const auto model = m_View->GetModel();
        ModelBuilder empty(model->GetSnapshot().GetColumns());
        model->Replace(std::move(empty).Build());
        return;
    }
    ++m_SemanticStructureRevision;
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
    if (m_View)
    {
        const auto model = m_View->GetModel();
        model->SetText(item, m_ModelColumn, TextBuffer::Sanitize(text));
        return;
    }
    auto& caption = m_Nodes.at(item).m_Text;
    text = TextBuffer::Sanitize(text);
    if (caption == text)
        return;
    caption = std::move(text);
    NotifySemanticsChanged();
    RequestRepaint();
}

std::string TreeWidget::GetItemText(NodeId item) const
{
    VerifyAccess();
    if (m_View)
        return TextBuffer::Sanitize(m_Delegate->GetText(m_View->GetModel()->GetSnapshot(), item, m_ModelColumn));
    return m_Nodes.at(item).m_Text;
}

void TreeWidget::SetExpanded(NodeId item, bool expanded)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (m_View)
    {
        if (!m_View->GetModel()->GetRootModel()->GetSnapshot().Contains(item))
            throw std::out_of_range("Cannot expand a missing canonical tree item.");
        if (m_ExpandedItems.contains(item) == expanded)
            return;
        if (expanded)
            m_ExpandedItems.insert(item);
        else
            m_ExpandedItems.erase(item);
        ++m_SemanticStructureRevision;
        m_RowsDirty = true;
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
        m_ExpandedChanged.Emit(item, expanded);
        return;
    }
    auto& node = m_Nodes.at(item);
    if (node.m_Expanded == expanded)
        return;
    node.m_Expanded = expanded;
    ++m_SemanticStructureRevision;
    m_RowsDirty = true;
    m_Hover = -1;
    Invalidate(Invalidation::Content);
    m_ExpandedChanged.Emit(item, expanded);
}

bool TreeWidget::IsExpanded(NodeId item) const
{
    VerifyAccess();
    return m_View ? m_ExpandedItems.contains(item) : m_Nodes.at(item).m_Expanded;
}

void TreeWidget::EnsureRows() const
{
    if (!m_RowsDirty)
        return;
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        m_RowMap.Reset(data.GetColumns().empty() ? ModelSnapshot{} : data, m_ExpandedItems);
        m_RowsDirty = false;
        return;
    }
    m_Rows.clear();
    m_SemanticVisibleIndices.clear();
    std::vector<Row> pending;
    for (auto it = m_Roots.rbegin(); it != m_Roots.rend(); ++it)
        pending.push_back({*it, 0});
    while (!pending.empty())
    {
        const auto row = pending.back();
        pending.pop_back();
        m_SemanticVisibleIndices.emplace(row.m_Id, m_Rows.size());
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
    if (m_View)
    {
        const auto model = m_View->GetModel();
        const auto selection = m_View->GetSelectionModel();
        const auto data = model->GetSnapshot();
        if (data.Contains(item))
        {
            for (auto parent = data.GetParent(item); parent; parent = data.GetParent(parent))
                if (m_ExpandedItems.insert(parent).second)
                    ++m_SemanticStructureRevision;
            m_RowsDirty = true;
            if (const auto index = VisibleIndex(item))
                EnsureVisible({0, static_cast<float>(*index) * GetStyle().m_RowHeight, 0, GetStyle().m_RowHeight});
            Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
        }
        selection->Select(item, m_ModelColumn);
        return;
    }
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
    {
        ++m_SemanticStructureRevision;
        Invalidate(Invalidation::Content);
    }
    if (m_Current == item)
        return;
    m_Current = item;
    NotifySemanticsChanged();
    RequestRepaint();
    m_CurrentChanged.Emit(item);
}

TreeWidget::NodeId TreeWidget::GetCurrentItem() const
{
    VerifyAccess();
    return m_View ? m_View->GetSelectionModel()->GetCurrent() : m_Current;
}

std::size_t TreeWidget::GetVisibleRowCount() const
{
    VerifyAccess();
    EnsureRows();
    return m_View ? m_RowMap.GetCount() : m_Rows.size();
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
    return {GetViewport().m_Width, static_cast<float>(GetVisibleRowCount()) * GetStyle().m_RowHeight};
}

int TreeWidget::RowAt(Point position) const
{
    if (!GetViewport().Contains(position))
        return -1;
    EnsureRows();
    const auto row =
        static_cast<int>((position.m_Y - GetViewport().m_Y + GetScrollOffset().m_Y) / GetStyle().m_RowHeight);
    return row < static_cast<int>(GetVisibleRowCount()) ? row : -1;
}

unsigned TreeWidget::ItemState(int index, int hover) const
{
    const auto row = VisibleRow(static_cast<std::size_t>(index));
    const bool selected = m_View ? m_View->GetSelectionModel()->IsSelected(row.m_Id) : row.m_Id == m_Current;
    unsigned state = selected ? static_cast<unsigned>(Selected) : 0u;
    if (index % 2)
        state |= Alternate;
    if (IsExpanded(row.m_Id))
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
    const int last = std::min(static_cast<int>(GetVisibleRowCount()),
                              static_cast<int>(std::ceil((offset + viewport.m_Height) / style.m_RowHeight)));
    renderer.PushState();
    if (style.m_ClipContent)
        renderer.PushClip(GetBounds(), style.m_Radius);
    renderer.PushClip(clip);
    for (int i = first; i < last; ++i)
    {
        const auto row = VisibleRow(static_cast<std::size_t>(i));
        const Rect bounds{viewport.m_X, viewport.m_Y + static_cast<float>(i) * style.m_RowHeight - offset,
                          viewport.m_Width, style.m_RowHeight};
        const unsigned state = ItemState(i, m_Hover);
        const auto& cssItem = GetPartStyle("item", state);
        const auto data = m_View ? m_View->GetModel()->GetSnapshot() : ModelSnapshot{};
        const auto styled =
            m_View ? m_Delegate->GetStyle(cssItem, data, row.m_Id, m_ModelColumn, (state & Selected) != 0) : Style{};
        const auto& item = m_View ? styled : cssItem;
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
        if (!m_View || !m_Delegate->Paint(renderer, font, data, row.m_Id, m_ModelColumn, textBounds, item, clip,
                                          opacity * item.m_Opacity))
            DrawRowText(renderer, font, GetItemText(row.m_Id), textBounds, item, textBounds, opacity * item.m_Opacity);
        if (HasChildren(row.m_Id))
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
            DrawRowText(renderer, font, IsExpanded(row.m_Id) ? "−" : "+", indicator, branch, indicator, branchOpacity);
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
    const auto row = VisibleRow(static_cast<std::size_t>(index));
    if (!ItemEnabled(row.m_Id))
        return;
    const auto viewport = GetViewport();
    const Rect bounds{viewport.m_X,
                      viewport.m_Y + static_cast<float>(index) * GetStyle().m_RowHeight - GetScrollOffset().m_Y,
                      viewport.m_Width, GetStyle().m_RowHeight};
    m_Pressed = row.m_Id;
    m_PressedBranch =
        HasChildren(row.m_Id) && BranchBounds(row, bounds, ItemState(index, index)).Contains(event.m_Position);
    const bool toggle = HasChildren(row.m_Id) && (m_PressedBranch || event.m_Clicks == 2);
    SetState(Pressed, true);
    const WidgetRef self(this);
    SelectItem(row.m_Id, event.m_Modifiers);
    if (!self.Get() || !HasItem(row.m_Id))
        return;
    if (toggle && HasItem(row.m_Id))
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
        const auto row = VisibleRow(static_cast<std::size_t>(index));
        const auto viewport = GetViewport();
        const Rect bounds{viewport.m_X,
                          viewport.m_Y + static_cast<float>(index) * GetStyle().m_RowHeight - GetScrollOffset().m_Y,
                          viewport.m_Width, GetStyle().m_RowHeight};
        branch = HasChildren(row.m_Id) && BranchBounds(row, bounds, ItemState(index, index)).Contains(event.m_Position);
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

void TreeWidget::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || !GetVisibleRowCount())
        return;
    const auto currentIndex = VisibleIndex(m_Current);
    int index = currentIndex ? static_cast<int>(*currentIndex) : 0;
    const auto id = VisibleRow(static_cast<std::size_t>(index)).m_Id;
    if ((key == Key::Right || key == Key::Left || key == Key::Enter) && !ItemEnabled(id))
        return;
    if (key == Key::Right)
    {
        if (HasChildren(id) && !IsExpanded(id))
            SetExpanded(id, true);
        else if (HasChildren(id))
            SelectItem(FirstChild(id), modifiers);
        return;
    }
    if (key == Key::Left)
    {
        if (IsExpanded(id))
            SetExpanded(id, false);
        else if (ParentOf(id))
            SelectItem(ParentOf(id), modifiers);
        return;
    }
    if (key == Key::Enter)
    {
        m_Activated.Emit(id);
        return;
    }
    const int page = std::max(1, static_cast<int>(GetViewport().m_Height / GetStyle().m_RowHeight));
    if (key == Key::Down)
        index += currentIndex ? 1 : 0;
    else if (key == Key::Up)
        --index;
    else if (key == Key::Home)
        index = 0;
    else if (key == Key::End)
        index = static_cast<int>(GetVisibleRowCount()) - 1;
    else if (key == Key::PageDown)
        index += page;
    else if (key == Key::PageUp)
        index -= page;
    else
        return;
    index = std::clamp(index, 0, static_cast<int>(GetVisibleRowCount()) - 1);
    const auto item = VisibleRow(static_cast<std::size_t>(index)).m_Id;
    EnsureVisible({0, static_cast<float>(index) * GetStyle().m_RowHeight, 0, GetStyle().m_RowHeight});
    SelectItem(item, modifiers);
}

SemanticKey TreeWidget::SemanticItemKey(NodeId item) const
{
    return {item, 0, m_View ? m_View->GetModel()->GetRootModel()->GetIdentity() : m_SemanticGeneration};
}

bool TreeWidget::SemanticKeyValid(SemanticKey key) const
{
    if (!key.m_Item || key.m_Part || key.m_Generation != SemanticItemKey(key.m_Item).m_Generation)
        return false;
    return m_View ? m_View->GetModel()->GetRootModel()->GetSnapshot().Contains(key.m_Item)
                  : m_Nodes.contains(key.m_Item);
}

SemanticInfo TreeWidget::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_StructureRevision = m_SemanticStructureRevision;
    info.m_MultiSelectable = m_View && m_View->GetSelectionModel()->GetMode() == SelectionMode::Multiple;
    return info;
}

std::size_t TreeWidget::GetSemanticChildCount(SemanticKey parent) const
{
    if (parent.m_Item && (!SemanticKeyValid(parent) || !IsExpanded(parent.m_Item)))
        return 0;
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        if (parent.m_Item && !data.Contains(parent.m_Item))
            return 0;
        return data.GetRowCount(parent.m_Item);
    }
    return parent.m_Item ? m_Nodes.at(parent.m_Item).m_Children.size() : m_Roots.size();
}

std::optional<SemanticKey> TreeWidget::GetSemanticChildAt(SemanticKey parent, std::size_t index) const
{
    if (index >= GetSemanticChildCount(parent))
        return {};
    return SemanticItemKey(m_View          ? m_View->GetModel()->GetSnapshot().GetItem(index, parent.m_Item)
                           : parent.m_Item ? m_Nodes.at(parent.m_Item).m_Children[index]
                                           : m_Roots[index]);
}

std::optional<SemanticVirtualNode> TreeWidget::DescribeSemanticChild(SemanticKey key) const
{
    if (!SemanticKeyValid(key))
        return {};
    SemanticVirtualNode node;
    auto& info = node.m_Info;
    info.m_Role = SemanticRole::TreeItem;
    const auto canonical = m_View ? m_View->GetModel()->GetRootModel()->GetSnapshot() : ModelSnapshot{};
    const auto parent = m_View ? canonical.GetParent(key.m_Item) : m_Nodes.at(key.m_Item).m_Parent;
    if (parent)
        node.m_Parent = SemanticItemKey(parent);
    info.m_Name = m_View ? TextBuffer::Sanitize(m_Delegate->GetText(canonical, key.m_Item, m_ModelColumn))
                         : m_Nodes.at(key.m_Item).m_Text;
    info.m_Selected = m_View ? m_View->GetSelectionModel()->IsSelected(key.m_Item) : m_Current == key.m_Item;
    info.m_Expandable = m_View ? canonical.GetRowCount(key.m_Item) != 0 : !m_Nodes.at(key.m_Item).m_Children.empty();
    info.m_Expanded = info.m_Expandable && IsExpanded(key.m_Item);
    info.m_StructureRevision = m_SemanticStructureRevision;
    info.m_Actions = SemanticAction::Select | SemanticAction::AddToSelection | SemanticAction::RemoveFromSelection |
                     SemanticAction::ScrollIntoView | SemanticAction::Invoke;
    if (info.m_Expandable)
        info.m_Actions = info.m_Actions | SemanticAction::Expand | SemanticAction::Collapse;
    node.m_Focused = m_Current == key.m_Item;
    std::optional<std::size_t> row;
    if (!m_RowsDirty)
    {
        if (m_View)
            row = m_RowMap.GetIndex(key.m_Item);
        else if (const auto found = m_SemanticVisibleIndices.find(key.m_Item); found != m_SemanticVisibleIndices.end())
            row = found->second;
    }
    if (row)
    {
        const auto viewport = GetViewport();
        node.m_Bounds =
            Rect{viewport.m_X,
                 viewport.m_Y + static_cast<float>(*row) * GetStyle().m_RowHeight - GetStoredScrollOffset().m_Y,
                 viewport.m_Width, GetStyle().m_RowHeight}
                .Intersect(viewport);
    }
    if (m_View)
    {
        const auto enabled = canonical.GetValue(key.m_Item, m_ModelColumn, ModelRole::Enabled);
        node.m_Enabled = !std::holds_alternative<bool>(enabled) || std::get<bool>(enabled);
    }
    return node;
}

std::optional<std::size_t> TreeWidget::GetSemanticChildIndex(SemanticKey key) const
{
    if (!SemanticKeyValid(key))
        return {};
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        return data.Contains(key.m_Item) ? data.GetPosition(key.m_Item) : std::nullopt;
    }
    return m_Nodes.at(key.m_Item).m_Position;
}

SemanticStatus TreeWidget::OnSemanticChildAction(SemanticKey key, const SemanticCommand& command)
{
    if (!SemanticKeyValid(key))
        return SemanticStatus::Unavailable;
    const auto item = key.m_Item;
    if (command.m_Action == SemanticAction::Expand || command.m_Action == SemanticAction::Collapse)
    {
        SetExpanded(item, command.m_Action == SemanticAction::Expand);
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::ScrollIntoView)
    {
        if (m_View && !m_View->GetModel()->GetSnapshot().Contains(item))
            return SemanticStatus::Unavailable;
        for (auto parent = ParentOf(item); parent; parent = ParentOf(parent))
        {
            if (m_View)
                m_ExpandedItems.insert(parent);
            else
                m_Nodes.at(parent).m_Expanded = true;
        }
        ++m_SemanticStructureRevision;
        m_RowsDirty = true;
        EnsureRows();
        const auto row = VisibleIndex(item);
        if (!row)
            return SemanticStatus::Unavailable;
        EnsureVisible({0, static_cast<float>(*row) * GetStyle().m_RowHeight, 0, GetStyle().m_RowHeight});
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::Invoke)
    {
        m_Activated.Emit(item);
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::Select || command.m_Action == SemanticAction::Focus ||
        command.m_Action == SemanticAction::AddToSelection)
    {
        if (m_View)
        {
            const auto selection = m_View->GetSelectionModel();
            if (command.m_Action == SemanticAction::AddToSelection && selection->GetMode() == SelectionMode::Single &&
                !selection->GetSnapshot()->m_Items.empty() && !selection->IsSelected(item))
                return SemanticStatus::InvalidValue;
            selection->Select(item, m_ModelColumn,
                              command.m_Action == SemanticAction::AddToSelection ? SelectionAction::Add
                                                                                 : SelectionAction::Replace);
        }
        else
        {
            if (command.m_Action == SemanticAction::AddToSelection && m_Current && m_Current != item)
                return SemanticStatus::InvalidValue;
            SetCurrentItem(item);
        }
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::RemoveFromSelection)
    {
        if (m_View)
        {
            const auto selection = m_View->GetSelectionModel();
            if (selection->IsSelected(item))
                selection->Select(item, m_ModelColumn, SelectionAction::Toggle);
        }
        else if (m_Current == item)
            SetCurrentItem(0);
        return SemanticStatus::Succeeded;
    }
    return SemanticStatus::Unsupported;
}

std::optional<SemanticKey> TreeWidget::SemanticHitTest(Point point) const
{
    if (m_RowsDirty || !GetViewport().Contains(point))
        return {};
    const auto row = static_cast<std::size_t>(
        std::max(0.0f, point.m_Y - GetViewport().m_Y + GetStoredScrollOffset().m_Y) / GetStyle().m_RowHeight);
    if (m_View)
        return row < m_RowMap.GetCount() ? std::optional<SemanticKey>(SemanticItemKey(m_RowMap.GetRow(row).m_Item))
                                         : std::nullopt;
    return row < m_Rows.size() ? std::optional<SemanticKey>(SemanticItemKey(m_Rows[row].m_Id)) : std::nullopt;
}

std::size_t TreeWidget::GetSemanticSelectionCount() const
{
    return m_View ? m_View->GetSelectionModel()->GetSnapshot()->m_Items.size() : m_Current ? 1 : 0;
}

std::vector<SemanticKey> TreeWidget::GetSemanticSelection(std::size_t offset, std::size_t limit) const
{
    std::vector<SemanticKey> result;
    if (!m_View)
    {
        if (!offset && limit && m_Current)
            result.push_back(SemanticItemKey(m_Current));
        return result;
    }
    const auto selection = m_View->GetSelectionModel()->GetSnapshot();
    auto item = selection->m_Items.begin();
    std::advance(item, std::min(offset, selection->m_Items.size()));
    for (; item != selection->m_Items.end() && result.size() < limit; ++item)
        result.push_back(SemanticItemKey(*item));
    return result;
}

std::optional<SemanticKey> TreeWidget::GetSemanticFocus() const
{
    const auto item = GetCurrentItem();
    return item ? std::optional<SemanticKey>(SemanticItemKey(item)) : std::nullopt;
}

} // namespace EStudioUI
