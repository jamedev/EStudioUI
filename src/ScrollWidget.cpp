#include <EStudioUI/ScrollWidget.h>
#include <EStudioUI/Renderer.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
ScrollWidget::ScrollWidget(std::string type) : Widget(std::move(type))
{
}

Rect ScrollWidget::GetViewport() const
{
    auto content = GetContentBounds();
    content.m_Width = std::max(0.0f, content.m_Width - 12);
    content.m_Height = std::max(0.0f, content.m_Height - 12);
    return content;
}

Point ScrollWidget::GetScrollOffset() const
{
    const auto size = GetVirtualSize();
    const auto viewport = GetViewport();
    return {std::clamp(m_Offset.m_X, 0.0f, std::max(0.0f, size.m_X - viewport.m_Width)),
            std::clamp(m_Offset.m_Y, 0.0f, std::max(0.0f, size.m_Y - viewport.m_Height))};
}

void ScrollWidget::SetScrollOffset(Point offset)
{
    if (!std::isfinite(offset.m_X) || !std::isfinite(offset.m_Y))
        throw std::invalid_argument("Scroll coordinates must be finite.");
    const auto previous = GetScrollOffset();
    m_Offset = offset;
    m_Offset = GetScrollOffset();
    if (m_Offset.m_X != previous.m_X || m_Offset.m_Y != previous.m_Y)
        Invalidate();
}

void ScrollWidget::EnsureVisible(Rect rectangle)
{
    auto offset = GetScrollOffset();
    const auto viewport = GetViewport();
    if (rectangle.m_X < offset.m_X)
        offset.m_X = rectangle.m_X;
    else if (rectangle.m_X + rectangle.m_Width > offset.m_X + viewport.m_Width)
        offset.m_X = rectangle.m_X + rectangle.m_Width - viewport.m_Width;
    if (rectangle.m_Y < offset.m_Y)
        offset.m_Y = rectangle.m_Y;
    else if (rectangle.m_Y + rectangle.m_Height > offset.m_Y + viewport.m_Height)
        offset.m_Y = rectangle.m_Y + rectangle.m_Height - viewport.m_Height;
    SetScrollOffset(offset);
}

void ScrollWidget::DrawRowText(Renderer& renderer, FontFace& font, std::string_view text, Rect rectangle, Color color,
                               Rect clip) const
{
    if (rectangle.Intersect(clip).m_Width <= 0 || rectangle.Intersect(clip).m_Height <= 0)
        return;
    if (m_CacheFontSize != GetStyle().m_FontSize || m_TextCache.size() >= 512)
    {
        m_TextCache.clear();
        m_CacheFontSize = GetStyle().m_FontSize;
    }
    const std::string key(text);
    auto found = m_TextCache.find(key);
    if (found == m_TextCache.end())
        found = m_TextCache.emplace(key, font.Shape(key, m_CacheFontSize)).first;
    const auto& run = found->second;
    renderer.DrawText(run,
                      {rectangle.m_X + 8,
                       rectangle.m_Y + (rectangle.m_Height - run.m_Ascender - run.m_Descender) * 0.5f + run.m_Ascender},
                      color, clip.Intersect(rectangle));
}

Rect ScrollWidget::ScrollThumb(bool horizontal) const
{
    const auto viewport = GetViewport();
    const auto size = GetVirtualSize();
    const auto offset = GetScrollOffset();
    const float length = horizontal ? viewport.m_Width : viewport.m_Height;
    const float total = horizontal ? size.m_X : size.m_Y;
    if (length <= 0 || total <= length)
        return {};
    const float thumb = std::min(length, std::max(20.0f, length * length / total));
    const float position = (horizontal ? offset.m_X : offset.m_Y) * (length - thumb) / (total - length);
    return horizontal ? Rect{viewport.m_X + position, viewport.m_Y + viewport.m_Height + 3, thumb, 6}
                      : Rect{viewport.m_X + viewport.m_Width + 3, viewport.m_Y + position, 6, thumb};
}

