#pragma once

#include "PlatformTypes.h"
#include <optional>

namespace EStudioUI
{
//! Preset design languages, each supporting light and dark palettes.
enum class ThemeFamily
{
    Minimal,
    Engineering,
    Office,
    Studio,
    Game,
    System
};
//! Explicit application mode or the OS preference with a deterministic light fallback.
enum class ThemeMode
{
    Light,
    Dark,
    System
};

//! Resolved semantic colors for custom GPU widgets.
struct ThemePalette
{
    //! Application background and raised/editor surfaces.
    Color m_Background, m_Surface, m_Input;
    //! Primary and secondary foreground colors.
    Color m_Foreground, m_Muted;
    //! Separators and pointer feedback surfaces.
    Color m_Border, m_Hover, m_Selected;
    //! Accent and its contrast-selected foreground.
    Color m_Accent, m_OnAccent;
    //! Resolved dark, high-contrast and motion preferences.
    bool m_Dark = false, m_HighContrast = false, m_ReducedMotion = false;
};

//! Owned, tunable GPU theme; CSS is generated only when settings change.
class Theme
{
  public:
    //! Defaults to a restrained neutral interface.
    ThemeFamily m_Family = ThemeFamily::Minimal;
    //! Follows OS application colors when available.
    ThemeMode m_Mode = ThemeMode::System;
    //! Optional independent semantic color overrides.
    std::optional<Color> m_Accent, m_Background, m_Foreground;
    //! Base font size in logical pixels.
    float m_FontSize = 14;
    //! Control spacing multiplier in the range 0.75 to 1.5.
    float m_Density = 1;
    //! Optional corner radius in logical pixels.
    std::optional<float> m_Radius;
    //! Overrides the OS motion preference when provided.
    std::optional<bool> m_ReducedMotion;
    //! Resolves semantic colors without touching a UI, GL or native handle.
    ThemePalette Resolve(const SystemAppearance& appearance = {}) const;
    //! Generates complete ordinary framework CSS without file dependencies.
    std::string StyleSheet(const SystemAppearance& appearance = {}) const;
    //! Returns the stable display name for a preset.
    static std::string_view Name(ThemeFamily family);
    //! Compares owned customization settings.
    bool operator==(const Theme&) const = default;
};
} // namespace EStudioUI
