#include "WidgetPalette.h"
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <cmath>

namespace EStudioUI
{
namespace
{
void Glyph(Renderer& renderer, std::string_view name, Rect bounds, Color color, Rect clip)
{
    const float x = bounds.m_X, y = bounds.m_Y;
    const auto line = [&](float ax, float ay, float bx, float by)
    { renderer.DrawLine({x + ax, y + ay}, {x + bx, y + by}, color, 1.3f, clip); };
    const auto box = [&](float ax, float ay, float w, float h, float radius = 1.5f)
    {
        Style s;
        s.m_Background = {0, 0, 0, 0};
        s.m_BorderColor = color;
        s.m_BorderWidth = 1.2f;
        s.m_Radius = radius;
        renderer.DrawBox({x + ax, y + ay, w, h}, s, clip);
    };
    if (name == "CheckBox" || name == "RadioButton")
    {
        box(1, 1, 14, 14, name == "RadioButton" ? 7.0f : 2.0f);
        line(4, 8, 7, 11);
        line(7, 11, 12, 5);
    }
    else if (name == "Slider" || name == "ScrollBar" || name == "ProgressBar" || name == "BusyIndicator")
    {
        line(0, 8, 16, 8);
        box(5, 4, name == "ProgressBar" ? 10.0f : 5.0f, 8, 2);
    }
    else if (name == "TableWidget" || name == "PropertyGrid")
    {
        box(0, 1, 16, 14);
        line(0, 6, 16, 6);
        line(0, 11, 16, 11);
        line(6, 1, 6, 15);
    }
    else if (name == "TreeWidget")
    {
        line(2, 1, 2, 13);
        line(2, 5, 7, 5);
        line(2, 12, 7, 12);
        box(8, 2, 7, 5);
        box(8, 10, 7, 5);
    }
    else if (name == "ListWidget" || name == "MenuBar" || name == "ToolBar")
    {
        for (int i = 0; i < 3; ++i)
        {
            const float row = 3 + static_cast<float>(i) * 5;
            box(0, row, 2, 2, 0);
            line(5, row + 1, 16, row + 1);
        }
    }
    else if (name == "TextBox" || name == "NumericInput" || name == "Label" || name == "UnitEditor" ||
             name == "VectorEditor")
    {
        box(0, 2, 16, 12);
        line(4, 5, 4, 11);
        line(3, 5, 6, 5);
        line(3, 11, 6, 11);
        line(9, 8, 13, 8);
    }
    else if (name == "ColorEditor" || name == "ColorPicker")
    {
        box(0, 1, 16, 14);
        line(5, 1, 5, 15);
        line(11, 1, 11, 15);
        line(0, 8, 16, 8);
    }
    else if (name == "ImageView" || name == "CanvasWidget" || name == "PaintWidget" || name == "SketchWidget")
    {
        box(0, 1, 16, 14);
        line(2, 12, 6, 6);
        line(6, 6, 10, 11);
        line(10, 11, 13, 8);
        box(11, 3, 2, 2);
    }
    else if (name == "OpenGLWidget")
    {
        line(8, 0, 15, 4);
        line(15, 4, 15, 12);
        line(15, 12, 8, 16);
        line(8, 16, 1, 12);
        line(1, 12, 1, 4);
        line(1, 4, 8, 0);
        line(1, 4, 8, 8);
        line(15, 4, 8, 8);
        line(8, 8, 8, 16);
    }
    else if (name.find("Button") != std::string_view::npos || name == "ComboBox")
    {
        box(0, 3, 16, 10, 3);
        line(4, 8, 10, 8);
        if (name == "ComboBox" || name == "SplitToolButton")
        {
            line(12, 7, 14, 9);
            line(14, 9, 16, 7);
        }
    }
    else
    {
        box(0, 1, 16, 14);
        line(0, 5, 16, 5);
        box(3, 8, 4, 4, 0);
        box(10, 8, 3, 4, 0);
    }
}
} // namespace

WidgetPalette::WidgetPalette()
{
    AddClass("creator-widget-palette");
}

void WidgetPalette::SetEntries(std::vector<std::string> entries)
{
    VerifyStructuralMutation();
    m_Entries = entries;
    SetItems(std::move(entries));
}

Signal<const std::string&, Point, bool>& WidgetPalette::Dragged()
{
    VerifyAccess();
    return m_Dragged;
}

void WidgetPalette::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    ListWidget::Paint(renderer, font, time, opacity);
    const auto view = GetViewport();
    const auto offset = GetScrollOffset();
    const float height = GetStyle().m_RowHeight;
    if (height <= 0)
    {
        return;
    }
    const int first = std::max(0, static_cast<int>(std::floor(offset.m_Y / height)));
    const int end = std::min(static_cast<int>(m_Entries.size()), first + static_cast<int>(view.m_Height / height) + 2);
    auto color = GetStyle().m_Color;
    color.m_A *= opacity;
    for (int i = first; i < end; ++i)
    {
        Glyph(renderer, m_Entries[static_cast<std::size_t>(i)],
              {view.m_X + 9, view.m_Y + static_cast<float>(i) * height - offset.m_Y + (height - 16) / 2, 16, 16}, color,
              view.Intersect(GetClip()));
    }
}

void WidgetPalette::OnPointerDown(const PointerEvent& event)
{
    const WidgetRef self(this);
    const bool row = GetViewport().Contains(event.m_Position);
    ListWidget::OnPointerDown(event);
    if (!self.Get())
    {
        return;
    }
    m_DragType.clear();
    m_Dragging = false;
    const int index = GetCurrentIndex();
    if (row && IsPressed() && index >= 0 && static_cast<std::size_t>(index) < m_Entries.size())
    {
        m_Start = GetContext()->ToWindowPoint(*this, event.m_Position);
        m_DragType = m_Entries[static_cast<std::size_t>(index)];
    }
}

void WidgetPalette::OnPointerMove(const PointerEvent& event)
{
    ListWidget::OnPointerMove(event);
    if (m_DragType.empty())
    {
        return;
    }
    const auto point = GetContext()->ToWindowPoint(*this, event.m_Position);
    if (std::hypot(point.m_X - m_Start.m_X, point.m_Y - m_Start.m_Y) >= 5)
    {
        m_Dragging = true;
    }
    if (m_Dragging)
    {
        const auto type = m_DragType;
        m_Dragged.Emit(type, point, false);
    }
}

void WidgetPalette::OnPointerUp(const PointerEvent& event)
{
    const auto type = m_DragType;
    const bool dragged = m_Dragging;
    const auto point = GetContext()->ToWindowPoint(*this, event.m_Position);
    m_DragType.clear();
    m_Dragging = false;
    if (dragged)
    {
        ListWidget::OnCaptureLost();
        m_Dragged.Emit(type, point, true);
    }
    else
    {
        ListWidget::OnPointerUp(event);
    }
}

void WidgetPalette::OnCaptureLost()
{
    const bool dragged = m_Dragging;
    m_DragType.clear();
    m_Dragging = false;
    ListWidget::OnCaptureLost();
    if (dragged)
    {
        const std::string empty;
        m_Dragged.Emit(empty, {}, true);
    }
}

void WidgetPalette::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (key == Key::Escape && down && !m_DragType.empty())
    {
        OnCaptureLost();
        return;
    }
    ListWidget::OnKey(key, down, repeat, modifiers);
}
} // namespace EStudioUI
