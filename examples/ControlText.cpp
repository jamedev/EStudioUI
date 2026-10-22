#include <EStudioUI/Label.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Window.h>

#include <filesystem>
#include <stdexcept>

// A compiled application-facing entry point; the host supplies bundled font paths.
void BuildTextForm(const std::filesystem::path& directory)
{
    EStudioUI::WindowOptions options;
    options.m_Fonts.push_back({directory / "NotoSansArabic.ttf"});
    options.m_Fonts.push_back({directory / "NotoSansHebrew.ttf"});
    EStudioUI::Window window(options, directory / "NotoSans.ttf");
    std::string error;
    if (!window.Ui().SetStyleSheet("window { font-family: sans-serif; font-size: 18px; gap: 12px; } "
                                   "label { text-wrap: wrap; } textbox { height: 42px; }",
                                   error))
        throw std::runtime_error(error);
    window.Ui().Root().Add<EStudioUI::Label>("Application / العربية / עברית");
    window.Ui().Root().Add<EStudioUI::TextBox>("abc אבג XYZ").SetAccessibleName("Project name");
    window.Run();
}

int main()
{
    // This target verifies the ordinary API without requiring a native window in CTest.
    return 0;
}
