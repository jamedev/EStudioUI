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
    SetCursor(CursorShape::Text);
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
    RegisterStylePart("preedit", 0,
                      [](const Style& owner, unsigned)
                      {
                          Style style;
                          style.m_Background = owner.m_Color;
                          style.m_Height = {Length::Unit::Pixels, 1};
                          return style;
                      });
    m_Buffer.SetText(text);
    UpdatePresentationState(ValidationState::Acceptable);
}

bool TextBox::IsFocusable() const
{
    VerifyAccess();
    return true;
}

const std::string& TextBox::GetText() const
{
    VerifyAccess();
    return m_Buffer.GetText();
}

void TextBox::SetValidator(std::shared_ptr<const InputValidator> validator)
{
    VerifyAccess();
    if (m_Validator == validator)
        return;
    const auto ownerLease = RetainOwner();
    if (!EndComposition())
        return;
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
    VerifyAccess();
    return m_ValidationState;
}

bool TextBox::IsReadOnly() const
{
    VerifyAccess();
    return m_ReadOnly;
}

std::string TextBox::GetSelectedText() const
{
    VerifyAccess();
    return m_Buffer.GetSelectedText();
}

std::size_t TextBox::GetSelectionAnchor() const
{
    VerifyAccess();
    return m_Buffer.GetAnchor();
}

std::size_t TextBox::GetSelectionCaret() const
{
    VerifyAccess();
    return m_Buffer.GetCaret();
}

void TextBox::SetSelection(std::size_t anchor, std::size_t caret)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    if (anchor > m_Buffer.GetText().size() || caret > m_Buffer.GetText().size())
        throw std::out_of_range("Text selection exceeds the committed text.");
    if (!EndComposition())
        return;
    m_Buffer.SetSelection(anchor, caret);
    ResetCaret();
}

Rect TextBox::GetTextRangeBounds(std::size_t start, std::size_t end) const
{
    VerifyAccess();
    if (start > end || end > m_Buffer.GetText().size())
        throw std::out_of_range("Text range exceeds the committed text.");
    if (auto* context = GetContext())
        EnsureLayout(context->GetFont());
    EnsureCaretVisible();
    const auto content = GetContentBounds();
    const float height = m_Layout.m_Ascender + m_Layout.m_Descender;
    const float left = m_Layout.CaretPosition(start);
    const float right = m_Layout.CaretPosition(end);
    return {TextOriginX() + std::min(left, right), content.m_Y + (content.m_Height - height) * 0.5f,
            std::max(start == end ? CaretWidth() : 0.0f, std::abs(right - left)), height};
}

void TextBox::RevealTextRange(std::size_t start, std::size_t end)
{
    VerifyAccess();
    if (start > end || end > m_Buffer.GetText().size())
        throw std::out_of_range("Text range exceeds the committed text.");
    if (auto* context = GetContext())
        EnsureLayout(context->GetFont());
    const float width = std::max(1.0f, GetContentBounds().m_Width - std::max(2.0f, CaretWidth()));
    const float left = m_Layout.CaretPosition(start);
    const float right = m_Layout.CaretPosition(end);
    if (right - m_ScrollX > width)
        m_ScrollX = right - width;
    if (left < m_ScrollX)
        m_ScrollX = left;
    m_ScrollX = std::clamp(m_ScrollX, 0.0f, std::max(0.0f, m_Layout.m_Width - width));
    m_RevealOverride = true;
    RequestRepaint();
}

std::size_t TextBox::TextOffsetAt(Point position) const
{
    VerifyAccess();
    return PointerOffset(position);
}

TextInputState TextBox::GetTextInputState() const
{
    VerifyAccess();
    if (auto* context = GetContext())
        EnsureLayout(context->GetFont());
    EnsureCaretVisible();
    TextInputState state;
    state.m_Text = m_Buffer.GetText();
    state.m_Anchor = m_Buffer.GetAnchor();
    state.m_Caret = m_Buffer.GetCaret();
    const auto content = GetContentBounds();
    const auto& line = DisplayLayout();
    const float height = line.m_Ascender + line.m_Descender;
    state.m_CaretBounds = {TextOriginX() + line.CaretPosition(DisplayCaret()),
                           content.m_Y + (content.m_Height - height) * 0.5f, CaretWidth(), height};
    state.m_Composing = m_Composing;
    state.m_ReadOnly = m_ReadOnly;
    state.m_Active = !m_ReadOnly;
    return state;
}

void TextBox::ClearComposition()
{
    if (!m_Composing)
        return;
    m_Composing = false;
    m_CompositionText.clear();
    m_CompositionBoundaries = {0};
    m_CompositionLayoutSize = -1;
    ResetCaret();
}

bool TextBox::EndComposition()
{
    const WidgetRef lifetime(this);
    if (auto* context = GetContext(); context && context->GetFocus() == this)
        context->CancelTextComposition();
    else
        ClearComposition();
    return lifetime.Get() != nullptr;
}

