#include "WidgetGallery.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Gallery verification: " + message);
}

Point Center(const Widget& widget)
{
    const auto bounds = widget.GetBounds();
    return {bounds.m_X + bounds.m_Width * 0.5f, bounds.m_Y + bounds.m_Height * 0.5f};
}

void Click(UiContext& ui, Point point)
{
    ui.PointerDown(point);
    ui.PointerUp(point);
}

Label& Caption(Widget& parent, std::string text, const std::string& style = "pane-title")
{
    auto& label = parent.Add<Label>(std::move(text));
    label.AddClass(style);
    return label;
}

void ConnectScene(OpenGLWidget& widget, const std::shared_ptr<DemoScene>& scene)
{
    widget.SetCallbacks([scene] { scene->Initialize(); }, [scene](const OpenGLFrame& frame) { scene->Render(frame); },
                        [scene] { scene->Release(); });
    widget.PointerPressed().Connect([scene](const PointerEvent& event) { scene->BeginDrag(event.m_Position); });
    const WidgetRef reference(&widget);
    widget.PointerDragged().Connect(
        [scene, reference](const PointerEvent& event)
        {
            scene->Drag(event.m_Position);
            if (auto* live = dynamic_cast<OpenGLWidget*>(reference.Get()))
                live->RequestFrame();
        });
    widget.Scrolled().Connect(
        [scene, reference](Point delta)
        {
            scene->Zoom(delta.m_Y);
            if (auto* live = dynamic_cast<OpenGLWidget*>(reference.Get()))
                live->RequestFrame();
        });
}
} // namespace

WidgetGallery::WidgetGallery(Window& window, TabWidget& tabs, const std::filesystem::path& assets)
    : m_Window(window), m_Tabs(tabs)
{
    BuildCollections(tabs.AddTab("Data & actions"));
    BuildScenes(tabs.AddTab("3D surfaces"));
    BuildLayouts(tabs.AddTab("Layout lab"));
    m_Animations = std::make_unique<AnimationGallery>(window, tabs.AddTab("Animations"), assets);
    m_RichCss = std::make_unique<RichCssGallery>(window, tabs.AddTab("Rich CSS"));
    m_Sketch = std::make_unique<SketchGallery>(window, tabs.AddTab("Sketcher"));
    m_CustomWidgets = std::make_unique<CustomWidgetGallery>(window, tabs.AddTab("Custom widgets"));
    m_WidgetStyles = std::make_unique<WidgetStylesGallery>(window, tabs.AddTab("Widget styles"), assets);
    m_Foundation = std::make_unique<FoundationGallery>(window, tabs.AddTab("Async"), assets);
    m_State = std::make_unique<StateGallery>(window, tabs.AddTab("State"), assets);
    m_Models = std::make_unique<ModelGallery>(window, tabs.AddTab("Models"), assets);
    m_Constraints = std::make_unique<ConstraintGallery>(window, tabs.AddTab("Constraints"));
}

