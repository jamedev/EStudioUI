#pragma once

#include <EStudioUI/Window.h>
#include <EStudioUI/Label.h>

namespace EStudioUI
{
//! Demonstrates shared CSS typography, fallback and visual single-line editing.
class TextGallery
{
  public:
    //! Builds interactive text examples from ordinary library widgets.
    TextGallery(Window& window, Widget& page);
    //! Registers the demo's bounded, bundled outline fonts before UI creation.
    static void AddFonts(WindowOptions& options, const std::filesystem::path& assets,
                         const std::filesystem::path& primary);
    //! Verifies actual GPU glyph identities, bidi selections and retained rendering.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Observes the containing native window.
    Window& m_Window;
    //! Keeps controller callbacks receiver-bound to their widgets.
    std::vector<ScopedConnection> m_Connections;
};
} // namespace EStudioUI
