#include "SketchGallery.h"

#include <EStudioUI/TextBox.h>
#include <EStudioUI/ToolBar.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Sketch gallery verification: " + message);
}

const char* ToolName(SketchTool tool)
{
    switch (tool)
    {
    case SketchTool::Line:
        return "Line";
    case SketchTool::Rectangle:
        return "Rectangle";
    case SketchTool::Circle:
        return "Circle";
    case SketchTool::Pan:
        return "Pan";
    default:
        return "Select";
    }
}

std::filesystem::path Utf8Path(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
} // namespace

SketchGallery::SketchGallery(Window& window, Widget& page) : m_Window(window)
{
    page.SetGap(8);
    auto& tools = page.Add<ToolBar>();
    for (const auto& [tool, label] :
         std::vector<std::pair<SketchTool, std::string>>{{SketchTool::Select, "Select [V]"},
                                                         {SketchTool::Line, "Line [L]"},
                                                         {SketchTool::Rectangle, "Rectangle [R]"},
                                                         {SketchTool::Circle, "Circle [C]"},
                                                         {SketchTool::Pan, "Pan [P]"}})
    {
        auto& button = tools.AddButton(label);
        button.SetCheckable(true);
        m_Tools.emplace_back(tool, &button);
        button.Clicked().Connect(
            [this, tool]
            {
                m_Sketch->SetTool(tool);
                FocusCanvas();
            });
    }
    tools.AddSeparator();
    auto& snap = tools.AddButton("Snap: 10 units");
    snap.SetCheckable(true);
    snap.SetChecked(true);
    snap.Toggled().Connect(
        [this](bool enabled)
        {
            m_Sketch->SetSnapping(enabled);
            FocusCanvas();
        });
    auto& actions = page.Add<ToolBar>();
    actions.AddButton("Undo").Clicked().Connect(
        [this]
        {
            m_Sketch->Undo();
            FocusCanvas();
        });
    actions.AddButton("Redo").Clicked().Connect(
        [this]
        {
            m_Sketch->Redo();
            FocusCanvas();
        });
    actions.AddButton("Delete").Clicked().Connect(
        [this]
        {
            m_Sketch->DeleteSelection();
            FocusCanvas();
        });
    actions.AddButton("Fit geometry")
        .Clicked()
        .Connect(
            [this]
            {
                m_Sketch->FitScene(36);
                FocusCanvas();
            });
    actions.AddSeparator();
    actions.AddButton("200,000 items").Clicked().Connect([this] { LoadStress(); });
    actions.AddButton("Reset sample").Clicked().Connect([this] { ResetSample(); });
    auto& files = page.Add<ToolBar>();
    files.Add<Label>("File").SetWidth(SizeRule::Fixed(30));
    m_FilePath = &files.Add<TextBox>("sketch.esketch");
    m_FilePath->SetWidth(SizeRule::Fill(1, 150));
    m_FilePath->SetHeight(SizeRule::Fixed(34));
    files.AddButton("Save").Clicked().Connect([this] { SaveDocument(); });
    files.AddButton("Open").Clicked().Connect([this] { OpenDocument(); });
    auto& help =
        page.Add<Label>("Drag to draw / select and move. Empty drag selects enclosed shapes; Shift adds or toggles.\n"
                        "Wheel zooms / Space or Alt + drag pans / Shift constrains lines / Esc cancels / Ctrl+Z, "
                        "Ctrl+Y undo and redo.");
    help.AddClass("layout-note");
    m_Sketch = &page.Add<SketchWidget>();
    m_Sketch->SetId("cad-sketch");
    m_Sketch->SetHeight(SizeRule::Fill(1, 200));
    m_Status = &page.Add<Label>();
    m_Status->AddClass("gallery-status");

    auto note = std::make_unique<Widget>();
    note->AddClass("gallery-pane");
    note->SetGap(5);
    auto& caption = note->Add<Label>("WORLD-ANCHORED WIDGET");
    caption.AddClass("pane-title");
    auto& row = note->Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetGap(8);
    row.SetHeight(SizeRule::Fixed(34));
    auto& editor = row.Add<TextBox>("Plate / revision A");
    editor.SetWidth(SizeRule::Fill(1, 80));
    auto& focus = row.Add<ToolButton>("Origin");
    focus.SetWidth(SizeRule::Fixed(74));
    focus.Clicked().Connect([this] { m_Sketch->SetCamera({0, 35}, 1); });
    m_Note = &m_Sketch->HostWidget(std::move(note), {220, -60, 300, 120});

    m_EditConnection = m_Sketch->Changed().Connect([this] { UpdateStatus(); });
    m_ViewConnection = m_Sketch->ViewChanged().Connect([this] { UpdateStatus(); });
    ResetSample();
}

