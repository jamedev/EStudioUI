#include "SketchGallery.h"

#include <EStudioUI/ToolBar.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
constexpr std::array<SketchTool, 4> s_CurveTools{SketchTool::Arc, SketchTool::Spline, SketchTool::Path,
                                                 SketchTool::Text};

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
    case SketchTool::Arc:
        return "Arc";
    case SketchTool::Spline:
        return "Spline";
    case SketchTool::Path:
        return "Path";
    case SketchTool::Text:
        return "Text";
    case SketchTool::Pan:
        return "Pan";
    default:
        return "Select";
    }
}

CanvasPrimitive ToolPrimitive(SketchTool tool)
{
    switch (tool)
    {
    case SketchTool::Rectangle:
        return CanvasPrimitive::Rectangle;
    case SketchTool::Circle:
        return CanvasPrimitive::Circle;
    case SketchTool::Arc:
        return CanvasPrimitive::Arc;
    case SketchTool::Spline:
        return CanvasPrimitive::Spline;
    case SketchTool::Path:
        return CanvasPrimitive::Path;
    case SketchTool::Text:
        return CanvasPrimitive::Text;
    default:
        return CanvasPrimitive::Line;
    }
}

const char* PrimitiveName(CanvasPrimitive kind)
{
    switch (kind)
    {
    case CanvasPrimitive::Rectangle:
        return "Rectangle";
    case CanvasPrimitive::Circle:
        return "Circle";
    case CanvasPrimitive::Arc:
        return "Arc";
    case CanvasPrimitive::Spline:
        return "Cubic spline";
    case CanvasPrimitive::Path:
        return "Polyline path";
    case CanvasPrimitive::Text:
        return "Text annotation";
    case CanvasPrimitive::Group:
        return "Group";
    default:
        return "Line";
    }
}

std::filesystem::path Utf8Path(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
} // namespace

SketchGallery::SketchGallery(Window& window, Widget& page) : m_Window(window)
{
    page.SetGap(6);
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
    m_CurveTools = &tools.AddSplitButton("Curves");
    for (const auto tool : s_CurveTools)
        m_CurveTools->AddAction(std::string(ToolName(tool)) + (tool == SketchTool::Arc      ? " [A]"
                                                               : tool == SketchTool::Spline ? " [B]"
                                                               : tool == SketchTool::Path   ? " [W]"
                                                                                            : " [T]"));
    m_CurveTools->Triggered().Connect(
        [this](std::size_t index)
        {
            m_Sketch->SetTool(s_CurveTools[index]);
            FocusCanvas();
        });
    tools.AddSeparator();
    auto& snap = tools.AddButton("Snap 10");
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
    actions.AddSeparator();
    actions.AddButton("Group").Clicked().Connect(
        [this]
        {
            m_Sketch->GetDocument().GroupSelection();
            DocumentChanged();
        });
    actions.AddButton("Ungroup").Clicked().Connect(
        [this]
        {
            m_Sketch->GetDocument().UngroupSelection();
            DocumentChanged();
        });
    actions.AddButton("Duplicate")
        .Clicked()
        .Connect(
            [this]
            {
                m_Sketch->GetDocument().DuplicateSelection({20, 20});
                DocumentChanged();
            });
    actions.AddSeparator();
    actions.AddButton("Horizontal")
        .Clicked()
        .Connect(
            [this]
            {
                m_Sketch->GetDocument().SetHorizontal();
                DocumentChanged();
            });
    actions.AddButton("Vertical")
        .Clicked()
        .Connect(
            [this]
            {
                m_Sketch->GetDocument().SetHorizontal(false);
                DocumentChanged();
            });
    auto& files = page.Add<ToolBar>();
    files.SetId("sketch-files");
    files.Add<Label>("File").SetWidth(SizeRule::Fixed(28));
    m_FilePath = &files.Add<TextBox>("sketch.esketch");
    m_FilePath->SetWidth(SizeRule::Fill(1, 120));
    m_FilePath->SetHeight(SizeRule::Fixed(30));
    files.AddButton("Save").Clicked().Connect([this] { SaveDocument(); });
    files.AddButton("Open").Clicked().Connect([this] { OpenDocument(); });
    files.AddButton("Fit").Clicked().Connect(
        [this]
        {
            m_Sketch->FitScene(36);
            FocusCanvas();
        });
    files.AddButton("200,000 items").Clicked().Connect([this] { LoadStress(); });
    files.AddButton("Reset sample").Clicked().Connect([this] { ResetSample(); });
    m_Help = &page.Add<Label>();
    m_Help->AddClass("layout-note");
    m_Help->SetHeight(SizeRule::Fixed(34));
    auto& workspace = page.Add<Widget>();
    workspace.SetLayoutDirection(Axis::Horizontal);
    workspace.SetGap(8);
    workspace.SetHeight(SizeRule::Fill(1, 300));
    m_Sketch = &workspace.Add<SketchWidget>();
    m_Sketch->SetId("cad-sketch");
    m_Sketch->SetWidth(SizeRule::Fill(1, 250));
    m_Sketch->SetHeight(SizeRule::Fill());
    BuildInspector(workspace);
    m_Status = &page.Add<Label>();
    m_Status->AddClass("gallery-status");

    auto note = std::make_unique<Widget>();
    note->AddClass("gallery-pane");
    note->AddClass("sketch-note");
    note->SetGap(5);
    auto& caption = note->Add<Label>("WORLD-ANCHORED WIDGET");
    caption.AddClass("pane-title");
    auto& row = note->Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetGap(8);
    row.SetHeight(SizeRule::Fixed(32));
    auto& editor = row.Add<TextBox>("Plate / revision A");
    editor.SetWidth(SizeRule::Fill(1, 80));
    editor.SetHeight(SizeRule::Fixed(30));
    auto& focus = row.Add<ToolButton>("Origin");
    focus.SetWidth(SizeRule::Fixed(66));
    focus.Clicked().Connect([this] { m_Sketch->SetCamera({60, 30}, 0.9); });
    m_Note = &m_Sketch->HostWidget(std::move(note), {220, -70, 270, 112});

    m_EditConnection = m_Sketch->Changed().Connect(
        [this]
        {
            UpdateInspector();
            UpdateStatus();
        });
    m_ViewConnection = m_Sketch->ViewChanged().Connect([this] { UpdateStatus(); });
    ResetSample();
}

