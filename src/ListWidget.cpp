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
    RequestRepaint();
}

ItemId ListWidget::GetCurrentItemId() const
{
    VerifyAccess();
    return m_View ? m_View->GetSelectionModel()->GetCurrent() : 0;
}

void ListWidget::SetCurrentItemId(ItemId item)
{
    VerifyAccess();
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
    RequestRepaint();
    if (changed)
        m_CurrentChanged.Emit(next);
}

void ListWidget::ModelChanged(const ModelChange& change)
{
    if (!change.m_Reset && !change.m_Structure && change.m_Fields.empty())
        return;

    const auto lease = RetainOwner();
    m_Hover = m_Pressed = -1;
    if (change.m_Reset || change.m_Structure)
    {
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
    m_Items.push_back(TextBuffer::Sanitize(text));
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
    VerifyAccess();
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
} // namespace EStudioUI
