#include <EStudioUI/TextBox.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
TextBox::TextBox(std::string text) : Widget("textbox")
{
    RegisterStylePart("selection", 0,
                      [](const Style& owner, unsigned)
                      {
                          Style style;
                          style.m_Background = owner.m_SelectionBackground;
                          style.m_Color = owner.m_SelectionColor;
                          return style;
                      });
    RegisterStylePart("caret", 0,
                      [](const Style& owner, unsigned)
                      {
                          Style style;
                          style.m_Background = owner.m_CaretColor;
                          style.m_Width = {Length::Unit::Pixels, 1.25f};
                          style.m_Height = {Length::Unit::Percent, 100};
                          return style;
                      });
    RegisterStylePart("placeholder", 0,
                      [](const Style& owner, unsigned)
                      {
                          Style style;
                          style.m_Color = owner.m_PlaceholderColor;
                          style.m_FontSize = owner.m_FontSize;
                          style.m_TextAlign = owner.m_TextAlign;
                          return style;
                      });
    m_Buffer.SetText(text);
    UpdatePresentationState(ValidationState::Acceptable);
}

bool TextBox::IsFocusable() const
{
    return true;
}

const std::string& TextBox::GetText() const
{
    return m_Buffer.GetText();
}

void TextBox::SetValidator(std::shared_ptr<const InputValidator> validator)
{
    const auto state = validator ? validator->Validate(m_Buffer.GetText()) : ValidationState::Acceptable;
    m_Validator = std::move(validator);
    ++m_EditRevision;
    const bool changed = UpdatePresentationState(state);
    RequestRepaint();
    if (changed)
        m_ValidationChanged.Emit(m_ValidationState);
}

