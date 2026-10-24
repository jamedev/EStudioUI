#pragma once
#include "FontEditor.h"
#include "ToolButton.h"

namespace EStudioUI
{
//! Compact font command with an owned draft editor and explicit Apply/Cancel semantics.
class FontPicker : public ToolButton
{
  public:
    //! Creates a font picker with the default requested font.
    FontPicker();
    //! Validates and commits a complete font selection.
    void SetFont(FontSelection font);
    //! Returns the last applied font.
    FontSelection GetFont() const;
    //! Opens a popup editing a copy; dismissal preserves the applied value.
    void ShowPicker();
    //! Reports applied complete font values.
    Signal<FontSelection>& FontChanged();

  private:
    //! Last applied requested font.
    FontSelection m_Font;
    //! Logically owned picker popup.
    WidgetRef m_Popup;
    //! Complete-value observers.
    Signal<FontSelection> m_FontChanged;
    //! Bounds observer feedback.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
