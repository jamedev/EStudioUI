#pragma once

#include "Image.h"
#include "Widget.h"

namespace EStudioUI
{
//! Displays shared image pixels with CSS scaling/alignment and the ordinary retained GPU renderer.
class ImageView : public Widget
{
  public:
    //! Creates an empty or image-backed view without uploading a texture.
    explicit ImageView(Image image = {});

    //! Replaces shared pixels; equal natural dimensions preserve measurement caches.
    void SetImage(Image image);

    //! Returns this view's immutable shared image value.
    const Image& GetImage() const;

    //! Overrides CSS object-fit using contain, cover, fill, none, or scale-down.
    void SetFit(std::string fit);

    //! Overrides CSS image alignment with finite horizontal/vertical fractions in [0,1].
    void SetAlignment(float horizontal, float vertical);

  protected:
    //! Reports natural source dimensions in logical pixels.
    Point Measure(FontFace& font) const override;

    //! Preserves intrinsic aspect ratio when the available content width determines height.
    MeasureResult MeasureConstrained(FontFace& font, const MeasureConstraints& constraints) const override;

    //! Paints the styled background and clipped, aligned image using deferred texture upload.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

  private:
    //! Shares CPU pixels without retaining a renderer or native window.
    Image m_Image;
};
} // namespace EStudioUI
