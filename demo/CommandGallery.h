#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI
{
class MenuBar;
class MessageBox;
class InputDialog;
class Dialog;
class Popup;

//! Demonstrates and verifies reusable command/dialog surfaces using the actual renderer.
class CommandGallery
{
  public:
    //! Creates an idle-by-default interactive workspace of menus, prompts and popovers.
    CommandGallery(Window& window, Widget& page);
    //! Verifies retained pixels, anchored placement, resize and asynchronous interactions.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Observes the native/software host mode for real framebuffer probes.
    Window& m_Window;
    //! Observes the owned demo menu bar.
    MenuBar* m_Menu = nullptr;
    //! Observes the reusable confirmation dialog.
    MessageBox* m_Confirm = nullptr;
    //! Observes the reusable validated input prompt.
    InputDialog* m_Input = nullptr;
    //! Observes the modeless tool surface.
    Dialog* m_Tools = nullptr;
    //! Observes the anchored interactive popup.
    Popup* m_Popover = nullptr;
    //! Observes its layout/resize anchor.
    WidgetRef m_Anchor;
};
} // namespace EStudioUI