SketchGallery::~SketchGallery()
{
    m_EditConnection.Disconnect();
    m_ViewConnection.Disconnect();
}

void SketchGallery::BuildInspector(Widget& parent)
{
    auto& panel = parent.Add<Widget>();
    panel.SetId("sketch-inspector");
    panel.AddClass("gallery-pane");
    panel.SetWidth(SizeRule::Fixed(300));
    panel.SetHeight(SizeRule::Fill());
    panel.SetGap(4);
    panel.Add<Label>("PRECISION GEOMETRY").AddClass("pane-title");
    m_InspectorTitle = &panel.Add<Label>();
    m_InspectorTitle->SetHeight(SizeRule::Fixed(20));
    const auto addField = [this](Widget& row, Field field, const char* label, double value) -> Label*
    {
        auto& column = row.Add<Widget>();
        column.SetGap(2);
        column.SetWidth(SizeRule::Fill(1, 100));
        auto& title = column.Add<Label>(label);
        title.SetHeight(SizeRule::Fixed(17));
        auto& input = column.Add<NumericInput>(value);
        input.SetHeight(SizeRule::Fixed(28));
        input.SetWidth(SizeRule::Fill());
        input.SetDecimals(3);
        m_Fields[field] = &input;
        return &title;
    };
    const auto addRow = [&panel]() -> Widget&
    {
        auto& row = panel.Add<Widget>();
        row.SetLayoutDirection(Axis::Horizontal);
        row.SetGap(8);
        row.SetHeight(SizeRule::Fixed(47));
        return row;
    };
    auto& origin = addRow();
    addField(origin, X, "Origin X", -80);
    addField(origin, Y, "Origin Y", -40);
    m_EndRow = &addRow();
    m_EndLabels[0] = addField(*m_EndRow, EndX, "End X", 80);
    m_EndLabels[1] = addField(*m_EndRow, EndY, "End Y", 40);
    m_PointRow = &addRow();
    addField(*m_PointRow, PointIndex, "Control / vertex index", 0);
    m_Fields[PointIndex]->SetDecimals(0);
    m_Fields[PointIndex]->SetRange(0, 65535);
    m_Fields[PointIndex]->ValueChanged().Connect(
        [this](double value)
        {
            if (!m_UpdatingInspector)
            {
                m_UpdatingInspector = true;
                m_Fields[PointIndex]->SetValue(std::round(value));
                m_UpdatingInspector = false;
                UpdatePointFields();
            }
        });
    m_DimensionRow = &addRow();
    m_DimensionLabel = addField(*m_DimensionRow, Dimension, "Radius", 40);
    m_Fields[Dimension]->SetRange(0.000001, 1e12);
    auto& setDimension = m_DimensionRow->Add<ToolButton>("Set size");
    setDimension.SetWidth(SizeRule::Fixed(88));
    setDimension.SetHeight(SizeRule::Fixed(47));
    setDimension.Clicked().Connect([this] { SetDimension(); });
    m_AngleRow = &addRow();
    addField(*m_AngleRow, StartAngle, "Start (degrees)", 0);
    addField(*m_AngleRow, SweepAngle, "Sweep (degrees)", 90);
    m_Fields[SweepAngle]->SetRange(-360, 360);
    m_TextRow = &panel.Add<Widget>();
    m_TextRow->SetGap(2);
    m_TextRow->SetHeight(SizeRule::Fixed(47));
    m_TextRow->Add<Label>("Annotation text").SetHeight(SizeRule::Fixed(17));
    m_Annotation = &m_TextRow->Add<TextBox>("Annotation");
    m_Annotation->SetMaxBytes(65536);
    m_Annotation->SetHeight(SizeRule::Fixed(28));
    m_Annotation->TextChanged().Connect(
        [this](const std::string& text)
        {
            if (!m_UpdatingInspector)
                m_Sketch->SetAnnotationText(text);
        });
    m_FontRow = &addRow();
    addField(*m_FontRow, FontSize, "Font size (world units)", 18);
    m_Fields[FontSize]->SetRange(1, 10000);
    m_Fields[FontSize]->ValueChanged().Connect(
        [this](double size)
        {
            if (!m_UpdatingInspector)
                m_Sketch->SetAnnotationFontSize(size);
        });
    m_ClosePath = &panel.Add<ToolButton>("Close path");
    m_ClosePath->SetHeight(SizeRule::Fixed(28));
    m_ClosePath->SetCheckable(true);
    m_ClosePath->Toggled().Connect(
        [this](bool closed)
        {
            if (!m_UpdatingInspector)
                m_Sketch->SetPathClosed(closed);
        });
    auto& commands = panel.Add<Widget>();
    commands.SetLayoutDirection(Axis::Horizontal);
    commands.SetGap(6);
    commands.SetHeight(SizeRule::Fixed(30));
    m_Create = &commands.Add<ToolButton>("Create");
    m_Create->SetWidth(SizeRule::Fill());
    m_Create->Clicked().Connect([this] { CommitInspector(true); });
    m_Apply = &commands.Add<ToolButton>("Apply edit");
    m_Apply->SetWidth(SizeRule::Fill());
    m_Apply->Clicked().Connect([this] { CommitInspector(false); });
    auto& zoomRow = panel.Add<Widget>();
    zoomRow.SetLayoutDirection(Axis::Horizontal);
    zoomRow.SetGap(8);
    zoomRow.SetHeight(SizeRule::Fixed(28));
    zoomRow.Add<Label>("Zoom").SetWidth(SizeRule::Fixed(40));
    m_Zoom = &zoomRow.Add<Slider>();
    m_Zoom->SetRange(0.1, 4);
    m_Zoom->SetStep(0.05);
    m_Zoom->SetValue(1);
    m_Zoom->SetWidth(SizeRule::Fill());
    m_Zoom->SetHeight(SizeRule::Fill());
    m_Zoom->ValueChanged().Connect(
        [this](double zoom)
        {
            if (!m_UpdatingZoom)
                m_Sketch->SetCamera(m_Sketch->GetCenter(), zoom);
        });
    m_Zoom->SliderReleased().Connect([this] { FocusCanvas(); });
}

