#include <EStudioUI/ScrollWidget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
constexpr std::size_t s_MaxTextEntries = 512;
constexpr std::size_t s_MaxTextBytes = 8 * 1024 * 1024;

float Dimension(const Length& length, float available, float fallback, float minimum, float maximum)
{
    minimum = std::max(0.0f, minimum);
    maximum = std::max(minimum, maximum);
    return std::min(available, std::clamp(length.Resolve(available, fallback), minimum, maximum));
}

Rect Inset(Rect bounds, const Style& style)
{
    const float border = std::max(0.0f, style.m_BorderWidth);
    const float left = std::min(bounds.m_Width, border + style.m_Padding.m_Left);
    const float top = std::min(bounds.m_Height, border + style.m_Padding.m_Top);
    return {bounds.m_X + left, bounds.m_Y + top,
            std::max(0.0f, bounds.m_Width - left - border - style.m_Padding.m_Right),
            std::max(0.0f, bounds.m_Height - top - border - style.m_Padding.m_Bottom)};
}
} // namespace

void ScrollWidget::OnFontsChanged() noexcept
{
    m_TextCache.clear();
    m_TextRecency.clear();
    m_TextScratch = {};
    m_TextBytes = 0;
}

ScrollWidget::ScrollWidget(std::string type) : Widget(std::move(type))
{
    RegisterStylePart("scroll-track", Horizontal | Vertical | Hover | Pressed,
                      [](const Style&, unsigned state)
                      {
                          Style style;
                          if (state & Horizontal)
                          {
                              style.m_Height = {Length::Unit::Pixels, 12};
                              style.m_Padding.m_Top = style.m_Padding.m_Bottom = 3;
                          }
                          else
                          {
                              style.m_Width = {Length::Unit::Pixels, 12};
                              style.m_Padding.m_Left = style.m_Padding.m_Right = 3;
                          }
                          return style;
                      });
    RegisterStylePart("scroll-thumb", Horizontal | Vertical | Hover | Pressed,
                      [](const Style& owner, unsigned state)
                      {
                          Style style;
                          style.m_Background = owner.m_PlaceholderColor;
                          style.m_Background.m_A *= 0.8f;
                          style.m_Radius = 3;
                          if (state & Horizontal)
                          {
                              style.m_Height = {Length::Unit::Pixels, 6};
                              style.m_MinWidth = 20;
                          }
                          else
                          {
                              style.m_Width = {Length::Unit::Pixels, 6};
                              style.m_MinHeight = 20;
                          }
                          return style;
                      });
}

float ScrollWidget::ScrollGutter(bool horizontal) const
{
    const auto& style = GetPartStyle("scroll-track", ScrollPartState(horizontal, false));
    if (!style.m_Displayed)
        return 0;
    const auto content = GetContentBounds();
    return horizontal ? Dimension(style.m_Height, content.m_Height, 12, style.m_MinHeight, style.m_MaxHeight)
                      : Dimension(style.m_Width, content.m_Width, 12, style.m_MinWidth, style.m_MaxWidth);
}

Rect ScrollWidget::GetViewport() const
{
    VerifyAccess();
    auto content = GetContentBounds();
    content.m_Width = std::max(0.0f, content.m_Width - ScrollGutter(false));
    content.m_Height = std::max(0.0f, content.m_Height - ScrollGutter(true));
    return content;
}

Point ScrollWidget::GetScrollOffset() const
{
    VerifyAccess();
    const auto size = GetVirtualSize();
    const auto viewport = GetViewport();
    return {std::clamp(m_Offset.m_X, 0.0f, std::max(0.0f, size.m_X - viewport.m_Width)),
            std::clamp(m_Offset.m_Y, 0.0f, std::max(0.0f, size.m_Y - viewport.m_Height))};
}

Point ScrollWidget::GetStoredScrollOffset() const
{
    VerifyAccess();
    return m_Offset;
}

