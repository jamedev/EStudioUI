#include <EStudioUI/SketchWidget.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
CanvasBounds Between(CanvasPoint a, CanvasPoint b)
{
    return {std::min(a.m_X, b.m_X), std::min(a.m_Y, b.m_Y), std::abs(a.m_X - b.m_X), std::abs(a.m_Y - b.m_Y)};
}
} // namespace

SketchWidget::SketchWidget() : m_Document(std::make_shared<SketchDocument>())
{
    SetScene(std::shared_ptr<CanvasScene>(m_Document, &m_Document->GetScene()));
    m_ShapeStyle = RegisterItemStyle("sketch-item");
    m_PreviewStyle = RegisterItemStyle("sketch-item", {"preview"});
    std::string error;
    if (!SetItemStyleSheet(R"css(
        sketch-item { color: #6fe1dc; background: rgba(35, 112, 126, 0.14);
                      border: 2px solid #6fe1dc; border-radius: 0; }
        sketch-item:selected { color: #ffcf70; border-color: #ffcf70;
                               background: rgba(255, 207, 112, 0.1); }
        sketch-item.preview { color: #edf8ff; border-color: #edf8ff;
                              background: rgba(237, 248, 255, 0.08); opacity: 0.75; }
    )css",
                           error))
        throw std::runtime_error("Invalid built-in sketch styles: " + error);
}

SketchDocument& SketchWidget::GetDocument()
{
    return *m_Document;
}

const SketchDocument& SketchWidget::GetDocument() const
{
    return *m_Document;
}

void SketchWidget::SetTool(SketchTool tool)
{
    CancelGesture();
    m_Tool = tool;
    SetPanMode(tool == SketchTool::Pan);
    NotifyChanged();
}

SketchTool SketchWidget::GetTool() const
{
    return m_Tool;
}

void SketchWidget::SetSnapping(bool enabled)
{
    m_Snapping = enabled;
    NotifyChanged();
}

bool SketchWidget::IsSnapping() const
{
    return m_Snapping;
}

bool SketchWidget::Undo()
{
    CancelGesture();
    const bool changed = m_Document->Undo();
    NotifyChanged();
    return changed;
}

bool SketchWidget::Redo()
{
    CancelGesture();
    const bool changed = m_Document->Redo();
    NotifyChanged();
    return changed;
}

bool SketchWidget::DeleteSelection()
{
    CancelGesture();
    const bool changed = m_Document->DeleteSelection();
    NotifyChanged();
    return changed;
}

std::uint32_t SketchWidget::GetShapeStyle() const
{
    return m_ShapeStyle;
}

Signal<>& SketchWidget::Changed()
{
    return m_Changed;
}

void SketchWidget::CancelGesture()
{
    m_Gesture = Gesture::None;
    m_Moved = false;
    SetPreview({});
    SetSelectionRectangle(std::nullopt);
}

void SketchWidget::NotifyChanged()
{
    SceneChanged();
    m_Changed.Emit();
}

CanvasPoint SketchWidget::GesturePoint(const PointerEvent& event) const
{
    auto point = ToWorld(event.m_Position);
    if (m_Gesture == Gesture::Create && m_Snapping)
        point = m_Document->Snap(point, 8 / GetZoom(), 10, true);
    if (m_Gesture == Gesture::Create && m_Tool == SketchTool::Line && event.m_Modifiers.m_Shift)
    {
        if (std::abs(point.m_X - m_Start.m_X) >= std::abs(point.m_Y - m_Start.m_Y))
            point.m_Y = m_Start.m_Y;
        else
            point.m_X = m_Start.m_X;
    }
    if (m_Gesture == Gesture::Move && m_Snapping)
    {
        point.m_X = m_Start.m_X + std::round((point.m_X - m_Start.m_X) / 10) * 10;
        point.m_Y = m_Start.m_Y + std::round((point.m_Y - m_Start.m_Y) / 10) * 10;
    }
    return point;
}

CanvasGeometry SketchWidget::PreviewShape() const
{
    CanvasGeometry geometry;
    geometry.m_A = m_Start;
    geometry.m_B = m_Current;
    geometry.m_Kind = m_Tool == SketchTool::Rectangle ? CanvasPrimitive::Rectangle
                      : m_Tool == SketchTool::Circle  ? CanvasPrimitive::Circle
                                                      : CanvasPrimitive::Line;
    if (geometry.m_Kind == CanvasPrimitive::Circle)
        geometry.m_Radius = std::hypot(m_Current.m_X - m_Start.m_X, m_Current.m_Y - m_Start.m_Y);
    return geometry;
}

