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

  protected:
    //! Measures shaped lines at the current CSS font size.
    Point Measure(FontFace& font) const override;

    //! Draws the background and aligned text lines.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

  private:
    //! Refreshes shaping only after text or font size changes.
    void EnsureLayout(FontFace& font) const;

    //! Stores the label's UTF-8 text.
    std::string m_Text;

    //! Stores shaped lines for reuse across frames.
    mutable std::vector<TextLayout> m_Lines;

    //! Tracks the last shaped font size.
    mutable float m_LayoutSize = -1;
};
} // namespace EStudioUI
