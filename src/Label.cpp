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
    Invalidate();
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
    std::size_t first = 0;
    do
    {
        const auto end = m_Text.find('\n', first);
        const auto line = TextBuffer::Sanitize(std::string_view(m_Text).substr(first, end - first));
        m_Lines.push_back(font.Shape(line, GetStyle().m_FontSize));
        if (end == std::string::npos)
            break;
        first = end + 1;
    } while (first <= m_Text.size());
    m_LayoutSize = GetStyle().m_FontSize;
}

Point Label::Measure(FontFace& font) const
{
    EnsureLayout(font);
    Point result;
    for (const auto& line : m_Lines)
    {
        result.m_X = std::max(result.m_X, line.m_Width);
        result.m_Y += std::max(line.m_FontSize * 1.35f, line.m_Ascender + line.m_Descender);
    }
    return result;
}

void Label::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    EnsureLayout(font);
    const auto content = GetContentBounds();
    const auto clip = GetClip().Intersect(content);
    const float height = Measure(font).m_Y;
    float y = content.m_Y + std::max(0.0f, (content.m_Height - height) * 0.5f);
    auto color = GetStyle().m_Color;
    color.m_A *= opacity;
    for (const auto& line : m_Lines)
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