void ScrollWidget::SetScrollOffset(Point offset)
{
    VerifyStructuralMutation();
    if (!std::isfinite(offset.m_X) || !std::isfinite(offset.m_Y))
        throw std::invalid_argument("Scroll coordinates must be finite.");
    const auto size = GetVirtualSize();
    const auto viewport = GetViewport();
    offset.m_X = std::clamp(offset.m_X, 0.0f, std::max(0.0f, size.m_X - viewport.m_Width));
    offset.m_Y = std::clamp(offset.m_Y, 0.0f, std::max(0.0f, size.m_Y - viewport.m_Height));
    if (m_Offset.m_X == offset.m_X && m_Offset.m_Y == offset.m_Y)
        return;
    const Point previous{std::clamp(m_Offset.m_X, 0.0f, std::max(0.0f, size.m_X - viewport.m_Width)),
                         std::clamp(m_Offset.m_Y, 0.0f, std::max(0.0f, size.m_Y - viewport.m_Height))};
    m_Offset = offset;
    if (m_Offset.m_X != previous.m_X || m_Offset.m_Y != previous.m_Y)
    {
        RefreshScrollHover();
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
    }
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

std::size_t ScrollWidget::TextHash::operator()(const TextKey& key) const
{
    return (*this)(TextLookup{key.m_Text, key.m_Font, key.m_FontSize, key.m_Identity, key.m_Revision, key.m_Family,
                              key.m_Weight, key.m_Style, key.m_Direction, key.m_Language});
}

std::size_t ScrollWidget::TextHash::operator()(const TextLookup& key) const
{
    auto hash = std::hash<std::string_view>{}(key.m_Text);
    hash ^= std::hash<const FontFace*>{}(key.m_Font) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
    hash ^= std::hash<float>{}(key.m_FontSize) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
    const auto add = [&](std::size_t value) { hash ^= value + 0x9e3779b9U + (hash << 6) + (hash >> 2); };
    add(std::hash<std::uint64_t>{}(key.m_Identity));
    add(std::hash<std::uint64_t>{}(key.m_Revision));
    add(std::hash<std::string_view>{}(key.m_Family));
    add(std::hash<float>{}(key.m_Weight));
    add(std::hash<std::string_view>{}(key.m_Style));
    add(std::hash<std::string_view>{}(key.m_Direction));
    add(std::hash<std::string_view>{}(key.m_Language));
    return hash;
}

bool ScrollWidget::TextKey::operator==(const TextLookup& other) const
{
    return m_Text == other.m_Text && m_Font == other.m_Font && m_FontSize == other.m_FontSize &&
           m_Identity == other.m_Identity && m_Revision == other.m_Revision && m_Family == other.m_Family &&
           m_Weight == other.m_Weight && m_Style == other.m_Style && m_Direction == other.m_Direction &&
           m_Language == other.m_Language;
}

const TextLayout& ScrollWidget::TextFor(FontFace& font, std::string_view text, float size) const
{
    auto style = GetStyle();
    style.m_FontSize = size;
    return TextFor(font, text, style);
}

const TextLayout& ScrollWidget::TextFor(FontFace& font, std::string_view text, const Style& style) const
{
    const float fontSize = style.m_FontSize;
    if (!std::isfinite(fontSize) || fontSize < 0)
        throw std::invalid_argument("Caption font size must be finite and nonnegative.");
    const TextLookup lookup{text,
                            &font,
                            fontSize,
                            font.GetIdentity(),
                            font.GetRevision(),
                            style.m_FontFamily,
                            style.m_FontWeight,
                            style.m_FontStyle,
                            style.m_TextDirection,
                            style.m_Language};
    if (auto found = m_TextCache.find(lookup); found != m_TextCache.end())
    {
        m_TextRecency.splice(m_TextRecency.begin(), m_TextRecency, found->second.m_Recency);
        return found->second.m_Layout;
    }
    const TextKey key{std::string(text),
                      &font,
                      fontSize,
                      lookup.m_Identity,
                      lookup.m_Revision,
                      std::string(lookup.m_Family),
                      lookup.m_Weight,
                      std::string(lookup.m_Style),
                      std::string(lookup.m_Direction),
                      std::string(lookup.m_Language)};
    auto layout = font.Shape(text, style);
    const auto bytes =
        2 * (key.m_Text.capacity() + sizeof(TextKey)) + sizeof(TextEntry) + 128 + layout.StorageBytes() +
        2 * (key.m_Family.capacity() + key.m_Style.capacity() + key.m_Direction.capacity() + key.m_Language.capacity());
    if (bytes > s_MaxTextBytes)
    {
        m_TextScratch = std::move(layout);
        return m_TextScratch;
    }
    while (!m_TextCache.empty() && (m_TextCache.size() >= s_MaxTextEntries || m_TextBytes + bytes > s_MaxTextBytes))
    {
        const auto& oldest = m_TextRecency.back();
        m_TextBytes -= m_TextCache.at(oldest).m_Bytes;
        m_TextCache.erase(oldest);
        m_TextRecency.pop_back();
    }
    m_TextRecency.push_front(key);
    try
    {
        auto [entry, inserted] = m_TextCache.emplace(key, TextEntry{std::move(layout), bytes, m_TextRecency.begin()});
        (void)inserted;
        m_TextBytes += bytes;
        return entry->second.m_Layout;
    }
    catch (...)
    {
        m_TextRecency.pop_front();
        throw;
    }
}

std::size_t ScrollWidget::GetTextCacheEntryCount() const
{
    VerifyAccess();
    return m_TextCache.size();
}

std::size_t ScrollWidget::GetTextCacheBytes() const
{
    VerifyAccess();
    return m_TextBytes;
}

void ScrollWidget::DrawRowText(Renderer& renderer, FontFace& font, std::string_view text, Rect rectangle, Color color,
                               Rect clip) const
{
    Style style;
    style.m_FontSize = GetStyle().m_FontSize;
    style.m_Color = color;
    style.m_Padding.m_Left = 8;
    DrawRowText(renderer, font, text, rectangle, style, clip);
}

void ScrollWidget::DrawRowText(Renderer& renderer, FontFace& font, std::string_view text, Rect rectangle,
                               const Style& style, Rect clip, float opacity) const
{
    if (!style.m_Displayed || style.m_Opacity <= 0)
        return;
    const auto content = Inset(rectangle, style);
    const auto textClip = clip.Intersect(content);
    if (textClip.m_Width <= 0 || textClip.m_Height <= 0)
        return;
    const auto& run = TextFor(font, text, style);
    float x = content.m_X;
    if (style.m_TextAlign == "center")
        x += (content.m_Width - run.m_Width) * 0.5f;
    else if (style.m_TextAlign == "right")
        x += content.m_Width - run.m_Width;
    auto color = style.m_Color;
    color.m_A *= opacity;
    renderer.DrawText(run,
                      {x, content.m_Y + (content.m_Height - run.m_Ascender - run.m_Descender) * 0.5f + run.m_Ascender},
                      color, textClip);
}

unsigned ScrollWidget::ScrollPartState(bool horizontal, bool thumb) const
{
    const int axis = horizontal ? 1 : 2;
    unsigned state = horizontal ? Horizontal : Vertical;
    const auto pointer = GetContext() ? GetContext()->GetPointerPosition() : m_HoverPointer;
    if (IsHovered() && pointer == m_HoverPointer && m_HoverAxis == axis && (!thumb || m_HoverThumb))
        state |= Hover;
    if (m_DragAxis == axis)
        state |= Pressed;
    return state;
}

Rect ScrollWidget::GetScrollTrackBounds(bool horizontal) const
{
    VerifyAccess();
    const auto viewport = GetViewport();
    const auto size = GetVirtualSize();
    const float length = horizontal ? viewport.m_Width : viewport.m_Height;
    const float total = horizontal ? size.m_X : size.m_Y;
    const float gutter = ScrollGutter(horizontal);
    if (length <= 0 || total <= length || gutter <= 0)
        return {};
    return horizontal ? Rect{viewport.m_X, viewport.m_Y + viewport.m_Height, viewport.m_Width, gutter}
                      : Rect{viewport.m_X + viewport.m_Width, viewport.m_Y, gutter, viewport.m_Height};
}

Rect ScrollWidget::ScrollTrackContent(bool horizontal) const
{
    return Inset(GetScrollTrackBounds(horizontal), GetPartStyle("scroll-track", ScrollPartState(horizontal, false)));
}

Rect ScrollWidget::GetScrollThumbBounds(bool horizontal) const
{
    VerifyAccess();
    const auto viewport = GetViewport();
    const auto track = ScrollTrackContent(horizontal);
    const auto& style = GetPartStyle("scroll-thumb", ScrollPartState(horizontal, true));
    if (!style.m_Displayed || track.m_Width <= 0 || track.m_Height <= 0)
        return {};
    const auto size = GetVirtualSize();
    const auto offset = GetScrollOffset();
    const float visible = horizontal ? viewport.m_Width : viewport.m_Height;
    const float length = horizontal ? track.m_Width : track.m_Height;
    const float total = horizontal ? size.m_X : size.m_Y;
    if (visible <= 0 || total <= visible)
        return {};
    const float idealLength = length * visible / total;
    const float thumbLength =
        horizontal ? Dimension(style.m_Width, length, idealLength, style.m_MinWidth, style.m_MaxWidth)
                   : Dimension(style.m_Height, length, idealLength, style.m_MinHeight, style.m_MaxHeight);
    const float cross =
        horizontal ? Dimension(style.m_Height, track.m_Height, track.m_Height, style.m_MinHeight, style.m_MaxHeight)
                   : Dimension(style.m_Width, track.m_Width, track.m_Width, style.m_MinWidth, style.m_MaxWidth);
    const float position = (horizontal ? offset.m_X : offset.m_Y) * (length - thumbLength) / (total - visible);
    return horizontal ? Rect{track.m_X + position, track.m_Y + (track.m_Height - cross) * 0.5f, thumbLength, cross}
                      : Rect{track.m_X + (track.m_Width - cross) * 0.5f, track.m_Y + position, cross, thumbLength};
}

void ScrollWidget::PaintScrollBars(Renderer& renderer, float opacity) const
{
    RefreshScrollHover();
    renderer.PushState();
    if (GetStyle().m_ClipContent)
        renderer.PushClip(GetBounds(), GetStyle().m_Radius);
    for (bool horizontal : {false, true})
    {
        for (bool thumb : {false, true})
        {
            const auto bounds = thumb ? GetScrollThumbBounds(horizontal) : GetScrollTrackBounds(horizontal);
            const auto& style =
                GetPartStyle(thumb ? "scroll-thumb" : "scroll-track", ScrollPartState(horizontal, thumb));
            if (!style.m_Displayed || style.m_Opacity <= 0 || bounds.m_Width <= 0 || bounds.m_Height <= 0)
                continue;
            renderer.PushState(style.m_Transform.Resolve(bounds));
            renderer.SetWidgetPaint(bounds, GetClip(), nullptr, false, 0);
            renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Offset),
                                      (horizontal ? 2U : 0U) + (thumb ? 1U : 0U));
            renderer.DrawBox(bounds, style, GetClip(), opacity * style.m_Opacity);
            renderer.PopState();
        }
    }
    renderer.PopState();
}

