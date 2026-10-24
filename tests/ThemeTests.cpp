#include <EStudioUI/ThemeController.h>
#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/Style.h>
#include <iostream>
#include <stdexcept>

using namespace EStudioUI;

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        UiContext ui(font);
        auto memory = std::make_unique<MemoryPlatformServices>(ui.GetDispatcher());
        auto* adapter = memory.get();
        ui.SetPlatformServices(std::move(memory));
        auto& button = ui.Root().Add<Button>("Engineering");
        ThemeController controller(ui);
        std::size_t checks = 0;
        const auto check = [&](bool passed, const char* message)
        {
            if (!passed)
            {
                throw std::runtime_error(message);
            }
            ++checks;
        };
        for (int family = 0; family < 6; ++family)
        {
            for (const auto mode : {ThemeMode::Light, ThemeMode::Dark})
            {
                Theme theme;
                theme.m_Family = static_cast<ThemeFamily>(family);
                theme.m_Mode = mode;
                const auto css = theme.StyleSheet();
                StyleSheet parsed;
                std::string error;
                check(parsed.Parse(css, error), error.c_str());
                controller.SetTheme(theme);
                ui.Prepare(640, 400);
                const auto palette = theme.Resolve();
                check(ui.Root().GetStyle().m_Background == palette.m_Background,
                      "Theme palette reaches the actual root style");
                check(button.GetStyle().m_Color == palette.m_Foreground, "Preset text colors reach real controls");
                const auto revision = ui.GetStyleSheetRevision();
                controller.SetTheme(theme);
                check(ui.GetStyleSheetRevision() == revision, "An unchanged theme does not parse or republish CSS");
            }
        }
        controller.SetTheme(Theme{});
        ui.PumpPlatformServices();
        const auto revision = ui.GetStyleSheetRevision();
        SystemAppearance appearance;
        appearance.m_ModeKnown = true;
        appearance.m_Dark = true;
        appearance.m_AccentKnown = true;
        appearance.m_Accent = {.2f, .7f, .4f, 1};
        adapter->SetSystemAppearance(appearance);
        check(ui.GetStyleSheetRevision() == revision, "OS changes wait for an idle platform turn");
        ui.PumpPlatformServices();
        check(controller.GetSystemAppearance().m_Dark && ui.GetStyleSheetRevision() == revision + 1,
              "System mode follows one coalesced OS change");
        const auto changed = ui.GetStyleSheetRevision();
        adapter->SetSystemAppearance(appearance);
        ui.PumpPlatformServices();
        check(ui.GetStyleSheetRevision() == changed, "Identical preferences perform no work");
        bool rejected = false;
        auto invalid = controller.GetTheme();
        invalid.m_Density = 0;
        try
        {
            controller.SetTheme(invalid);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        check(rejected && ui.GetStyleSheetRevision() == changed, "Invalid customizations preserve the last good theme");
        controller.SetOverrideCss("button { border-radius: 11px; }");
        ui.Prepare(640, 400);
        check(button.GetStyle().m_Radius == 11, "Application CSS remains above the preset cascade");
        appearance.m_HighContrast = true;
        appearance.m_ReducedMotion = true;
        appearance.m_Background = {0, 0, 0, 1};
        appearance.m_Foreground = {1, 1, 1, 1};
        const auto resolved = Theme{}.Resolve(appearance);
        check(resolved.m_HighContrast && resolved.m_ReducedMotion && resolved.m_Background == appearance.m_Background,
              "System contrast and motion preferences are owned semantic values");
        adapter->SetSystemAppearance(appearance);
        bool recursiveRejected = false;
        auto connection = adapter->AppearanceChanged().Connect(
            [&](const SystemAppearance&)
            {
                try
                {
                    adapter->Pump();
                }
                catch (const std::logic_error&)
                {
                    recursiveRejected = true;
                }
            });
        ui.PumpPlatformServices();
        check(recursiveRejected, "Appearance delivery rejects recursive platform pumping");
        check(controller.GetSystemAppearance().m_HighContrast,
              "High-contrast and reduced-motion CSS parses and publishes on an idle turn");
        ui.Prepare(640, 400);
        const auto external = ui.GetStyleSheetRevision();
        std::string error;
        check(ui.SetStyleSheet("button { border-radius: 3px; }", error), "Raw stylesheet replacement succeeds");
        appearance.m_Dark = false;
        adapter->SetSystemAppearance(appearance);
        ui.PumpPlatformServices();
        check(ui.GetStyleSheetRevision() == external + 1,
              "A raw stylesheet explicitly hands the cascade back to the application");
        controller.SetTheme(Theme{});
        check(ui.GetStyleSheetRevision() == external + 2, "Configuring themes explicitly restores cascade ownership");
        std::cout << "Theme: " << checks << " checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Theme: " << error.what() << '\n';
        return 1;
    }
}
