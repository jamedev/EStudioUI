#include <EStudioUI/FontEditor.h>
#include <EStudioUI/UiContext.h>
#include "PropertyNotification.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
class FamilyValidator final : public InputValidator
{
  public:
    ValidationState Validate(std::string_view text) const override
    {
        if (text.size() > FontSelection::s_MaxFamilyBytes)
        {
            return ValidationState::Invalid;
        }
        return FontSelection::IsValidFamilyList(text) ? ValidationState::Acceptable : ValidationState::Intermediate;
    }
};
} // namespace
FontEditor::FontEditor() : Widget("fonteditor")
{
    m_Family = &Add<ComboBox>();
    m_Family->SetEditable(true);
    m_Family->GetEditor().SetMaxBytes(FontSelection::s_MaxFamilyBytes);
    m_Family->GetEditor().SetValidator(std::make_shared<FamilyValidator>());
    m_Family->SetItems({"sans-serif"});
    m_Family->SetWidth(SizeRule::Fill());
    m_Family->SetAccessibleName("Font family");
    auto& row = Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetWidth(SizeRule::Fill());
    auto& sizeField = row.Add<Widget>();
    sizeField.SetWidth(SizeRule::Fill());
    sizeField.SetGap(4);
    sizeField.Add<Label>("Size (px)");
    m_Size = &sizeField.Add<NumericInput>(16);
    m_Size->SetRange(1, 512);
    m_Size->SetDecimals(1);
    m_Size->SetWidth(SizeRule::Fill());
    m_Size->SetAccessibleName("Font size in pixels");
    auto& weightField = row.Add<Widget>();
    weightField.SetWidth(SizeRule::Fill());
    weightField.SetGap(4);
    weightField.Add<Label>("Weight");
    m_Weight = &weightField.Add<NumericInput>(400);
    m_Weight->SetRange(1, 1000);
    m_Weight->SetStep(100);
    m_Weight->SetDecimals(0);
    m_Weight->SetWidth(SizeRule::Fill());
    m_Weight->SetAccessibleName("Font weight");
    m_Italic = &row.Add<CheckBox>("Italic");
    m_Preview = &Add<Label>("The quick brown fox · 0123456789");
    m_Preview->SetWordWrap(true);
    m_Preview->SetWidth(SizeRule::Fill());
    m_Preview->SetHeight(SizeRule::Content(64, 240));
    m_Preview->AddClass("font-preview");
    const WidgetRef self(this);
    auto edit = [self]
    {
        if (auto* live = dynamic_cast<FontEditor*>(self.Get()); live && !live->m_Syncing)
        {
            live->Edited();
        }
    };
    m_Family->CurrentChanged().Connect([edit](int) { edit(); });
    m_Family->GetEditor().FocusLost().Connect(edit);
    m_Family->GetEditor().Submitted().Connect([edit](const std::string&) { edit(); });
    m_Size->ValueChanged().Connect([edit](double) { edit(); });
    m_Weight->ValueChanged().Connect([edit](double) { edit(); });
    m_Italic->Toggled().Connect([edit](bool) { edit(); });
    Refresh();
}

void FontEditor::Validate(const FontSelection& font)
{
    font.Validate();
    if (font.m_Size < 1 || font.m_Size > 512)
    {
        throw std::invalid_argument("Font editor sizes must be between 1 and 512 logical pixels.");
    }
}

void FontEditor::SetFont(FontSelection font)
{
    VerifyStructuralMutation();
    Validate(font);
    if (m_Font == font)
    {
        return;
    }
    m_Font = font;
    const WidgetRef self(this);
    Refresh();
    if (auto* live = dynamic_cast<FontEditor*>(self.Get()); live && live->m_Font == font)
    {
        Detail::EmitPropertyNotification(*live, live->m_NotificationDepth, live->m_FontChanged, font);
    }
}

FontSelection FontEditor::GetFont() const
{
    VerifyAccess();
    return m_Font;
}

void FontEditor::RefreshFamilies()
{
    VerifyStructuralMutation();
    if (!GetContext())
    {
        throw std::logic_error("Attach the font editor before querying registered families.");
    }
    auto families = GetContext()->GetFont().GetFamilies();
    if (std::find(families.begin(), families.end(), m_Font.m_Family) == families.end())
    {
        families.push_back(m_Font.m_Family);
    }
    Detail::PropertySyncScope scope(*this, m_Syncing);
    m_Family->SetItems(std::move(families));
    m_Family->GetEditor().SetText(m_Font.m_Family);
}

void FontEditor::SetPreviewText(std::string text)
{
    VerifyStructuralMutation();
    if (text.size() > 4096)
    {
        throw std::length_error("Font preview accepts at most 4096 bytes.");
    }
    m_Preview->SetText(std::move(text));
}

ComboBox& FontEditor::GetFamilyEditor() const
{
    VerifyAccess();
    return *m_Family;
}

Label& FontEditor::GetPreview() const
{
    VerifyAccess();
    return *m_Preview;
}

Signal<FontSelection>& FontEditor::FontChanged()
{
    VerifyAccess();
    return m_FontChanged;
}

void FontEditor::Refresh()
{
    Detail::PropertySyncScope scope(*this, m_Syncing);
    const WidgetRef self(this);
    m_Family->GetEditor().SetText(m_Font.m_Family);
    if (!self.Get())
    {
        return;
    }
    m_Size->SetValue(m_Font.m_Size);
    if (!self.Get())
    {
        return;
    }
    m_Weight->SetValue(m_Font.m_Weight);
    if (!self.Get())
    {
        return;
    }
    m_Italic->SetChecked(m_Font.m_Style != "normal");
    if (!self.Get())
    {
        return;
    }
    m_Preview->SetStyleProperty(StyleProperty::FontFamily, m_Font.m_Family);
    m_Preview->SetStyleProperty(StyleProperty::FontSize, m_Font.m_Size);
    m_Preview->SetStyleProperty(StyleProperty::FontWeight, m_Font.m_Weight);
    m_Preview->SetStyleProperty(StyleProperty::FontStyle, m_Font.m_Style);
    m_Preview->SetStyleProperty(StyleProperty::TextDirection, m_Font.m_Direction);
    m_Preview->SetStyleProperty(StyleProperty::Language, m_Font.m_Language);
}

void FontEditor::Edited()
{
    auto font = m_Font;
    font.m_Family = m_Family->GetCurrentText();
    if (!FontSelection::IsValidFamilyList(font.m_Family))
    {
        return;
    }
    font.m_Size = static_cast<float>(m_Size->GetValue());
    font.m_Weight = static_cast<float>(m_Weight->GetValue());
    font.m_Style = m_Italic->IsChecked() ? (m_Font.m_Style == "oblique" ? "oblique" : "italic") : "normal";
    SetFont(std::move(font));
}
} // namespace EStudioUI