void ScrollWidget::UpdateScrollHover(const PointerEvent& event)
{
    if (RecomputeScrollHover(event.m_Position))
        RequestRepaint();
}

void ScrollWidget::RefreshScrollHover() const
{
    if (!IsHovered())
        return;
    if (const auto* context = GetContext())
        if (const auto pointer = context->GetPointerPosition())
            RecomputeScrollHover(context->ToLayoutPoint(*this, *pointer));
}

bool ScrollWidget::RecomputeScrollHover(Point position) const
{
    const auto oldAxis = m_HoverAxis;
    const auto oldThumb = m_HoverThumb;
    m_HoverAxis = 0;
    m_HoverThumb = false;
    m_HoverPointer = GetContext() ? GetContext()->GetPointerPosition() : std::optional<Point>(position);
    for (bool horizontal : {false, true})
    {
        const auto track = GetScrollTrackBounds(horizontal);
        if (track.m_Width > 0 && track.m_Height > 0 && track.Contains(position))
        {
            m_HoverAxis = horizontal ? 1 : 2;
            m_HoverThumb = GetScrollThumbBounds(horizontal).Contains(position);
            break;
        }
    }
    return oldAxis != m_HoverAxis || oldThumb != m_HoverThumb;
}

void ScrollWidget::OnPointerDown(const PointerEvent& event)
{
    HandleScrollPointerDown(event);
}

