#include "DatabaseBrowserWindow.h"
#include "Verification.h"
#include <EStudioUI/ThemeController.h>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{
int Run(const std::vector<std::filesystem::path>& arguments)
{
    try
    {
        bool headless = false, selfTest = false;
        std::filesystem::path open, output = "database-verification";
        auto assets = std::filesystem::absolute(arguments.front()).parent_path() / "assets";
        for (std::size_t i = 1; i < arguments.size(); ++i)
        {
            if (arguments[i] == "--headless")
            {
                headless = true;
            }
            else if (arguments[i] == "--self-test")
            {
                selfTest = true;
            }
            else if (arguments[i] == "--open" && i + 1 < arguments.size())
            {
                open = arguments[++i];
            }
            else if (arguments[i] == "--output" && i + 1 < arguments.size())
            {
                output = arguments[++i];
            }
            else if (arguments[i] == "--assets" && i + 1 < arguments.size())
            {
                assets = arguments[++i];
            }
            else
            {
                throw std::invalid_argument("Usage: EStudioUIDatabaseBrowser [--open database.sqlite] [--headless "
                                            "--self-test --output folder] [--assets folder]");
            }
        }
        EStudioUI::Application application;
        EStudioUI::WindowOptions options;
        options.m_Title = "EStudioUI Database Browser";
        options.m_Width = 1500;
        options.m_Height = 920;
        options.m_Headless = headless;
        EStudioUI::Window window(options, assets / "NotoSans.ttf");
        auto theme = window.Themes().GetTheme();
        theme.m_Mode = EStudioUI::ThemeMode::Light;
        window.Themes().SetTheme(theme);
        EStudioUI::DatabaseBrowserWindow browser(application, window);
        if (selfTest)
        {
            std::filesystem::create_directories(output);
            const auto path = output / "browser-sample.sqlite";
            std::filesystem::remove(path);
            browser.OpenExample(path, 100000);
            EStudioUI::VerifyDatabaseBrowser(browser, window, assets, output);
            return 0;
        }
        if (open.empty())
        {
            browser.OpenExample(std::filesystem::temp_directory_path() / "EStudioUI" /
                                "database-browser-example.sqlite");
        }
        else
        {
            browser.Open(open, false);
        }
        return application.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "EStudioUI Database Browser: " << error.what() << '\n';
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    std::vector<std::filesystem::path> arguments;
    for (int i = 0; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
    return Run(arguments);
}
