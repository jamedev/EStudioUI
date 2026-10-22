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

void ListWidget::SetItems(std::vector<std::string> items)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    for (auto& text : items)
        text = TextBuffer::Sanitize(text);
    m_Items = std::move(items);
    const bool changed = m_Current != -1;
    m_Current = -1;
    m_Hover = -1;
    m_Pressed = -1;
    SetState(Pressed, false);
    SetScrollOffset({});
    Invalidate();
    if (changed)
        m_CurrentChanged.Emit(-1);
}

int ListWidget::AddItem(std::string text)
{
    VerifyStructuralMutation();
    m_Items.push_back(TextBuffer::Sanitize(text));
    Invalidate();
    return GetItemCount() - 1;
}

void ListWidget::RemoveItem(int index)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    (void)GetItem(index);
    m_Items.erase(m_Items.begin() + index);
    const int next = m_Current == index ? -1 : m_Current > index ? m_Current - 1 : m_Current;
    m_Hover = -1;
    m_Pressed = -1;
    SetState(Pressed, false);
    Invalidate();
    if (m_Current != next)
    {
        m_Current = next;
        m_CurrentChanged.Emit(next);
    }
}

const std::string& ListWidget::GetItem(int index) const
{
    VerifyAccess();
    if (index < 0 || index >= GetItemCount())
        throw std::out_of_range("Invalid list item index.");
    return m_Items[static_cast<std::size_t>(index)];
}

int ListWidget::GetItemCount() const
{
    VerifyAccess();
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
    if (m_Current == index)
        return;
    m_Current = index;
    RequestRepaint();
    m_CurrentChanged.Emit(index);
}

int ListWidget::GetCurrentIndex() const
{
    VerifyAccess();
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
    return {GetViewport().m_Width, static_cast<float>(m_Items.size()) * GetStyle().m_RowHeight};
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
    unsigned state = index == m_Current ? static_cast<unsigned>(Selected) : 0u;
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
        const auto& item = GetPartStyle("item", ItemState(i, m_Hover));
        if (!item.m_Displayed || item.m_Opacity <= 0)
            continue;
        renderer.PushState(item.m_Transform.Resolve(row));
        renderer.SetWidgetPaint(row, clip, nullptr, false, 0);
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Items), static_cast<std::uint64_t>(i) + 1);
        renderer.DrawBox(row, item, clip, opacity * item.m_Opacity);
        if (item.m_ClipContent)
            renderer.PushClip(row, item.m_Radius);
        DrawRowText(renderer, font, m_Items[static_cast<std::size_t>(i)], row, item, row, opacity * item.m_Opacity);
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
    if (index < 0)
        return;
    m_Pressed = index;
    SetState(Pressed, true);
    const WidgetRef self(this);
    SetCurrentIndex(index);
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
    if (!down || m_Items.empty())
        return;
    if (key == Key::Enter)
    {
        if (m_Current >= 0)
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
    SetCurrentIndex(std::clamp(index, 0, GetItemCount() - 1));
}

void ListWidget::OnText(std::string_view text)
{
    if (text.empty() || m_Items.empty())
        return;
    for (int step = 1; step <= GetItemCount(); ++step)
    {
        const int index = (std::max(-1, m_Current) + step) % GetItemCount();
        const auto& item = m_Items[static_cast<std::size_t>(index)];
        if (item.size() >= text.size() &&
            std::equal(text.begin(), text.end(), item.begin(),
                       [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }))
        {
            SetCurrentIndex(index);
            return;
        }
    }
}
} // namespace EStudioUI
