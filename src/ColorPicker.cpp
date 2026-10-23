#include "PropertyNotification.h"
#include <EStudioUI/ColorPicker.h>
#include <EStudioUI/ToolButton.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
ColorPicker::ColorPicker() : Widget("colorpicker")
{
    SetGap(6);
    m_Chooser = &Add<ComboBox>();
    m_Chooser->SetItems({"RGB", "HSV"});
    m_Chooser->SetAccessibleName("Color channels");
    const WidgetRef self(this);
    m_Chooser->CurrentChanged().Connect(
        [self](int index)
        {
            if (auto* live = dynamic_cast<ColorPicker*>(self.Get()); live && !live->m_Syncing)
            {
                live->SetModel(index == 1 ? ColorModel::Hsv : ColorModel::Rgb);
            }
        });
    for (std::size_t index = 0; index < 4; ++index)
    {
        auto& row = Add<Widget>("color-channel");
        row.SetLayoutDirection(Axis::Horizontal);
        row.SetGap(6);
        m_Labels[index] = &row.Add<Label>();
        m_Labels[index]->SetWidth(SizeRule::Fixed(18));
        m_Sliders[index] = &row.Add<Slider>();
        m_Sliders[index]->SetWidth(SizeRule::Fill(1, 60));
        m_Channels[index] = &row.Add<NumericInput>();
        m_Channels[index]->SetWidth(SizeRule::Fixed(108));
        m_Channels[index]->SetDecimals(3);
        m_Channels[index]->ValueChanged().Connect(
            [self](double)
            {
                if (auto* live = dynamic_cast<ColorPicker*>(self.Get()); live && !live->m_Syncing)
                {
                    live->CommitChannels();
                }
            });
        m_Sliders[index]->ValueChanged().Connect(
            [self, index](double value)
            {
                if (auto* live = dynamic_cast<ColorPicker*>(self.Get()); live && !live->m_Syncing)
                {
                    live->m_Channels[index]->SetValue(value);
                }
            });
    }
    auto& palette = Add<Widget>("color-palette");
    palette.SetLayoutDirection(Axis::Horizontal);
    palette.SetGap(4);
    for (const Color color : {Color{0.95f, 0.2f, 0.25f, 1}, Color{1, 0.6f, 0.1f, 1}, Color{0.9f, 0.85f, 0.2f, 1},
                              Color{0.2f, 0.8f, 0.5f, 1}, Color{0.2f, 0.65f, 1, 1}, Color{0.6f, 0.4f, 0.95f, 1},
                              Color{1, 1, 1, 1}, Color{0.08f, 0.1f, 0.14f, 1}})
    {
        auto& swatch = palette.Add<ToolButton>();
        swatch.SetWidth(SizeRule::Fill(1, 20));
        swatch.SetHeight(SizeRule::Fixed(24));
        swatch.SetStyleProperty(StyleProperty::Background, color);
        swatch.SetAccessibleName(ColorToHex(color));
        swatch.Clicked().Connect(
            [self, color]
            {
                if (auto* live = dynamic_cast<ColorPicker*>(self.Get()))
                {
                    live->SetColor(color);
                }
            });
    }
    Refresh();
}

void ColorPicker::SetColor(Color color)
{
    VerifyStructuralMutation();
    ValidateColor(color);
    if (color == m_Color)
    {
        return;
    }
    m_Color = color;
    const WidgetRef self(this);
    Refresh();
    if (self.Get() && m_Color == color)
    {
        Detail::EmitPropertyNotification(*this, m_NotificationDepth, m_ColorChanged, color);
    }
}

Color ColorPicker::GetColor() const
{
    VerifyAccess();
    return m_Color;
}

void ColorPicker::SetModel(ColorModel model)
{
    VerifyStructuralMutation();
    if (model != ColorModel::Rgb && model != ColorModel::Hsv)
    {
        throw std::invalid_argument("Unknown color channel model.");
    }
    m_Model = model;
    Refresh();
}

ColorModel ColorPicker::GetModel() const
{
    VerifyAccess();
    return m_Model;
}

NumericInput& ColorPicker::Channel(std::size_t index) const
{
    VerifyAccess();
    return *m_Channels.at(index);
}

Signal<Color>& ColorPicker::ColorChanged()
{
    VerifyAccess();
    return m_ColorChanged;
}

void ColorPicker::Refresh()
{
    double values[]{m_Color.m_R, m_Color.m_G, m_Color.m_B, m_Color.m_A};
    if (m_Model == ColorModel::Hsv)
    {
        const double maximum = std::max({values[0], values[1], values[2]});
        const double minimum = std::min({values[0], values[1], values[2]});
        const double delta = maximum - minimum;
        if (delta > 1e-8)
        {
            const double hue = maximum == values[0]   ? (values[1] - values[2]) / delta
                               : maximum == values[1] ? 2 + (values[2] - values[0]) / delta
                                                      : 4 + (values[0] - values[1]) / delta;
            m_Hue = std::fmod(hue * 60 + 360, 360);
        }
        values[0] = m_Hue;
        values[1] = maximum == 0 ? 0 : delta / maximum;
        values[2] = maximum;
    }
    const WidgetRef self(this);
    Detail::PropertySyncScope synchronizing(*this, m_Syncing);
    m_Chooser->SetCurrentIndex(m_Model == ColorModel::Rgb ? 0 : 1);
    if (!self.Get())
    {
        return;
    }
    for (std::size_t index = 0; index < 4; ++index)
    {
        const std::string label(1, (m_Model == ColorModel::Rgb ? "RGBA" : "HSVA")[index]);
        m_Labels[index]->SetText(label);
        m_Channels[index]->SetAccessibleName(label);
        const double maximum = m_Model == ColorModel::Hsv && index == 0 ? 360 : 1;
        m_Channels[index]->SetRange(0, maximum);
        if (!self.Get())
        {
            return;
        }
        m_Sliders[index]->SetRange(0, maximum);
        if (!self.Get())
        {
            return;
        }
        m_Sliders[index]->SetStep(maximum / 1000);
        m_Channels[index]->SetStep(maximum / 1000);
        m_Channels[index]->SetValue(values[index]);
        if (!self.Get())
        {
            return;
        }
        m_Sliders[index]->SetValue(values[index]);
        if (!self.Get())
        {
            return;
        }
    }
    synchronizing.Reset();
}

void ColorPicker::CommitChannels()
{
    double channels[4]{};
    for (std::size_t index = 0; index < 4; ++index)
    {
        channels[index] = m_Channels[index]->GetValue();
    }
    if (m_Model == ColorModel::Hsv)
    {
        m_Hue = channels[0];
        const double hue = std::fmod(channels[0], 360) / 60;
        const double chroma = channels[2] * channels[1];
        const double secondary = chroma * (1 - std::abs(std::fmod(hue, 2) - 1));
        const double offset = channels[2] - chroma;
        const double tuples[6][3]{{chroma, secondary, 0}, {secondary, chroma, 0}, {0, chroma, secondary},
                                  {0, secondary, chroma}, {secondary, 0, chroma}, {chroma, 0, secondary}};
        const auto sector = static_cast<std::size_t>(hue) % 6;
        for (std::size_t index = 0; index < 3; ++index)
        {
            channels[index] = std::clamp(tuples[sector][index] + offset, 0.0, 1.0);
        }
    }
    SetColor({static_cast<float>(channels[0]), static_cast<float>(channels[1]), static_cast<float>(channels[2]),
              static_cast<float>(channels[3])});
}
} // namespace EStudioUI