bool TextBox::OnTextComposition(const TextCompositionEvent& event)
{
    VerifyAccess();
    ValidateTextComposition(event);
    if (event.m_Kind == TextCompositionKind::Cancel)
    {
        ClearComposition();
        return true;
    }
    if (m_ReadOnly)
        return false;
    if (event.m_Kind == TextCompositionKind::Preedit)
    {
        if (TextBuffer::Sanitize(event.m_Text) != event.m_Text)
            return false;
        const bool changed = !m_Composing || m_CompositionText != event.m_Text;
        if (changed)
        {
            auto boundaries = TextBuffer::Graphemes(event.m_Text);
            m_CompositionText = event.m_Text;
            m_CompositionBoundaries = std::move(boundaries);
            m_CompositionLayoutSize = -1;
        }
        if (!m_Composing)
        {
            m_CompositionAnchor = m_Buffer.GetAnchor();
            m_CompositionOriginalCaret = m_Buffer.GetCaret();
        }
        m_Composing = true;
        const auto next =
            std::upper_bound(m_CompositionBoundaries.begin(), m_CompositionBoundaries.end(), event.m_Caret);
        m_CompositionCaret = next == m_CompositionBoundaries.begin() ? 0 : *std::prev(next);
        ResetCaret();
        return true;
    }
    auto candidate = m_Buffer;
    if (m_Composing)
        candidate.SetSelection(m_CompositionAnchor, m_CompositionOriginalCaret);
    const bool changed = candidate.ReplaceSelection(event.m_Text);
    if (m_Validator && m_Validator->Validate(candidate.GetText()) == ValidationState::Invalid)
    {
        ClearComposition();
        return false;
    }
    ClearComposition();
    m_Buffer = std::move(candidate);
    AfterEdit(changed);
    return true;
}

const TextLayout& TextBox::DisplayLayout() const
{
    return m_Composing ? m_CompositionLayout : m_Layout;
}

std::size_t TextBox::DisplayCaret() const
{
    return m_Composing ? std::min(m_CompositionAnchor, m_CompositionOriginalCaret) + m_CompositionCaret
                       : m_Buffer.GetCaret();
}

Signal<const std::string&>& TextBox::TextChanged()
{
    VerifyAccess();
    return m_TextChanged;
}

Signal<ValidationState>& TextBox::ValidationChanged()
{
    VerifyAccess();
    return m_ValidationChanged;
}

Signal<const std::string&>& TextBox::Submitted()
{
    VerifyAccess();
    return m_Submitted;
}

Signal<>& TextBox::FocusLost()
{
    VerifyAccess();
    return m_FocusLost;
}

void TextBox::SetText(std::string_view text)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    const std::string replacement(text);
    if (!EndComposition())
        return;
    const auto previous = m_Buffer.GetText();
    if (m_Validator)
    {
        auto candidate = m_Buffer;
        candidate.SetText(replacement);
        if (m_Validator->Validate(candidate.GetText()) == ValidationState::Invalid)
            throw std::invalid_argument("Text does not satisfy the editor's input validator.");
        m_Buffer = std::move(candidate);
    }
    else
        m_Buffer.SetText(replacement);
    AfterEdit(previous != m_Buffer.GetText());
}

bool TextBox::CanSetText(std::string_view text) const
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    const auto validator = m_Validator;
    auto candidate = m_Buffer;
    candidate.SetText(text);
    if (candidate.GetText() != text)
        return false;
    return !validator || validator->Validate(candidate.GetText()) != ValidationState::Invalid;
}

void TextBox::SetPlaceholder(std::string text)
{
    VerifyAccess();
    text = TextBuffer::Sanitize(text);
    if (m_Placeholder == text)
        return;
    m_Placeholder = std::move(text);
    m_PlaceholderLayoutSize = -1;
    UpdatePresentationState(m_ValidationState);
    Invalidate(Invalidation::Content);
}

void TextBox::SetReadOnly(bool readOnly)
{
    VerifyAccess();
    if (m_ReadOnly == readOnly)
        return;
    const auto ownerLease = RetainOwner();
    if (!EndComposition())
        return;
    m_ReadOnly = readOnly;
    SetState(ReadOnly, readOnly);
}