const CanvasItem* SketchGallery::InspectedItem() const
{
    const auto selection = m_Sketch->GetDocument().GetSelection();
    if (selection.size() != 1)
        return nullptr;
    const auto* item = m_Sketch->GetScene().Get(selection.front());
    const auto tool = m_Sketch->GetTool();
    if (item &&
        (tool == SketchTool::Select || tool == SketchTool::Pan || item->m_Geometry.m_Kind == ToolPrimitive(tool)))
        return item;
    return nullptr;
}

void SketchGallery::UpdateInspector()
{
    m_UpdatingInspector = true;
    const auto* item = InspectedItem();
    const auto tool = m_Sketch->GetTool();
    m_InspectorKind = item ? item->m_Geometry.m_Kind : ToolPrimitive(tool);
    const auto kind = m_InspectorKind;
    const bool curve = kind == CanvasPrimitive::Spline || kind == CanvasPrimitive::Path;
    m_InspectorTitle->SetText(std::string(item ? "Edit: " : "Create: ") + PrimitiveName(kind));
    m_EndRow->SetVisible(kind == CanvasPrimitive::Line || kind == CanvasPrimitive::Rectangle ||
                         kind == CanvasPrimitive::Text || curve);
    m_PointRow->SetVisible(curve && item);
    m_DimensionRow->SetVisible(kind == CanvasPrimitive::Circle || kind == CanvasPrimitive::Arc ||
                               kind == CanvasPrimitive::Line);
    m_DimensionLabel->SetText(kind == CanvasPrimitive::Line ? "Line length" : "Radius");
    m_AngleRow->SetVisible(kind == CanvasPrimitive::Arc);
    m_TextRow->SetVisible(kind == CanvasPrimitive::Text);
    m_FontRow->SetVisible(kind == CanvasPrimitive::Text);
    m_ClosePath->SetVisible(kind == CanvasPrimitive::Path);
    m_Create->SetEnabled(kind != CanvasPrimitive::Group);
    m_Apply->SetEnabled(item != nullptr);
    const bool sized = kind == CanvasPrimitive::Rectangle || kind == CanvasPrimitive::Text;
    m_EndLabels[0]->SetText(sized ? "Width" : curve ? "Point X (local)" : "End X");
    m_EndLabels[1]->SetText(sized ? "Height" : curve ? "Point Y (local)" : "End Y");
    m_Fields[EndX]->SetRange(sized ? 0.001 : -1e12, 1e12);
    m_Fields[EndY]->SetRange(sized ? 0.001 : -1e12, 1e12);
    if (item)
    {
        const auto& geometry = item->m_Geometry;
        const auto bounds = geometry.Bounds();
        m_Fields[X]->SetValue(sized ? bounds.m_X : geometry.m_A.m_X);
        m_Fields[Y]->SetValue(sized ? bounds.m_Y : geometry.m_A.m_Y);
        if (kind == CanvasPrimitive::Line || kind == CanvasPrimitive::Rectangle || kind == CanvasPrimitive::Text)
        {
            m_Fields[EndX]->SetValue(sized ? bounds.m_Width : geometry.m_B.m_X);
            m_Fields[EndY]->SetValue(sized ? bounds.m_Height : geometry.m_B.m_Y);
        }
        if (kind == CanvasPrimitive::Circle || kind == CanvasPrimitive::Arc)
            m_Fields[Dimension]->SetValue(geometry.m_Radius);
        else if (kind == CanvasPrimitive::Line)
            m_Fields[Dimension]->SetValue(
                std::hypot(geometry.m_B.m_X - geometry.m_A.m_X, geometry.m_B.m_Y - geometry.m_A.m_Y));
        if (kind == CanvasPrimitive::Arc)
        {
            m_Fields[StartAngle]->SetValue(geometry.m_Data->m_StartAngle * 180 / std::numbers::pi);
            m_Fields[SweepAngle]->SetValue(geometry.m_Data->m_SweepAngle * 180 / std::numbers::pi);
        }
        if (kind == CanvasPrimitive::Text)
        {
            m_Annotation->SetText(geometry.m_Data->m_Text);
            m_Fields[FontSize]->SetValue(geometry.m_Data->m_FontSize);
        }
        if (curve)
        {
            m_Fields[PointIndex]->SetRange(0, static_cast<double>(geometry.m_Data->m_Points.size() - 1));
            UpdatePointFields();
        }
        if (kind == CanvasPrimitive::Path)
            m_ClosePath->SetChecked(geometry.m_Data->m_Closed);
    }
    else if (kind == CanvasPrimitive::Text)
    {
        m_Annotation->SetText(m_Sketch->GetAnnotationText());
        m_Fields[FontSize]->SetValue(m_Sketch->GetAnnotationFontSize());
    }
    else if (kind == CanvasPrimitive::Path)
        m_ClosePath->SetChecked(m_Sketch->IsPathClosed());
    m_UpdatingInspector = false;
    if (kind == CanvasPrimitive::Text)
    {
        m_Sketch->SetAnnotationText(m_Annotation->GetText());
        m_Sketch->SetAnnotationFontSize(m_Fields[FontSize]->GetValue());
    }
    else if (kind == CanvasPrimitive::Path)
        m_Sketch->SetPathClosed(m_ClosePath->IsChecked());
}

