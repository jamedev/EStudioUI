#include <EStudioUI/TextBuffer.h>

#include <utf8proc.h>

#include <algorithm>
#include <limits>

namespace EStudioUI
{
std::string TextBuffer::Sanitize(std::string_view text)
{
    std::string result;
    result.reserve(std::min<std::size_t>(text.size(), 1048576));
    std::size_t offset = 0;
    while (offset < text.size() && result.size() < 1048576)
    {
        utf8proc_int32_t codepoint = 0;
        auto count = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
                                      static_cast<utf8proc_ssize_t>(text.size() - offset), &codepoint);
        if (count < 0)
        {
            codepoint = 0xFFFD;
            count = 1;
        }
        offset += static_cast<std::size_t>(count);
        if (codepoint == '\r' && offset < text.size() && text[offset] == '\n')
            continue;
        if (codepoint == '\r' || codepoint == '\n' || codepoint == '\t' || codepoint == 0x2028 || codepoint == 0x2029)
            codepoint = ' ';
        if (codepoint < 32 || codepoint == 127)
            continue;
        utf8proc_uint8_t encoded[4]{};
        const auto length = utf8proc_encode_char(codepoint, encoded);
        result.append(reinterpret_cast<const char*>(encoded), static_cast<std::size_t>(length));
    }
    return result;
}

std::vector<std::size_t> TextBuffer::Graphemes(std::string_view text)
{
    std::vector<std::size_t> boundaries{0};
    utf8proc_int32_t previous = -1;
    utf8proc_int32_t state = 0;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        utf8proc_int32_t current = 0;
        auto count = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
                                      static_cast<utf8proc_ssize_t>(text.size() - offset), &current);
        if (count <= 0)
        {
            current = 0xFFFD;
            count = 1;
        }
        if (previous >= 0 && utf8proc_grapheme_break_stateful(previous, current, &state))
            boundaries.push_back(offset);
        previous = current;
        offset += static_cast<std::size_t>(count);
    }
    if (boundaries.back() != text.size())
        boundaries.push_back(text.size());
    return boundaries;
}

void TextBuffer::SetText(std::string_view text)
{
    m_Text = Sanitize(text);
    const auto boundaries = Graphemes(m_Text);
    const auto end = std::upper_bound(boundaries.begin(), boundaries.end(), m_MaxBytes);
    m_Text.resize(*std::prev(end));
    m_Caret = m_Text.size();
    m_Anchor = m_Caret;
    Reindex();
    m_Undo.clear();
    m_Redo.clear();
}

const std::string& TextBuffer::GetText() const
{
    return m_Text;
}

std::size_t TextBuffer::GetCaret() const
{
    return m_Caret;
}

std::size_t TextBuffer::GetAnchor() const
{
    return m_Anchor;
}

const std::vector<std::size_t>& TextBuffer::GetBoundaries() const
{
    return m_Boundaries;
}

bool TextBuffer::HasSelection() const
{
    return m_Caret != m_Anchor;
}

std::string TextBuffer::GetSelectedText() const
{
    const auto [first, last] = std::minmax(m_Caret, m_Anchor);
    return m_Text.substr(first, last - first);
}

std::size_t TextBuffer::Snap(std::size_t offset) const
{
    const auto next = std::upper_bound(m_Boundaries.begin(), m_Boundaries.end(), std::min(offset, m_Text.size()));
    return *std::prev(next);
}

void TextBuffer::SetSelection(std::size_t anchor, std::size_t caret)
{
    m_Anchor = Snap(anchor);
    m_Caret = Snap(caret);
}

void TextBuffer::SelectAll()
{
    m_Anchor = 0;
    m_Caret = m_Text.size();
}

int TextBuffer::WordClass(std::size_t offset) const
{
    if (offset >= m_Text.size())
        return 0;
    utf8proc_int32_t codepoint = 0;
    utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(m_Text.data() + offset),
                     static_cast<utf8proc_ssize_t>(m_Text.size() - offset), &codepoint);
    const auto category = utf8proc_category(codepoint);
    if (category == UTF8PROC_CATEGORY_ZS || codepoint == '\t')
        return 0;
    if ((category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_NO) || codepoint == '_')
        return 1;
    return 2;
}

std::size_t TextBuffer::WordBoundary(std::size_t from, int direction) const
{
    auto index = static_cast<std::size_t>(std::lower_bound(m_Boundaries.begin(), m_Boundaries.end(), from) -
                                          m_Boundaries.begin());
    if (direction < 0)
    {
        while (index > 0 && WordClass(m_Boundaries[index - 1]) == 0)
            --index;
        if (index > 0)
        {
            const int type = WordClass(m_Boundaries[index - 1]);
            while (index > 0 && WordClass(m_Boundaries[index - 1]) == type)
                --index;
        }
    }
    else
    {
        const auto count = m_Boundaries.size() - 1;
        if (index < count)
        {
            const int type = WordClass(m_Boundaries[index]);
            while (index < count && WordClass(m_Boundaries[index]) == type)
                ++index;
        }
        while (index < count && WordClass(m_Boundaries[index]) == 0)
            ++index;
    }
    return m_Boundaries[index];
}

