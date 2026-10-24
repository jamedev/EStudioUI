#pragma once
#include "TextBox.h"

namespace EStudioUI
{
//! Single-line selectable text, reusing the complete read-only text/IME/accessibility engine.
class SelectableLabel : public TextBox
{
  public:
    //! Creates read-only selectable text with transparent default presentation.
    explicit SelectableLabel(std::string text = {});
};
} // namespace EStudioUI
