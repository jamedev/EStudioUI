#pragma once

#include <EStudioUI/Window.h>
#include <EStudioUI/Label.h>

namespace EStudioUI
{
//! Exercises capability-aware native services and GPU-independent widget semantics.
class PlatformGallery
{
  public:
    //! Builds a usable native-service page using the public API.
    PlatformGallery(Window& window, Widget& page, std::filesystem::path assets);
    //! Checks injected dialogs, production file-drop callbacks, owned windows and actual pixels.
    void Verify(const std::filesystem::path& output);

  private:
    //! Starts one asynchronous picker without a nested framework event loop.
    void Pick(FileDialogKind kind);
    //! Opens an owned secondary native window with an ordinary close command.
    void OpenOwnedWindow();
    //! Reads current capabilities and physical/logical coordinate metrics on demand.
    void Inspect();
    //! Observes the containing window.
    Window& m_Window;
    //! Locates the font used by secondary windows and the isolated probe.
    std::filesystem::path m_Assets;
    //! Observes the clipboard editor.
    WidgetRef m_Text;
    //! Observes the owned file-drop display.
    WidgetRef m_Drop;
    //! Observes the operation status.
    WidgetRef m_Status;
    //! Observes the capability and semantic readout.
    WidgetRef m_Readout;
    //! Observes the copy command for the integration probe.
    WidgetRef m_Copy;
    //! Observes the open command for the integration probe.
    WidgetRef m_Open;
    //! Owns the cancellable asynchronous result.
    FileDialogSession m_Picker;
    //! Owns secondary C++ windows independently of native ownership.
    std::vector<std::unique_ptr<Window>> m_Windows;
    //! Disconnects controller callbacks before its state is destroyed.
    std::vector<ScopedConnection> m_Connections;
    //! Expires deferred controller inspection before callback connections or widgets are torn down.
    std::shared_ptr<int> m_Lifetime = std::make_shared<int>(0);
};
} // namespace EStudioUI
