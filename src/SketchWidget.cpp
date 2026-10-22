#include <EStudioUI/SketchWidget.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <cmath>
#include <numbers>
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
    VerifyAccess();
    return *m_Document;
}

const SketchDocument& SketchWidget::GetDocument() const
{
    VerifyAccess();
    return *m_Document;
}

void SketchWidget::SetTool(SketchTool tool)
{
    VerifyStructuralMutation();
    CancelGesture();
    m_Tool = tool;
    SetPanMode(tool == SketchTool::Pan);
    NotifyChanged();
}

SketchTool SketchWidget::GetTool() const
{
    VerifyAccess();
    return m_Tool;
}

void SketchWidget::SetSnapping(bool enabled)
{
    VerifyAccess();
    if (m_Snapping == enabled)
        return;
    m_Snapping = enabled;
    NotifyChanged();
}

bool SketchWidget::IsSnapping() const
{
    VerifyAccess();
    return m_Snapping;
}

void SketchWidget::SetAnnotationText(std::string text)
{
    VerifyAccess();
    MakeCanvasText({0, 0, 100, 100}, text, m_AnnotationFontSize);
    m_AnnotationText = std::move(text);
}

const std::string& SketchWidget::GetAnnotationText() const
{
    VerifyAccess();
    return m_AnnotationText;
}

void SketchWidget::SetAnnotationFontSize(double size)
{
    VerifyAccess();
    if (!std::isfinite(size) || size <= 0 || size > 1000000)
        throw std::invalid_argument("Annotation font size must be finite and between zero and one million.");
    m_AnnotationFontSize = size;
}

double SketchWidget::GetAnnotationFontSize() const
{
    VerifyAccess();
    return m_AnnotationFontSize;
}

void SketchWidget::SetPathClosed(bool closed)
{
    VerifyAccess();
    m_PathClosed = closed;
}

bool SketchWidget::IsPathClosed() const
{
    VerifyAccess();
    return m_PathClosed;
}

std::size_t SketchWidget::GetPendingPointCount() const
{
    VerifyAccess();
    return m_ToolPoints.size();
}

bool SketchWidget::FinishTool()
{
    VerifyStructuralMutation();
    if (m_Tool != SketchTool::Path || m_ToolPoints.size() < (m_PathClosed ? 3U : 2U))
        return false;
    const auto origin = m_ToolPoints.front();
    auto points = m_ToolPoints;
    for (auto& point : points)
    {
        point.m_X -= origin.m_X;
        point.m_Y -= origin.m_Y;
    }
    const bool changed = CommitShape(MakeCanvasPath(origin, std::move(points), m_PathClosed));
    CancelGesture();
    NotifyChanged();
    return changed;
}

bool SketchWidget::Undo()
{
    VerifyStructuralMutation();
    CancelGesture();
    const bool changed = m_Document->Undo();
    NotifyChanged();
    return changed;
}

bool SketchWidget::Redo()
{
    VerifyStructuralMutation();
    CancelGesture();
    const bool changed = m_Document->Redo();
    NotifyChanged();
    return changed;
}

bool SketchWidget::DeleteSelection()
{
    VerifyStructuralMutation();
    CancelGesture();
    const bool changed = m_Document->DeleteSelection();
    NotifyChanged();
    return changed;
}

std::uint32_t SketchWidget::GetShapeStyle() const
{
    VerifyAccess();
    return m_ShapeStyle;
}

Signal<>& SketchWidget::Changed()
{
    VerifyAccess();
    return m_Changed;
}

void SketchWidget::CancelGesture()
{
    m_Gesture = Gesture::None;
    m_Moved = false;
    m_ToolPoints.clear();
    SetPreview({});
    SetSelectionRectangle(std::nullopt);
}

void SketchWidget::NotifyChanged()
{
    const auto ownerLease = RetainOwner();
    SceneChanged();
    m_Changed.Emit();
}