SketchGallery::~SketchGallery()
{
    m_EditConnection.Disconnect();
    m_ViewConnection.Disconnect();
}

void SketchGallery::FocusCanvas()
{
    m_Window.Ui().SetFocus(m_Sketch);
}

void SketchGallery::ResetSample()
{
    m_Sketch->SetTool(SketchTool::Select);
    auto& document = m_Sketch->GetDocument();
    document.Clear();
    auto& scene = document.GetScene();
    const auto style = m_Sketch->GetShapeStyle();
    scene.Add({CanvasPrimitive::Rectangle, {-180, -100}, {180, 100}, 0}, style);
    for (const double x : {-135.0, 135.0})
        for (const double y : {-60.0, 60.0})
            scene.Add({CanvasPrimitive::Circle, {x, y}, {}, 18}, style);
    scene.Add({CanvasPrimitive::Circle, {0, 0}, {}, 42}, style);
    scene.Add({CanvasPrimitive::Line, {-200, 0}, {200, 0}, 0}, style);
    scene.Add({CanvasPrimitive::Line, {0, -120}, {0, 120}, 0}, style);
    m_LoadMilliseconds = 0;
    m_FileStatus.clear();
    m_Note->SetVisible(true);
    m_Sketch->SetCamera({0, 45}, 1);
    m_Sketch->SceneChanged();
    UpdateStatus();
}

void SketchGallery::LoadStress()
{
    m_Sketch->SetTool(SketchTool::Select);
    auto& document = m_Sketch->GetDocument();
    document.Clear();
    auto& scene = document.GetScene();
    m_FileStatus.clear();
    constexpr std::size_t count = 200000;
    const auto start = std::chrono::steady_clock::now();
    scene.Reserve(count);
    const auto style = m_Sketch->GetShapeStyle();
    for (std::size_t index = 0; index < count; ++index)
    {
        const double x = static_cast<double>(index % 500) * 40;
        const double y = static_cast<double>(index / 500) * 40;
        if (index % 3 == 0)
            scene.Add({CanvasPrimitive::Circle, {x + 12, y + 12}, {}, 10}, style);
        else if (index % 3 == 1)
            scene.Add({CanvasPrimitive::Rectangle, {x, y}, {x + 24, y + 24}, 0}, style);
        else
            scene.Add({CanvasPrimitive::Line, {x, y}, {x + 24, y + 24}, 0}, style);
    }
    m_LoadMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_Note->SetVisible(false);
    m_Sketch->SetCamera({10000, 8000}, 1);
    m_Sketch->SceneChanged();
    UpdateStatus();
    FocusCanvas();
}

void SketchGallery::SaveDocument()
{
    try
    {
        const auto path = Utf8Path(m_FilePath->GetText());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            throw std::runtime_error("Cannot open the destination file");
        m_Sketch->GetDocument().Save(file);
        file.flush();
        if (!file)
            throw std::runtime_error("Cannot complete the file write");
        m_FileStatus = "Saved " + m_FilePath->GetText();
    }
    catch (const std::exception& error)
    {
        m_FileStatus = std::string("Save failed: ") + error.what();
    }
    UpdateStatus();
}

