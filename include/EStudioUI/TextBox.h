#pragma once

#include "FontFace.h"
#include "TextBuffer.h"
#include "Widget.h"

namespace EStudioUI
{
//! A single-line UTF-8 editor with grapheme selection and cached GPU text.
class TextBox final : public Widget
{
  public:
    //! Creates an editable text box.
    explicit TextBox(std::string text = {});

    //! Returns whether the control participates in keyboard navigation.
    bool IsFocusable() const override;

    //! Replaces the text, resetting editing history.
    void SetText(std::string_view text);

    //! Returns valid UTF-8 text.
    const std::string& GetText() const;

    //! Sets text shown when the editor is empty.
    void SetPlaceholder(std::string text);

    //! Allows selection and copying while preventing edits.
    void SetReadOnly(bool readOnly);

    //! Returns whether editing is disabled independently of focus.
    bool IsReadOnly() const;

    //! Limits input bytes without splitting graphemes.
    void SetMaxBytes(std::size_t count);

    //! Selects the entire line.
    void SelectAll();

    //! Returns selected UTF-8 text.
    std::string GetSelectedText() const;

    //! Returns the signal for programmatic or user text changes.
    Signal<const std::string&>& TextChanged();

    //! Returns the signal emitted on Enter.
    Signal<const std::string&>& Submitted();

    //! Returns the caret's next repaint deadline.
    double NextCaretDeadline(double now) const;

  protected:
    //! Measures intrinsic editor dimensions.
    Point Measure(FontFace& font) const override;

    //! Draws clipped text, selection, placeholder, and caret.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

    //! Positions the caret, including double and triple click selection.
    void OnPointerDown(const PointerEvent& event) override;

    //! Extends selection while captured, including horizontal scrolling.
    void OnPointerMove(const PointerEvent& event) override;

    //! Ends pointer selection.
    void OnPointerUp(const PointerEvent& event) override;

    //! Performs navigation, clipboard commands, and undo/redo.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Inserts committed text as one undoable operation.
    void OnText(std::string_view text) override;

    //! Restarts caret timing on keyboard focus.
    void OnFocus(bool focused) override;

    //! Cancels pointer selection.
    void OnCaptureLost() override;

  private:
    //! Recomputes shaping only after text or font size changes.
    void EnsureLayout(FontFace& font) const;

    //! Restarts blinking and brings the caret into view.
    void ResetCaret();

    //! Invalidates presentation and emits committed text changes safely.
    void AfterEdit(bool changed);

    //! Keeps the caret inside the horizontal viewport.
    void EnsureCaretVisible() const;

    //! Converts a pointer position into a grapheme boundary.
    std::size_t PointerOffset(Point position) const;

    //! Stores editing state independently of rendering.
    TextBuffer m_Buffer;

    //! Stores empty-editor hint text.
    std::string m_Placeholder;

    //! Stores read-only mode.
    bool m_ReadOnly = false;

    //! Tracks an active selection drag.
    bool m_Dragging = false;

    //! Stores the start of the caret's current blink cycle.
    double m_CaretEpoch = 0;

    //! Caches the shaped text run.
    mutable TextLayout m_Layout;

    //! Caches the shaped placeholder.
    mutable TextLayout m_PlaceholderLayout;

    //! Records the text used by the shaping cache.
    mutable std::string m_LayoutText;

    //! Records the last shaped CSS font size.
    mutable float m_LayoutSize = -1;

    //! Stores horizontal content scrolling in logical pixels.
    mutable float m_ScrollX = 0;

    //! Delivers committed text changes.
    Signal<const std::string&> m_TextChanged;

    //! Delivers Enter-key submissions.
    Signal<const std::string&> m_Submitted;
};
} // namespace EStudioUI