CanvasPoint SketchWidget::GesturePoint(const PointerEvent& event) const
{
    auto point = ToWorld(event.m_Position);
    if ((m_Gesture == Gesture::Create || m_Gesture == Gesture::Points) && m_Snapping)
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
    if (m_Gesture == Gesture::Points)
        UpdatePointPreview(m_Current, event.m_Modifiers.m_Shift);
    else if (m_Gesture == Gesture::Create)
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

bool SketchWidget::IsPointTool() const
{
    return m_Tool == SketchTool::Arc || m_Tool == SketchTool::Spline || m_Tool == SketchTool::Path ||
           m_Tool == SketchTool::Text;
}

CanvasGeometry SketchWidget::ArcFromPoints(CanvasPoint center, CanvasPoint start, CanvasPoint end, bool reverse) const
{
    const double startAngle = std::atan2(start.m_Y - center.m_Y, start.m_X - center.m_X);
    const double endAngle = std::atan2(end.m_Y - center.m_Y, end.m_X - center.m_X);
    constexpr double turn = 2 * std::numbers::pi;
    double sweep = std::fmod(endAngle - startAngle + turn, turn);
    if (reverse)
        sweep -= turn;
    if (std::abs(sweep) < 1e-8)
        sweep = reverse ? -turn : turn;
    return MakeCanvasArc(center, std::hypot(start.m_X - center.m_X, start.m_Y - center.m_Y), startAngle, sweep);
}

bool SketchWidget::CommitShape(const CanvasGeometry& geometry)
{
    return static_cast<bool>(m_Document->AddShape(geometry, m_ShapeStyle));
}

void SketchWidget::UpdatePointPreview(CanvasPoint point, bool reverseArc)
{
    std::vector<CanvasGeometry> preview;
    if (m_Tool == SketchTool::Text)
        return;
    if (m_Tool == SketchTool::Arc)
    {
        if (m_ToolPoints.size() == 1)
        {
            const auto center = m_ToolPoints.front();
            const double radius = std::hypot(point.m_X - center.m_X, point.m_Y - center.m_Y);
            preview.push_back({CanvasPrimitive::Circle, center, {}, radius});
            preview.push_back({CanvasPrimitive::Line, center, point});
        }
        else if (m_ToolPoints.size() == 2)
        {
            preview.push_back(ArcFromPoints(m_ToolPoints[0], m_ToolPoints[1], point, reverseArc));
            preview.push_back({CanvasPrimitive::Line, m_ToolPoints[0], m_ToolPoints[1]});
        }
    }
    else if (!m_ToolPoints.empty())
    {
        auto points = m_ToolPoints;
        points.push_back(point);
        preview.push_back(MakeCanvasPath({}, points, false));
        if (m_Tool == SketchTool::Spline && points.size() == 4)
            preview.push_back(MakeCanvasSpline({}, std::move(points)));
        else if (m_Tool == SketchTool::Path && m_PathClosed && points.size() >= 3)
            preview.push_back({CanvasPrimitive::Line, point, points.front()});
    }
    SetPreview(std::move(preview), m_PreviewStyle);
}

void SketchWidget::AcceptToolPoint(const PointerEvent& event)
{
    const auto point = GesturePoint(event);
    if (m_Tool == SketchTool::Text)
    {
        if (!m_AnnotationText.empty())
        {
            double width =
                std::max(m_AnnotationFontSize, static_cast<double>(m_AnnotationText.size()) * m_AnnotationFontSize);
            double height = m_AnnotationFontSize * 1.5;
            if (const auto* context = GetContext())
            {
                const auto layout =
                    context->GetFont().Shape(m_AnnotationText, static_cast<float>(m_AnnotationFontSize));
                width = std::max(m_AnnotationFontSize, static_cast<double>(layout.m_Width) + 4);
                height = std::max(height, static_cast<double>(layout.m_Ascender - layout.m_Descender) + 4);
            }
            CommitShape(MakeCanvasText({point.m_X, point.m_Y, width, height}, m_AnnotationText, m_AnnotationFontSize));
        }
        CancelGesture();
        NotifyChanged();
        return;
    }
    if (m_Tool == SketchTool::Arc && m_ToolPoints.size() == 1 &&
        std::hypot(point.m_X - m_ToolPoints[0].m_X, point.m_Y - m_ToolPoints[0].m_Y) * GetZoom() < 1)
        return;
    if (m_Tool == SketchTool::Path && m_ToolPoints.size() >= 4096)
        return;
    if (m_ToolPoints.empty() || m_ToolPoints.back() != point || m_Tool == SketchTool::Spline)
        m_ToolPoints.push_back(point);
    if (m_Tool == SketchTool::Arc && m_ToolPoints.size() == 3)
    {
        CommitShape(ArcFromPoints(m_ToolPoints[0], m_ToolPoints[1], m_ToolPoints[2], event.m_Modifiers.m_Shift));
        CancelGesture();
    }
    else if (m_Tool == SketchTool::Spline && m_ToolPoints.size() == 4)
    {
        const auto origin = m_ToolPoints.front();
        auto points = m_ToolPoints;
        for (auto& controlPoint : points)
        {
            controlPoint.m_X -= origin.m_X;
            controlPoint.m_Y -= origin.m_Y;
        }
        CommitShape(MakeCanvasSpline(origin, std::move(points)));
        CancelGesture();
    }
    else if (m_Tool == SketchTool::Path && m_PressClicks >= 2 && m_ToolPoints.size() >= (m_PathClosed ? 3U : 2U))
    {
        FinishTool();
        return;
    }
    else
        UpdatePointPreview(point, event.m_Modifiers.m_Shift);
    NotifyChanged();
}

void SketchWidget::OnPointerDown(const PointerEvent& event)
{
    const WidgetRef lifetime(this);
    CanvasWidget::OnPointerDown(event);
    if (!lifetime.Get() || IsPanning())
        return;
    if (IsPointTool())
    {
        m_Gesture = Gesture::Points;
        m_PressClicks = event.m_Clicks;
        m_StartScreen = event.m_Position;
        return;
    }
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
    if (m_Gesture == Gesture::Points)
    {
        AcceptToolPoint(event);
        return;
    }
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
                CommitShape(geometry);
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
    else if (key == Key::Enter && m_Tool == SketchTool::Path)
        FinishTool();
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
        else if (key == Key::A)
            SetTool(SketchTool::Arc);
        else if (key == Key::B)
            SetTool(SketchTool::Spline);
        else if (key == Key::W)
            SetTool(SketchTool::Path);
        else if (key == Key::T)
            SetTool(SketchTool::Text);
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
