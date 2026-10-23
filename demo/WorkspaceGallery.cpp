#include "WorkspaceGallery.h"
#include "GpuProbeWindow.h"

#include <EStudioUI/Bindings.h>
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/FileDialog.h>
#include <EStudioUI/FilePathEditor.h>
#include <EStudioUI/FolderPicker.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/MainWindow.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/OpenGLWidget.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/TreeWidget.h>

#include <glad/gl.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#include <fstream>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
int s_Checks = 0;

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(std::string("Workspace GPU: ") + message);
}

Point Center(Rect bounds)
{
    return {bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2};
}

std::vector<unsigned char> Pixels()
{
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    std::vector<unsigned char> result(static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) *
                                      4);
    glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}

MainWindow& BuildWorkspace(Window& window)
{
    auto& shell = window.Ui().Root().Add<MainWindow>(window);
    const WidgetRef target(&shell);
    auto saved = std::make_shared<std::string>();
    auto& menus = shell.TitleBar().Menus();
    auto& file = menus.AddMenu("File");
    const auto open = [target]
    {
        if (auto* live = dynamic_cast<MainWindow*>(target.Get()))
        {
            FileDialogOptions options;
            options.m_Title = "Open a document";
            options.m_Filters = {{"Documents", "*.txt;*.cpp;*.h;*.css"}, {"All files", "*.*"}};
            FileDialog::Open(*live, options)
                .OnFinished(
                    [target](const FileDialogResult& result)
                    {
                        if (auto* shell = dynamic_cast<MainWindow*>(target.Get()))
                            shell->Status().SetMessage(result.m_Status == FileDialogStatus::Accepted &&
                                                               !result.m_Paths.empty()
                                                           ? "Selected: " + result.m_Paths.front().filename().string()
                                                           : "Open cancelled or unavailable");
                    });
        }
    };
    auto openAction = window.Ui().CreateAction("Open…");
    openAction->SetShortcut({Key::O, ShortcutModifier::Primary});
    openAction->Triggered().Connect(open);
    file.AddAction(openAction);
    file.AddSeparator();
    file.AddAction("Close window")
        .Clicked()
        .Connect([owner = window.GetHandle()] { owner.Post([](Window& live) { live.Close(); }); });
    auto& view = menus.AddMenu("View");
    auto& workspace = menus.AddMenu("Workspace");
    const auto save = [target, saved]
    {
        if (auto* live = dynamic_cast<MainWindow*>(target.Get()))
        {
            *saved = live->Docks().SaveState();
            live->Status().SetMessage("Workspace saved in memory");
        }
    };
    const auto restore = [target, saved]
    {
        if (auto* live = dynamic_cast<MainWindow*>(target.Get()))
        {
            std::string error;
            const bool restored = live->Docks().RestoreState(*saved, error);
            live->Status().SetMessage(restored ? "Workspace restored" : error);
        }
    };
    auto saveAction = window.Ui().CreateAction("Save workspace");
    saveAction->Triggered().Connect(save);
    auto restoreAction = window.Ui().CreateAction("Restore workspace");
    restoreAction->Triggered().Connect(restore);
    workspace.AddAction(saveAction);
    workspace.AddAction(restoreAction);
    auto& search = shell.TitleBar().Content().Add<TextBox>();
    search.SetWidth(SizeRule::Fixed(210));
    search.SetHeight(SizeRule::Fill());
    search.SetPlaceholder("Search commands");
    search.SetAccessibleName("Search commands in title bar");
    auto& tools = shell.AddToolBar();
    tools.AddAction(openAction);
    tools.AddAction(saveAction);
    tools.AddAction(restoreAction);
    tools.AddSeparator();
    for (int index = 1; index <= 10; ++index)
        tools.AddButton("Tool " + std::to_string(index))
            .Clicked()
            .Connect(
                [target, index]
                {
                    if (auto* live = dynamic_cast<MainWindow*>(target.Get()))
                        live->Status().SetMessage("Tool " + std::to_string(index) + " activated");
                });
    auto& documents = shell.Central().Add<TabWidget>();
    documents.SetHeight(SizeRule::Fill());
    auto& drawing = documents.AddTab("Sketch document");
    drawing.Add<Label>("EStudioUI application workspace").AddClass("workspace-heading");
    auto& description =
        drawing.Add<Label>("Drag a panel's header to an edge to dock it, or away from the workspace to float it. Use "
                           "the header commands to float/dock or hide panels. View reopens them.");
    description.SetWordWrap(true);
    drawing.Add<Label>("Title menus, search, splitters, toolbars and panel contents are ordinary CSS-styled widgets.")
        .SetWordWrap(true);
    auto& table = documents.AddTab("Document data").Add<TableWidget>();
    table.SetHeight(SizeRule::Fill());
    table.SetColumns({{"Name", SizeRule::Fill()}, {"Value", SizeRule::Fill()}});
    table.SetRows({{"Width", "128.0"}, {"Height", "64.0"}, {"Radius", "12.5"}});
    documents.SetCurrentIndex(0);
    auto& project = shell.Docks().AddDock("project", "Project", DockArea::Left);
    auto& tree = project.Content().Add<TreeWidget>();
    tree.SetHeight(SizeRule::Fill());
    const auto root = tree.AddItem(0, "My application");
    tree.AddItem(root, "Sketch document");
    tree.AddItem(root, "Materials");
    const auto source = tree.AddItem(root, "Source");
    tree.AddItem(source, "main.cpp");
    tree.AddItem(source, "application.css");
    tree.SetExpanded(root, true);
    tree.SetExpanded(source, true);
    auto& properties = shell.Docks().AddDock("properties", "Properties", DockArea::Right);
    auto& propertiesScroll = properties.Content().Add<ScrollArea>();
    propertiesScroll.SetHeight(SizeRule::Fill());
    auto& fields = propertiesScroll.Content();
    fields.Add<Label>("Document width");
    auto& width = fields.Add<NumericInput>(128);
    width.SetRange(1, 100000);
    fields.Add<CheckBox>("Snap to grid").SetChecked(true);
    fields.Add<Label>("Document path");
    fields.Add<FilePathEditor>().Editor().SetPlaceholder("Choose a document…");
    fields.Add<Label>("Project folder");
    fields.Add<FolderPicker>().Editor().SetPlaceholder("Choose a folder…");
    auto& output = shell.Docks().AddDock("output", "Output", DockArea::Bottom);
    auto& log = output.Content().Add<ListWidget>();
    log.SetHeight(SizeRule::Fill());
    log.SetItems({"Workspace ready", "Native title-bar move/resize remains available",
                  "Floating preserves widget identities and bindings",
                  "Save/restore uses a bounded versioned snapshot"});
    for (const auto* panel : {&project, &properties, &output})
    {
        const auto id = panel->GetDockId();
        view.AddAction(panel->GetTitle())
            .Clicked()
            .Connect(
                [target, id]
                {
                    if (auto* live = dynamic_cast<MainWindow*>(target.Get()))
                        if (auto* dock = live->Docks().FindDock(id))
                            live->Docks().SetDockVisible(*dock, true);
                });
    }
    *saved = shell.Docks().SaveState();
    shell.Status().SetMessage("Ready — drag panel headers or use their dock/float commands");
    shell.Status().Permanent().Add<Label>("C++20  |  GPU");
    return shell;
}
} // namespace

