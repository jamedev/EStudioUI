#include "CreatorWindow.h"
#include "../demo/GpuProbeWindow.h"

#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/UnitEditor.h>
#include <EStudioUI/VectorEditor.h>
#include <EStudioUI/TabWidget.h>

#include <filesystem>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
using namespace EStudioUI;

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Cannot read Creator asset: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void Check(bool condition, const char* description, int& count)
{
    if (!condition)
    {
        throw std::runtime_error(description);
    }
    ++count;
}

int VerifyAuthoring(CreatorWindow& creator, Window& window, const std::filesystem::path& output)
{
    int checks = 0;
    creator.LoadDocument(CreatorWindow::ExampleDocument());
    auto& ui = window.Ui();
    ui.CloseAllPopups();
    const auto frame = [&]
    {
        creator.Refresh();
        window.RenderFrame(ui.GetTime() + .1);
    };
    frame();
    auto* palette = dynamic_cast<WidgetPalette*>(ui.Root().FindById("creator-widget-palette"));
    Check(palette != nullptr, "The palette is an ordinary extensible virtual ListWidget", checks);
    for (auto* ancestor = palette->GetParent(); ancestor; ancestor = ancestor->GetParent())
    {
        if (auto* tabs = dynamic_cast<TabWidget*>(ancestor))
        {
            for (int index = 0; index < tabs->GetTabCount(); ++index)
            {
                for (auto* child = static_cast<Widget*>(palette); child && child != tabs; child = child->GetParent())
                {
                    if (child == &tabs->GetPage(index))
                    {
                        tabs->SetCurrentIndex(index);
                    }
                }
            }
        }
    }
    int buttonIndex = -1;
    for (int index = 0; index < palette->GetItemCount(); ++index)
    {
        if (palette->GetItem(index) == "Button")
        {
            buttonIndex = index;
        }
    }
    Check(buttonIndex >= 0, "The palette exposes registered widget identities", checks);
    palette->SetCurrentIndex(buttonIndex);
    frame();
    const auto view = palette->GetViewport();
    const Point from{view.m_X + 50, view.m_Y +
                                        (static_cast<float>(buttonIndex) + .5f) * palette->GetStyle().m_RowHeight -
                                        palette->GetScrollOffset().m_Y};
    auto* settings = creator.Surface().Host().FindById("settings");
    auto& slot = creator.Registry().Find("GroupBox").m_Content(*settings);
    const auto area = slot.GetContentBounds();
    const Point to{area.m_X + 20, area.m_Y + 20};
    Check(creator.Surface().ContainerAt(to) == "settings",
          "Drop hit testing chooses the deepest registered content slot", checks);
    ui.PointerDown(from);
    ui.PointerMove(to);
    frame();
    window.Graphics().SavePpm(output / "creator-palette-drag.ppm");
    ui.PointerUp(to);
    frame();
    Check(creator.Document().Get().Find("Button1") && creator.Document().Get().Find("settings")->m_Children.size() == 6,
          "Captured palette dragging creates the actual widget inside the targeted nested container", checks);
    creator.Undo();
    frame();
    Check(!creator.Document().Get().Find("Button1"), "A complete palette drop is one undoable document edit", checks);
    creator.RegisterComponent("AcquisitionPanel", "settings");
    const auto component = creator.AddWidget("AcquisitionPanel", "design-root", Point{320, 430});
    frame();
    const auto* instance = creator.Document().Get().Find(component);
    Check(instance && instance->m_Children.size() == 5 && instance->m_Children.front().m_Id != "channel",
          "Reusable compositions clone owned children with collision-free identities", checks);
    Check(UiDocument::FromJson(creator.Document().Get().ToJson(creator.Registry()), creator.Registry()) ==
              creator.Document().Get(),
          "Compositions remain independent open JSON without an external template catalog", checks);
    creator.Undo();
    frame();
    creator.SelectNode("settings");
    DesignSnapSettings snapping;
    snapping.m_KeepRelations = true;
    creator.SetSnapping(snapping);
    settings = creator.Surface().Host().FindById("settings");
    const auto bounds = settings->GetBounds();
    const auto parent = creator.Surface().Host().FindById("design-root")->GetContentBounds();
    const Point start{bounds.m_X + 40, bounds.m_Y + 12};
    const Point end{start.m_X + parent.m_X + 3 - bounds.m_X, start.m_Y};
    ui.PointerDown(start);
    ui.PointerMove(end);
    frame();
    Check(!creator.Surface().GetGuides().empty(), "Moving a real widget displays GPU alignment guides", checks);
    window.Graphics().SavePpm(output / "creator-snap-guides.ppm");
    ui.PointerUp(end);
    frame();
    bool linked = false;
    for (const auto& rule : creator.Document().Get().m_Root.m_Constraints)
    {
        linked = linked || (rule.m_Item == "settings" && rule.m_Anchor == "Left" && rule.m_Target == "parent" &&
                            rule.m_Offset == 0);
    }
    Check(linked, "Keep alignment commits an actual named parent-anchor constraint", checks);
    creator.LoadDocument(CreatorWindow::ExampleDocument());
    frame();
    creator.SetSnapping({});
    auto* retained = creator.Surface().Host().FindById("submit");
    for (int preset = 0; preset < 6; ++preset)
    {
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark})
        {
            Theme theme;
            theme.m_Family = static_cast<ThemeFamily>(preset);
            theme.m_Mode = mode;
            creator.Themes().SetTheme(theme);
            frame();
            window.RenderFrame(ui.GetTime() + .25);
            const auto stem = std::string(Theme::Name(theme.m_Family)) + (mode == ThemeMode::Dark ? "-dark" : "-light");
            window.Graphics().SavePpm(output / ("theme-" + stem + ".ppm"));
            Check(creator.Surface().Host().FindById("submit") == retained,
                  "Theme switching retains live widget identities", checks);
        }
    }
    creator.Themes().SetTheme(Theme{});
    frame();
    return checks;
}