void TextBox::SetMaxBytes(std::size_t count)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    if (!EndComposition())
        return;
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
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    if (!EndComposition())
        return;
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
    if (m_Composing && m_CompositionLayoutSize != GetStyle().m_FontSize)
    {
        const auto start = std::min(m_CompositionAnchor, m_CompositionOriginalCaret);
        const auto end = std::max(m_CompositionAnchor, m_CompositionOriginalCaret);
        std::string visual;
        visual.reserve(m_Buffer.GetText().size() - (end - start) + m_CompositionText.size());
        visual.append(m_Buffer.GetText(), 0, start);
        visual += m_CompositionText;
        visual.append(m_Buffer.GetText(), end);
        m_CompositionLayout = font.Shape(visual, GetStyle().m_FontSize, true);
        m_CompositionLayoutSize = GetStyle().m_FontSize;
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

MeasureResult TextBox::MeasureConstrained(FontFace& font, const MeasureConstraints& constraints) const
{
    EnsureLayout(font);
    const bool placeholder =
        m_Buffer.GetText().empty() && !m_Placeholder.empty() && GetPartStyle("placeholder").m_Displayed;
    const auto& line = placeholder ? m_PlaceholderLayout : m_Layout;
    const float height = constraints.m_Height.value_or(GetIntrinsicSize().m_Y);
    const float baseline = std::max(0.0f, (height - line.m_Ascender - line.m_Descender) * 0.5f + line.m_Ascender);
    return {{}, GetIntrinsicSize(), baseline, false};
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
    const float extra = std::max(0.0f, content.m_Width - CaretWidth() - DisplayLayout().m_Width);
    const auto& align = GetStyle().m_TextAlign;
    return content.m_X - m_ScrollX + (align == "right" ? extra : align == "center" ? extra * 0.5f : 0);
}

void TextBox::EnsureCaretVisible() const
{
    if (m_RevealOverride)
        return;
    const float width = std::max(1.0f, GetContentBounds().m_Width - std::max(2.0f, CaretWidth()));
    const float caret = DisplayLayout().CaretPosition(DisplayCaret());
    if (caret - m_ScrollX > width)
        m_ScrollX = caret - width;
    else if (caret < m_ScrollX)
        m_ScrollX = caret;
    m_ScrollX = std::clamp(m_ScrollX, 0.0f, std::max(0.0f, DisplayLayout().m_Width - width));
}

void TextBox::ResetCaret()
{
    m_RevealOverride = false;
    if (auto* context = GetContext())
    {
        context->NotifyTextInputChanged();
        m_CaretEpoch = context->GetTime();
        EnsureLayout(context->GetFont());
        EnsureCaretVisible();
    }
    RequestRepaint();
}

void TextBox::AfterEdit(bool changed)
{
    const auto ownerLease = RetainOwner();
    const auto revision = ++m_EditRevision;
    const bool validationChanged =
        UpdatePresentationState(m_Validator ? m_Validator->Validate(m_Buffer.GetText()) : ValidationState::Acceptable);
    if (changed)
    {
        m_LayoutSize = -1;
        Invalidate(Invalidation::Content);
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
    const auto ownerLease = RetainOwner();
    if (!EndComposition())
        return;
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
        ClearComposition();
        m_Dragging = false;
        m_FocusLost.Emit();
    }
}

void TextBox::OnText(std::string_view text)
{
    if (m_ReadOnly || m_Composing)
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
    if (m_Composing)
    {
        if (key == Key::Escape)
            EndComposition();
        return;
    }
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
    VerifyAccess();
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
    const auto& line = DisplayLayout();
    const float height = line.m_Ascender + line.m_Descender;
    const Point baseline{TextOriginX(), content.m_Y + (content.m_Height - height) * 0.5f + line.m_Ascender};
    auto color = GetStyle().m_Color;
    color.m_A *= opacity;
    const auto start = std::min(m_Buffer.GetCaret(), m_Buffer.GetAnchor());
    const auto end = std::max(m_Buffer.GetCaret(), m_Buffer.GetAnchor());
    const float selectionStart = m_Layout.CaretPosition(start);
    const float selectionEnd = m_Layout.CaretPosition(end);
    const Rect selection{baseline.m_X + selectionStart, content.m_Y, selectionEnd - selectionStart, content.m_Height};
    if (!m_Composing && m_Buffer.HasSelection())
    {
        const auto& style = GetPartStyle("selection");
        if (style.m_Displayed && style.m_Opacity > 0)
        {
            renderer.PushState(style.m_Transform.Resolve(selection));
            renderer.DrawBox(selection, style, clip, opacity * style.m_Opacity);
            renderer.PopState();
        }
    }
    if (m_Buffer.GetText().empty() && !m_Composing)
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
        renderer.DrawText(line, baseline, color, clip);
    if (!m_Composing && m_Buffer.HasSelection())
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
    if (m_Composing && !m_CompositionText.empty())
    {
        const auto& style = GetPartStyle("preedit");
        const auto compositionStart = std::min(m_CompositionAnchor, m_CompositionOriginalCaret);
        const float left = line.CaretPosition(compositionStart);
        const float right = line.CaretPosition(compositionStart + m_CompositionText.size());
        const float thickness = std::max(0.0f, style.m_Height.Resolve(content.m_Height, 1));
        const Rect underline{baseline.m_X + std::min(left, right), baseline.m_Y + line.m_Descender - thickness,
                             std::abs(right - left), thickness};
        if (style.m_Displayed && style.m_Opacity > 0)
        {
            renderer.PushState(style.m_Transform.Resolve(underline));
            renderer.DrawBox(underline, style, clip, opacity * style.m_Opacity);
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
            const Rect caret{baseline.m_X + line.CaretPosition(DisplayCaret()),
                             content.m_Y + (content.m_Height - caretHeight) * 0.5f, CaretWidth(), caretHeight};
            renderer.PushState(style.m_Transform.Resolve(caret));
            renderer.DrawBox(caret, style, clip, opacity * style.m_Opacity);
            renderer.PopState();
        }
    }
    renderer.PopState();
}
} // namespace EStudioUI
