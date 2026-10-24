#pragma once
#include "ComboBox.h"
#include "NumericInput.h"
#include "CheckBox.h"
#include "Label.h"

namespace EStudioUI
{
//! Font-family, pixel-size, weight and italic controls with a cached live GPU preview.
class FontEditor : public Widget
{
  public:
    //! Creates a sans-serif 16 px font editor.
    FontEditor();
    //! Validates and changes the complete font selection.
    void SetFont(FontSelection font);
    //! Returns the complete requested font selection, including language and direction.
    FontSelection GetFont() const;
    //! Refreshes family choices from the attached font service's registered catalog.
    void RefreshFamilies();
    //! Changes preview text, bounded to 4096 bytes.
    void SetPreviewText(std::string text);
    //! Returns the family chooser for advanced completion configuration.
    ComboBox& GetFamilyEditor() const;
    //! Reports complete requested font selections.
    Signal<FontSelection>& FontChanged();
    //! Validates bounds and supported CSS font values without changing UI.
    static void Validate(const FontSelection& font);

  private:
    //! Synchronizes child controls and preview with bounded feedback suppression.
    void Refresh();
    //! Applies a complete candidate from child controls.
    void Edited();
    //! Committed requested font.
    FontSelection m_Font;
    //! Family choice field.
    ComboBox* m_Family = nullptr;
    //! Pixel-size field.
    NumericInput* m_Size = nullptr;
    //! Weight field.
    NumericInput* m_Weight = nullptr;
    //! Italic toggle.
    CheckBox* m_Italic = nullptr;
    //! Live cached preview.
    Label* m_Preview = nullptr;
    //! Suppresses refresh feedback.
    bool m_Syncing = false;
    //! Selection observers.
    Signal<FontSelection> m_FontChanged;
    //! Bounds observer feedback.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