ValidationState TextBox::GetValidationState() const
{
    return m_ValidationState;
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

Signal<ValidationState>& TextBox::ValidationChanged()
{
    return m_ValidationChanged;
}

Signal<const std::string&>& TextBox::Submitted()
{
    return m_Submitted;
}

Signal<>& TextBox::FocusLost()
{
    return m_FocusLost;
}

void TextBox::SetText(std::string_view text)
{
    const auto previous = m_Buffer.GetText();
    if (m_Validator)
    {
        auto candidate = m_Buffer;
        candidate.SetText(text);
        if (m_Validator->Validate(candidate.GetText()) == ValidationState::Invalid)
            throw std::invalid_argument("Text does not satisfy the editor's input validator.");
        m_Buffer = std::move(candidate);
    }
    else
        m_Buffer.SetText(text);
    AfterEdit(previous != m_Buffer.GetText());
}

void TextBox::SetPlaceholder(std::string text)
{
    m_Placeholder = TextBuffer::Sanitize(text);
    m_PlaceholderLayoutSize = -1;
    UpdatePresentationState(m_ValidationState);
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
    if (m_Validator)
    {
        auto candidate = m_Buffer;
        candidate.SetMaxBytes(count);
        if (m_Validator->Validate(candidate.GetText()) == ValidationState::Invalid)
            throw std::invalid_argument("The byte limit would violate the editor's input validator.");
        m_Buffer = std::move(candidate);
    }
    else
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
    if (m_LayoutSize != GetStyle().m_FontSize || m_LayoutText != m_Buffer.GetText())
    {
        m_Layout = font.Shape(m_Buffer.GetText(), GetStyle().m_FontSize, true);
        m_LayoutText = m_Buffer.GetText();
        m_LayoutSize = GetStyle().m_FontSize;
    }
    const float placeholderSize = GetPartStyle("placeholder").m_FontSize;
    if (m_PlaceholderLayoutSize != placeholderSize)
    {
        m_PlaceholderLayout = font.Shape(m_Placeholder, placeholderSize, true);
        m_PlaceholderLayoutSize = placeholderSize;
    }
}

Point TextBox::Measure(FontFace& font) const
{
    EnsureLayout(font);
    float height = std::max(m_Layout.m_FontSize * 1.35f, m_Layout.m_Ascender + m_Layout.m_Descender);
    if (m_Buffer.GetText().empty() && !m_Placeholder.empty() && GetPartStyle("placeholder").m_Displayed)
        height = std::max(height, m_PlaceholderLayout.m_Ascender + m_PlaceholderLayout.m_Descender);
    return {180, height};
}

float TextBox::CaretWidth() const
{
    const auto& style = GetPartStyle("caret");
    const float available = std::max(0.0f, GetContentBounds().m_Width);
    return std::clamp(style.m_Width.Resolve(available, 1.25f), std::min(style.m_MinWidth, available),
                      std::min(std::max(style.m_MinWidth, style.m_MaxWidth), available));
}

float TextBox::TextOriginX() const
{
    const auto content = GetContentBounds();
    const float extra = std::max(0.0f, content.m_Width - CaretWidth() - m_Layout.m_Width);
    const auto& align = GetStyle().m_TextAlign;
    return content.m_X - m_ScrollX + (align == "right" ? extra : align == "center" ? extra * 0.5f : 0);
}

void TextBox::EnsureCaretVisible() const
{
    const float width = std::max(1.0f, GetContentBounds().m_Width - std::max(2.0f, CaretWidth()));
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
    const auto revision = ++m_EditRevision;
    const bool validationChanged =
        UpdatePresentationState(m_Validator ? m_Validator->Validate(m_Buffer.GetText()) : ValidationState::Acceptable);
    if (changed)
    {
        m_LayoutSize = -1;
        Invalidate();
    }
    ResetCaret();
    const auto text = changed ? m_Buffer.GetText() : std::string{};
    const WidgetRef lifetime(this);
    if (validationChanged)
        m_ValidationChanged.Emit(m_ValidationState);
    if (lifetime.Get() && revision == m_EditRevision && changed)
    {
        m_TextChanged.Emit(text);
    }
}

bool TextBox::UpdatePresentationState(ValidationState state)
{
    const bool changed = m_ValidationState != state;
    m_ValidationState = state;
    SetState(Valid, state == ValidationState::Acceptable);
    SetState(Invalid, state == ValidationState::Invalid);
    SetState(Intermediate, state == ValidationState::Intermediate);
    SetState(Empty, m_Buffer.GetText().empty());
    SetState(PlaceholderShown, m_Buffer.GetText().empty() && !m_Placeholder.empty());
    return changed;
}

std::size_t TextBox::PointerOffset(Point position) const
{
    if (auto* context = GetContext())
        EnsureLayout(context->GetFont());
    EnsureCaretVisible();
    return m_Layout.HitTest(position.m_X - TextOriginX());
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
    {
        m_Dragging = false;
        m_FocusLost.Emit();
    }
}

void TextBox::OnText(std::string_view text)
{
    if (m_ReadOnly)
        return;
    if (m_Validator)
    {
        auto candidate = m_Buffer;
        const bool changed = candidate.ReplaceSelection(text);
        if (m_Validator->Validate(candidate.GetText()) == ValidationState::Invalid)
            return;
        m_Buffer = std::move(candidate);
        AfterEdit(changed);
    }
    else
        AfterEdit(m_Buffer.ReplaceSelection(text));
}

void TextBox::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down)
        return;
    const auto previous = m_Validator ? std::optional<TextBuffer>(m_Buffer) : std::nullopt;
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
        if (GetValidationState() == ValidationState::Acceptable)
        {
            const auto text = m_Buffer.GetText();
            m_Submitted.Emit(text);
        }
        return;
    }
    if (changed && previous && m_Validator->Validate(m_Buffer.GetText()) == ValidationState::Invalid)
    {
        m_Buffer = *previous;
        changed = false;
    }
    AfterEdit(changed);
}

double TextBox::NextCaretDeadline(double now) const
{
    const auto& caret = GetPartStyle("caret");
    if (!IsFocused() || m_ReadOnly || !caret.m_Displayed || caret.m_Opacity <= 0 || CaretWidth() <= 0)
        return now + 3600;
    return m_CaretEpoch + (std::floor(std::max(0.0, now - m_CaretEpoch) * 2) + 1) * 0.5 + 0.001;
}

