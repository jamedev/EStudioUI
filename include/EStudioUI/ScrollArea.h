#pragma once
#include "ScrollWidget.h"

namespace EStudioUI
{
//! Scrolls arbitrary child controls with shared scrollbars, clipping and focus reveal.
class ScrollArea final : public ScrollWidget
{
  public:
    //! Creates an empty viewport and ordinary content container.
    ScrollArea();
    //! Returns the child container; configure its layout and add controls here.
    Widget& Content() const;
    //! Scrolls and invalidates the composition layout dependency.
    void SetScrollOffset(Point offset) override;
    //! Reveals a prepared descendant using transformed layout geometry.
    void Reveal(Widget& descendant);

  protected:
    //! Provides a bounded natural viewport.
    Point Measure(FontFace& font) const override;
    //! Returns the prepared content extent.
    Point GetVirtualSize() const override;
    //! Draws the background and shared scrollbars.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;
    //! Reveals keyboard-focused descendant controls.
    void OnDescendantFocused(Widget& descendant) override;

  private:
    //! Observes the clipping viewport.
    Widget* m_Viewport = nullptr;
    //! Observes the stable content container.
    Widget* m_Content = nullptr;
};
} // namespace EStudioUI
