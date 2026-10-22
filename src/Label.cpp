#include <EStudioUI/Label.h>
#include <EStudioUI/TextBuffer.h>
#include <EStudioUI/Renderer.h>

#include <algorithm>

namespace EStudioUI
{
Label::Label(std::string text, std::string type) : Widget(std::move(type)), m_Text(std::move(text))
{
}

void Label::SetText(std::string text)
{
    VerifyAccess();
    if (text == m_Text)
        return;
    m_Text = std::move(text);
    m_LayoutSize = -1;
    Invalidate(Invalidation::Content);
}

const std::string& Label::GetText() const
{
    VerifyAccess();
    return m_Text;
}

void Label::EnsureLayout(FontFace& font) const
{
    if (m_LayoutSize == GetStyle().m_FontSize)
        return;
    m_Lines.clear();
    m_Paragraphs.clear();
    for (auto& entry : m_Wrapped)
        entry.m_Width = -1;
    std::size_t first = 0;
    do
    {
        const auto end = m_Text.find('\n', first);
        const auto line = TextBuffer::Sanitize(std::string_view(m_Text).substr(first, end - first));
        m_Paragraphs.push_back(line);
        m_Lines.push_back(font.Shape(line, GetStyle().m_FontSize));
        if (end == std::string::npos)
            break;
        first = end + 1;
    } while (first <= m_Text.size());
    m_LayoutSize = GetStyle().m_FontSize;
}

void Label::SetWordWrap(bool enabled)
{
    VerifyStructuralMutation();
    SetStyleProperty(StyleProperty::WordWrap, enabled);
}

bool Label::IsWordWrap() const
{
    return GetStyle().m_WordWrap;
}

namespace
{
Point LineSize(const std::vector<TextLayout>& lines)
{
    Point size;
    for (const auto& line : lines)
    {
        size.m_X = std::max(size.m_X, line.m_Width);
        size.m_Y += std::max(line.m_FontSize * 1.35f, line.m_Ascender + line.m_Descender);
    }
    return size;
}
} // namespace

const std::vector<TextLayout>& Label::Lines(FontFace& font, std::optional<float> width) const
{
    EnsureLayout(font);
    if (!GetStyle().m_WordWrap || !width)
        return m_Lines;
    for (const auto& entry : m_Wrapped)
        if (entry.m_Width == *width)
            return entry.m_Lines;
    WrappedLines wrapped;
    wrapped.m_Width = *width;
    for (std::size_t paragraph = 0; paragraph < m_Lines.size(); ++paragraph)
    {
        const auto& natural = m_Lines[paragraph];
        const auto& text = m_Paragraphs[paragraph];
        if (natural.m_Width <= *width || text.empty())
        {
            wrapped.m_Lines.push_back(natural);
            continue;
        }
        const auto& boundaries = natural.m_Offsets;
        std::size_t first = 0;
        while (first + 1 < boundaries.size())
        {
            const float edge = natural.m_Carets[first] + *width;
            const auto found = std::upper_bound(natural.m_Carets.begin() + static_cast<std::ptrdiff_t>(first + 1),
                                                natural.m_Carets.end(), edge + 0.001f);
            std::size_t end = std::max(first + 1, static_cast<std::size_t>(found - natural.m_Carets.begin()) - 1);
            if (end + 1 < boundaries.size())
                for (auto at = end; at > first; --at)
                {
                    const auto byte = boundaries[at - 1];
                    if (text[byte] == ' ' || text[byte] == '-')
                    {
                        end = at;
                        break;
                    }
                }
            auto visibleEnd = end;
            while (visibleEnd > first + 1 && text[boundaries[visibleEnd - 1]] == ' ')
                --visibleEnd;
            auto line =
                font.Shape(std::string_view(text).substr(boundaries[first], boundaries[visibleEnd] - boundaries[first]),
                           GetStyle().m_FontSize);
            // Reshape actual line boundaries; ligatures or kerning may differ from the paragraph estimate.
            while (line.m_Width > *width + 0.01f && visibleEnd > first + 1)
            {
                --visibleEnd;
                end = visibleEnd;
                line = font.Shape(
                    std::string_view(text).substr(boundaries[first], boundaries[visibleEnd] - boundaries[first]),
                    GetStyle().m_FontSize);
            }
            wrapped.m_Lines.push_back(std::move(line));
            first = end;
            while (first + 1 < boundaries.size() && text[boundaries[first]] == ' ')
                ++first;
        }
    }
    auto& entry = m_Wrapped[m_NextWrapped++ % m_Wrapped.size()];
    entry = std::move(wrapped);
    return entry.m_Lines;
}

Point Label::Measure(FontFace& font) const
{
    EnsureLayout(font);
    return LineSize(m_Lines);
}

MeasureResult Label::MeasureConstrained(FontFace& font, const MeasureConstraints& constraints) const
{
    const auto& lines = Lines(font, constraints.m_Width);
    const auto size = LineSize(lines);
    MeasureResult result{{}, size, {}, GetStyle().m_WordWrap};
    if (!lines.empty())
        result.m_Baseline =
            lines.front().m_Ascender + std::max(0.0f, (constraints.m_Height.value_or(size.m_Y) - size.m_Y) * 0.5f);
    return result;
}

void Label::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto content = GetContentBounds();
    const auto& lines = Lines(font, content.m_Width);
    const auto clip = GetClip().Intersect(content);
    const float height = LineSize(lines).m_Y;
    float y = content.m_Y + std::max(0.0f, (content.m_Height - height) * 0.5f);
    auto color = GetStyle().m_Color;
    color.m_A *= opacity;
    for (const auto& line : lines)
    {
        float x = content.m_X;
        if (GetStyle().m_TextAlign == "center")
            x += (content.m_Width - line.m_Width) * 0.5f;
        else if (GetStyle().m_TextAlign == "right")
            x += content.m_Width - line.m_Width;
        renderer.DrawText(line, {x, y + line.m_Ascender}, color, clip);
        y += std::max(line.m_FontSize * 1.35f, line.m_Ascender + line.m_Descender);
    }
}
} // namespace EStudioUI
