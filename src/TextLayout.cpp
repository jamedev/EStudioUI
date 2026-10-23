#include <EStudioUI/FontFace.h>
#include "FontResource.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace EStudioUI
{
std::size_t TextLayout::HitTest(float x) const
{
    return HitTestCaret(x).m_Offset;
}

TextCaret TextLayout::HitTestCaret(float x) const
{
    if (!std::isfinite(x))
        throw std::invalid_argument("Text hit coordinates must be finite.");
    if (m_VisualCarets.empty())
        return {};
    auto next = std::lower_bound(m_VisualCarets.begin(), m_VisualCarets.end(), x,
                                 [](const VisualCaret& stop, float value) { return stop.m_X < value; });
    if (next == m_VisualCarets.end())
        next = std::prev(m_VisualCarets.end());
    else if (next != m_VisualCarets.begin() && x - std::prev(next)->m_X < next->m_X - x)
        --next;
    const float edge = next->m_X;
    next = std::lower_bound(m_VisualCarets.begin(), next, edge,
                            [](const VisualCaret& stop, float value) { return stop.m_X < value; });
    const bool fromRight = edge == m_VisualCarets.front().m_X || (edge != m_VisualCarets.back().m_X && x > edge);
    // Different logical endpoints can occupy one bidi join. Follow the pointed grapheme's edge.
    const auto limit = std::min<std::size_t>(8, static_cast<std::size_t>(m_VisualCarets.end() - next));
    for (std::size_t i = 0; i < limit; ++i)
    {
        const auto& stop = next[static_cast<std::ptrdiff_t>(i)];
        if (stop.m_X != next->m_X)
            break;
        const auto span =
            stop.m_Caret.m_Trailing
                ? std::lower_bound(m_Spans.begin(), m_Spans.end(), stop.m_Caret.m_Offset,
                                   [](const TextSpan& value, std::size_t offset) { return value.m_Last < offset; })
                : std::lower_bound(m_Spans.begin(), m_Spans.end(), stop.m_Caret.m_Offset,
                                   [](const TextSpan& value, std::size_t offset) { return value.m_First < offset; });
        if (span != m_Spans.end() && span->m_Right > span->m_Left &&
            (stop.m_Caret.m_Trailing ? span->m_Last : span->m_First) == stop.m_Caret.m_Offset &&
            (span->m_RightToLeft == stop.m_Caret.m_Trailing) == fromRight)
            return stop.m_Caret;
    }
    return next->m_Caret;
}

float TextLayout::CaretPosition(std::size_t offset) const
{
    if (m_Carets.empty() || m_Offsets.empty())
        return 0;
    const auto end = std::upper_bound(m_Offsets.begin(), m_Offsets.end(), offset);
    const auto index = end == m_Offsets.begin() ? 0 : static_cast<std::size_t>(std::prev(end) - m_Offsets.begin());
    return m_Carets.at(index);
}

float TextLayout::CaretPosition(TextCaret caret) const
{
    if (caret.m_Trailing)
    {
        const auto span =
            std::lower_bound(m_Spans.begin(), m_Spans.end(), caret.m_Offset,
                             [](const TextSpan& value, std::size_t offset) { return value.m_Last < offset; });
        if (span != m_Spans.end() && span->m_Last == caret.m_Offset)
            return span->m_RightToLeft ? span->m_Left : span->m_Right;
    }
    return CaretPosition(caret.m_Offset);
}

TextCaret TextLayout::MoveCaret(TextCaret caret, int direction, bool word) const
{
    if (m_VisualCarets.empty() || !direction)
        return caret;
    const float x = CaretPosition(caret);
    auto found =
        std::lower_bound(m_VisualCarets.begin(), m_VisualCarets.end(), std::tuple{x, caret.m_Offset, false},
                         [](const VisualCaret& stop, const auto& key)
                         { return std::tuple{stop.m_X, stop.m_Caret.m_Offset, stop.m_Caret.m_Trailing} < key; });
    if (found == m_VisualCarets.end())
        found = std::prev(m_VisualCarets.end());
    auto index = static_cast<std::ptrdiff_t>(found - m_VisualCarets.begin());
    const auto last = static_cast<std::ptrdiff_t>(m_VisualCarets.size() - 1);
    do
    {
        const auto next = std::clamp(index + (direction < 0 ? -1 : 1), std::ptrdiff_t{0}, last);
        if (index == next)
            break;
        index = next;
    } while (word && !std::binary_search(m_WordOffsets.begin(), m_WordOffsets.end(),
                                         m_VisualCarets[static_cast<std::size_t>(index)].m_Caret.m_Offset));
    return m_VisualCarets[static_cast<std::size_t>(index)].m_Caret;
}

TextCaret TextLayout::EdgeCaret(bool end) const
{
    if (m_VisualCarets.empty())
        return {};
    return end != m_RightToLeft ? m_VisualCarets.back().m_Caret : m_VisualCarets.front().m_Caret;
}

std::vector<Rect> TextLayout::SelectionRectangles(std::size_t first, std::size_t last) const
{
    if (first > last || (!m_Offsets.empty() && last > m_Offsets.back()))
        throw std::out_of_range("Selection exceeds the shaped text range.");
    std::vector<Rect> result;
    for (const auto& span : m_Spans)
        if (span.m_First < last && span.m_Last > first && span.m_Right > span.m_Left)
            result.push_back({span.m_Left, 0, span.m_Right - span.m_Left, m_Ascender + m_Descender});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.m_X < b.m_X; });
    std::size_t count = 0;
    for (const auto& rectangle : result)
    {
        if (count && rectangle.m_X <= result[count - 1].m_X + result[count - 1].m_Width + 0.01f)
            result[count - 1].m_Width =
                std::max(result[count - 1].m_X + result[count - 1].m_Width, rectangle.m_X + rectangle.m_Width) -
                result[count - 1].m_X;
        else
            result[count++] = rectangle;
    }
    result.resize(count);
    return result;
}

std::uint64_t TextLayout::GetFontIdentity(std::uint32_t slot) const
{
    return m_Fonts.at(slot)->m_Id;
}

const GlyphGeometry& TextLayout::GetGlyph(const PositionedGlyph& glyph) const
{
    return m_Fonts.at(glyph.m_FontSlot)->Glyph(glyph.m_Id);
}

std::size_t TextLayout::StorageBytes() const
{
    return sizeof(TextLayout) + m_Glyphs.capacity() * sizeof(PositionedGlyph) +
           m_Offsets.capacity() * sizeof(std::size_t) + m_Carets.capacity() * sizeof(float) +
           m_VisualCarets.capacity() * sizeof(VisualCaret) + m_Spans.capacity() * sizeof(TextSpan) +
           m_WordOffsets.capacity() * sizeof(std::size_t) + m_Fonts.capacity() * sizeof(m_Fonts.front()) +
           m_Selection.m_Family.capacity() + m_Selection.m_Style.capacity() + m_Selection.m_Direction.capacity() +
           m_Selection.m_Language.capacity();
}
} // namespace EStudioUI