void SketchGallery::UpdatePointFields()
{
    const auto* item = InspectedItem();
    if (!item ||
        (item->m_Geometry.m_Kind != CanvasPrimitive::Spline && item->m_Geometry.m_Kind != CanvasPrimitive::Path))
        return;
    const auto& points = item->m_Geometry.m_Data->m_Points;
    const auto index = std::min(points.size() - 1, static_cast<std::size_t>(m_Fields[PointIndex]->GetValue()));
    m_Fields[EndX]->SetValue(points[index].m_X);
    m_Fields[EndY]->SetValue(points[index].m_Y);
}

void SketchGallery::CommitInspector(bool create)
{
    try
    {
        for (auto* field : m_Fields)
            if (field->IsVisible() && !field->Commit())
                throw std::invalid_argument("Enter a valid number before applying geometry.");
        const auto* item = InspectedItem();
        if (!create && !item)
            return;
        const auto kind = m_InspectorKind;
        const CanvasPoint origin{m_Fields[X]->GetValue(), m_Fields[Y]->GetValue()};
        const CanvasPoint end{m_Fields[EndX]->GetValue(), m_Fields[EndY]->GetValue()};
        const double radius = m_Fields[Dimension]->GetValue();
        CanvasGeometry geometry;
        if (kind == CanvasPrimitive::Line)
            geometry = {kind, origin, end};
        else if (kind == CanvasPrimitive::Rectangle)
            geometry = {kind, origin, {origin.m_X + end.m_X, origin.m_Y + end.m_Y}};
        else if (kind == CanvasPrimitive::Circle)
            geometry = {kind, origin, {}, radius};
        else if (kind == CanvasPrimitive::Arc)
            geometry = MakeCanvasArc(origin, radius, m_Fields[StartAngle]->GetValue() * std::numbers::pi / 180,
                                     m_Fields[SweepAngle]->GetValue() * std::numbers::pi / 180);
        else if (kind == CanvasPrimitive::Text)
            geometry = MakeCanvasText({origin.m_X, origin.m_Y, end.m_X, end.m_Y}, m_Annotation->GetText(),
                                      m_Fields[FontSize]->GetValue());
        else if (kind == CanvasPrimitive::Spline || kind == CanvasPrimitive::Path)
        {
            std::vector<CanvasPoint> points;
            if (!create)
            {
                points = item->m_Geometry.m_Data->m_Points;
                const auto index =
                    std::min(points.size() - 1, static_cast<std::size_t>(m_Fields[PointIndex]->GetValue()));
                points[index] = end;
            }
            else if (kind == CanvasPrimitive::Spline)
                points = {{0, 0}, {end.m_X / 3, 0}, {end.m_X * 2 / 3, end.m_Y}, end};
            else
            {
                points = {{0, 0}, end};
                if (m_ClosePath->IsChecked())
                    points.insert(points.begin() + 1, {end.m_X, 0});
            }
            geometry = kind == CanvasPrimitive::Spline
                           ? MakeCanvasSpline(origin, std::move(points))
                           : MakeCanvasPath(origin, std::move(points), m_ClosePath->IsChecked());
        }
        else
            geometry = TranslateGeometry(
                item->m_Geometry, {origin.m_X - item->m_Geometry.m_A.m_X, origin.m_Y - item->m_Geometry.m_A.m_Y});
        auto& document = m_Sketch->GetDocument();
        const bool changed = create ? static_cast<bool>(document.AddShape(geometry, m_Sketch->GetShapeStyle()))
                                    : document.UpdateShape(item->m_Id, geometry);
        m_FileStatus = changed ? (create ? "Created exact geometry" : "Applied undoable edit") : "No geometry change";
        DocumentChanged();
    }
    catch (const std::exception& error)
    {
        m_FileStatus = std::string("Geometry: ") + error.what();
        UpdateStatus();
    }
}

