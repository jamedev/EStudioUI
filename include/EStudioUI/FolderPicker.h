#pragma once

#include "FilePathEditor.h"

namespace EStudioUI
{
//! A folder path field using the ordinary path editor and asynchronous native folder picker.
class FolderPicker final : public FilePathEditor
{
  public:
    //! Creates an editable folder field with recent-path choices.
    FolderPicker();
};
} // namespace EStudioUI
