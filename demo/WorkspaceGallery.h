#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Opens a real application shell and verifies docking on the production renderer.
class WorkspaceGallery
{
  public:
    //! Adds a launch command without embedding fake native chrome inside the gallery.
    WorkspaceGallery(Window& owner, Widget& page);

    //! Exercises panel identity, floating GPU resources, restore and actual pixels.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Opens or focuses the independently usable workspace example.
    void Open();

    //! Observes the native/headless owner.
    Window& m_Owner;

    //! Owns the example surface while the gallery remains live.
    std::unique_ptr<Window> m_Workspace;
};
} // namespace EStudioUI