void SketchGallery::SetDimension()
{
    try
    {
        if (!m_Fields[Dimension]->Commit())
            throw std::invalid_argument("Enter a positive finite dimension.");
        auto& document = m_Sketch->GetDocument();
        const double size = m_Fields[Dimension]->GetValue();
        const bool changed =
            m_InspectorKind == CanvasPrimitive::Line ? document.SetLineLength(size) : document.SetCircleRadius(size);
        m_FileStatus = changed ? "Applied exact dimension" : "Select compatible geometry first";
        DocumentChanged();
    }
    catch (const std::exception& error)
    {
        m_FileStatus = std::string("Dimension: ") + error.what();
        UpdateStatus();
    }
}

void SketchGallery::DocumentChanged()
{
    m_Sketch->SceneChanged();
    UpdateInspector();
    UpdateStatus();
    FocusCanvas();
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
    scene.Add(MakeCanvasArc({-125, 130}, 35, 0, std::numbers::pi), style);
    scene.Add(MakeCanvasSpline({-55, 150}, {{0, 0}, {25, -60}, {70, 50}, {105, 0}}), style);
    scene.Add(MakeCanvasPath({85, 135}, {{0, 0}, {25, 35}, {55, 5}, {90, 35}}), style);
    scene.Add(MakeCanvasText({-180, -148, 390, 30}, "PLATE  /  editable curves + dimensions", 17), style);
    m_LoadMilliseconds = 0;
    m_FileStatus.clear();
    m_Note->SetVisible(true);
    m_Sketch->SetCamera({60, 25}, 0.9);
    m_Sketch->SceneChanged();
    UpdateInspector();
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
    UpdateInspector();
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
    UpdateInspector();
    UpdateStatus();
}

