#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Demonstrates shared raster resources and verifies real retained image/icon rendering.
class ImageGallery
{
  public:
    //! Creates an interactive fit/fill, alpha, and command-icon gallery.
    ImageGallery(Window& window, Widget& page, const std::filesystem::path& assets);

    //! Verifies alpha/orientation, cropping, DPI choice, cache budgets and actual framebuffer equivalence.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Observes the host's native/headless mode without owning its context.
    Window& m_Window;
};
} // namespace EStudioUI
