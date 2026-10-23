#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Demonstrates persistent inspectors and viewport scrolling without a tab strip.
class ScrollStackGallery
{
  public:
    //! Builds an interactive drawing, properties and notes workspace.
    ScrollStackGallery(Window& window, Widget& page);
    //! Verifies real styled pixels, capture, resize and retained/full equivalence.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Observes the native/headless mode for an isolated framebuffer probe.
    Window& m_Window;
};
} // namespace EStudioUI
