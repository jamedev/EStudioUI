#pragma once

#include "ComboBox.h"
#include "NumericInput.h"
#include "PropertyValue.h"
#include "Slider.h"

#include <array>

namespace EStudioUI
{
//! RGB or HSV channel presentation; canonical colors always remain normalized RGBA.
enum class ColorModel
{
    Rgb,
    Hsv
};

//! A composed GPU-painted palette and RGB/HSV/alpha editor.
class ColorPicker : public Widget
{
  public:
    //! Creates a picker with opaque black selected.
    ColorPicker();

    //! Validates all RGBA channels before changing the canonical color.
    void SetColor(Color color);

    //! Returns canonical normalized RGBA.
    Color GetColor() const;

    //! Changes display channels without changing the color.
    void SetModel(ColorModel model);

    //! Returns the display channel model.
    ColorModel GetModel() const;

    //! Returns one displayed channel for composition and testing.
    NumericInput& Channel(std::size_t index) const;

    //! Reports complete canonical colors after actual changes.
    Signal<Color>& ColorChanged();

  private:
    //! Synchronizes every channel using a bounded suppression scope.
    void Refresh();

    //! Converts the current channel tuple to canonical RGBA.
    void CommitChannels();

    //! Canonical complete color.
    Color m_Color{0, 0, 0, 1};

    //! Current channel presentation.
    ColorModel m_Model = ColorModel::Rgb;

    //! Observes the owned model chooser.
    ComboBox* m_Chooser = nullptr;

    //! Observes the four numeric channel controls.
    std::array<NumericInput*, 4> m_Channels{};

    //! Observes the four channel sliders.
    std::array<Slider*, 4> m_Sliders{};

    //! Observes the four channel labels.
    std::array<Label*, 4> m_Labels{};

    //! Suppresses internal channel synchronization.
    bool m_Syncing = false;

    //! Preserves hue while saturation is zero.
    double m_Hue = 0;

    //! Reports actual canonical changes.
    Signal<Color> m_ColorChanged;
    //! Bounds synchronous reentrant observer delivery.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
