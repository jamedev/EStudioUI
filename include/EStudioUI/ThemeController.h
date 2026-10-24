#pragma once

#include "Theme.h"
#include "Widget.h"

namespace EStudioUI
{
class UiContext;

//! Applies one theme and follows coalesced OS changes without per-frame CSS parsing.
class ThemeController
{
  public:
    //! Applies defaults after structural CSS and before optional application overrides.
    explicit ThemeController(UiContext& ui, Theme theme = {}, std::string structuralCss = {});
    //! Retires preference callbacks before releasing controller state.
    ~ThemeController();
    //! A controller has one appearance subscription and one UI owner.
    ThemeController(const ThemeController&) = delete;
    //! A controller cannot duplicate its subscription.
    ThemeController& operator=(const ThemeController&) = delete;
    //! Atomically validates and applies a new configuration.
    void SetTheme(Theme theme);
    //! Replaces settings and both CSS layers in one validated publication.
    void Configure(Theme theme, std::string structuralCss, std::string overrideCss = {});
    //! Returns the current owned customization value.
    const Theme& GetTheme() const;
    //! Replaces structural styles without changing user customization.
    void SetStructuralCss(std::string css);
    //! Applies application/document CSS after the theme cascade.
    void SetOverrideCss(std::string css);
    //! Explicitly refreshes preferences on adapters without live notifications.
    void RefreshSystemAppearance();
    //! Returns the latest owned OS preference snapshot.
    const SystemAppearance& GetSystemAppearance() const;

  private:
    //! Resolves the owner and diagnoses expired or foreign-thread access.
    UiContext& Owner() const;
    //! Publishes CSS only if its compiled source would change.
    void Apply(const Theme& theme, const SystemAppearance& appearance, const std::string& structural,
               const std::string& overrides);
    //! Observes the UI root without extending the window lifetime.
    WidgetRef m_Root;
    //! Owns current settings and the latest OS snapshot.
    Theme m_Theme;
    //! Owns queried preferences.
    SystemAppearance m_Appearance;
    //! Owns structural, application and last-applied CSS.
    std::string m_Structural, m_Overrides, m_Applied;
    //! Detects external stylesheet changes without reparsing unchanged owned CSS.
    std::uint64_t m_AppliedRevision = 0;
    //! Retires the OS observation automatically.
    std::optional<ScopedConnection> m_Connection;
};
} // namespace EStudioUI
