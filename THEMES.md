# Application themes

Every `Window` starts with the **Minimal** family and **System** mode: neutral
surfaces, restrained borders, readable text and a modest accent. A platform with
no known application-mode preference falls back to light. The gallery starts
explicitly in Minimal Light; Creator follows System by default.

| Family | Intended character | Light and dark |
| --- | --- | --- |
| Minimal | Neutral surfaces, subtle selection, few visual effects | Yes |
| Engineering | Graphite/gray surfaces, blue accent, smaller corners | Yes |
| Office | Neutral document surfaces and blue commands | Yes |
| Studio | Neutral panels and teal accents | Yes |
| Game | Purple accents, larger corners and short hover transitions | Yes |
| System | OS foreground/background/accent when available | Yes; explicit modes use a neutral fallback |

These are GPU styles for EStudioUI controls, not OS-native widget implementations.
They share geometry and interaction contracts. Choosing a family does not change
the renderer, add runtime dependencies or require external theme files.

## Ordinary application code

```cpp
#include <EStudioUI/Window.h>

void SetEngineeringStyle(EStudioUI::Window& window)
{
    auto theme = window.Themes().GetTheme();
    theme.m_Family = EStudioUI::ThemeFamily::Engineering;
    theme.m_Mode = EStudioUI::ThemeMode::System;
    theme.m_FontSize = 14;
    theme.m_Density = 1;
    theme.m_Accent = EStudioUI::Color{.18f, .42f, .68f, 1};
    theme.m_Radius = 4;
    window.Themes().SetTheme(theme);
    window.Themes().SetOverrideCss("button.primary { font-size: 15px; }");
}
```

`Theme` is an owned value. Optional accent, background, foreground, radius and
reduced-motion overrides can be reset to return to the selected preset. Font size
accepts 9–32 logical pixels, density 0.75–1.5, and radius 0–24. Invalid values or
CSS preserve the last successfully applied configuration. The density setting
adjusts standard control and collection-row heights; explicit application size
contracts still win. Font-family selection remains the existing font API.

`Theme::Resolve()` exposes semantic colors to custom painting.
`Theme::StyleSheet()` generates CSS without a window or filesystem. For a bare
`UiContext`, create an owner-side `ThemeController` whose lifetime ends before the
context. Ordinary `Window` users should use its existing `Themes()` controller.

`Configure(theme, structuralCss, overrideCss)` publishes all three layers once:
structural CSS, generated theme, application/document overrides. This is ordinary
CSS specificity and cascade order, not an unconditional priority override. Scoped
application selectors remain the recommended way to customize individual areas.
An unchanged generated source does not parse or increment the stylesheet revision.
Changing the theme preserves widget objects, models, focus and bindings. Each
window owns its configuration. Owned windows retain the existing initial
stylesheet-copy behavior; configure their own controller explicitly when they
need to follow subsequent preference changes independently.

Calling `UiContext::SetStyleSheet()` directly explicitly hands stylesheet ownership
to the application. Subsequent OS notifications update the controller's owned
snapshot without replacing that raw stylesheet. Call `Configure`, `SetTheme` or
another controller setter to resume controller ownership.

## Operating-system preferences

`window.Platform().GetSystemAppearance()` returns owned, validated values and an
explicit `Native`, `Emulated` or `Unsupported` support result. Mode-known and
accent-known flags distinguish actual observations from fallback colors.

The Windows adapter uses documented `UISettings` foreground/background/accent
colors when C++/WinRT is available in the Windows SDK, with classic system-color
and DWM accent fallbacks. It also reads high contrast and client-area animation
preferences. `WM_SETTINGCHANGE`, `WM_THEMECHANGED`, `WM_SYSCOLORCHANGE` and DWM accent
messages only mark preferences dirty and wake the ordinary loop. One idle platform
turn queries a coalesced snapshot; user callbacks and CSS parsing never run inside
those native messages. COM initialization is balanced and respects an existing
apartment. No system preference is changed.

System mode follows a known dark/light mode and accent. System family additionally
uses the observed foreground/background pair. High contrast in System mode uses
the system palette and disables decorative effects; OS reduced motion applies to
all families unless explicitly overridden. This is a first appearance integration,
not a claim of complete assistive-technology or native-theme fidelity.

The Linux/macOS GLFW adapter currently reports appearance as `Unsupported`; no
desktop preference is guessed. `MemoryPlatformServices` provides deterministic
`Emulated` snapshots and coalesced changes for headless applications and tests.
`RefreshSystemAppearance()` offers an explicit refresh for adapters without live
notifications. All UI/controller/service operations retain owner-thread and idle
phase requirements. An ordinary unchanged frame performs no appearance query.

Windows API references: [Win32 application themes](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes)
and [RoInitialize lifetime](https://learn.microsoft.com/en-us/windows/win32/api/roapi/nf-roapi-roinitialize).

## Gallery and Creator

The gallery header provides a family selector plus Light/Dark controls. Its rich
CSS examples remain deliberately expressive demonstrations; scoped gradients and
animations are not removed by switching the application's neutral chrome. F5
reloads the current CSS assets, and F6 switches light/dark. A custom `--css` file is
also applied after the generated preset so application rules retain normal cascade
precedence.

Creator has an **Appearance** dock tab beside the property Inspector. It edits the
family, System/Light/Dark mode, font size, density, optional colors, corners and
motion policy through the same typed property controls. Interactive edits coalesce
at frame preparation. These are session settings; saving the design stores its
own properties/CSS, not the Creator workspace's theme preferences.