void WidgetGallery::BuildCollections(Widget& page)
{
    auto& toolbar = page.Add<ToolBar>();
    m_AddRow = &toolbar.AddButton("+ Add row");
    m_AddRow->Clicked().Connect([this] { AddTableRow(); });
    auto& sort = toolbar.AddButton("Sort name");
    sort.Clicked().Connect([this] { m_Table->SortByColumn(1); });
    toolbar.AddSeparator();
    m_Compact = &toolbar.AddButton("Compact rows");
    m_Compact->SetCheckable(true);
    m_Compact->Toggled().Connect(
        [this](bool compact)
        {
            if (compact)
                m_Table->AddClass("compact");
            else
                m_Table->RemoveClass("compact");
        });
    auto& actions = toolbar.AddButton("Actions …");
    toolbar.AddButton("Unavailable").SetEnabled(false);
    auto& body = page.Add<Widget>();
    body.AddClass("gallery-body");
    body.SetLayoutDirection(Axis::Horizontal);
    body.SetHeight(SizeRule::Fill(1, 160));
    auto& sidebar = body.Add<Widget>();
    sidebar.AddClass("gallery-pane");
    sidebar.SetWidth(SizeRule::Fixed(240));
    Caption(sidebar, "PROJECT / TreeWidget");
    m_Tree = &sidebar.Add<TreeWidget>();
    m_Tree->SetHeight(SizeRule::Fill(1, 80));
    const auto project = m_Tree->AddItem(0, "EStudioUI");
    const auto source = m_Tree->AddItem(project, "src");
    for (const auto& name : {"Widget.cpp", "Renderer.cpp", "UiContext.cpp", "OpenGLWidget.cpp"})
        m_Tree->AddItem(source, name);
    const auto assets = m_Tree->AddItem(project, "assets");
    m_Tree->AddItem(assets, "dark.css");
    m_Tree->AddItem(assets, "light.css");
    m_Tree->AddItem(project, "tests");
    m_Tree->SetExpanded(project, true);
    m_Tree->SetExpanded(source, true);
    Caption(sidebar, "COMMANDS / ListWidget");
    m_List = &sidebar.Add<ListWidget>();
    m_List->SetHeight(SizeRule::Fill(1, 80));
    std::vector<std::string> commands{"Build project",     "Run tests",     "Inspect scene",
                                      "Reload stylesheet", "Export report", "Open settings"};
    for (int i = 0; i < 2000; ++i)
        commands.push_back("Command " + std::to_string(i + 7));
    m_List->SetItems(std::move(commands));
    auto& tablePane = body.Add<Widget>();
    tablePane.AddClass("gallery-pane");
    tablePane.SetWidth(SizeRule::Fill(1, 320));
    Caption(tablePane, "10,000 ROWS / TableWidget   •   click headers to sort   •   drag column edges   •   F2 or "
                       "double-click to edit");
    m_Table = &tablePane.Add<TableWidget>();
    m_Table->SetHeight(SizeRule::Fill());
    m_Table->SetColumns({{"ID", SizeRule::Fixed(72)},
                         {"Name", SizeRule::Fill(2, 140)},
                         {"Type", SizeRule::Fixed(100)},
                         {"Status", SizeRule::Fill(1, 90, 150)},
                         {"Value", SizeRule::Fill(1, 80)}});
    std::vector<std::vector<std::string>> rows;
    for (int i = 0; i < 10000; ++i)
        rows.push_back({std::to_string(i + 1001), "Frame_" + std::to_string(i + 1), i % 3 == 0 ? "CQUAD4" : "CTRIA3",
                        i % 7 == 0 ? "Review" : "Ready", std::to_string((i * 17) % 1000)});
    m_Table->SetRows(std::move(rows));
    m_Status = &Caption(page, "Ready / virtualized rows, typed signals, shared CSS theme", "gallery-status");
    m_List->CurrentChanged().Connect(
        [this](int row)
        {
            if (row >= 0)
                m_Status->SetText("List selection: " + m_List->GetItem(row));
        });
    m_List->Activated().Connect([this](int row) { m_Status->SetText("Command activated: " + m_List->GetItem(row)); });
    m_Tree->CurrentChanged().Connect(
        [this](TreeWidget::NodeId item)
        {
            if (item)
                m_Status->SetText("Tree selection: " + m_Tree->GetItemText(item));
        });
    m_Table->CurrentCellChanged().Connect(
        [this](int row, int column)
        {
            if (row >= 0)
                m_Status->SetText("Cell (" + std::to_string(row) + ", " + std::to_string(column) +
                                  "): " + m_Table->GetCell(row, column));
        });
    m_Table->CellChanged().Connect(
        [this](int row, int column)
        {
            m_Status->SetText("CellChanged(" + std::to_string(row) + ", " + std::to_string(column) +
                              ") / value committed");
        });
    m_Menu = &m_Window.Ui().Root().Add<ContextMenu>();
    m_CopyAction = &m_Menu->AddAction("Copy selected value");
    m_CopyAction->Clicked().Connect([this] { CopyCurrentItem(); });
    m_Menu->AddAction("Add table row").Clicked().Connect([this] { AddTableRow(); });
    m_Menu->AddSeparator();
    m_Menu->AddAction("Reset scroll positions")
        .Clicked()
        .Connect(
            [this]
            {
                m_List->SetScrollOffset({});
                m_Tree->SetScrollOffset({});
                m_Table->SetScrollOffset({});
            });
    m_Menu->AddAction("Unavailable action", false);
    const auto connectMenu = [this](Widget& sourceWidget)
    {
        const WidgetRef sourceRef(&sourceWidget);
        sourceWidget.ContextMenuRequested().Connect(
            [this, sourceRef](Point point)
            {
                m_MenuSource = sourceRef;
                const bool selected = dynamic_cast<TableWidget*>(sourceRef.Get()) ? m_Table->GetCurrentCell().first >= 0
                                      : dynamic_cast<ListWidget*>(sourceRef.Get()) ? m_List->GetCurrentIndex() >= 0
                                                                                   : m_Tree->GetCurrentItem() != 0;
                m_CopyAction->SetEnabled(selected);
                m_Menu->Open(point);
            });
    };
    connectMenu(*m_List);
    connectMenu(*m_Tree);
    connectMenu(*m_Table);
    const WidgetRef actionButton(&actions);
    actions.Clicked().Connect(
        [this, actionButton]
        {
            m_MenuSource = WidgetRef(m_Table);
            m_CopyAction->SetEnabled(m_Table->GetCurrentCell().first >= 0);
            const auto bounds = actionButton.Get()->GetBounds();
            m_Menu->Open({bounds.m_X, bounds.m_Y + bounds.m_Height});
        });
}

