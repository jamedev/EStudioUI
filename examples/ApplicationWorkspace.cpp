#include <EStudioUI/Bindings.h>
#include <EStudioUI/FilePathEditor.h>
#include <EStudioUI/FolderPicker.h>
#include <EStudioUI/MainWindow.h>

// The same ordinary controls can be composed in title bars, toolbars, dock panels or the central view.
void BuildApplicationWorkspace(EStudioUI::Window& window, EStudioUI::State<std::filesystem::path>& document)
{
    using namespace EStudioUI;
    auto& shell = window.Ui().Root().Add<MainWindow>(window);
    auto& file = shell.TitleBar().Menus().AddMenu("File");
    file.AddAction("Exit").Clicked().Connect([owner = window.GetHandle()]
                                             { owner.Post([](Window& live) { live.Close(); }); });
    auto& search = shell.TitleBar().Content().Add<TextBox>();
    search.SetWidth(SizeRule::Fixed(220));
    search.SetPlaceholder("Search");
    auto& tools = shell.AddToolBar();
    tools.AddButton("Save");
    tools.Content().Add<TextBox>().SetPlaceholder("Custom toolbar control");
    auto& inspector = shell.Docks().AddDock("inspector", "Properties", DockArea::Right);
    BindPath(inspector.Content().Add<FilePathEditor>(), document);
    inspector.Content().Add<FolderPicker>();
    shell.Central().Add<Label>("Application content");
    shell.Status().SetMessage("Ready");
    const auto workspace = shell.Docks().SaveState();
    std::string error;
    shell.Docks().RestoreState(workspace, error);
}

int main()
{
    return 0;
}
