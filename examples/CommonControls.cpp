#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/Frame.h>
#include <EStudioUI/GroupBox.h>
#include <EStudioUI/ProgressBar.h>
#include <EStudioUI/RadioButton.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/Separator.h>
#include <EStudioUI/Splitter.h>
#include <EStudioUI/Window.h>

// The host owns the window and application state; containers own their controls.
void BuildCommonControls(EStudioUI::Window& window)
{
    using namespace EStudioUI;
    auto& split = window.Ui().Root().Add<Splitter>();
    split.SetHeight(SizeRule::Fill());
    auto& settings = split.Add<GroupBox>("Sketch settings");
    settings.SetCheckable(true);
    auto& snap = settings.Content().Add<CheckBox>("Enable snapping");
    snap.SetChecked(true);
    snap.SetToolTip("Snap points while editing geometry.");
    auto group = std::make_shared<RadioGroup>();
    settings.Content().Add<RadioButton>("Distance", group).SetChecked(true);
    settings.Content().Add<RadioButton>("Angle", group);
    settings.Content().Add<Separator>();
    auto& units = settings.Content().Add<ComboBox>();
    units.SetItems({"Millimeters", "Meters", "Inches"});
    units.SetEditable(true);
    auto& frame = split.Add<Frame>();
    auto& scroll = frame.Add<ScrollArea>();
    scroll.SetHeight(SizeRule::Fill());
    for (int i = 0; i < 20; ++i)
        scroll.Content().Add<TextBox>("Parameter " + std::to_string(i));
    auto& progress = frame.Add<ProgressBar>();
    progress.SetValue(65);
    split.SetSizes({1, 2});
}

int main()
{
    // Compile-checks the ordinary API without opening a native window in CTest.
    return 0;
}
