#include <EStudioUI/ListWidget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/TextBuffer.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <cctype>
#include <stdexcept>

namespace EStudioUI
{
ListWidget::ListWidget() : ScrollWidget("listwidget")
{
    RegisterStylePart("item", Hover | Pressed | Selected | Alternate,
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
}

void ListWidget::SetModel(std::shared_ptr<ItemModel> model, std::size_t column)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!model || column >= model->GetSnapshot().GetColumns().size())
        throw std::invalid_argument("List model requires an existing display column.");
    auto selection = m_View && m_View->GetModel()->GetRootModel() == model->GetRootModel()
                         ? m_View->GetSelectionModel()
                         : std::shared_ptr<SelectionModel>{};
    auto view = std::make_unique<CollectionView>(
        *this, model,
        [](Widget& owner, const ModelChange& change) { static_cast<ListWidget&>(owner).ModelChanged(change); },
        [](Widget& owner) { static_cast<ListWidget&>(owner).SelectionChanged(); }, std::move(selection));
    ++m_SemanticStructureRevision;
    m_View = std::move(view);
    m_ModelColumn = column;
    m_Items.clear();
    m_Hover = m_Pressed = -1;
    SetScrollOffset({});
    Invalidate(Invalidation::Content);
    SelectionChanged();
}

std::shared_ptr<ItemModel> ListWidget::GetModel() const
{
    VerifyAccess();
    return m_View ? m_View->GetModel() : nullptr;
}

void ListWidget::SetSelectionModel(std::shared_ptr<SelectionModel> selection)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!m_View)
        throw std::logic_error("Attach a shared list model before assigning shared selection.");
    m_View->SetSelectionModel(std::move(selection));
    SelectionChanged();
}

std::shared_ptr<SelectionModel> ListWidget::GetSelectionModel() const
{
    VerifyAccess();
    return m_View ? m_View->GetSelectionModel() : nullptr;
}

void ListWidget::SetItemDelegate(std::shared_ptr<const ItemDelegate> delegate)
{
    VerifyStructuralMutation();
    m_Delegate = delegate ? std::move(delegate) : std::make_shared<const ItemDelegate>();
    NotifySemanticsChanged();
    RequestRepaint();
}

ItemId ListWidget::GetCurrentItemId() const
{
    VerifyAccess();
    return m_View ? m_View->GetSelectionModel()->GetCurrent() : 0;
}

void ListWidget::SetCurrentItemId(ItemId item)
{
    VerifyStructuralMutation();
    if (!m_View)
        throw std::logic_error("Stable ID selection requires a shared list model.");
    const auto selection = m_View->GetSelectionModel();
    selection->Select(item, m_ModelColumn);
}

void ListWidget::SelectionChanged()
{
    const auto data = m_View->GetModel()->GetSnapshot();
    const auto item = m_View->GetSelectionModel()->GetCurrent();
    const auto position = data.Contains(item) && !data.GetParent(item) ? data.GetPosition(item) : std::nullopt;
    const int next = position ? static_cast<int>(*position) : -1;
    const bool changed = m_Current != next;
    m_Current = next;
    NotifySemanticsChanged();
    RequestRepaint();
    if (changed)
        m_CurrentChanged.Emit(next);
}

void ListWidget::ModelChanged(const ModelChange& change)
{
    if (!change.m_Reset && !change.m_Structure && change.m_Fields.empty())
    {
        // Filtered selected records still have queryable canonical semantic values.
        NotifySemanticsChanged();
        return;
    }

    const auto lease = RetainOwner();
    m_Hover = m_Pressed = -1;
    if (change.m_Reset || change.m_Structure)
    {
        ++m_SemanticStructureRevision;
        if (m_ModelColumn >= change.m_After.GetColumns().size())
            m_ModelColumn = 0;
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
    }
    else
        RequestRepaint();
    SelectionChanged();
}

