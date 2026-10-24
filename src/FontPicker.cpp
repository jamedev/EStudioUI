#include <EStudioUI/FontPicker.h>
#include <EStudioUI/Popup.h>
#include <EStudioUI/UiContext.h>
#include "PropertyNotification.h"
#include <stdexcept>

namespace EStudioUI
{
FontPicker::FontPicker() : ToolButton("sans-serif · 16 px")
{
    AddClass("fontpicker");
    const WidgetRef self(this);
    Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<FontPicker*>(self.Get()))
            {
                live->ShowPicker();
            }
        });
}

void FontPicker::SetFont(FontSelection font)
{
    VerifyStructuralMutation();
    FontEditor::Validate(font);
    if (font == m_Font)
    {
        return;
    }
    m_Font = font;
    SetText(font.m_Family + " · " + std::to_string(static_cast<int>(font.m_Size)) + " px");
    Detail::EmitPropertyNotification(*this, m_NotificationDepth, m_FontChanged, font);
}

FontSelection FontPicker::GetFont() const
{
    VerifyAccess();
    return m_Font;
}

Signal<FontSelection>& FontPicker::FontChanged()
{
    VerifyAccess();
    return m_FontChanged;
}

void FontPicker::ShowPicker()
{
    VerifyStructuralMutation();
    if (!GetContext())
    {
        throw std::logic_error("Attach the font picker before showing its editor.");
    }
    auto* popup = dynamic_cast<Popup*>(m_Popup.Get());
    if (popup && popup->GetContext() != GetContext())
    {
        popup = nullptr;
    }
    if (!popup)
    {
        popup = &Popup::Create(*this);
        popup->AddClass("font-picker-popup");
        popup->SetWidth(SizeRule::Fixed(420));
        popup->SetHeight(SizeRule::Content());
        auto& editor = popup->Add<FontEditor>();
        editor.SetWidth(SizeRule::Fill());
        auto& row = popup->Add<Widget>();
        row.SetLayoutDirection(Axis::Horizontal);
        auto& apply = row.Add<ToolButton>("Apply");
        auto& cancel = row.Add<ToolButton>("Cancel");
        const WidgetRef self(this), surface(popup), field(&editor);
        apply.Clicked().Connect(
            [self, surface, field]
            {
                auto* edit = dynamic_cast<FontEditor*>(field.Get());
                if (!edit)
                {
                    return;
                }
                const auto font = edit->GetFont();
                if (auto* menu = dynamic_cast<Popup*>(surface.Get()))
                {
                    menu->Close({PopupCloseReason::Accepted, 0});
                }
                if (auto* live = dynamic_cast<FontPicker*>(self.Get()))
                {
                    live->SetFont(font);
                }
            });
        cancel.Clicked().Connect(
            [surface]
            {
                if (auto* menu = dynamic_cast<Popup*>(surface.Get()))
                {
                    menu->Close({PopupCloseReason::Rejected, 0});
                }
            });
        m_Popup = WidgetRef(popup);
    }
    auto& editor = static_cast<FontEditor&>(*popup->GetChildren().front());
    editor.SetFont(m_Font);
    editor.RefreshFamilies();
    popup->Open(*this);
}
} // namespace EStudioUI