void SketchGallery::UpdateStatus()
{
    if (!m_Status)
        return;
    const auto tool = m_Sketch->GetTool();
    for (const auto& [candidate, button] : m_Tools)
        if (button->IsChecked() != (tool == candidate))
            button->SetChecked(tool == candidate);
    const auto curve = std::find(s_CurveTools.begin(), s_CurveTools.end(), tool);
    if (curve != s_CurveTools.end())
        m_CurveTools->SetDefaultAction(static_cast<std::size_t>(curve - s_CurveTools.begin()));
    std::string help;
    if (tool == SketchTool::Arc)
        help = "Arc: click center, start, end direction. Shift on the final click reverses the sweep.";
    else if (tool == SketchTool::Spline)
        help = "Spline: click start, first handle, second handle, end. Edit each control point in the inspector.";
    else if (tool == SketchTool::Path)
        help = "Path: click vertices; Enter or double-click finishes. Choose Close path before drawing. Esc cancels.";
    else if (tool == SketchTool::Text)
        help = "Text: enter annotation and font size in the inspector, then click to place it in world coordinates.";
    else
        help =
            "Drag to draw / select / move. Shift adds selection or constrains lines. Wheel zooms; Space/Alt+drag pans.";
    help +=
        "\nEsc cancels / Ctrl+Z, Ctrl+Y undo and redo. Geometry commands are undoable; angles use degrees clockwise.";
    m_Help->SetText(help);
    std::ostringstream text;
    text << ToolName(tool) << " / " << m_Sketch->GetScene().Size() << " items";
    if (m_Sketch->GetContentBounds().m_Width > 0)
        text << " / " << m_Sketch->GetVisibleItemCount() << " visible / " << m_Sketch->GetQueryVisitCount()
             << " BVH visits";
    text << " / " << m_Sketch->GetDocument().GetSelection().size() << " selected / " << std::fixed
         << std::setprecision(1) << m_Sketch->GetZoom() * 100 << "%";
    if (m_Sketch->GetPendingPointCount() != 0)
        text << " / " << m_Sketch->GetPendingPointCount() << " points";
    if (m_LoadMilliseconds > 0)
        text << " / build " << std::setprecision(0) << m_LoadMilliseconds << " ms";
    if (!m_FileStatus.empty())
        text << " / " << m_FileStatus;
    m_Status->SetText(text.str());
    m_UpdatingZoom = true;
    m_Zoom->SetValue(m_Sketch->GetZoom());
    m_UpdatingZoom = false;
}

