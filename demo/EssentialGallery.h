#pragma once
#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Demonstrates the first common-control batch and verifies its real GPU output.
class EssentialGallery
{
  public:
    //! Creates an interactive scrolling form with rich CSS controls.
    EssentialGallery(Window& window, Widget& page);
    //! Checks actual pixels, virtual popups, layout and idle retained rendering.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Observes the host's headless/native mode.
    Window& m_Window;
};
} // namespace EStudioUI
