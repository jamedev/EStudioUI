#pragma once

#include "ColorPicker.h"
#include "Popup.h"
#include "TextBox.h"
#include "ToolButton.h"

namespace EStudioUI
{
//! A compact hexadecimal color field with an asynchronous RGB/HSV/alpha popover.
class ColorEditor : public Widget
{
  public:
    //! Creates an opaque black editor.
    ColorEditor();

    //! Validates and publishes a complete canonical color.
    void SetColor(Color color);

    //! Returns the canonical color independently of intermediate hex input.
    Color GetColor() const;

    //! Returns the ordinary hexadecimal editor.
    TextBox& Editor() const;

    //! Commits exact hexadecimal input, preserving the last color on invalid input.
    bool Commit();

    //! Opens an anchored asynchronous picker; requires attachment to a UI.
    PopupSession OpenPicker();

    //! Reports complete canonical colors.
    Signal<Color>& ColorChanged();

  private:
    //! Canonical complete color.
    Color m_Color{0, 0, 0, 1};

    //! Observes the hexadecimal field.
    TextBox* m_Editor = nullptr;

    //! Observes the swatch command.
    ToolButton* m_Swatch = nullptr;

    //! Observes a root-owned popover.
    WidgetRef m_Popup;

    //! Observes the owned picker without extending its UI lifetime.
    WidgetRef m_Picker;

    //! Suppresses synchronization feedback.
    bool m_Syncing = false;

    //! Reports actual canonical changes.
    Signal<Color> m_ColorChanged;
    //! Bounds synchronous reentrant observer delivery.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