void WidgetGallery::BuildScenes(Widget& page)
{
    auto& toolbar = page.Add<ToolBar>();
    auto& animate = toolbar.AddButton("Animate");
    animate.SetCheckable(true);
    auto& reset = toolbar.AddButton("Reset cameras");
    Caption(page, "Drag to orbit / wheel to zoom. Each viewport owns a separate OpenGL context and depth framebuffer.",
            "layout-note");
    auto& row = page.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetHeight(SizeRule::Fill(1, 120));
    row.SetGap(16);
    auto& primary = row.Add<Widget>();
    primary.AddClass("gallery-pane");
    primary.SetWidth(SizeRule::Fill(2, 200));
    Caption(primary, "SHADED / context A");
    m_MainSurface = &primary.Add<OpenGLWidget>();
    m_MainSurface->SetHeight(SizeRule::Fill());
    auto& secondary = row.Add<Widget>();
    secondary.AddClass("gallery-pane");
    secondary.SetWidth(SizeRule::Fill(1, 160));
    Caption(secondary, "WIREFRAME / context B");
    m_SecondarySurface = &secondary.Add<OpenGLWidget>();
    m_SecondarySurface->SetHeight(SizeRule::Fill());
    m_MainScene = std::make_shared<DemoScene>();
    m_SecondaryScene = std::make_shared<DemoScene>(true);
    ConnectScene(*m_MainSurface, m_MainScene);
    ConnectScene(*m_SecondarySurface, m_SecondaryScene);
    animate.Toggled().Connect(
        [this](bool animated)
        {
            m_MainScene->SetAnimated(animated);
            m_SecondaryScene->SetAnimated(animated);
            m_MainSurface->SetContinuousRendering(animated);
            m_SecondarySurface->SetContinuousRendering(animated);
        });
    reset.Clicked().Connect(
        [this]
        {
            m_MainScene->ResetCamera();
            m_SecondaryScene->ResetCamera();
            m_MainSurface->RequestFrame();
            m_SecondarySurface->RequestFrame();
        });
}