void SketchGallery::Verify(const std::filesystem::path&, const std::filesystem::path& output)
{
    ResetSample();
    m_Window.RenderFrame();
    constexpr std::size_t sampleCount = 12;
    Check(m_Sketch->GetScene().Size() == sampleCount, "sample contains primitive and curve examples");
    Check(m_Sketch->GetVisibleItemCount() == sampleCount, "sample BVH query submits every sample shape");
    m_Window.Graphics().SavePpm(output / "canvas-sketcher.ppm");
    auto& ui = m_Window.Ui();
    auto& document = m_Sketch->GetDocument();
    const auto click = [&](CanvasPoint point, int clicks = 1)
    {
        const auto screen = m_Sketch->ToScreen(point);
        ui.PointerDown(screen, {}, clicks);
        ui.PointerUp(screen);
    };
    const auto clickControl = [&](Widget& control)
    {
        m_Window.RenderFrame();
        const auto bounds = control.GetBounds();
        const Point center{bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2};
        ui.PointerDown(center);
        ui.PointerUp(center);
    };
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
    Check(m_Sketch->GetScene().Size() == sampleCount + 1, "pointer gesture creates an undoable line");
    Check(m_Sketch->Undo() && m_Sketch->GetScene().Size() == sampleCount, "undo removes the line");
    Check(m_Sketch->Redo() && m_Sketch->GetScene().Size() == sampleCount + 1, "redo restores the line");
    m_Sketch->SetTool(SketchTool::Select);
    click({0, -40});
    Check(document.GetSelection().size() == 1, "BVH pointer pick selects the line");
    ui.KeyEvent(Key::Delete, true);
    Check(m_Sketch->GetScene().Size() == sampleCount, "Delete removes the selected line");

    m_Sketch->SetSnapping(false);
    m_Sketch->SetTool(SketchTool::Arc);
    click({-80, -30});
    click({-40, -30});
    ui.PointerMove(m_Sketch->ToScreen({-80, 10}));
    m_Window.RenderFrame();
    click({-80, 10});
    Check(m_Sketch->GetScene().Size() == sampleCount + 1, "three native clicks create an arc");
    auto selection = document.GetSelection();
    const auto arcId = selection.front();
    Check(m_Sketch->GetScene().Get(arcId)->m_Geometry.m_Kind == CanvasPrimitive::Arc,
          "arc tool stores analytic geometry");
    m_Sketch->SetTool(SketchTool::Spline);
    for (const CanvasPoint point : std::array<CanvasPoint, 4>{{{-90, 30}, {-60, -20}, {-20, 80}, {30, 30}}})
        click(point);
    Check(m_Sketch->GetScene().Size() == sampleCount + 2, "four native clicks create a cubic spline");
    const auto splineId = document.GetSelection().front();
    m_Sketch->SetTool(SketchTool::Path);
    click({-70, 65});
    click({-20, 85});
    click({30, 65}, 2);
    Check(m_Sketch->GetScene().Size() == sampleCount + 3, "double-click finishes a path");
    const auto pathId = document.GetSelection().front();
    click({-90, 70});
    click({-50, 95});
    ui.KeyEvent(Key::Escape, true);
    Check(m_Sketch->GetScene().Size() == sampleCount + 3 && m_Sketch->GetPendingPointCount() == 0,
          "Escape discards a pending path");
    m_Sketch->SetTool(SketchTool::Text);
    m_Sketch->SetAnnotationText("Native annotation");
    click({-150, -80});
    Check(m_Sketch->GetScene().Size() == sampleCount + 4, "text tool places a world annotation");
    m_Window.RenderFrame();
    m_Window.Graphics().SavePpm(output / "canvas-sketcher-text.ppm");

    m_Sketch->SetTool(SketchTool::Select);
    document.Select(splineId);
    DocumentChanged();
    m_Fields[PointIndex]->SetValue(1);
    auto& editor = m_Fields[EndY]->GetEditor();
    ui.SetFocus(&editor);
    editor.SelectAll();
    ui.TextInput("-75.125");
    ui.KeyEvent(Key::Enter, true);
    clickControl(*m_Apply);
    Check(std::abs(m_Sketch->GetScene().Get(splineId)->m_Geometry.m_Data->m_Points[1].m_Y + 75.125) < 1e-9,
          "validated numeric editing updates a spline control through an undoable document command");
    Check(document.Undo(), "control-point editing can be undone");
    Check(document.Redo(), "control-point editing can be redone");
    DocumentChanged();
    m_Window.RenderFrame();
    m_Window.Graphics().SavePpm(output / "canvas-sketcher-curves.ppm");

    document.Select(arcId);
    document.Select(splineId, true);
    document.Select(pathId, true);
    const auto groupId = document.GroupSelection();
    Check(static_cast<bool>(groupId) && m_Sketch->GetScene().Size() == sampleCount + 2,
          "group command replaces three selected primitives with one reusable item");
    Check(document.DuplicateSelection({15, 15}), "group duplication is undoable");
    Check(document.Undo(), "undo removes the duplicate group");
    Check(document.UngroupSelection() && m_Sketch->GetScene().Size() == sampleCount + 4,
          "ungroup restores editable primitives");

    document.ClearSelection();
    m_Sketch->SetTool(SketchTool::Circle);
    m_Fields[X]->SetValue(90);
    m_Fields[Y]->SetValue(45);
    m_Fields[Dimension]->SetValue(23.75);
    clickControl(*m_Create);
    Check(m_Sketch->GetScene().Size() == sampleCount + 5,
          "inspector creates exact geometry through native button input");
    Check(std::abs(m_Sketch->GetScene().Get(document.GetSelection().front())->m_Geometry.m_Radius - 23.75) < 1e-9,
          "numeric circle creation preserves the requested precision");
    m_Zoom->SetValue(1.25);
    Check(std::abs(m_Sketch->GetZoom() - 1.25) < 1e-9, "slider drives the camera");
    m_Sketch->SetCamera(m_Sketch->GetCenter(), 0.8);
    Check(std::abs(m_Zoom->GetValue() - 0.8) < 1e-9, "camera changes update the zoom slider without recursion");

    std::stringstream serialized;
    document.Save(serialized);
    const auto* boundScene = &m_Sketch->GetScene();
    document.Load(serialized);
    m_Sketch->SceneChanged();
    m_Window.RenderFrame();
    Check(&m_Sketch->GetScene() == boundScene && m_Sketch->GetScene().Size() == sampleCount + 5,
          "native sketch reload preserves the viewport scene binding and all new primitive types");
    std::ofstream report(output / "canvas-sketcher.txt");
    report << "PASS: native line/arc/spline/path/text creation, double-click finish, Escape cancellation, BVH pick, "
              "undo/redo, deletion, validated control-point edit, exact circle creation, group/ungroup/duplicate, "
              "camera/slider synchronization, versioned sketch reload.\n"
           << "Actual C++ OpenGL framebuffers: canvas-sketcher.ppm, canvas-sketcher-curves.ppm, "
              "canvas-sketcher-text.ppm.\n"
           << "Scale workload available from the 200,000 items toolbar action.\n";
    ResetSample();
    m_Sketch->SetSnapping(true);
    m_Window.RenderFrame();
}
} // namespace EStudioUI