void ListWidget::SelectIndex(int index, Modifiers modifiers)
{
    if (!m_View)
    {
        SetCurrentIndex(index);
        return;
    }
    const auto data = m_View->GetModel()->GetSnapshot();
    const auto id = data.GetItem(static_cast<std::size_t>(index));
    if (const auto enabled = data.GetValue(id, m_ModelColumn, ModelRole::Enabled);
        std::holds_alternative<bool>(enabled) && !std::get<bool>(enabled))
        return;
    std::vector<ItemId> order;
    if (modifiers.m_Shift && m_View->GetSelectionModel()->GetMode() == SelectionMode::Multiple)
        for (std::size_t row = 0; row < data.GetRowCount(); ++row)
            order.push_back(data.GetItem(row));
    EnsureVisible({0, static_cast<float>(index) * GetStyle().m_RowHeight, 0, GetStyle().m_RowHeight});
    m_View->Select(id, m_ModelColumn, modifiers, order);
}

void ListWidget::SetItems(std::vector<std::string> items)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    for (auto& text : items)
        text = TextBuffer::Sanitize(text);
    m_View.reset();
    std::vector<ItemId> semanticIds;
    std::unordered_map<ItemId, std::size_t> semanticIndices;
    semanticIds.reserve(items.size());
    semanticIndices.reserve(items.size());
    for (std::size_t row = 0; row < items.size(); ++row)
    {
        const auto id = AllocateItemId();
        semanticIds.push_back(id);
        semanticIndices.emplace(id, row);
    }
    m_SemanticGeneration = AllocateItemId();
    ++m_SemanticStructureRevision;
    m_SemanticRowIds = std::move(semanticIds);
    m_SemanticRowIndices = std::move(semanticIndices);
    m_Items = std::move(items);
    const bool changed = m_Current != -1;
    m_Current = -1;
    m_Hover = -1;
    m_Pressed = -1;
    SetState(Pressed, false);
    SetScrollOffset({});
    Invalidate(Invalidation::Content);
    if (changed)
        m_CurrentChanged.Emit(-1);
}

int ListWidget::AddItem(std::string text)
{
    VerifyStructuralMutation();
    if (m_View)
    {
        const auto model = m_View->GetModel();
        const auto data = model->GetSnapshot();
        if (data.GetColumns().size() != 1 || data.GetColumns()[0].m_ValueType != ModelValueType::Text)
            throw std::logic_error(
                "AddItem on a shared list requires a single text column; use model.Restructure for records.");
        const auto next = static_cast<int>(data.GetRowCount());
        model->Restructure([text = TextBuffer::Sanitize(text)](ModelBuilder& builder) { builder.Add({text}); });
        return next;
    }
    text = TextBuffer::Sanitize(text);
    if (m_Items.size() == m_Items.capacity())
        m_Items.reserve(std::max<std::size_t>(8, m_Items.size() * 2));
    if (m_SemanticRowIds.size() == m_SemanticRowIds.capacity())
        m_SemanticRowIds.reserve(std::max<std::size_t>(8, m_SemanticRowIds.size() * 2));
    const auto id = AllocateItemId();
    m_SemanticRowIndices.emplace(id, m_Items.size());
    m_SemanticRowIds.push_back(id);
    ++m_SemanticStructureRevision;
    m_Items.push_back(std::move(text));
    Invalidate(Invalidation::Content);
    return GetItemCount() - 1;
}

void ListWidget::RemoveItem(int index)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    (void)GetItem(index);
    if (m_View)
    {
        const auto model = m_View->GetModel();
        const auto item = model->GetSnapshot().GetItem(static_cast<std::size_t>(index));
        model->Restructure([item](ModelBuilder& builder) { builder.Remove(item); });
        return;
    }
    m_SemanticRowIndices.erase(m_SemanticRowIds[static_cast<std::size_t>(index)]);
    m_SemanticRowIds.erase(m_SemanticRowIds.begin() + index);
    for (std::size_t row = static_cast<std::size_t>(index); row < m_SemanticRowIds.size(); ++row)
        m_SemanticRowIndices[m_SemanticRowIds[row]] = row;
    ++m_SemanticStructureRevision;
    m_Items.erase(m_Items.begin() + index);
    const int next = m_Current == index ? -1 : m_Current > index ? m_Current - 1 : m_Current;
    m_Hover = -1;
    m_Pressed = -1;
    SetState(Pressed, false);
    Invalidate(Invalidation::Content);
    if (m_Current != next)
    {
        m_Current = next;
        m_CurrentChanged.Emit(next);
    }
}