void ScrollWidget::PaintScrollBars(Renderer& renderer, float opacity) const
{
    auto color = GetStyle().m_PlaceholderColor;
    color.m_A *= opacity * 0.8f;
    for (bool horizontal : {false, true})
    {
        Style style;
        style.m_Background = color;
        style.m_Radius = 3;
        renderer.DrawBox(ScrollThumb(horizontal), style, GetClip());
    }
}

bool ScrollWidget::HandleScrollPointerDown(const PointerEvent& event)
{
    const auto viewport = GetViewport();
    for (bool horizontal : {false, true})
    {
        auto thumb = ScrollThumb(horizontal);
        const Rect track = horizontal ? Rect{viewport.m_X, viewport.m_Y + viewport.m_Height, viewport.m_Width, 12}
                                      : Rect{viewport.m_X + viewport.m_Width, viewport.m_Y, 12, viewport.m_Height};
        if (!track.Contains(event.m_Position) || thumb.m_Width <= 0 || thumb.m_Height <= 0)
            continue;
        auto offset = GetScrollOffset();
        if (!thumb.Contains(event.m_Position))
        {
            const auto size = GetVirtualSize();
            const float total = horizontal ? size.m_X : size.m_Y;
            const float length = horizontal ? viewport.m_Width : viewport.m_Height;
            const float thumbLength = horizontal ? thumb.m_Width : thumb.m_Height;
            const float pointer =
                horizontal ? event.m_Position.m_X - viewport.m_X : event.m_Position.m_Y - viewport.m_Y;
            const float value =
                length > thumbLength ? (pointer - thumbLength * 0.5f) * (total - length) / (length - thumbLength) : 0;
            if (horizontal)
                offset.m_X = value;
            else
                offset.m_Y = value;
            SetScrollOffset(offset);
        }
        m_DragAxis = horizontal ? 1 : 2;
        m_DragOrigin = horizontal ? event.m_Position.m_X : event.m_Position.m_Y;
        m_OffsetOrigin = horizontal ? GetScrollOffset().m_X : GetScrollOffset().m_Y;
        return true;
    }
    return false;
}

void ScrollWidget::OnPointerMove(const PointerEvent& event)
{
    if (!m_DragAxis)
        return;
    const bool horizontal = m_DragAxis == 1;
    const auto viewport = GetViewport();
    const auto size = GetVirtualSize();
    const auto thumb = ScrollThumb(horizontal);
    const float length = horizontal ? viewport.m_Width : viewport.m_Height;
    const float thumbLength = horizontal ? thumb.m_Width : thumb.m_Height;
    const float total = horizontal ? size.m_X : size.m_Y;
    const float distance = (horizontal ? event.m_Position.m_X : event.m_Position.m_Y) - m_DragOrigin;
    const float value = m_OffsetOrigin + distance * (total - length) / std::max(1.0f, length - thumbLength);
    auto offset = GetScrollOffset();
    if (horizontal)
        offset.m_X = value;
    else
        offset.m_Y = value;
    SetScrollOffset(offset);
}

void ScrollWidget::OnPointerUp(const PointerEvent&)
{
    m_DragAxis = 0;
}

void ScrollWidget::OnCaptureLost()
{
    m_DragAxis = 0;
}

bool ScrollWidget::OnScroll(Point delta, Modifiers modifiers)
{
    if (modifiers.m_Shift)
        std::swap(delta.m_X, delta.m_Y);
    const auto before = GetScrollOffset();
    SetScrollOffset(
        {before.m_X - delta.m_X * GetStyle().m_RowHeight * 3, before.m_Y - delta.m_Y * GetStyle().m_RowHeight * 3});
    const auto after = GetScrollOffset();
    return before.m_X != after.m_X || before.m_Y != after.m_Y;
}
} // namespace EStudioUI
