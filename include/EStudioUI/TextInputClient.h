#pragma once

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace EStudioUI
{
//! Describes one native composition update independently of window-system headers.
enum class TextCompositionKind
{
    Preedit,
    Commit,
    Cancel
};

//! Owns a composition update; positions count UTF-8 bytes, never UTF-16 code units.
struct TextCompositionEvent
{
    //! Selects temporary presentation, one canonical edit, or cancellation.
    TextCompositionKind m_Kind = TextCompositionKind::Preedit;
    //! Temporary or committed UTF-8; empty for cancellation.
    std::string m_Text{};
    //! Temporary caret byte offset; ignored for commit and cancellation.
    std::size_t m_Caret = 0;
    //! Focused client's lifetime identity; zero targets the current owner-side client.
    std::uint64_t m_Client = 0;
    //! Focus-session generation; zero targets the current owner-side session.
    std::uint64_t m_Epoch = 0;
};

//! Owns the focused editor's surrounding text, selection, and prepared caret geometry.
struct TextInputState
{
    //! Lifetime identity filled by the context, independent of widget addresses.
    std::uint64_t m_Client = 0;
    //! Focus-session generation filled by the context to reject late native updates.
    std::uint64_t m_Epoch = 0;
    //! Committed surrounding UTF-8, without temporary preedit text.
    std::string m_Text{};
    //! Selection anchor byte offset in the committed surrounding text.
    std::size_t m_Anchor = 0;
    //! Selection caret byte offset in the committed surrounding text.
    std::size_t m_Caret = 0;
    //! Client hooks supply layout coordinates; context queries return window coordinates.
    Rect m_CaretBounds{};
    //! Physical desktop caret bounds, filled when the adapter supplies screen metrics.
    std::optional<Rect> m_ScreenCaretBounds{};
    //! Reports an enabled, visible, focused editable client.
    bool m_Active = true;
    //! Reports a temporary preedit presentation that has not changed canonical text.
    bool m_Composing = false;
    //! Prevents native editing while retaining surrounding text for accessibility.
    bool m_ReadOnly = false;
};

//! Optional editor interface; inherit alongside Widget to share native composition handling.
class TextInputClient
{
  public:
    //! Supports ordinary derived-editor lifetime without native ownership.
    virtual ~TextInputClient();

    //! Pure inspection using the last prepared layout; never changes focus or pumps events.
    virtual TextInputState GetTextInputState() const = 0;

    //! Applies an owned temporary update or one undoable validated commit on the UI owner.
    virtual bool OnTextComposition(const TextCompositionEvent& event) = 0;
};

//! Rejects malformed, excessive, or byte-splitting composition updates before delivery.
void ValidateTextComposition(const TextCompositionEvent& event);
} // namespace EStudioUI