std::string ListWidget::GetItem(int index) const
{
    VerifyAccess();
    if (index < 0 || index >= GetItemCount())
        throw std::out_of_range("Invalid list item index.");
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        return TextBuffer::Sanitize(
            m_Delegate->GetText(data, data.GetItem(static_cast<std::size_t>(index)), m_ModelColumn));
    }
    return m_Items[static_cast<std::size_t>(index)];
}

int ListWidget::GetItemCount() const
{
    VerifyAccess();
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        return data.GetColumns().empty() ? 0 : static_cast<int>(data.GetRowCount());
    }
    return static_cast<int>(m_Items.size());
}

void ListWidget::SetCurrentIndex(int index)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (index != -1)
        (void)GetItem(index);
    if (index >= 0)
        EnsureVisible({0, static_cast<float>(index) * GetStyle().m_RowHeight, 0, GetStyle().m_RowHeight});
    if (m_View)
    {
        const auto selection = m_View->GetSelectionModel();
        const auto data = m_View->GetModel()->GetSnapshot();
        selection->Select(index < 0 ? 0 : data.GetItem(static_cast<std::size_t>(index)), m_ModelColumn);
        return;
    }
    if (m_Current == index)
        return;
    m_Current = index;
    NotifySemanticsChanged();
    RequestRepaint();
    m_CurrentChanged.Emit(index);
}

int ListWidget::GetCurrentIndex() const
{
    VerifyAccess();
    if (m_View)
    {
        const auto data = m_View->GetModel()->GetSnapshot();
        const auto item = m_View->GetSelectionModel()->GetCurrent();
        const auto position = data.Contains(item) && !data.GetParent(item) ? data.GetPosition(item) : std::nullopt;
        return position ? static_cast<int>(*position) : -1;
    }
    return m_Current;
}

Signal<int>& ListWidget::CurrentChanged()
{
    VerifyAccess();
    return m_CurrentChanged;
}

Signal<int>& ListWidget::Activated()
{
    VerifyAccess();
    return m_Activated;
}

bool ListWidget::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Point ListWidget::Measure(FontFace&) const
{
    return {180, 160};
}

Point ListWidget::GetVirtualSize() const
{
    return {GetViewport().m_Width, static_cast<float>(GetItemCount()) * GetStyle().m_RowHeight};
}

bool ListWidget::ItemEnabled(int index) const
{
    if (!m_View)
        return true;
    const auto data = m_View->GetModel()->GetSnapshot();
    const auto enabled =
        data.GetValue(data.GetItem(static_cast<std::size_t>(index)), m_ModelColumn, ModelRole::Enabled);
    return !std::holds_alternative<bool>(enabled) || std::get<bool>(enabled);
}

int ListWidget::ItemAt(Point position) const
{
    const auto viewport = GetViewport();
    if (!viewport.Contains(position))
        return -1;
    const int index = static_cast<int>((position.m_Y - viewport.m_Y + GetScrollOffset().m_Y) / GetStyle().m_RowHeight);
    return index < GetItemCount() ? index : -1;
}

unsigned ListWidget::ItemState(int index, int hover) const
{
    const bool selected = m_View ? m_View->GetSelectionModel()->IsSelected(
                                       m_View->GetModel()->GetSnapshot().GetItem(static_cast<std::size_t>(index)))
                                 : index == m_Current;
    unsigned state = selected ? static_cast<unsigned>(Selected) : 0u;
    if (index % 2)
        state |= Alternate;
    if (hover == index)
        state |= Hover;
    if (index == m_Pressed)
        state |= Pressed;
    return state;
}