void WidgetGallery::BuildLayouts(Widget& page)
{
    auto& toolbar = page.Add<ToolBar>();
    for (const auto& option :
         std::vector<std::pair<std::string, int>>{{"Narrow", 880}, {"Standard", 1180}, {"Wide", 1520}})
    {
        toolbar.AddButton(option.first)
            .Clicked()
            .Connect(
                [this, width = option.second]
                {
                    m_Window.SetSize(width, 820);
                    m_Window.RenderFrame();
                    UpdateLayoutReadout();
                });
    }
    toolbar.AddSeparator();
    toolbar.AddButton("Inspect sizes").Clicked().Connect([this] { UpdateLayoutReadout(); });
    auto& constraints = toolbar.AddButton("Constraint layouts");
    m_ConstraintShortcut = WidgetRef(&constraints);
    constraints.Clicked().Connect([this] { m_Tabs.SetCurrentIndex(m_Tabs.GetTabCount() - 1); });
    Caption(page,
            "Resize the native window or choose a preset. Fixed and content sizes stay stable; Fill divides the "
            "remaining space.\nA capped Fill returns its unused share to the others. Hard minima overflow explicitly "
            "instead of being silently squeezed.",
            "layout-note");
    auto& row = page.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetHeight(SizeRule::Fixed(160));
    row.SetGap(12);
    const auto box = [&](const std::string& style, const std::string& title, const std::string& description) -> Widget&
    {
        auto& panel = row.Add<Widget>();
        panel.AddClass("layout-box");
        panel.AddClass(style);
        Caption(panel, title, "layout-caption");
        Caption(panel, description, "layout-description");
        return panel;
    };
    m_LayoutFixed =
        &box("layout-fixed", "Fixed(180)", "Always 180 logical px.\nContent cannot change\nthis size contract.");
    m_LayoutFixed->SetWidth(SizeRule::Fixed(180));
    m_LayoutCapped = &box("layout-capped", "Fill(1, 120, 240)", "Minimum: 120 px\nMaximum: 240 px\nWeight: 1");
    m_LayoutCapped->SetWidth(SizeRule::Fill(1, 120, 240));
    m_LayoutFluid = &box("layout-fluid", "Fill(2, 160)",
                         "Minimum: 160 px / weight: 2\nReceives the remaining free space,\nincluding space returned by "
                         "a capped neighbor.");
    m_LayoutFluid->SetWidth(SizeRule::Fill(2, 160));
    m_LayoutFoundation = std::make_unique<LayoutGallery>(m_Window, page);
    m_LayoutReadout = &Caption(page, "Use Inspect sizes to read the current geometry.", "gallery-status");
}

void WidgetGallery::AddTableRow()
{
    const auto index =
        m_Table->AddRow({std::to_string(m_Table->GetRowCount() + 1001), "New frame", "CQUAD4", "New", "0"});
    m_Table->SetCurrentCell(index, 1);
    m_Status->SetText("Added row " + std::to_string(index) + " / " + std::to_string(m_Table->GetRowCount()) +
                      " total rows");
}

void WidgetGallery::CopyCurrentItem()
{
    std::string value;
    if (m_MenuSource.Get() == m_Table)
    {
        const auto [row, column] = m_Table->GetCurrentCell();
        if (row >= 0)
            value = m_Table->GetCell(row, column);
    }
    else if (m_MenuSource.Get() == m_List && m_List->GetCurrentIndex() >= 0)
        value = m_List->GetItem(m_List->GetCurrentIndex());
    else if (m_MenuSource.Get() == m_Tree && m_Tree->GetCurrentItem())
        value = m_Tree->GetItemText(m_Tree->GetCurrentItem());
    m_Window.Ui().SetClipboardText(value);
    m_Status->SetText("Copied: " + value);
}

