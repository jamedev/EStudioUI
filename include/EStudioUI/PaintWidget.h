#pragma once

#include "Painter.h"
#include "Widget.h"

namespace EStudioUI
{
//! A CSS-styled widget extension point for custom GPU drawing in local content coordinates.
class PaintWidget : public Widget
{
  public:
    //! Creates a named custom widget; ordinary CSS and child composition remain available.
    explicit PaintWidget(std::string type = "paintwidget");

  protected:
    //! Draws custom content after the CSS background; the painter is clipped to the content box.
    virtual void PaintContent(Painter& painter) const;

    //! Returns content dimensions in local coordinates, with origin at zero.
    Rect GetPaintBounds() const;

    //! Requests a new custom-paint frame without changing layout or resolving CSS.
    void InvalidatePainting();

    //! Bridges the existing widget renderer to the scoped local painter.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;
};
} // namespace EStudioUI