WorkspaceGallery::WorkspaceGallery(Window& owner, Widget& page) : m_Owner(owner)
{
    page.Add<Label>("Application workspace").AddClass("workspace-heading");
    page.Add<Label>("Open a separate MainWindow with title-bar menus and search, overflowing toolbars, resizable dock "
                    "groups, floating panels, file/folder fields and workspace save/restore.")
        .SetWordWrap(true);
    auto& launch = page.Add<Button>("Open application workspace");
    launch.SetWidth(SizeRule::Content());
    launch.Clicked().Connect([this] { Open(); });
}

void WorkspaceGallery::Open()
{
    if (m_Workspace && !m_Workspace->IsClosed())
    {
        m_Workspace->Platform().FocusWindow();
        return;
    }
    WindowOptions options;
    options.m_Title = "EStudioUI Workspace";
    options.m_Width = 1280;
    options.m_Height = 820;
    m_Workspace = m_Owner.CreateOwnedWindow(std::move(options));
    BuildWorkspace(*m_Workspace);
    m_Workspace->RequestRepaint();
}

void WorkspaceGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    s_Checks = 0;
    WindowOptions options;
    options.m_Title = "Workspace verification";
    options.m_Width = 1180;
    options.m_Height = 820;
    auto probe = m_Owner.CreateOwnedWindow(options);
    AllowOversizedGpuProbe(*probe);
    std::string stylesheet;
    for (const auto* name :
         {"dark.css", "rich-widgets.css", "text-controls.css", "essential-widgets.css", "command-dialogs.css"})
    {
        std::ifstream stream(assets / name);
        Check(static_cast<bool>(stream),
              "Workspace CSS assets are available independently of the current gallery theme");
        stylesheet.append(std::istreambuf_iterator<char>(stream), {});
        stylesheet += '\n';
    }
    std::string styleError;
    Check(probe->Ui().SetStyleSheet(stylesheet, styleError), "Workspace rich CSS compiles");
    auto& shell = BuildWorkspace(*probe);
    auto& docks = shell.Docks();
    auto& ui = probe->Ui();
    auto& gpu = probe->Graphics();
    auto* project = docks.FindDock("project");
    auto* properties = docks.FindDock("properties");
    auto* log = docks.FindDock("output");
    Check(project && properties && log, "Stable IDs resolve all initial panels");
    auto& editor = properties->Content().Add<TextBox>("persistent value");
    State<std::string> value(ui.States(), "bound inspector");
    const auto binding = BindText(editor, value);
    const WidgetRef identity(&editor);
    probe->RenderFrame(ui.GetTime());
    Check(shell.Central().GetBounds().m_Width > 100 && docks.GetAreaBounds(DockArea::Left).m_Width > 0,
          "Visible dock groups surround a usable central viewport");
    gpu.SavePpm(output / "workspace-wide.ppm");
    const auto original = docks.SaveState();
    std::string error;
    Check(!docks.RestoreState("EStudioUI.Workspace 999\n", error) && docks.SaveState() == original,
          "Unknown workspace versions fail before any mutation");
    Check(!docks.RestoreState(original + "trailing data", error) && docks.SaveState() == original,
          "Trailing workspace bytes cannot partially mutate live panels");
    Check(!docks.RestoreState(std::string(65537, 'x'), error) && docks.SaveState() == original,
          "Workspace parsing has a fixed payload budget");
    auto duplicate = original;
    const auto propertiesId = duplicate.find("\"properties\"");
    duplicate.replace(propertiesId, std::string("\"properties\"").size(), "\"project\"");
    Check(!docks.RestoreState(duplicate, error) && docks.SaveState() == original,
          "Duplicate panel IDs are rejected before live workspace changes");
    auto& grip = *project->GetChildren().front()->GetChildren().front();
    const auto rightEdge = docks.GetBounds();
    const Point drop{rightEdge.m_X + rightEdge.m_Width - 12, rightEdge.m_Y + rightEdge.m_Height / 2};
    ui.PointerDown(Center(grip.GetBounds()));
    ui.PointerMove(drop);
    probe->RenderFrame(ui.GetTime());
    gpu.SavePpm(output / "workspace-drag-preview.ppm");
    ui.PointerUp(drop);
    Check(docks.GetDockArea(*project) == DockArea::Right && project->IsOpen(),
          "The production captured header grip docks into a prepared edge target");
    Check(docks.RestoreState(original, error), "Restoring after a grip drag preserves panel identities");
    {
        WindowOptions independentOptions;
        independentOptions.m_Headless = probe->IsHeadless();
        independentOptions.m_Width = 320;
        independentOptions.m_Height = 240;
        Window independent(independentOptions, assets / "NotoSans.ttf");
        bool rejected = false;
        try
        {
            editor.ReparentTo(independent.Ui().Root());
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Check(rejected && editor.GetContext() == &ui,
              "Incompatible GPU groups are rejected before any widget ownership change");
    }
    docks.Tabify(*project, *properties);
    Check(docks.GetDockArea(*project) == DockArea::Right && identity.Get() == &editor,
          "Tabifying preserves every existing application widget");
    project->SetTitle("Project renamed");
    Check(project->GetTitle() == "Project renamed", "Renaming refreshes the existing selector");
    project->Close();
    Check(!project->IsOpen() && project->Content().GetChildren().size() == 1, "Closing a dock retains its content");
    docks.SetDockVisible(*project, true);
    Check(project->IsOpen(), "A closed panel reopens without recreation");
    docks.Float(*properties, Rect{140, 160, 440, 560});
    auto* floating = docks.GetFloatingWindow(*properties);
    Check(floating && properties->IsFloating() && editor.GetContext() == &floating->Ui() && identity.Get() == &editor,
          "Floating reparents the actual editor into an owned window");
    value.Set("changed while floating");
    floating->RenderFrame(floating->Ui().GetTime());
    Check(editor.GetText() == "changed while floating" && binding.IsConnected(),
          "Canonical bindings continue across native window transfer");
#ifdef _WIN32
    if (!probe->IsHeadless())
    {
        const auto setFocus = [](Window& window, bool focused)
        {
            auto* native = reinterpret_cast<GLFWwindow*>(window.GetContextIdentity());
            const auto callback = glfwSetWindowFocusCallback(native, nullptr);
            glfwSetWindowFocusCallback(native, callback);
            if (callback)
                callback(native, focused ? GLFW_TRUE : GLFW_FALSE);
        };
        const auto invalidateNative = [](Window& window)
        {
            auto* native = reinterpret_cast<GLFWwindow*>(window.GetContextIdentity());
            RedrawWindow(glfwGetWin32Window(native), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        };
        setFocus(m_Owner, false);
        setFocus(*probe, false);
        setFocus(*floating, true);
        const auto before = gpu.GetStats().m_FrameSerial;
        value.Set("Floating inspector updates its unfocused owner");
        shell.Status().SetMessage("Updated while the floating inspector has focus");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        invalidateNative(*probe);
        Check(gpu.GetStats().m_FrameSerial > before,
              "The native scheduler presents an unfocused owner when its floating panel has focus");
        setFocus(*floating, false);
        const auto paused = gpu.GetStats().m_FrameSerial;
        value.Set("Background update waits until group activation");
        shell.Status().SetMessage("Background presentation is paused");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        invalidateNative(*probe);
        Check(gpu.GetStats().m_FrameSerial == paused,
              "A wholly unfocused owned-window group retains the background presentation pause");
        setFocus(*floating, true);
    }
#endif
    const auto oldSheetRevision = floating->Ui().GetStyleSheetRevision();
    std::string reloadError;
    Check(ui.SetStyleSheet(stylesheet + "\n.path-browse { border-color: #90d8fa; }", reloadError),
          "A live workspace theme can be replaced");
    probe->RenderFrame(ui.GetTime());
    const auto copiedRevision = floating->Ui().GetStyleSheetRevision();
    Check(copiedRevision > oldSheetRevision, "Floating windows follow a replacement of their owner's compiled CSS");
    probe->RenderFrame(ui.GetTime());
    Check(floating->Ui().GetStyleSheetRevision() == copiedRevision,
          "Unchanged parent frames do not recopy or reparse floating styles");
    editor.SetText("edited in float");
    Check(value.Get() == "edited in float", "Floating edits publish to shared application state");
    docks.Dock(*properties, DockArea::Left);
    Check(!properties->IsFloating() && editor.GetContext() == &ui && identity.Get() == &editor,
          "Redocking preserves widget identity and returns to the owner dispatcher");
    Check(docks.RestoreState(original, error), "A complete saved workspace restores successfully");
    Check(docks.GetDockArea(*project) == DockArea::Left && docks.GetDockArea(*properties) == DockArea::Right,
          "Restoring reinstates original panel edges");
    probe->RenderFrame(ui.GetTime());
    const auto retained = Pixels();
    Check(!retained.empty(), "Retiring floating hosts restores the containing GPU context after the frame");
    gpu.SetPartialRedrawEnabled(false);
    probe->RenderFrame(ui.GetTime());
    Check(retained == Pixels(), "Restored workspace retained pixels exactly match full redraw");
    gpu.SetPartialRedrawEnabled(true);
    ui.PointerMove({-10000, -10000});
    probe->RenderFrame(ui.GetTime() + 2);
    probe->RenderFrame(ui.GetTime());
    Check(gpu.GetStats().m_RepaintPixels == 0 && !ui.HasAnimations(),
          "A settled workspace adds no repaint or animation deadline");
    // A real surface must keep its separate shared context and synchronization across both transfers.
    auto& scenePanel = docks.AddDock("gpu-preview", "GPU preview", DockArea::Right);
    auto& scene = scenePanel.Content().Add<OpenGLWidget>();
    scene.SetHeight(SizeRule::Fill());
    scene.SetCallbacks({},
                       [](const OpenGLFrame& frame)
                       {
                           glViewport(0, 0, frame.m_Width, frame.m_Height);
                           glClearColor(.1f, .7f, .3f, 1);
                           glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                       });
    probe->RenderFrame(ui.GetTime());
    const auto sceneIdentity = scene.GetContextIdentity();
    Check(sceneIdentity != 0 && sceneIdentity != probe->GetContextIdentity(),
          "Docked scene uses a separate OpenGL context");
    Check(gpu.ReadPixel(Center(scene.GetContentBounds())).m_G > .6f,
          "Docked shared texture reaches actual framebuffer pixels");
    docks.Float(scenePanel, Rect{220, 180, 420, 420});
    auto* sceneHost = docks.GetFloatingWindow(scenePanel);
    sceneHost->RenderFrame(sceneHost->Ui().GetTime());
    Check(scene.GetContextIdentity() == sceneIdentity &&
              sceneHost->Graphics().ReadPixel(Center(scene.GetContentBounds())).m_G > .6f,
          "Floating shares the GPU group without recreating the scene context");
    docks.Dock(scenePanel, DockArea::Right);
    probe->RenderFrame(ui.GetTime());
    Check(scene.GetContextIdentity() == sceneIdentity && gpu.ReadPixel(Center(scene.GetContentBounds())).m_G > .6f,
          "Redocked scene retains synchronized texture presentation");
    scenePanel.Close();
    for (const Point size : {Point{420, 420}, Point{1180, 820}})
    {
        probe->SetSize(static_cast<int>(size.m_X), static_cast<int>(size.m_Y));
        probe->RenderFrame(ui.GetTime());
        const auto title = shell.TitleBar().GetBounds();
        const auto& captionCommands = shell.TitleBar().GetChildren();
        if (probe->GetTitleBarSupport() != PlatformSupport::Unsupported)
        {
            const auto closeBounds = captionCommands.back()->GetBounds();
            Check(closeBounds.m_X + closeBounds.m_Width <= title.m_X + title.m_Width + .1f,
                  "Close command stays visible when title-row menus and custom widgets overflow");
            Check(shell.TitleBar().HitTest(Center(closeBounds)) == WindowHitRegion::Close,
                  "Prepared title-bar hit geometry matches the close command presentation");
        }
        Check(title.m_Width <= ui.Root().GetContentBounds().m_Width + .1f,
              "Native caption commands stay in the resized title viewport");
        const auto resized = Pixels();
        gpu.SetPartialRedrawEnabled(false);
        probe->RenderFrame(ui.GetTime());
        Check(resized == Pixels(), "Narrow/wide workspace retained pixels match full redraw");
        gpu.SetPartialRedrawEnabled(true);
        gpu.SavePpm(output / (size.m_X < 500 ? "workspace-narrow.ppm" : "workspace-restored.ppm"));
    }
    docks.Float(*properties, Rect{140, 160, 440, 560});
#ifdef _WIN32
    if (!probe->IsHeadless())
    {
        auto* nativeFloat = reinterpret_cast<GLFWwindow*>(docks.GetFloatingWindow(*properties)->GetContextIdentity());
        SendMessageW(glfwGetWin32Window(nativeFloat), WM_CLOSE, 0, 0);
    }
    else
#endif
        docks.GetFloatingWindow(*properties)->Close();
    ui.GetDispatcher().Drain();
    Check(!properties->IsFloating() && !properties->IsOpen() && identity.Get() == &editor,
          "The native floating close path hides the panel without deleting its content");
    docks.SetDockVisible(*properties, true);
    Check(editor.GetContext() == &ui && binding.IsConnected(), "Closed floating content reopens with bindings intact");
#ifdef _WIN32
    if (!probe->IsHeadless())
    {
        const auto native = glfwGetWin32Window(reinterpret_cast<GLFWwindow*>(probe->GetContextIdentity()));
        const auto style = GetWindowLongPtrW(native, GWL_STYLE);
        Check((style & WS_THICKFRAME) && (style & WS_MAXIMIZEBOX) && (style & WS_SYSMENU),
              "Custom title chrome retains native resizing, maximization and system-menu styles");
        const auto point = shell.TitleBar().GetBounds();
        (void)point;
        // Full region/SC_SIZE behavior is also covered by the independent native chrome probe.
        Check(probe->GetTitleBarSupport() == PlatformSupport::Native,
              "Actual Windows surface reports native title-bar integration");
    }
#endif
    docks.Float(*properties);
    auto* finalHost = docks.GetFloatingWindow(*properties);
    probe->Close();
    Check(finalHost->IsClosed(), "Closing MainWindow closes its owned floating surfaces");
    // Stop callbacks may inspect the manager while floating host facades are being destroyed.
    auto lifetimeProbe = m_Owner.CreateOwnedWindow(options);
    auto& lifetimeShell = lifetimeProbe->Ui().Root().Add<MainWindow>(*lifetimeProbe);
    auto& lifetimeDocks = lifetimeShell.Docks();
    auto& first = lifetimeDocks.AddDock("first", "First");
    auto& second = lifetimeDocks.AddDock("second", "Second");
    lifetimeDocks.Float(first);
    lifetimeDocks.Float(second);
    bool emptyDuringRetirement = false;
    std::stop_callback observeRetirement(lifetimeDocks.GetFloatingWindow(second)->GetHandle().GetTargetStopToken(),
                                         [&] { emptyDuringRetirement = lifetimeDocks.FindDock("first") == nullptr; });
    lifetimeProbe->Ui().Root().RemoveChild(lifetimeShell);
    Check(emptyDuringRetirement, "Floating retirement callbacks cannot observe partially destroyed panel metadata");
    std::ofstream report(output / "workspace-report.txt");
    report << "Application workspace actual GPU: PASS (" << s_Checks << " checks)\n"
           << "File/path controls; native title-row integration; stable docking/floating identities; shared State; "
              "bounded workspace restore; real scene context transfers; narrow/wide retained/full pixels; zero settled "
              "repaint.\n"
           << "Windows 11 Snap Layout overlay, physical mixed-monitor dragging and interactive accessibility remain "
              "human checks.\n";
}
} // namespace EStudioUI