void TextBox::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    renderer.PushState();
    if (GetStyle().m_ClipContent)
        renderer.PushClip(GetBounds(), GetStyle().m_Radius);
    EnsureLayout(font);
    EnsureCaretVisible();
    const auto content = GetContentBounds();
    const auto clip = GetClip().Intersect(content);
    const float height = m_Layout.m_Ascender + m_Layout.m_Descender;
    const Point baseline{TextOriginX(), content.m_Y + (content.m_Height - height) * 0.5f + m_Layout.m_Ascender};
    auto color = GetStyle().m_Color;
    color.m_A *= opacity;
    const auto start = std::min(m_Buffer.GetCaret(), m_Buffer.GetAnchor());
    const auto end = std::max(m_Buffer.GetCaret(), m_Buffer.GetAnchor());
    const float selectionStart = m_Layout.CaretPosition(start);
    const float selectionEnd = m_Layout.CaretPosition(end);
    const Rect selection{baseline.m_X + selectionStart, content.m_Y, selectionEnd - selectionStart, content.m_Height};
    if (m_Buffer.HasSelection())
    {
        const auto& style = GetPartStyle("selection");
        if (style.m_Displayed && style.m_Opacity > 0)
        {
            renderer.PushState(style.m_Transform.Resolve(selection));
            renderer.DrawBox(selection, style, clip, opacity * style.m_Opacity);
            renderer.PopState();
        }
    }
    if (m_Buffer.GetText().empty())
    {
        const auto& style = GetPartStyle("placeholder");
        if (style.m_Displayed && style.m_Opacity > 0 && !m_Placeholder.empty())
        {
            const float extra = std::max(0.0f, content.m_Width - m_PlaceholderLayout.m_Width);
            const float x = content.m_X + (style.m_TextAlign == "right"    ? extra
                                           : style.m_TextAlign == "center" ? extra * 0.5f
                                                                           : 0);
            const float placeholderHeight = m_PlaceholderLayout.m_Ascender + m_PlaceholderLayout.m_Descender;
            const Point origin{x, content.m_Y + (content.m_Height - placeholderHeight) * 0.5f +
                                      m_PlaceholderLayout.m_Ascender};
            auto placeholder = style.m_Color;
            placeholder.m_A *= opacity * style.m_Opacity;
            renderer.PushState(style.m_Transform.Resolve(content));
            renderer.DrawBox(content, style, clip, opacity * style.m_Opacity);
            renderer.DrawText(m_PlaceholderLayout, origin, placeholder, clip);
            renderer.PopState();
        }
    }
    else
        renderer.DrawText(m_Layout, baseline, color, clip);
    if (m_Buffer.HasSelection())
    {
        const auto& style = GetPartStyle("selection");
        if (style.m_Displayed && style.m_Opacity > 0)
        {
            auto selected = style.m_Color;
            selected.m_A *= opacity * style.m_Opacity;
            renderer.PushState(style.m_Transform.Resolve(selection));
            renderer.DrawText(m_Layout, baseline, selected, clip.Intersect(selection));
            renderer.PopState();
        }
    }
    if (IsFocused() && !m_ReadOnly && static_cast<long long>(std::max(0.0, time - m_CaretEpoch) * 2) % 2 == 0)
    {
        const auto& style = GetPartStyle("caret");
        if (style.m_Displayed && style.m_Opacity > 0)
        {
            const float caretHeight =
                std::clamp(style.m_Height.Resolve(content.m_Height, content.m_Height),
                           std::min(style.m_MinHeight, content.m_Height),
                           std::min(std::max(style.m_MinHeight, style.m_MaxHeight), content.m_Height));
            const Rect caret{baseline.m_X + m_Layout.CaretPosition(m_Buffer.GetCaret()),
                             content.m_Y + (content.m_Height - caretHeight) * 0.5f, CaretWidth(), caretHeight};
            renderer.PushState(style.m_Transform.Resolve(caret));
            renderer.DrawBox(caret, style, clip, opacity * style.m_Opacity);
            renderer.PopState();
        }
    }
    renderer.PopState();
}
} // namespace EStudioUI
