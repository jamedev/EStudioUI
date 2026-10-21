#pragma once

#include <EStudioUI/Button.h>
#include <EStudioUI/Window.h>

#include <array>

namespace EStudioUI
{
//! Interactive original CSS examples and real GPU regression/performance probes.
class RichCssGallery
{
  public:
    //! Builds twelve working buttons and a styled text input.
    RichCssGallery(Window& window, Widget& page);

    //! Checks pixels, transformed input, batching, and cache reuse in separate probe windows.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Observes the containing native window.
    Window& m_Window;

    //! Observes button targets for deterministic hover captures.
    std::array<Button*, 12> m_Buttons{};

    //! Counts working button activations.
    int m_Clicks = 0;
};
} // namespace EStudioUI