void SketchWidget::UpdatePreview(const PointerEvent& event)
{
    const auto dx = event.m_Position.m_X - m_StartScreen.m_X;
    const auto dy = event.m_Position.m_Y - m_StartScreen.m_Y;
    m_Moved = m_Moved || dx * dx + dy * dy >= 9;
    m_Current = GesturePoint(event);
    if (m_Gesture == Gesture::Create)
        SetPreview({PreviewShape()}, m_PreviewStyle);
    else if (m_Gesture == Gesture::Marquee)
        SetSelectionRectangle(Between(m_Start, m_Current));
    else if (m_Gesture == Gesture::Move && m_Moved)
    {
        std::vector<CanvasGeometry> preview;
        const auto selection = m_Document->GetSelection();
        preview.reserve(selection.size());
        const CanvasPoint delta{m_Current.m_X - m_Start.m_X, m_Current.m_Y - m_Start.m_Y};
        for (const auto id : selection)
            if (const auto* item = GetScene().Get(id))
                preview.push_back(TranslateGeometry(item->m_Geometry, delta));
        SetPreview(std::move(preview), m_PreviewStyle);
    }
}

void SketchWidget::OnPointerDown(const PointerEvent& event)
{
    const WidgetRef lifetime(this);
    CanvasWidget::OnPointerDown(event);
    if (!lifetime.Get() || IsPanning())
        return;
    CancelGesture();
    m_StartScreen = event.m_Position;
    m_Start = ToWorld(event.m_Position);
    m_Current = m_Start;
    m_Additive = event.m_Modifiers.m_Shift;
    if (m_Tool == SketchTool::Select)
    {
        const auto hit = m_Document->Pick(m_Start, 8 / GetZoom());
        if (hit)
        {
            const auto* item = GetScene().Get(hit);
            if (m_Additive || !item->m_Selected)
                m_Document->Select(hit, m_Additive);
            item = GetScene().Get(hit);
            m_Gesture = item && item->m_Selected ? Gesture::Move : Gesture::None;
        }
        else
        {
            if (!m_Additive)
                m_Document->ClearSelection();
            m_Gesture = Gesture::Marquee;
        }
    }
    else if (m_Tool != SketchTool::Pan)
    {
        m_Gesture = Gesture::Create;
        m_Start = GesturePoint(event);
        m_Current = m_Start;
    }
    NotifyChanged();
}

void SketchWidget::OnPointerMove(const PointerEvent& event)
{
    const WidgetRef lifetime(this);
    CanvasWidget::OnPointerMove(event);
    if (!lifetime.Get() || IsPanning() || m_Gesture == Gesture::None)
        return;
    UpdatePreview(event);
}

void SketchWidget::OnPointerUp(const PointerEvent& event)
{
    const bool wasPanning = IsPanning();
    const WidgetRef lifetime(this);
    CanvasWidget::OnPointerUp(event);
    if (!lifetime.Get() || wasPanning || m_Gesture == Gesture::None)
        return;
    UpdatePreview(event);
    if (m_Moved)
    {
        if (m_Gesture == Gesture::Create)
        {
            const auto geometry = PreviewShape();
            const auto bounds = geometry.Bounds();
            const bool hasArea = geometry.m_Kind != CanvasPrimitive::Rectangle ||
                                 (bounds.m_Width * GetZoom() >= 1 && bounds.m_Height * GetZoom() >= 1);
            if (hasArea && (bounds.m_Width + bounds.m_Height) * GetZoom() >= 1)
                m_Document->Select(m_Document->AddShape(geometry, m_ShapeStyle));
        }
        else if (m_Gesture == Gesture::Move)
            m_Document->MoveSelection({m_Current.m_X - m_Start.m_X, m_Current.m_Y - m_Start.m_Y});
        else if (m_Gesture == Gesture::Marquee)
            m_Document->SelectBox(Between(m_Start, m_Current), m_Additive);
    }
    CancelGesture();
    NotifyChanged();
}

void SketchWidget::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    CanvasWidget::OnKey(key, down, repeat, modifiers);
    if (!down || repeat)
        return;
    if (key == Key::Escape)
    {
        if (m_Gesture == Gesture::None)
            m_Document->ClearSelection();
        CancelGesture();
        NotifyChanged();
    }
    else if (key == Key::Delete || key == Key::Backspace)
        DeleteSelection();
    else if (modifiers.m_Control && key == Key::Z)
    {
        if (modifiers.m_Shift)
            Redo();
        else
            Undo();
    }
    else if (modifiers.m_Control && key == Key::Y)
        Redo();
    else if (!modifiers.m_Control && !modifiers.m_Alt)
    {
        if (key == Key::L)
            SetTool(SketchTool::Line);
        else if (key == Key::R)
            SetTool(SketchTool::Rectangle);
        else if (key == Key::C)
            SetTool(SketchTool::Circle);
        else if (key == Key::V)
            SetTool(SketchTool::Select);
        else if (key == Key::P)
            SetTool(SketchTool::Pan);
    }
}

void SketchWidget::OnFocus(bool focused)
{
    CanvasWidget::OnFocus(focused);
    if (!focused)
        CancelGesture();
}

void SketchWidget::OnCaptureLost()
{
    CanvasWidget::OnCaptureLost();
    CancelGesture();
}
} // namespace EStudioUI