void TextBuffer::SelectWord(std::size_t offset)
{
    if (m_Text.empty())
        return;
    std::size_t index = static_cast<std::size_t>(
        std::lower_bound(m_Boundaries.begin(), m_Boundaries.end(), Snap(offset)) - m_Boundaries.begin());
    if (index == m_Boundaries.size() - 1)
        --index;
    const auto type = WordClass(m_Boundaries[index]);
    auto first = index;
    while (first > 0 && WordClass(m_Boundaries[first - 1]) == type)
        --first;
    auto last = index + 1;
    while (last < m_Boundaries.size() - 1 && WordClass(m_Boundaries[last]) == type)
        ++last;
    SetSelection(m_Boundaries[first], m_Boundaries[last]);
}

TextBuffer::Snapshot TextBuffer::Capture() const
{
    return {m_Text, m_Caret, m_Anchor};
}

void TextBuffer::Record()
{
    if (m_Undo.size() == 100)
        m_Undo.erase(m_Undo.begin());
    m_Undo.push_back(Capture());
    m_Redo.clear();
}

void TextBuffer::Reindex()
{
    m_Boundaries = Graphemes(m_Text);
    m_Caret = Snap(m_Caret);
    m_Anchor = Snap(m_Anchor);
}

bool TextBuffer::ReplaceSelection(std::string_view text)
{
    auto insertion = Sanitize(text);
    const auto [first, last] = std::minmax(m_Caret, m_Anchor);
    const auto available = m_MaxBytes - std::min(m_MaxBytes, m_Text.size() - (last - first));
    const auto boundaries = Graphemes(insertion);
    const auto end = std::upper_bound(boundaries.begin(), boundaries.end(), available);
    insertion.resize(*std::prev(end));
    if (first == last && insertion.empty())
        return false;
    Record();
    m_Text.replace(first, last - first, insertion);
    m_Caret = first + insertion.size();
    m_Anchor = m_Caret;
    Reindex();
    return true;
}

bool TextBuffer::Backspace(bool word)
{
    const auto before = Capture();
    if (!HasSelection())
    {
        if (m_Caret == 0)
            return false;
        const auto index = std::lower_bound(m_Boundaries.begin(), m_Boundaries.end(), m_Caret);
        m_Anchor = word ? WordBoundary(m_Caret, -1) : *std::prev(index);
    }
    const bool changed = ReplaceSelection({});
    if (changed)
        m_Undo.back() = before;
    return changed;
}

bool TextBuffer::DeleteForward(bool word)
{
    const auto before = Capture();
    if (!HasSelection())
    {
        if (m_Caret == m_Text.size())
            return false;
        const auto index = std::lower_bound(m_Boundaries.begin(), m_Boundaries.end(), m_Caret);
        m_Anchor = word ? WordBoundary(m_Caret, 1) : *std::next(index);
    }
    const bool changed = ReplaceSelection({});
    if (changed)
        m_Undo.back() = before;
    return changed;
}

void TextBuffer::Move(int direction, bool extend, bool word)
{
    if (HasSelection() && !extend && !word)
        m_Caret = direction < 0 ? std::min(m_Caret, m_Anchor) : std::max(m_Caret, m_Anchor);
    else if (word)
        m_Caret = WordBoundary(m_Caret, direction);
    else
    {
        const auto index = std::lower_bound(m_Boundaries.begin(), m_Boundaries.end(), m_Caret);
        if (direction < 0 && index != m_Boundaries.begin())
            m_Caret = *std::prev(index);
        else if (direction > 0 && std::next(index) != m_Boundaries.end())
            m_Caret = *std::next(index);
    }
    if (!extend)
        m_Anchor = m_Caret;
}

void TextBuffer::MoveToEdge(bool end, bool extend)
{
    m_Caret = end ? m_Text.size() : 0;
    if (!extend)
        m_Anchor = m_Caret;
}

void TextBuffer::Restore(const Snapshot& snapshot)
{
    m_Text = snapshot.m_Text;
    m_Caret = snapshot.m_Caret;
    m_Anchor = snapshot.m_Anchor;
    Reindex();
}

bool TextBuffer::Undo()
{
    if (m_Undo.empty())
        return false;
    m_Redo.push_back(Capture());
    Restore(m_Undo.back());
    m_Undo.pop_back();
    return true;
}

bool TextBuffer::Redo()
{
    if (m_Redo.empty())
        return false;
    m_Undo.push_back(Capture());
    Restore(m_Redo.back());
    m_Redo.pop_back();
    return true;
}

void TextBuffer::SetMaxBytes(std::size_t count)
{
    m_MaxBytes = std::min<std::size_t>(count, 1048576);
    const auto text = m_Text;
    SetText(text);
}
} // namespace EStudioUI
