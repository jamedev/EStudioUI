#include <EStudioUI/ThemeController.h>
#include <EStudioUI/UiContext.h>
#include <stdexcept>

namespace EStudioUI
{
ThemeController::ThemeController(UiContext& ui, Theme theme, std::string structuralCss)
    : m_Root(&ui.Root()), m_Theme(std::move(theme)), m_Structural(std::move(structuralCss))
{
    m_Appearance = ui.Platform().GetSystemAppearance();
    Apply(m_Theme, m_Appearance, m_Structural, m_Overrides);
    m_Connection.emplace(ui.Platform().AppearanceChanged().Connect(
        [this](const SystemAppearance& appearance)
        {
            // Installing a raw stylesheet is an explicit escape from the owned cascade.
            if (Owner().GetStyleSheetRevision() == m_AppliedRevision)
            {
                Apply(m_Theme, appearance, m_Structural, m_Overrides);
            }
            m_Appearance = appearance;
        }));
}

ThemeController::~ThemeController() = default;

UiContext& ThemeController::Owner() const
{
    auto* root = m_Root.Get();
    if (!root || !root->GetContext())
    {
        throw std::logic_error("The theme's UI owner has expired.");
    }
    root->VerifyAccess();
    return *root->GetContext();
}

void ThemeController::Apply(const Theme& theme, const SystemAppearance& appearance, const std::string& structural,
                            const std::string& overrides)
{
    auto& ui = Owner();
    auto css = structural + '\n' + theme.StyleSheet(appearance) + '\n' + overrides;
    if (css == m_Applied && m_AppliedRevision == ui.GetStyleSheetRevision())
    {
        return;
    }
    std::string error;
    if (!ui.SetStyleSheet(css, error))
    {
        throw std::invalid_argument("Theme stylesheet: " + error);
    }
    m_Applied = std::move(css);
    m_AppliedRevision = ui.GetStyleSheetRevision();
}

void ThemeController::Configure(Theme theme, std::string structuralCss, std::string overrideCss)
{
    Apply(theme, m_Appearance, structuralCss, overrideCss);
    m_Theme = std::move(theme);
    m_Structural = std::move(structuralCss);
    m_Overrides = std::move(overrideCss);
}

void ThemeController::SetTheme(Theme theme)
{
    Apply(theme, m_Appearance, m_Structural, m_Overrides);
    m_Theme = std::move(theme);
}

const Theme& ThemeController::GetTheme() const
{
    (void)Owner();
    return m_Theme;
}

void ThemeController::SetStructuralCss(std::string css)
{
    Apply(m_Theme, m_Appearance, css, m_Overrides);
    m_Structural = std::move(css);
}

void ThemeController::SetOverrideCss(std::string css)
{
    Apply(m_Theme, m_Appearance, m_Structural, css);
    m_Overrides = std::move(css);
}

void ThemeController::RefreshSystemAppearance()
{
    const auto appearance = Owner().Platform().GetSystemAppearance();
    Apply(m_Theme, appearance, m_Structural, m_Overrides);
    m_Appearance = appearance;
}

const SystemAppearance& ThemeController::GetSystemAppearance() const
{
    (void)Owner();
    return m_Appearance;
}
} // namespace EStudioUI