bool ScrollWidget::HandleScrollPointerDown(const PointerEvent& event)
{
    UpdateScrollHover(event);
    const auto viewport = GetViewport();
    for (bool horizontal : {false, true})
    {
        const auto thumb = GetScrollThumbBounds(horizontal);
        const auto track = GetScrollTrackBounds(horizontal);
        if (!track.Contains(event.m_Position) || thumb.m_Width <= 0 || thumb.m_Height <= 0)
            continue;
        auto offset = GetScrollOffset();
        if (!thumb.Contains(event.m_Position))
        {
            const auto size = GetVirtualSize();
            const auto interior = ScrollTrackContent(horizontal);
            const float total = horizontal ? size.m_X : size.m_Y;
            const float visible = horizontal ? viewport.m_Width : viewport.m_Height;
            const float length = horizontal ? interior.m_Width : interior.m_Height;
            const float thumbLength = horizontal ? thumb.m_Width : thumb.m_Height;
            const float pointer =
                horizontal ? event.m_Position.m_X - interior.m_X : event.m_Position.m_Y - interior.m_Y;
            const float value =
                length > thumbLength ? (pointer - thumbLength * 0.5f) * (total - visible) / (length - thumbLength) : 0;
            if (horizontal)
                offset.m_X = value;
            else
                offset.m_Y = value;
            SetScrollOffset(offset);
        }
        m_DragAxis = horizontal ? 1 : 2;
        m_DragOrigin = horizontal ? event.m_Position.m_X : event.m_Position.m_Y;
        m_OffsetOrigin = horizontal ? GetScrollOffset().m_X : GetScrollOffset().m_Y;
        UpdateScrollHover(event);
        RequestRepaint();
        return true;
    }
    return false;
}