void ListWidget::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto viewport = GetViewport();
    const auto clip = GetClip().Intersect(viewport);
    const auto& style = GetStyle();
    const float offset = GetScrollOffset().m_Y;
    const auto pointer = IsHovered() && GetContext() ? GetContext()->GetPointerPosition() : std::optional<Point>{};
    const Point position = pointer ? GetContext()->ToLayoutPoint(*this, *pointer) : m_HoverPosition;
    m_Hover = IsHovered() ? ItemAt(position) : -1;
    const int first = std::max(0, static_cast<int>(offset / style.m_RowHeight));
    const int last =
        std::min(GetItemCount(), static_cast<int>(std::ceil((offset + viewport.m_Height) / style.m_RowHeight)));
    renderer.PushState();
    if (style.m_ClipContent)
        renderer.PushClip(GetBounds(), style.m_Radius);
    renderer.PushClip(clip);
    for (int i = first; i < last; ++i)
    {
        const Rect row{viewport.m_X, viewport.m_Y + static_cast<float>(i) * style.m_RowHeight - offset,
                       viewport.m_Width, style.m_RowHeight};
        const auto state = ItemState(i, m_Hover);
        const auto& cssItem = GetPartStyle("item", state);
        const auto data = m_View ? m_View->GetModel()->GetSnapshot() : ModelSnapshot{};
        const auto id = m_View ? data.GetItem(static_cast<std::size_t>(i)) : 0;
        const auto styled =
            m_View ? m_Delegate->GetStyle(cssItem, data, id, m_ModelColumn, (state & Selected) != 0) : Style{};
        const auto& item = m_View ? styled : cssItem;
        if (!item.m_Displayed || item.m_Opacity <= 0)
            continue;
        renderer.PushState(item.m_Transform.Resolve(row));
        renderer.SetWidgetPaint(row, clip, nullptr, false, 0);
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Items),
                                  m_View ? m_View->GetModel()->GetSnapshot().GetItem(static_cast<std::size_t>(i))
                                         : static_cast<std::uint64_t>(i) + 1);
        renderer.DrawBox(row, item, clip, opacity * item.m_Opacity);
        if (item.m_ClipContent)
            renderer.PushClip(row, item.m_Radius);
        if (!m_View ||
            !m_Delegate->Paint(renderer, font, data, id, m_ModelColumn, row, item, clip, opacity * item.m_Opacity))
            DrawRowText(renderer, font, GetItem(i), row, item, row, opacity * item.m_Opacity);
        renderer.PopState();
    }
    renderer.PopState();
    PaintScrollBars(renderer, opacity);
}

void ListWidget::OnPointerDown(const PointerEvent& event)
{
    if (HandleScrollPointerDown(event))
        return;
    const int index = ItemAt(event.m_Position);
    if (index < 0 || !ItemEnabled(index))
        return;
    m_Pressed = index;
    SetState(Pressed, true);
    const WidgetRef self(this);
    SelectIndex(index, event.m_Modifiers);
    if (self.Get() && event.m_Clicks == 2)
        m_Activated.Emit(index);
}

void ListWidget::OnPointerMove(const PointerEvent& event)
{
    ScrollWidget::OnPointerMove(event);
    m_HoverPosition = event.m_Position;
    const int hover = ItemAt(event.m_Position);
    if (hover != m_Hover)
    {
        m_Hover = hover;
        RequestRepaint();
    }
}

void ListWidget::OnPointerUp(const PointerEvent& event)
{
    m_Pressed = -1;
    SetState(Pressed, false);
    ScrollWidget::OnPointerUp(event);
}

void ListWidget::OnCaptureLost()
{
    m_Pressed = -1;
    SetState(Pressed, false);
    ScrollWidget::OnCaptureLost();
}

void ListWidget::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || GetItemCount() == 0)
        return;
    if (key == Key::Enter)
    {
        if (m_Current >= 0 && ItemEnabled(m_Current))
            m_Activated.Emit(m_Current);
        return;
    }
    if (modifiers.m_Control && key == Key::C && m_Current >= 0)
    {
        GetContext()->SetClipboardText(GetItem(m_Current));
        return;
    }
    int index = m_Current < 0 ? 0 : m_Current;
    const int page = std::max(1, static_cast<int>(GetViewport().m_Height / GetStyle().m_RowHeight));
    if (key == Key::Down)
        index = m_Current < 0 ? 0 : index + 1;
    else if (key == Key::Up)
        index = m_Current < 0 ? GetItemCount() - 1 : index - 1;
    else if (key == Key::Home)
        index = 0;
    else if (key == Key::End)
        index = GetItemCount() - 1;
    else if (key == Key::PageDown)
        index += page;
    else if (key == Key::PageUp)
        index -= page;
    else
        return;
    SelectIndex(std::clamp(index, 0, GetItemCount() - 1), modifiers);
}

