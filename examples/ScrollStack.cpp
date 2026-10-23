#include <EStudioUI/Bindings.h>
#include <EStudioUI/ScrollBar.h>
#include <EStudioUI/StackedWidget.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Window.h>

// Viewport offset can be shared with an application model and updated through State's worker handle.
void BuildScrollStack(EStudioUI::Window& window, EStudioUI::State<double>& offset)
{
    using namespace EStudioUI;
    auto& root = window.Ui().Root();
    auto& pages = root.Add<StackedWidget>();
    pages.SetHeight(SizeRule::Fill());
    auto& general = pages.AddPage();
    general.Add<TextBox>("Name");
    auto& advanced = pages.AddPage();
    advanced.Add<TextBox>("Advanced setting");
    pages.SetCurrentPage(advanced);
    auto& scrollbar = root.Add<ScrollBar>(Axis::Horizontal);
    scrollbar.SetHeight(SizeRule::Content());
    // A 100-unit viewport over 1,000 content units permits origins from 0 through 900.
    scrollbar.SetRange(0, 900);
    scrollbar.SetPageStep(100);
    scrollbar.SetSingleStep(5);
    BindValue(scrollbar, offset);
}

int main()
{
    // Compiles the ordinary public API without opening a native window during CTest.
    return 0;
}
