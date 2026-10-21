#include <EStudioUI/TextBox.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <cmath>

namespace EStudioUI
{
TextBox::TextBox(std::string text) : Widget("textbox")
{
    m_Buffer.SetText(text);
}

bool TextBox::IsFocusable() const
{
    return true;
}

const std::string& TextBox::GetText() const
{
    return m_Buffer.GetText();
}

bool TextBox::IsReadOnly() const
{
    return m_ReadOnly;
}

std::string TextBox::GetSelectedText() const
{
    return m_Buffer.GetSelectedText();
}

Signal<const std::string&>& TextBox::TextChanged()
{
    return m_TextChanged;
}

Signal<const std::string&>& TextBox::Submitted()
{
    return m_Submitted;
}

void TextBox::SetText(std::string_view text)
{
    const auto previous = m_Buffer.GetText();
    m_Buffer.SetText(text);
    AfterEdit(previous != m_Buffer.GetText());
}

void TextBox::SetPlaceholder(std::string text)
{
    m_Placeholder = TextBuffer::Sanitize(text);
    m_LayoutSize = -1;
    Invalidate();
}

void TextBox::SetReadOnly(bool readOnly)
{
    m_ReadOnly = readOnly;
    SetState(ReadOnly, readOnly);
}

void TextBox::SetMaxBytes(std::size_t count)
{
    const auto previous = m_Buffer.GetText();
    m_Buffer.SetMaxBytes(count);
    AfterEdit(previous != m_Buffer.GetText());
}

void TextBox::SelectAll()
{
    m_Buffer.SelectAll();
    ResetCaret();
}

void TextBox::EnsureLayout(FontFace& font) const
{
    if (m_LayoutSize == GetStyle().m_FontSize && m_LayoutText == m_Buffer.GetText())
        return;
    m_Layout = font.Shape(m_Buffer.GetText(), GetStyle().m_FontSize, true);
    m_PlaceholderLayout = font.Shape(m_Placeholder, GetStyle().m_FontSize, true);
    m_LayoutText = m_Buffer.GetText();
    m_LayoutSize = GetStyle().m_FontSize;
}

Point TextBox::Measure(FontFace& font) const
{
    EnsureLayout(font);
    return {180, std::max(m_Layout.m_FontSize * 1.35f, m_Layout.m_Ascender + m_Layout.m_Descender)};
}

void TextBox::EnsureCaretVisible() const
{
    const float width = std::max(1.0f, GetContentBounds().m_Width - 2);
    const float caret = m_Layout.CaretPosition(m_Buffer.GetCaret());
    if (caret - m_ScrollX > width)
        m_ScrollX = caret - width;
    else if (caret < m_ScrollX)
        m_ScrollX = caret;
    m_ScrollX = std::clamp(m_ScrollX, 0.0f, std::max(0.0f, m_Layout.m_Width - width));
}

void TextBox::ResetCaret()
{
    if (auto* context = GetContext())
    {
        m_CaretEpoch = context->GetTime();
        EnsureLayout(context->GetFont());
        EnsureCaretVisible();
    }
    RequestRepaint();
}

void TextBox::AfterEdit(bool changed)
{
    if (changed)
    {
        m_LayoutSize = -1;
        Invalidate();
    }
    ResetCaret();
    if (changed)
    {
        const auto text = m_Buffer.GetText();
        m_TextChanged.Emit(text);
    }
}

std::size_t TextBox::PointerOffset(Point position) const
{
    if (auto* context = GetContext())
        EnsureLayout(context->GetFont());
    return m_Layout.HitTest(position.m_X - GetContentBounds().m_X + m_ScrollX);
}

void TextBox::OnPointerDown(const PointerEvent& event)
{
    const auto offset = PointerOffset(event.m_Position);
    if (event.m_Clicks >= 3)
        m_Buffer.SelectAll();
    else if (event.m_Clicks == 2)
        m_Buffer.SelectWord(offset);
    else
        m_Buffer.SetSelection(event.m_Modifiers.m_Shift ? m_Buffer.GetAnchor() : offset, offset);
    m_Dragging = event.m_Clicks == 1;
    ResetCaret();
}

void TextBox::OnPointerMove(const PointerEvent& event)
{
    if (m_Dragging)
    {
        m_Buffer.SetSelection(m_Buffer.GetAnchor(), PointerOffset(event.m_Position));
        ResetCaret();
    }
}

void TextBox::OnPointerUp(const PointerEvent&)
{
    m_Dragging = false;
}

void TextBox::OnCaptureLost()
{
    m_Dragging = false;
}

void TextBox::OnFocus(bool focused)
{
    if (focused)
        ResetCaret();
    else
        m_Dragging = false;
}

void TextBox::OnText(std::string_view text)
{
    if (!m_ReadOnly)
        AfterEdit(m_Buffer.ReplaceSelection(text));
}

void TextBox::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down)
        return;
    bool changed = false;
    if (modifiers.m_Control)
    {
        if (key == Key::A)
            m_Buffer.SelectAll();
        else if (key == Key::C || key == Key::X)
        {
            if (m_Buffer.HasSelection())
            {
                GetContext()->SetClipboardText(m_Buffer.GetSelectedText());
                if (key == Key::X && !m_ReadOnly)
                    changed = m_Buffer.ReplaceSelection({});
            }
        }
        else if (key == Key::V && !m_ReadOnly)
            changed = m_Buffer.ReplaceSelection(GetContext()->GetClipboard());
        else if (key == Key::Z && !m_ReadOnly)
            changed = modifiers.m_Shift ? m_Buffer.Redo() : m_Buffer.Undo();
        else if (key == Key::Y && !m_ReadOnly)
            changed = m_Buffer.Redo();
    }
    if (key == Key::Left)
        m_Buffer.Move(-1, modifiers.m_Shift, modifiers.m_Control);
    else if (key == Key::Right)
        m_Buffer.Move(1, modifiers.m_Shift, modifiers.m_Control);
    else if (key == Key::Home)
        m_Buffer.MoveToEdge(false, modifiers.m_Shift);
    else if (key == Key::End)
        m_Buffer.MoveToEdge(true, modifiers.m_Shift);
    else if (key == Key::Backspace && !m_ReadOnly)
        changed = m_Buffer.Backspace(modifiers.m_Control);
    else if (key == Key::Delete && !m_ReadOnly)
        changed = m_Buffer.DeleteForward(modifiers.m_Control);
    else if (key == Key::Escape)
        m_Buffer.SetSelection(m_Buffer.GetCaret(), m_Buffer.GetCaret());
    else if (key == Key::Enter)
    {
        const auto text = m_Buffer.GetText();
        m_Submitted.Emit(text);
        return;
    }
    AfterEdit(changed);
}

