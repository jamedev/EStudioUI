#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Interactive anchor relationships and real-renderer resize verification.
class ConstraintGallery
{
  public:
    //! Creates CSS-styled controls arranged by linear constraints.
    ConstraintGallery(Window& window, Widget& page);
    //! Checks intermediate geometry, retained/full pixels, and callback-time presentation.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Observes the owner window.
    Window& m_Window;
    //! Observes the constraint container.
    WidgetRef m_Body;
    //! Observes the sidebar.
    WidgetRef m_Sidebar;
    //! Observes the expanding document area.
    WidgetRef m_Document;
    //! Observes the aspect-ratio badge.
    WidgetRef m_Badge;
};
} // namespace EStudioUI