void WidgetGallery::UpdateLayoutReadout()
{
    const auto pixels = [](const Widget* widget)
    { return std::to_string(static_cast<int>(std::lround(widget->GetBounds().m_Width))); };
    m_LayoutReadout->SetText(
        "Last inspection: fixed " + pixels(m_LayoutFixed) + " px / capped " + pixels(m_LayoutCapped) + " px / fluid " +
        pixels(m_LayoutFluid) +
        " px / overflowing containers: " + std::to_string(m_Window.Ui().GetOverflowingContainers().size()));
}

void WidgetGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    auto& ui = m_Window.Ui();
    auto& gpu = m_Window.Graphics();
    std::string error;
    Check(ui.LoadStyleSheet(assets / "dark.css", error), "dark gallery CSS: " + error);
    m_Tabs.SetCurrentIndex(1);
    m_Window.RenderFrame();
    Check(m_Table->GetRowCount() == 10000 && m_List->GetItemCount() == 2006, "large gallery models");
    Check(gpu.GetStats().m_Glyphs < 10000, "virtualized painting submits visible rows only");
    gpu.SavePpm(output / "widgets-dark.ppm");
    const auto cell = m_Table->GetCellBounds(2, 1);
    Click(ui, {cell.m_X + 20, cell.m_Y + cell.m_Height * 0.5f});
    Check(m_Table->GetCurrentCell() == std::pair<int, int>{2, 1}, "table pointer selection");
    m_Table->BeginEdit(2, 1);
    ui.TextInput("Edited from the GPU test");
    m_Window.RenderFrame();
    gpu.SavePpm(output / "table-editing.ppm");
    ui.KeyEvent(Key::Enter, true);
    Check(m_Table->GetCell(2, 1) == "Edited from the GPU test", "gallery cell edit commit");
    ui.ContextMenu({cell.m_X + 20, cell.m_Y + 10});
    m_Window.RenderFrame();
    Check(m_Menu->IsOpen(), "right-click opens the real popup layer");
    gpu.SavePpm(output / "context-menu.ppm");
    ui.KeyEvent(Key::Down, true);
    ui.KeyEvent(Key::Enter, true);
    Check(ui.GetClipboard() == "Edited from the GPU test" && !m_Menu->IsOpen(), "menu copies selected table value");
    Click(ui, Center(*m_Compact));
    m_Window.RenderFrame();
    Check(m_Compact->IsChecked() && m_Table->GetStyle().m_RowHeight == 24, "toolbar toggle changes CSS row density");
    Click(ui, Center(*m_Compact));
    Click(ui, Center(*m_AddRow));
    Check(m_Table->GetRowCount() == 10001, "toolbar command changes table model");
    m_Table->SetCurrentCell(0, 0);
    Check(ui.LoadStyleSheet(assets / "light.css", error), "light gallery CSS");
    m_Window.RenderFrame();
    gpu.SavePpm(output / "widgets-light.ppm");
    Check(ui.LoadStyleSheet(assets / "dark.css", error), "restore dark gallery CSS");
    m_Tabs.SetCurrentIndex(2);
    m_Window.RenderFrame();
    Check(m_MainSurface->GetContextIdentity() != m_SecondarySurface->GetContextIdentity(),
          "surfaces own different contexts");
    Check(m_MainSurface->GetContextIdentity() != m_Window.GetContextIdentity(),
          "surface context differs from UI context");
    Check(m_MainScene->GetContextIdentity() == m_MainSurface->GetContextIdentity() &&
              m_SecondaryScene->GetContextIdentity() == m_SecondarySurface->GetContextIdentity(),
          "scene callbacks run in their surface contexts");
    gpu.SavePpm(output / "opengl-scenes.ppm");
    const auto rendered = m_MainScene->GetRenderCount();
    m_Window.RenderFrame();
    Check(m_MainScene->GetRenderCount() == rendered, "static 3D scenes reuse their texture");
    const auto point = Center(*m_MainSurface);
    ui.PointerDown(point);
    ui.PointerMove({point.m_X + 50, point.m_Y + 20});
    ui.PointerUp({point.m_X + 50, point.m_Y + 20});
    m_Window.RenderFrame();
    Check(m_MainScene->GetRenderCount() == rendered + 1, "orbit gesture requests one scene render");
    m_Tabs.SetCurrentIndex(3);
    for (int width : {880, 1180, 1520})
    {
        m_Window.SetSize(width, 820);
        m_Window.RenderFrame();
        UpdateLayoutReadout();
        m_Window.RenderFrame();
        Check(std::abs(m_LayoutFixed->GetBounds().m_Width - 180) < 0.01f, "fixed layout width survives resize");
        Check(m_LayoutCapped->GetBounds().m_Width >= 120 && m_LayoutCapped->GetBounds().m_Width <= 240.01f,
              "capped Fill obeys limits");
        Check(m_LayoutFluid->GetBounds().m_Width >= 160, "remaining Fill obeys its minimum");
        gpu.SavePpm(output / ("layout-" + std::to_string(width) + ".ppm"));
    }
    m_LayoutFoundation->Verify(output, [this] { UpdateLayoutReadout(); });
    VerifyContexts(assets, output);
    m_Tabs.SetCurrentIndex(4);
    m_Animations->Verify(output);
    m_Tabs.SetCurrentIndex(5);
    m_RichCss->Verify(assets, output);
    m_Tabs.SetCurrentIndex(6);
    m_Window.SetSize(1180, 820);
    m_Sketch->Verify(assets, output);
    m_Tabs.SetCurrentIndex(7);
    m_CustomWidgets->Verify(output);
    m_Tabs.SetCurrentIndex(8);
    m_WidgetStyles->Verify(assets, output);
    m_Tabs.SetCurrentIndex(9);
    m_Foundation->Verify(output);
    m_Tabs.SetCurrentIndex(10);
    m_State->Verify(output);
    m_Tabs.SetCurrentIndex(11);
    m_Models->Verify(output);
    m_Tabs.SetCurrentIndex(3);
    m_Window.RenderFrame(ui.GetTime());
    Click(ui, Center(*m_ConstraintShortcut.Get()));
    Check(m_Tabs.GetCurrentIndex() == 12, "visible Layout lab shortcut opens the constraints page");
    m_Constraints->Verify(assets, output);
    m_Window.RenderFrame(ui.GetTime());
    std::ofstream report(output / "widgets-report.txt");
    report << "EStudioUI 0.3 widget integration: PASS\n"
           << "Real renderer captures: dark/light collections, table editor, popup menu, two 3D scenes, three layout "
              "sizes.\n"
           << "10,000 table rows and 2,006 list items; only viewport rows submitted.\n"
           << "Checked: selection/editing, toolbar toggles/actions, popup keyboard/clipboard, fixed and capped Fill "
              "sizing,\n"
           << "distinct native contexts, scene callback contexts, static-scene caching, orbit input, shared-texture "
              "colors,\n"
           << "context state isolation, DPI-sized FBOs, depth attachments, resize, callback failure recovery and "
              "cleanup.\n";
}