void ListWidget::OnText(std::string_view text)
{
    if (text.empty() || GetItemCount() == 0)
        return;
    for (int step = 1; step <= GetItemCount(); ++step)
    {
        const int index = (std::max(-1, m_Current) + step) % GetItemCount();
        const auto item = GetItem(index);
        if (ItemEnabled(index) && item.size() >= text.size() &&
            std::equal(text.begin(), text.end(), item.begin(),
                       [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }))
        {
            SelectIndex(index, {});
            return;
        }
    }
}

SemanticKey ListWidget::SemanticRowKey(std::size_t row, std::uint64_t part) const
{
    if (m_View)
        return {m_View->GetModel()->GetSnapshot().GetItem(row), part,
                m_View->GetModel()->GetRootModel()->GetIdentity()};
    return {m_SemanticRowIds.at(row), part, m_SemanticGeneration};
}

std::optional<std::size_t> ListWidget::SemanticRowIndex(SemanticKey key) const
{
    if (!key.m_Item || key.m_Part)
        return {};
    if (m_View)
    {
        const auto model = m_View->GetModel();
        const auto data = model->GetSnapshot();
        if (key.m_Generation != model->GetRootModel()->GetIdentity() || !data.Contains(key.m_Item) ||
            data.GetParent(key.m_Item))
            return {};
        return data.GetPosition(key.m_Item);
    }
    const auto found = m_SemanticRowIndices.find(key.m_Item);
    return key.m_Generation == m_SemanticGeneration && found != m_SemanticRowIndices.end()
               ? std::optional<std::size_t>(found->second)
               : std::nullopt;
}

SemanticInfo ListWidget::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_StructureRevision = m_SemanticStructureRevision;
    info.m_MultiSelectable = m_View && m_View->GetSelectionModel()->GetMode() == SelectionMode::Multiple;
    return info;
}

std::size_t ListWidget::GetSemanticChildCount(SemanticKey parent) const
{
    return parent.m_Item ? 0 : static_cast<std::size_t>(GetItemCount());
}

std::optional<SemanticKey> ListWidget::GetSemanticChildAt(SemanticKey parent, std::size_t index) const
{
    return !parent.m_Item && index < static_cast<std::size_t>(GetItemCount())
               ? std::optional<SemanticKey>(SemanticRowKey(index))
               : std::nullopt;
}

std::optional<SemanticVirtualNode> ListWidget::DescribeSemanticChild(SemanticKey key) const
{
    const auto row = SemanticRowIndex(key);
    const auto canonical = m_View ? m_View->GetModel()->GetRootModel()->GetSnapshot() : ModelSnapshot{};
    if (!row && (!m_View || key.m_Part || key.m_Generation != m_View->GetModel()->GetRootModel()->GetIdentity() ||
                 !canonical.Contains(key.m_Item)))
        return {};
    SemanticVirtualNode node;
    auto& info = node.m_Info;
    info.m_Role = SemanticRole::ListItem;
    info.m_Name = row ? GetItem(static_cast<int>(*row))
                      : TextBuffer::Sanitize(m_Delegate->GetText(canonical, key.m_Item, m_ModelColumn));
    info.m_Selected =
        m_View ? m_View->GetSelectionModel()->IsSelected(key.m_Item) : row && static_cast<int>(*row) == m_Current;
    info.m_Actions = SemanticAction::Select | SemanticAction::AddToSelection | SemanticAction::RemoveFromSelection |
                     SemanticAction::ScrollIntoView | SemanticAction::Invoke;
    node.m_Focused = m_View ? m_View->GetSelectionModel()->GetCurrent() == key.m_Item : info.m_Selected;
    if (row)
    {
        const auto viewport = GetViewport();
        const auto offset = GetStoredScrollOffset();
        node.m_Bounds =
            Rect{viewport.m_X, viewport.m_Y + static_cast<float>(*row) * GetStyle().m_RowHeight - offset.m_Y,
                 viewport.m_Width, GetStyle().m_RowHeight}
                .Intersect(viewport);
        node.m_Enabled = ItemEnabled(static_cast<int>(*row));
    }
    else
    {
        const auto enabled = canonical.GetValue(key.m_Item, m_ModelColumn, ModelRole::Enabled);
        node.m_Enabled = !std::holds_alternative<bool>(enabled) || std::get<bool>(enabled);
    }
    return node;
}

