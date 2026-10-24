#pragma once
#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Real search, feedback and common-control examples sharing application-owned state.
class ProductivityGallery
{
  public:
    //! Builds two interactive pages using existing application services and GPU controls.
    ProductivityGallery(Window& window, Widget& searchPage, Widget& controlsPage);
    //! Retires receiver delivery before bounded background work.
    ~ProductivityGallery();
    //! Exercises real GPU controls and retained/full framebuffer equality in both theme modes.
    void Verify(const std::filesystem::path& output);

  private:
    //! Owns application state and typed weak controls independently of callback execution.
    struct Data;
    //! Owns examples without raw callbacks retaining this gallery.
    std::shared_ptr<Data> m_Data;
    //! Observes the native owner used for real framebuffer probes.
    Window& m_Window;
};
} // namespace EStudioUI
