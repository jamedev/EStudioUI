#pragma once

#include "FontFace.h"
#include "Widget.h"

namespace EStudioUI
{
//! Draws cached shaped text, including explicit newline-separated labels.
class Label : public Widget
{
  public:
    //! Creates a text label or a specialized label-derived control.
    explicit Label(std::string text = {}, std::string type = "label");

    //! Changes UTF-8 text and invalidates cached layout.
    void SetText(std::string text);

    //! Returns the UTF-8 label text.
    const std::string& GetText() const;

    //! Overrides CSS text-wrap and requests height-for-width measurement.
    void SetWordWrap(bool enabled);

    //! Returns the current text-wrap presentation.
    bool IsWordWrap() const;

  protected:
    //! Measures shaped lines at the current CSS font size.
    Point Measure(FontFace& font) const override;

    //! Reports wrapped height and the actual vertically aligned first baseline.
    MeasureResult MeasureConstrained(FontFace& font, const MeasureConstraints& constraints) const override;

    //! Draws the background and aligned text lines.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

  private:
    //! Refreshes shaping only after text or font size changes.
    void EnsureLayout(FontFace& font) const;

    //! Returns natural or width-specific shaped lines from a bounded cache.
    const std::vector<TextLayout>& Lines(FontFace& font, std::optional<float> width) const;

    //! Stores the label's UTF-8 text.
    std::string m_Text;

    //! Stores shaped lines for reuse across frames.
    mutable std::vector<TextLayout> m_Lines;

    //! Retains sanitized paragraph bytes corresponding to the natural lines.
    mutable std::vector<std::string> m_Paragraphs;

    //! A bounded width-specific shaping result.
    struct WrappedLines
    {
        //! Exact logical content width, or minus one for an unused entry.
        float m_Width = -1;
        //! Shaped display lines, retaining grapheme boundaries.
        std::vector<TextLayout> m_Lines;
    };

    //! Retains at most three widths rather than a resize history.
    mutable std::array<WrappedLines, 3> m_Wrapped;

    //! Selects the next width-cache replacement.
    mutable std::size_t m_NextWrapped = 0;

    //! Tracks the last shaped font size.
    mutable float m_LayoutSize = -1;
};
} // namespace EStudioUI
