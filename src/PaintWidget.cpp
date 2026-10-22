#include <EStudioUI/PaintWidget.h>

namespace EStudioUI
{
PaintWidget::PaintWidget(std::string type) : Widget(std::move(type))
{
}

void PaintWidget::PaintContent(Painter&) const
{
}

Rect PaintWidget::GetPaintBounds() const
{
    VerifyAccess();
    const auto content = GetContentBounds();
    return {0, 0, content.m_Width, content.m_Height};
}

void PaintWidget::InvalidatePainting()
{
    VerifyAccess();
    RequestRepaint();
}

void PaintWidget::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto content = GetContentBounds();
    const auto clip = content.Intersect(GetClip());
    if (clip.m_Width <= 0 || clip.m_Height <= 0)
        return;
    Painter painter(renderer, font, clip, opacity, {content.m_X, content.m_Y});
    if (GetStyle().m_ClipContent)
    {
        auto bounds = GetBounds();
        bounds.m_X -= content.m_X;
        bounds.m_Y -= content.m_Y;
        painter.ClipRect(bounds, GetStyle().m_Radius);
    }
    PaintContent(painter);
}
} // namespace EStudioUI
