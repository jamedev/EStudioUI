#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace EStudioUI
{
//! A bounded UTF-8 single-line editor with grapheme navigation and undo history.
class TextBuffer
{
  public:
    //! Replaces all text, sanitizes input, and resets selection and history.
    void SetText(std::string_view text);

    //! Returns the valid UTF-8 text.
    const std::string& GetText() const;

    //! Returns the caret byte offset, always on a grapheme boundary.
    std::size_t GetCaret() const;

    //! Returns the selection anchor byte offset.
    std::size_t GetAnchor() const;

    //! Returns all extended grapheme boundaries, including zero and text length.
    const std::vector<std::size_t>& GetBoundaries() const;

    //! Returns the selected UTF-8 text.
    std::string GetSelectedText() const;

    //! Returns whether the selection contains text.
    bool HasSelection() const;

    //! Sets selection endpoints, snapping them to valid grapheme boundaries.
    void SetSelection(std::size_t anchor, std::size_t caret);

    //! Selects the entire line.
    void SelectAll();

    //! Selects a word or punctuation/whitespace group at the given byte offset.
    void SelectWord(std::size_t offset);

    //! Replaces the selection and records a single undoable operation.
    bool ReplaceSelection(std::string_view text);

    //! Deletes the previous grapheme or word, or the current selection.
    bool Backspace(bool word = false);

    //! Deletes the next grapheme or word, or the current selection.
    bool DeleteForward(bool word = false);

    //! Moves one grapheme or word, optionally extending selection.
    void Move(int direction, bool extend, bool word = false);

    //! Moves to the beginning or end of the line.
    void MoveToEdge(bool end, bool extend);

    //! Restores the previous edit.
    bool Undo();

    //! Restores an undone edit.
    bool Redo();

    //! Changes the byte limit, truncating only at grapheme boundaries.
    void SetMaxBytes(std::size_t count);

    //! Returns sanitized single-line UTF-8, replacing invalid sequences.
    static std::string Sanitize(std::string_view text);

    //! Finds extended grapheme boundaries using utf8proc's Unicode tables.
    static std::vector<std::size_t> Graphemes(std::string_view text);

  private:
    //! An undo checkpoint including selection.
    struct Snapshot
    {
        //! Saved text.
        std::string m_Text;

        //! Saved caret.
        std::size_t m_Caret = 0;

        //! Saved selection anchor.
        std::size_t m_Anchor = 0;
    };

    //! Rebuilds grapheme indexes and clamps selection.
    void Reindex();

    //! Returns the previous or next word boundary.
    std::size_t WordBoundary(std::size_t from, int direction) const;

    //! Returns the word, whitespace, or punctuation category of a grapheme.
    int WordClass(std::size_t offset) const;

    //! Snaps a byte position to the preceding grapheme boundary.
    std::size_t Snap(std::size_t offset) const;

    //! Captures the current editing state.
    Snapshot Capture() const;

    //! Restores a checkpoint and rebuilds indexes.
    void Restore(const Snapshot& snapshot);

    //! Stores a bounded undo checkpoint and clears redo history.
    void Record();

    //! Stores valid UTF-8 text.
    std::string m_Text;

    //! Stores caret location as a UTF-8 byte offset.
    std::size_t m_Caret = 0;

    //! Stores the fixed selection endpoint.
    std::size_t m_Anchor = 0;

    //! Limits input size independently of the platform clipboard.
    std::size_t m_MaxBytes = 16384;

    //! Caches extended grapheme boundaries.
    std::vector<std::size_t> m_Boundaries{0};

    //! Retains at most 100 undo checkpoints.
    std::vector<Snapshot> m_Undo;

    //! Retains redo checkpoints after undo.
    std::vector<Snapshot> m_Redo;
};
} // namespace EStudioUI