void WidgetGallery::VerifyContexts(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    WindowOptions options;
    options.m_Width = 520;
    options.m_Height = 240;
    options.m_Headless = m_Window.IsHeadless();
    options.m_Title = "EStudioUI context verification";
    int released = 0;
    {
        Window probe(options, assets / "NotoSans.ttf");
        auto& ui = probe.Ui();
        ui.Root().SetLayoutDirection(Axis::Horizontal);
        ui.Root().SetGap(10);
        std::string error;
        Check(
            ui.SetStyleSheet(
                "window { background: black; padding: 10px; } #overlay { background: rgba(255, 255, 0, 0.5); }", error),
            "surface probe CSS");
        auto& first = ui.Root().Add<OpenGLWidget>();
        first.SetWidth(SizeRule::Fill());
        first.SetHeight(SizeRule::Fill());
        auto& second = ui.Root().Add<OpenGLWidget>();
        second.SetWidth(SizeRule::Fill());
        second.SetHeight(SizeRule::Fill());
        int redFrames = 0;
        first.SetCallbacks(
            []
            {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, 1, 1);
            },
            [&](const OpenGLFrame& frame)
            {
                GLint depthType = 0;
                GLint framebuffer = 0;
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
                GLint viewport[4]{};
                glGetIntegerv(GL_VIEWPORT, viewport);
                Check(viewport[2] == frame.m_Width && viewport[3] == frame.m_Height,
                      "scene viewport is restored after initialization");
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                                      GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthType);
                Check(static_cast<unsigned>(framebuffer) == frame.m_Framebuffer && depthType == GL_RENDERBUFFER,
                      "scene receives its own depth framebuffer");
                glClearColor(1, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glDisable(GL_BLEND);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                ++redFrames;
            },
            [&] { ++released; });
        second.SetCallbacks(
            {},
            [](const OpenGLFrame&)
            {
                glClearColor(0, 1, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT);
            },
            [&] { ++released; });
        auto& overlay = ui.Root().Add<Widget>();
        overlay.SetId("overlay");
        overlay.SetFloatingBounds({70, 30, 40, 40});
        probe.RenderFrame();
        const auto red = probe.Graphics().ReadPixel(Center(first));
        const auto green = probe.Graphics().ReadPixel(Center(second));
        const auto blended = probe.Graphics().ReadPixel({80, 40});
        Check(red.m_R > 0.95f && red.m_G < 0.05f && green.m_G > 0.95f && green.m_R < 0.05f,
              "two context textures retain distinct framebuffer colors");
        Check(blended.m_R > 0.95f && blended.m_G > 0.45f && blended.m_G < 0.55f,
              "scene GL state cannot leak into UI blending or polygon fill");
        Check(first.GetContextIdentity() != second.GetContextIdentity() &&
                  first.GetContextIdentity() != probe.GetContextIdentity(),
              "probe surfaces are separate native contexts");
        probe.Graphics().SavePpm(output / "context-isolation.ppm");
        probe.RenderFrame();
        Check(redFrames == 1, "unchanged surfaces do not render again");
        const auto content = first.GetContentBounds();
        ui.PrepareSurfaces(1, 2, 2);
        Check(first.GetFramebufferSize().m_X == std::ceil(content.m_Width * 2) &&
                  first.GetFramebufferSize().m_Y == std::ceil(content.m_Height * 2),
              "offscreen framebuffer scales to physical DPI pixels");
        const auto oldWidth = first.GetFramebufferSize().m_X;
        probe.SetSize(700, 300);
        probe.RenderFrame();
        Check(first.GetFramebufferSize().m_X > 0 && first.GetFramebufferSize().m_X != oldWidth,
              "surface framebuffer reallocates after resize");
        first.SetCallbacks([] { throw std::runtime_error("intentional initialization failure"); }, {},
                           [&] { ++released; });
        bool failed = false;
        try
        {
            probe.RenderFrame();
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        Check(failed && reinterpret_cast<std::uintptr_t>(glfwGetCurrentContext()) == probe.GetContextIdentity(),
              "scene exceptions restore the UI context");
        first.SetCallbacks(
            {},
            [](const OpenGLFrame&)
            {
                glClearColor(0, 0, 1, 1);
                glClear(GL_COLOR_BUFFER_BIT);
            },
            [&] { ++released; });
        probe.RenderFrame();
        Check(probe.Graphics().ReadPixel(Center(first)).m_B > 0.95f, "surface recovers after replacing a failed scene");
        first.SetContinuousRendering(true);
        Check(ui.HasAnimations(), "visible continuous surfaces request animation ticks");
        first.SetVisible(false);
        probe.RenderFrame();
        Check(!ui.HasAnimations(), "hidden surfaces do not keep the event loop animating");
    }
    Check(released == 4, "scene cleanup runs for replaced, failed, and destroyed surfaces");
}
} // namespace EStudioUI
