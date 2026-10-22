#pragma once

#include <EStudioUI/Window.h>

#include <functional>

namespace EStudioUI
{
//! Exercises F08 measurement, shared tracks, wrapping flow, and allocation reports.
class LayoutGallery
{
  public:
    //! Appends working form, flow, and grid examples to the Layout lab page.
    LayoutGallery(Window& window, Widget& page);

    //! Verifies real resizing, editing, baseline, cache, and framebuffer behavior.
    void Verify(const std::filesystem::path& output, const std::function<void()>& inspect);

  private:
    //! Observes the owner window.
    Window& m_Window;
    //! Observes the baseline-aligned form.
    WidgetRef m_Form;
    //! Observes the height-for-width notes label.
    WidgetRef m_Notes;
    //! Observes the editable name field.
    WidgetRef m_Name;
    //! Observes the wrapping tool row.
    WidgetRef m_Flow;
    //! Observes the shared track example.
    WidgetRef m_Grid;
    //! Observes the paint-only invalidation probe.
    WidgetRef m_Bar;
};
} // namespace EStudioUI