std::optional<std::size_t> ListWidget::GetSemanticChildIndex(SemanticKey key) const
{
    return SemanticRowIndex(key);
}

SemanticStatus ListWidget::OnSemanticChildAction(SemanticKey key, const SemanticCommand& command)
{
    const auto row = SemanticRowIndex(key);
    if (command.m_Action == SemanticAction::ScrollIntoView)
    {
        if (!row)
            return SemanticStatus::Unavailable;
        EnsureVisible({0, static_cast<float>(*row) * GetStyle().m_RowHeight, 0, GetStyle().m_RowHeight});
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::Invoke)
    {
        if (!row)
            return SemanticStatus::Unavailable;
        m_Activated.Emit(static_cast<int>(*row));
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::Focus || command.m_Action == SemanticAction::Select ||
        command.m_Action == SemanticAction::AddToSelection)
    {
        if (m_View)
        {
            const auto selection = m_View->GetSelectionModel();
            if (command.m_Action == SemanticAction::AddToSelection && selection->GetMode() == SelectionMode::Single &&
                !selection->GetSnapshot()->m_Items.empty() && !selection->IsSelected(key.m_Item))
                return SemanticStatus::InvalidValue;
            selection->Select(key.m_Item, m_ModelColumn,
                              command.m_Action == SemanticAction::AddToSelection ? SelectionAction::Add
                                                                                 : SelectionAction::Replace);
        }
        else if (row)
        {
            if (command.m_Action == SemanticAction::AddToSelection && m_Current >= 0 &&
                static_cast<int>(*row) != m_Current)
                return SemanticStatus::InvalidValue;
            SetCurrentIndex(static_cast<int>(*row));
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
                selection->Select(key.m_Item, m_ModelColumn, SelectionAction::Toggle);
        }
        else if (row && static_cast<int>(*row) == m_Current)
            SetCurrentIndex(-1);
        return SemanticStatus::Succeeded;
    }
    return SemanticStatus::Unsupported;
}

std::optional<SemanticKey> ListWidget::SemanticHitTest(Point point) const
{
    const int row = ItemAt(point);
    return row >= 0 ? std::optional<SemanticKey>(SemanticRowKey(static_cast<std::size_t>(row))) : std::nullopt;
}

std::size_t ListWidget::GetSemanticSelectionCount() const
{
    return m_View ? m_View->GetSelectionModel()->GetSnapshot()->m_Items.size() : m_Current >= 0 ? 1 : 0;
}

std::vector<SemanticKey> ListWidget::GetSemanticSelection(std::size_t offset, std::size_t limit) const
{
    std::vector<SemanticKey> result;
    if (!m_View)
    {
        if (!offset && limit && m_Current >= 0)
            result.push_back(SemanticRowKey(static_cast<std::size_t>(m_Current)));
        return result;
    }
    const auto selection = m_View->GetSelectionModel()->GetSnapshot();
    auto item = selection->m_Items.begin();
    std::advance(item, std::min(offset, selection->m_Items.size()));
    for (; item != selection->m_Items.end() && result.size() < limit; ++item)
        result.push_back({*item, 0, m_View->GetModel()->GetRootModel()->GetIdentity()});
    return result;
}

std::optional<SemanticKey> ListWidget::GetSemanticFocus() const
{
    if (m_View)
    {
        const auto current = m_View->GetSelectionModel()->GetCurrent();
        return current ? std::optional<SemanticKey>({current, 0, m_View->GetModel()->GetRootModel()->GetIdentity()})
                       : std::nullopt;
    }
    return m_Current >= 0 ? std::optional<SemanticKey>(SemanticRowKey(static_cast<std::size_t>(m_Current)))
                          : std::nullopt;
}

} // namespace EStudioUI
