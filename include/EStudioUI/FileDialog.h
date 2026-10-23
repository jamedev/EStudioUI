#pragma once

#include "FileDialogSession.h"

namespace EStudioUI
{
class Widget;

//! Small asynchronous native picker entry points bound to a logical widget owner.
class FileDialog
{
  public:
    //! Opens one or several existing files, cancelling when the owner is destroyed.
    static FileDialogSession Open(Widget& owner, FileDialogOptions options = {});

    //! Chooses a save destination without opening or writing the selected file.
    static FileDialogSession Save(Widget& owner, FileDialogOptions options = {});

    //! Selects a folder without introducing a nested framework event loop.
    static FileDialogSession SelectFolder(Widget& owner, FileDialogOptions options = {});

  private:
    //! Starts one validated request and registers owner-bound cancellation.
    static FileDialogSession Start(Widget& owner, FileDialogOptions options);
};
} // namespace EStudioUI