double TextBox::NextCaretDeadline(double now) const
{
    if (!IsFocused() || m_ReadOnly)
        return now + 3600;
    return m_CaretEpoch + (std::floor(std::max(0.0, now - m_CaretEpoch) * 2) + 1) * 0.5 + 0.001;
}

void TextBox::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    EnsureLayout(font);
    EnsureCaretVisible();
    const auto content = GetContentBounds();
    const auto clip = GetClip().Intersect(content);
    const float height = m_Layout.m_Ascender + m_Layout.m_Descender;
    const Point baseline{content.m_X - m_ScrollX,
                         content.m_Y + (content.m_Height - height) * 0.5f + m_Layout.m_Ascender};
    auto color = GetStyle().m_Color;
    color.m_A *= opacity;
    const auto start = std::min(m_Buffer.GetCaret(), m_Buffer.GetAnchor());
    const auto end = std::max(m_Buffer.GetCaret(), m_Buffer.GetAnchor());
    const float selectionStart = m_Layout.CaretPosition(start);
    const float selectionEnd = m_Layout.CaretPosition(end);
    const Rect selection{baseline.m_X + selectionStart, content.m_Y, selectionEnd - selectionStart, content.m_Height};
    if (m_Buffer.HasSelection())
    {
        auto background = GetStyle().m_SelectionBackground;
        background.m_A *= opacity;
        renderer.DrawSolid(selection, background, clip);
    }
    if (m_Buffer.GetText().empty())
    {
        auto placeholder = GetStyle().m_PlaceholderColor;
        placeholder.m_A *= opacity;
        renderer.DrawText(m_PlaceholderLayout, {content.m_X, baseline.m_Y}, placeholder, clip);
    }
    else
        renderer.DrawText(m_Layout, baseline, color, clip);
    if (m_Buffer.HasSelection())
    {
        auto selected = GetStyle().m_SelectionColor;
        selected.m_A *= opacity;
        renderer.DrawText(m_Layout, baseline, selected, clip.Intersect(selection));
    }
    if (IsFocused() && !m_ReadOnly && static_cast<long long>(std::max(0.0, time - m_CaretEpoch) * 2) % 2 == 0)
    {
        auto caret = GetStyle().m_CaretColor;
        caret.m_A *= opacity;
        renderer.DrawSolid(
            {baseline.m_X + m_Layout.CaretPosition(m_Buffer.GetCaret()), content.m_Y, 1.25f, content.m_Height}, caret,
            clip);
    }
}
} // namespace EStudioUI
