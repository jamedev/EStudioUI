#pragma once

#include "WindowsSupport.h"
#include <EStudioUI/FileDialogSession.h>

#ifdef _WIN32
#include <memory>

namespace EStudioUI::Detail
{
//! Owns one native STA picker worker while the host retains its HWND.
class WindowsFileDialog
{
  public:
    //! Observes the host HWND and its thread-safe native wake operation.
    WindowsFileDialog(HWND owner, std::function<void()> wake);

    //! Joins after native retirement has observed completed worker cleanup.
    ~WindowsFileDialog();

    //! Starts one copied picker request, reporting Busy while a previous picker unwinds.
    void Begin(FileDialogOptions options, std::stop_token stop, std::function<void(FileDialogResult)> complete);

    //! Posts cancellation to the picker STA without blocking the owner.
    void Cancel();

    //! Reports whether the host HWND still has an active native worker lease.
    bool IsRunning() const;

  private:
    //! Owns worker synchronization and join state on the UI owner.
    struct Impl;

    //! Keeps Windows and thread headers out of the service boundary.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI::Detail
#endif
