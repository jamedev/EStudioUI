#include "PropertyNotification.h"
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/Renderer.h>

#include <stdexcept>

namespace EStudioUI
{
namespace
{
class ColorSwatch final : public ToolButton
{
  protected:
    void Paint(Renderer& renderer, FontFace&, double, float opacity) const override
    {
        const auto bounds = GetBounds();
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                Style tile;
                const float shade = (x + y) % 2 == 0 ? 0.8f : 0.4f;
                tile.m_Background = {shade, shade, shade, 1};
                renderer.DrawBox({bounds.m_X + static_cast<float>(x) * bounds.m_Width / 4,
                                  bounds.m_Y + static_cast<float>(y) * bounds.m_Height / 4, bounds.m_Width / 4,
                                  bounds.m_Height / 4},
                                 tile, GetClip(), opacity);
            }
        }
        renderer.DrawBox(bounds, GetStyle(), GetClip(), opacity);
    }
};
} // namespace

ColorEditor::ColorEditor() : Widget("coloreditor")
{
    SetLayoutDirection(Axis::Horizontal);
    SetGap(6);
    m_Swatch = &Add<ColorSwatch>();
    m_Swatch->SetWidth(SizeRule::Fixed(32));
    m_Swatch->SetHeight(SizeRule::Fixed(30));
    m_Swatch->SetAccessibleName("Choose color");
    m_Swatch->SetStyleProperty(StyleProperty::Background, m_Color);
    m_Editor = &Add<TextBox>(ColorToHex(m_Color));
    m_Editor->SetMaxBytes(9);
    m_Editor->SetWidth(SizeRule::Fill(1, 80));
    m_Editor->SetAccessibleName("Hexadecimal RGBA");
    SetBaselineChild(m_Editor);
    const WidgetRef self(this);
    m_Swatch->Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<ColorEditor*>(self.Get()))
            {
                live->OpenPicker();
            }
        });
    m_Editor->Submitted().Connect(
        [self](const std::string&)
        {
            if (auto* live = dynamic_cast<ColorEditor*>(self.Get()))
            {
                live->Commit();
            }
        });
    m_Editor->FocusLost().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<ColorEditor*>(self.Get()))
            {
                live->Commit();
            }
        });
}

void ColorEditor::SetColor(Color color)
{
    VerifyStructuralMutation();
    ValidateColor(color);
    const bool changed = m_Color != color;
    m_Color = color;
    const WidgetRef self(this);
    Detail::PropertySyncScope synchronizing(*this, m_Syncing);
    m_Editor->SetText(ColorToHex(color));
    if (!self.Get() || m_Color != color)
    {
        return;
    }
    SetState(Invalid, false);
    m_Swatch->SetStyleProperty(StyleProperty::Background, color);
    if (auto* picker = dynamic_cast<ColorPicker*>(m_Picker.Get()))
    {
        picker->SetColor(color);
    }
    if (!self.Get() || m_Color != color)
    {
        return;
    }
    synchronizing.Reset();
    if (changed)
    {
        Detail::EmitPropertyNotification(*this, m_NotificationDepth, m_ColorChanged, color);
    }
}

Color ColorEditor::GetColor() const
{
    VerifyAccess();
    return m_Color;
}

TextBox& ColorEditor::Editor() const
{
    VerifyAccess();
    return *m_Editor;
}

bool ColorEditor::Commit()
{
    VerifyStructuralMutation();
    if (m_Syncing)
    {
        return true;
    }
    Color color;
    try
    {
        color = ColorFromHex(m_Editor->GetText());
    }
    catch (const std::invalid_argument&)
    {
        SetState(Invalid, true);
        return false;
    }
    SetColor(color);
    return true;
}

PopupSession ColorEditor::OpenPicker()
{
    VerifyStructuralMutation();
    if (!GetContext())
    {
        throw std::logic_error("Attach a color editor before opening its picker.");
    }
    auto* popup = dynamic_cast<Popup*>(m_Popup.Get());
    if (!popup)
    {
        popup = &Popup::Create(*this);
        popup->AddClass("color-popover");
        popup->SetWidth(SizeRule::Fixed(360));
        popup->SetHeight(SizeRule::Content());
        auto& picker = popup->Add<ColorPicker>();
        m_Popup = WidgetRef(popup);
        m_Picker = WidgetRef(&picker);
        const WidgetRef self(this);
        picker.ColorChanged().Connect(
            [self](Color color)
            {
                if (auto* live = dynamic_cast<ColorEditor*>(self.Get()); live && !live->m_Syncing)
                {
                    live->SetColor(color);
                }
            });
    }
    const WidgetRef self(this);
    const WidgetRef surface(popup);
    static_cast<ColorPicker*>(m_Picker.Get())->SetColor(m_Color);
    if (!self.Get() || !surface.Get())
    {
        return {};
    }
    return popup->Open(*m_Swatch);
}

Signal<Color>& ColorEditor::ColorChanged()
{
    VerifyAccess();
    return m_ColorChanged;
}
} // namespace EStudioUI
