#include <EStudioUI/Label.h>
#include <EStudioUI/TextBuffer.h>
#include <EStudioUI/Renderer.h>

#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
void Label::OnFontsChanged() noexcept
{
    m_Lines.clear();
    m_Paragraphs.clear();
    for (auto& wrapped : m_Wrapped)
    {
        wrapped.m_Lines.clear();
        wrapped.m_Width = -1;
    }
    m_LayoutSize = -1;
}

Label::Label(std::string text, std::string type) : Widget(std::move(type))
{
    SetText(std::move(text));
}

void Label::SetText(std::string text)
{
    VerifyAccess();
    if (text.size() > 1048576)
        throw std::length_error("A display label is limited to 1 MiB of UTF-8.");
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
    if (m_LayoutSize == GetStyle().m_FontSize && !m_Lines.empty() && font.Matches(m_Lines.front(), GetStyle()))
        return;
    m_Lines.clear();
    m_Paragraphs.clear();
    for (auto& entry : m_Wrapped)
        entry.m_Width = -1;
    std::string display;
    display.reserve(m_Text.size());
    for (std::size_t i = 0; i < m_Text.size(); ++i)
    {
        if (m_Text[i] == '\r')
        {
            display += '\n';
            if (i + 1 < m_Text.size() && m_Text[i + 1] == '\n')
                ++i;
        }
        else if (m_Text.compare(i, 2, "\xC2\x85") == 0)
        {
            display += '\n';
            ++i;
        }
        else if (m_Text.compare(i, 3, "\xE2\x80\xA8") == 0 || m_Text.compare(i, 3, "\xE2\x80\xA9") == 0)
        {
            display += '\n';
            i += 2;
        }
        else
            display += m_Text[i];
    }
    std::size_t first = 0;
    do
    {
        const auto end = display.find('\n', first);
        const auto line = TextBuffer::Sanitize(std::string_view(display).substr(first, end - first));
        m_Paragraphs.push_back(line);
        m_Lines.push_back(font.Shape(line, GetStyle()));
        if (end == std::string::npos)
            break;
        first = end + 1;
        if (m_Lines.size() > 4096)
            throw std::length_error("A display label is limited to 4,096 paragraphs.");
    } while (first <= display.size());
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
        auto lines = font.Wrap(text, GetStyle(), *width);
        for (auto& line : lines)
            wrapped.m_Lines.push_back(std::move(line));
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