void ScrollWidget::OnPointerMove(const PointerEvent& event)
{
    if (!m_DragAxis)
    {
        UpdateScrollHover(event);
        return;
    }
    const bool horizontal = m_DragAxis == 1;
    const auto viewport = GetViewport();
    const auto interior = ScrollTrackContent(horizontal);
    const auto size = GetVirtualSize();
    const auto thumb = GetScrollThumbBounds(horizontal);
    const float length = horizontal ? interior.m_Width : interior.m_Height;
    const float visible = horizontal ? viewport.m_Width : viewport.m_Height;
    const float thumbLength = horizontal ? thumb.m_Width : thumb.m_Height;
    const float total = horizontal ? size.m_X : size.m_Y;
    if (length <= thumbLength || total <= visible || thumb.m_Width <= 0 || thumb.m_Height <= 0)
    {
        m_DragAxis = 0;
        UpdateScrollHover(event);
        RequestRepaint();
        return;
    }
    const float distance = (horizontal ? event.m_Position.m_X : event.m_Position.m_Y) - m_DragOrigin;
    const float value = m_OffsetOrigin + distance * (total - visible) / (length - thumbLength);
    auto offset = GetScrollOffset();
    if (horizontal)
        offset.m_X = value;
    else
        offset.m_Y = value;
    SetScrollOffset(offset);
    UpdateScrollHover(event);
}

void ScrollWidget::OnPointerUp(const PointerEvent& event)
{
    m_DragAxis = 0;
    UpdateScrollHover(event);
    RequestRepaint();
}

void ScrollWidget::OnCaptureLost()
{
    m_DragAxis = 0;
    RequestRepaint();
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