void SketchGallery::OpenDocument()
{
    try
    {
        const auto path = Utf8Path(m_FilePath->GetText());
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Cannot open the source file");
        m_Sketch->GetDocument().Load(file);
        m_Sketch->SetTool(SketchTool::Select);
        m_Sketch->SceneChanged();
        m_Sketch->FitScene(36);
        m_LoadMilliseconds = 0;
        m_FileStatus = "Opened " + m_FilePath->GetText();
        FocusCanvas();
    }
    catch (const std::exception& error)
    {
        m_FileStatus = std::string("Open failed: ") + error.what();
    }
    UpdateStatus();
}

void SketchGallery::UpdateStatus()
{
    if (!m_Status)
        return;
    const auto tool = m_Sketch->GetTool();
    for (const auto& [candidate, button] : m_Tools)
        button->SetChecked(tool == candidate);
    std::ostringstream text;
    text << ToolName(tool) << " / " << m_Sketch->GetScene().Size() << " items";
    if (m_Sketch->GetContentBounds().m_Width > 0)
        text << " / " << m_Sketch->GetVisibleItemCount() << " visible / " << m_Sketch->GetQueryVisitCount()
             << " BVH visits";
    text << " / " << m_Sketch->GetDocument().GetSelection().size() << " selected / " << std::fixed
         << std::setprecision(1) << m_Sketch->GetZoom() * 100 << "% zoom";
    if (m_LoadMilliseconds > 0)
        text << " / BVH build " << std::setprecision(0) << m_LoadMilliseconds << " ms";
    if (!m_FileStatus.empty())
        text << " / " << m_FileStatus;
    m_Status->SetText(text.str());
}

void SketchGallery::Verify(const std::filesystem::path&, const std::filesystem::path& output)
{
    ResetSample();
    m_Window.RenderFrame();
    Check(m_Sketch->GetScene().Size() == 8, "sample contains eight editable shapes");
    Check(m_Sketch->GetVisibleItemCount() == 8, "sample BVH query submits visible shapes");
    m_Window.Graphics().SavePpm(output / "canvas-sketcher.ppm");
    auto& ui = m_Window.Ui();
    const auto draw = [&](SketchTool tool, CanvasPoint start, CanvasPoint end)
    {
        m_Sketch->SetTool(tool);
        const auto a = m_Sketch->ToScreen(start);
        const auto b = m_Sketch->ToScreen(end);
        ui.PointerDown(a);
        ui.PointerMove(b);
        ui.PointerUp(b);
    };
    draw(SketchTool::Line, {-80, -40}, {80, -40});
    Check(m_Sketch->GetScene().Size() == 9, "pointer gesture creates an undoable line");
    Check(m_Sketch->Undo() && m_Sketch->GetScene().Size() == 8, "undo removes the line");
    Check(m_Sketch->Redo() && m_Sketch->GetScene().Size() == 9, "redo restores the line");
    m_Sketch->SetTool(SketchTool::Select);
    ui.PointerDown(m_Sketch->ToScreen({0, -40}));
    ui.PointerUp(m_Sketch->ToScreen({0, -40}));
    Check(m_Sketch->GetDocument().GetSelection().size() == 1, "BVH pointer pick selects the line");
    ui.KeyEvent(Key::Delete, true);
    Check(m_Sketch->GetScene().Size() == 8, "Delete removes the selected line");
    std::stringstream serialized;
    m_Sketch->GetDocument().Save(serialized);
    const auto* boundScene = &m_Sketch->GetScene();
    m_Sketch->GetDocument().Load(serialized);
    m_Sketch->SceneChanged();
    m_Window.RenderFrame();
    Check(&m_Sketch->GetScene() == boundScene && m_Sketch->GetScene().Size() == 8,
          "native sketch reload preserves the viewport's live scene binding");
    std::ofstream report(output / "canvas-sketcher.txt");
    report << "PASS: live SketchWidget sample, pointer creation, BVH selection, undo/redo, keyboard deletion, "
              "native sketch reload.\n"
           << "Actual C++ OpenGL framebuffer: canvas-sketcher.ppm.\n"
           << "Scale workload available from the 200,000 items toolbar action.\n";
    ResetSample();
    m_Window.RenderFrame();
}
} // namespace EStudioUI