int Verify(CreatorWindow& creator, Window& window, const std::filesystem::path& assets,
           const std::filesystem::path& output)
{
    AllowOversizedGpuProbe(window);
    window.SetSize(1440, 1000);
    int checks = 0;
    std::filesystem::create_directories(output);
    window.RenderFrame(0);
    window.Graphics().SavePpm(output / "creator-wide.ppm");
    const auto before = creator.Document().GetSnapshot();
    creator.SelectNode("submit");
    creator.Inspector().SelectProperty("text", true);
    auto* text = dynamic_cast<TextBox*>(creator.Inspector().GetActiveEditor());
    Check(text != nullptr, "The property grid uses an actual text editor", checks);
    text->SetText("Edited in the real inspector");
    text->Submitted().Emit(text->GetText());
    Check(std::get<std::string>(creator.Document().Get().Find("submit")->m_Properties.at("text")) ==
              "Edited in the real inspector",
          "Inspector commits update the canonical design", checks);
    creator.Refresh();
    window.RenderFrame(.1);
    auto* live = dynamic_cast<Button*>(creator.Surface().Host().FindById("submit"));
    Check(live && live->GetText() == "Edited in the real inspector", "Canonical edits update actual preview widgets",
          checks);
    creator.Undo();
    creator.Refresh();
    Check(creator.Document().Get() == *before, "Undo restores one coherent document", checks);
    creator.Redo();
    creator.Refresh();
    Check(creator.IsDirty(), "Redo restores modified document state", checks);
    const auto added = creator.AddWidget("Slider", "design-root");
    creator.Refresh();
    window.RenderFrame(.2);
    Check(creator.Surface().Host().FindById(added) != nullptr, "Palette construction creates a real registered widget",
          checks);
    const auto serialized = creator.Document().Get().ToJson(creator.Registry());
    Check(UiDocument::FromJson(serialized, creator.Registry()) == creator.Document().Get(),
          "JSON round trip preserves the complete design", checks);
    const auto cpp = creator.Document().Get().ToCpp(creator.Registry());
    Check(cpp.find("BuildUiDocument") == std::string::npos && cpp.find("FromJson") == std::string::npos,
          "Generated C++ constructs widgets directly", checks);
    std::ofstream(output / "dashboard.json") << serialized;
    std::ofstream(output / "GeneratedUi.cpp") << cpp;
    creator.DeleteSelected();
    creator.Refresh();
    Check(creator.Document().Get().Find(added) == nullptr, "Delete removes the design subtree", checks);
    creator.Undo();
    creator.Refresh();
    Check(creator.Document().Get().Find(added) != nullptr, "Undo restores subtree and named constraints", checks);
    creator.SelectNode("accent");
    creator.Inspector().SelectProperty("color", true);
    auto* color = dynamic_cast<ColorEditor*>(creator.Inspector().GetActiveEditor());
    Check(color != nullptr, "Color properties use the actual RGBA editor", checks);
    color->SetColor({.7f, .3f, .9f, .8f});
    creator.Refresh();
    window.RenderFrame(.3);
    const auto popup = color->OpenPicker();
    window.RenderFrame(.4);
    Check(popup.IsPending(), "Color picker uses the asynchronous popup stack", checks);
    window.Graphics().SavePpm(output / "creator-color-picker.ppm");
    window.Ui().KeyEvent(Key::Escape, true);
    Check(!popup.IsPending(), "Escape closes the color popover", checks);
    const auto canonical = creator.Document().GetSnapshot();
    bool rejected = false;
    try
    {
        creator.SetProperty("accent", "color", Color{2, 0, 0, 1});
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected && creator.Document().GetSnapshot() == canonical, "Rejected colors preserve the entire document",
          checks);
    creator.LoadDocument(CreatorWindow::ExampleDocument());
    creator.Refresh();
    window.RenderFrame(.5);
    window.Graphics().SavePpm(output / "creator-restored.ppm");
    creator.SelectNode("headline");
    const auto snapSettings = creator.GetSnapping();
    auto unsnapped = snapSettings;
    unsnapped.m_Enabled = false;
    creator.SetSnapping(unsnapped);
    window.RenderFrame(.51);
    const auto headlineBounds = creator.Surface().Host().FindById("headline")->GetBounds();
    const Point dragStart{headlineBounds.m_X + 20, headlineBounds.m_Y + 20};
    window.Ui().PointerDown(dragStart);
    window.Ui().PointerMove({dragStart.m_X + 16, dragStart.m_Y + 9});
    window.Ui().PointerUp({dragStart.m_X + 16, dragStart.m_Y + 9});
    creator.Refresh();
    window.RenderFrame(.52);
    const auto movedBounds = creator.Surface().Host().FindById("headline")->GetBounds();
    Check(std::abs(movedBounds.m_X - headlineBounds.m_X - 16) < .01f &&
              std::abs(movedBounds.m_Y - headlineBounds.m_Y - 9) < .01f,
          "Captured design dragging updates named constraints and real widget placement", checks);
    creator.Undo();
    creator.Refresh();
    window.RenderFrame(.53);
    Check(creator.Surface().Host().FindById("headline")->GetBounds() == headlineBounds,
          "One undo restores an entire drag gesture", checks);
    const Point resizeStart{headlineBounds.m_X + headlineBounds.m_Width - 2,
                            headlineBounds.m_Y + headlineBounds.m_Height - 2};
    window.Ui().PointerDown(resizeStart);
    window.Ui().PointerMove({resizeStart.m_X + 24, resizeStart.m_Y + 8});
    window.Ui().PointerUp({resizeStart.m_X + 24, resizeStart.m_Y + 8});
    creator.Refresh();
    window.RenderFrame(.54);
    const auto resized = creator.Surface().Host().FindById("headline")->GetBounds();
    Check(std::abs(resized.m_Width - headlineBounds.m_Width - 24) < .01f &&
              std::abs(resized.m_Height - headlineBounds.m_Height - 8) < .01f,
          "Captured corner resizing changes explicit size contracts", checks);
    creator.Undo();
    creator.Refresh();
    window.RenderFrame(.55);
    creator.SetSnapping(snapSettings);
    window.RenderFrame(.6);
    Check(window.Graphics().GetStats().m_RepaintPixels == 0, "Unchanged Creator frames perform no framebuffer repaint",
          checks);
    const auto stable = creator.Surface().Host().FindById("submit");
    creator.SetProperty("submit", "style.opacity", .8);
    creator.Refresh();
    Check(creator.Surface().Host().FindById("submit") == stable,
          "Property-only preview updates preserve widget identity", checks);
    window.RenderFrame(.7);
    window.Graphics().SavePpm(output / "creator-retained.ppm");
    window.Graphics().SetPartialRedrawEnabled(false);
    window.RenderFrame(.8);
    window.Graphics().SavePpm(output / "creator-full.ppm");
    Check(Read(output / "creator-retained.ppm") == Read(output / "creator-full.ppm"),
          "Creator retained pixels exactly match the full-redraw reference", checks);
    window.Graphics().SetPartialRedrawEnabled(true);
    {
        WindowOptions options;
        options.m_Width = 760;
        options.m_Height = 620;
        options.m_Title = "Runtime canvas dashboard";
        auto canvasWindow = window.CreateOwnedWindow(options);
        auto& canvas = canvasWindow->Ui().Root().Add<CanvasWidget>();
        canvas.SetWidth(SizeRule::Fill());
        canvas.SetHeight(SizeRule::Fill());
        canvas.SetCamera({200, 120}, 1);
        UiDocument dashboard;
        dashboard.m_Root.m_Id = "canvas-dashboard";
        dashboard.m_Root.m_Properties = {{"widthPolicy", std::string("Fill")}, {"heightPolicy", std::string("Fill")}};
        dashboard.m_Root.m_Children.emplace_back(
            "canvas-button", "Button",
            std::map<std::string, PropertyValue>{
                {"text", std::string("A runtime-loaded dashboard on the infinite canvas")},
                {"widthPolicy", std::string("Fill")},
                {"height", 60.0},
                {"style.background", Color{.15f, .65f, .35f, 1}}});
        auto built = BuildUiDocument(canvas, CanvasBounds{0, 0, 400, 240}, dashboard, creator.Registry());
        canvasWindow->RenderFrame(0);
        auto* button = dynamic_cast<Button*>(built.m_Widgets.at("canvas-button").Get());
        Check(button && button->GetBounds().m_Width > 100,
              "Runtime loader hosts real controls through the existing canvas widget API", checks);
        bool clicked = false;
        button->Clicked().Connect([&] { clicked = true; });
        auto bounds = button->GetBounds();
        canvasWindow->Ui().PointerDown({bounds.m_X + 20, bounds.m_Y + 20});
        canvasWindow->Ui().PointerUp({bounds.m_X + 20, bounds.m_Y + 20});
        Check(clicked, "Canvas-hosted runtime widgets retain normal pointer interaction", checks);
        canvas.SetCamera({230, 150}, 1.25);
        canvasWindow->RenderFrame(.1);
        Check(button->GetBounds() != bounds, "Camera pan/zoom updates the dashboard host's prepared geometry", checks);
        bounds = button->GetBounds();
        const auto pixel = canvasWindow->Graphics().ReadPixel({bounds.m_X + 16, bounds.m_Y + 16});
        Check(pixel.m_G > .4f && pixel.m_R < .4f, "Canvas dashboard controls reach the actual GPU framebuffer", checks);
        canvasWindow->Graphics().SavePpm(output / "creator-canvas.ppm");
        canvasWindow->Close();
    }
    checks += VerifyAuthoring(creator, window, output);
    checks += VerifyGpuProbeChrome(window, assets, output);
    std::cout << "Creator GPU integration: " << checks << " checks passed\n";
    return checks;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        bool headless = false, verify = false;
        std::filesystem::path output = "creator-verification";
        std::filesystem::path design;
        const auto executable = std::filesystem::absolute(argv[0]).parent_path();
        std::filesystem::path assets = executable / "assets";
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--headless")
            {
                headless = true;
            }
            else if (argument == "--self-test")
            {
                verify = true;
            }
            else if (argument == "--output" && index + 1 < argc)
            {
                output = argv[++index];
            }
            else if (argument == "--assets" && index + 1 < argc)
            {
                assets = argv[++index];
            }
            else if (argument == "--open" && index + 1 < argc)
            {
                design = argv[++index];
            }
            else
            {
                throw std::invalid_argument("Usage: EStudioUICreator [--open design.json] [--headless --self-test "
                                            "--output folder] [--assets folder]");
            }
        }
        Application application;
        WindowOptions options;
        options.m_Width = 1440;
        options.m_Height = 1000;
        options.m_Title = "EStudioUI Creator";
        options.m_Headless = headless;
        Window window(options, assets / "NotoSans.ttf");
        const auto css = Read(assets / "dark.css") + '\n' + Read(assets / "essential-widgets.css") + '\n' +
                         Read(assets / "command-dialogs.css") + '\n' + Read(assets / "property-editors.css") + '\n' +
                         Read(assets / "creator.css");
        CreatorWindow creator(application, window, css);
        if (!design.empty())
        {
            creator.LoadDocument(UiDocument::FromJson(Read(design), creator.Registry()));
        }
        if (verify)
        {
            Verify(creator, window, assets, output);
            return 0;
        }
        return application.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "EStudioUICreator: " << error.what() << '\n';
        return 1;
    }
}
