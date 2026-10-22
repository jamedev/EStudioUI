#include <EStudioUI/Painter.h>

#include <exception>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Validate(Point point)
{
    if (!std::isfinite(point.m_X) || !std::isfinite(point.m_Y))
        throw std::invalid_argument("Painter coordinates must be finite.");
}

void Validate(Rect bounds)
{
    Validate(Point{bounds.m_X, bounds.m_Y});
    if (!std::isfinite(bounds.m_Width) || !std::isfinite(bounds.m_Height) || bounds.m_Width < 0 ||
        bounds.m_Height < 0 || !std::isfinite(bounds.m_X + bounds.m_Width) ||
        !std::isfinite(bounds.m_Y + bounds.m_Height))
        throw std::invalid_argument("Painter rectangles must have finite, nonnegative extents.");
}

void Validate(Color color)
{
    if (!std::isfinite(color.m_R) || !std::isfinite(color.m_G) || !std::isfinite(color.m_B) ||
        !std::isfinite(color.m_A))
        throw std::invalid_argument("Painter colors must be finite.");
}

float Opacity(float value)
{
    if (!std::isfinite(value) || value < 0 || value > 1)
        throw std::invalid_argument("Painter opacity must be within [0,1].");
    return value;
}
} // namespace

Painter::Painter(Renderer& renderer, FontFace& font, Rect clip, float opacity, Point origin, const Style* textStyle)
    : m_Renderer(renderer), m_Font(font), m_TextStyle(textStyle), m_Clip(clip), m_InheritedOpacity(Opacity(opacity))
{
    Validate(clip);
    Validate(origin);
    m_State.m_Transform.m_X = origin.m_X;
    m_State.m_Transform.m_Y = origin.m_Y;
    m_Renderer.PushState();
    ++m_Depth;
    try
    {
        m_Renderer.PushClip(clip);
        m_Renderer.PushState(m_State.m_Transform);
        ++m_Depth;
    }
    catch (...)
    {
        while (m_Depth)
        {
            m_Renderer.PopState();
            --m_Depth;
        }
        throw;
    }
}

Painter::~Painter()
{
    if (!m_Affinity.IsOwnerThread())
        std::terminate();
    while (m_Depth)
    {
        m_Renderer.PopState();
        --m_Depth;
    }
}

void Painter::Save()
{
    m_Affinity.VerifyAccess("Painter::Save");
    if (m_Saved.size() == 128)
        throw std::length_error("Painter supports at most 128 nested Save calls.");
    auto saved = m_State;
    saved.m_Depth = m_Depth;
    m_Saved.push_back(saved);
    try
    {
        m_Renderer.PushState();
        ++m_Depth;
    }
    catch (...)
    {
        m_Saved.pop_back();
        throw;
    }
}

void Painter::Restore()
{
    m_Affinity.VerifyAccess("Painter::Restore");
    if (m_Saved.empty())
        throw std::logic_error("Painter Restore requires a matching Save.");
    const auto saved = m_Saved.back();
    while (m_Depth > saved.m_Depth)
    {
        m_Renderer.PopState();
        --m_Depth;
    }
    m_State = saved;
    m_Saved.pop_back();
}

void Painter::Transform(const Affine& transform)
{
    m_Affinity.VerifyAccess("Painter::Transform");
    const auto combined = m_State.m_Transform * transform;
    Affine inverse;
    if (!std::isfinite(combined.m_A) || !std::isfinite(combined.m_B) || !std::isfinite(combined.m_C) ||
        !std::isfinite(combined.m_D) || !std::isfinite(combined.m_X) || !std::isfinite(combined.m_Y) ||
        !combined.Inverse(inverse))
        throw std::invalid_argument("Painter transforms must remain finite and invertible.");
    m_Renderer.PushState(transform);
    ++m_Depth;
    m_State.m_Transform = combined;
}

void Painter::Translate(float x, float y)
{
    m_Affinity.VerifyAccess("Painter::Translate");
    Transform({1, 0, 0, 1, x, y});
}

void Painter::Scale(float x, float y)
{
    m_Affinity.VerifyAccess("Painter::Scale");
    Transform({x, 0, 0, y, 0, 0});
}

void Painter::Rotate(float radians)
{
    m_Affinity.VerifyAccess("Painter::Rotate");
    if (!std::isfinite(radians))
        throw std::invalid_argument("Painter rotation must be finite.");
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Transform({cosine, sine, -sine, cosine, 0, 0});
}

void Painter::ClipRect(Rect bounds, float radius)
{
    m_Affinity.VerifyAccess("Painter::ClipRect");
    Validate(bounds);
    if (!std::isfinite(radius) || radius < 0)
        throw std::invalid_argument("Painter clipping radius must be finite and nonnegative.");
    m_Renderer.PushClip(bounds, radius);
}

void Painter::SetPen(Color color, float width)
{
    m_Affinity.VerifyAccess("Painter::SetPen");
    Validate(color);
    if (!std::isfinite(width) || width < 0)
        throw std::invalid_argument("Painter pen width must be finite and nonnegative.");
    m_State.m_Pen = color;
    m_State.m_Width = width;
}

void Painter::SetBrush(Color color)
{
    m_Affinity.VerifyAccess("Painter::SetBrush");
    Validate(color);
    m_State.m_Brush = {};
    m_State.m_Brush.m_Background = color;
}

void Painter::SetBrush(const Style& style)
{
    m_Affinity.VerifyAccess("Painter::SetBrush");
    Validate(style.m_Background);
    Opacity(style.m_Opacity);
    m_State.m_Brush = style;
}

void Painter::SetOpacity(float opacity)
{
    m_Affinity.VerifyAccess("Painter::SetOpacity");
    m_State.m_Opacity = Opacity(opacity);
}

Rect Painter::DrawingClip() const
{
    Affine inverse;
    m_State.m_Transform.Inverse(inverse);
    return inverse.MapBounds(m_Clip);
}

Style Painter::ShapeStyle() const
{
    auto style = m_State.m_Brush;
    style.m_BorderColor = m_State.m_Pen;
    style.m_BorderWidth = m_State.m_Width;
    return style;
}

void Painter::DrawLine(Point from, Point to)
{
    m_Affinity.VerifyAccess("Painter::DrawLine");
    Validate(from);
    Validate(to);
    auto color = m_State.m_Pen;
    color.m_A *= m_State.m_Opacity * m_InheritedOpacity;
    m_Renderer.DrawLine(from, to, color, m_State.m_Width, DrawingClip());
}

void Painter::DrawRect(Rect bounds)
{
    m_Affinity.VerifyAccess("Painter::DrawRect");
    DrawRoundedRect(bounds, 0);
}

void Painter::DrawRoundedRect(Rect bounds, float radius)
{
    m_Affinity.VerifyAccess("Painter::DrawRoundedRect");
    Validate(bounds);
    if (!std::isfinite(radius) || radius < 0)
        throw std::invalid_argument("Painter shape radius must be finite and nonnegative.");
    auto style = ShapeStyle();
    style.m_Radius = radius;
    m_Renderer.DrawBox(bounds, style, DrawingClip(), m_InheritedOpacity * m_State.m_Opacity * style.m_Opacity);
}

void Painter::DrawCircle(Point center, float radius)
{
    m_Affinity.VerifyAccess("Painter::DrawCircle");
    Validate(center);
    if (!std::isfinite(radius) || radius < 0 || !std::isfinite(radius * 2) || !std::isfinite(center.m_X - radius) ||
        !std::isfinite(center.m_X + radius) || !std::isfinite(center.m_Y - radius) ||
        !std::isfinite(center.m_Y + radius))
        throw std::invalid_argument("Painter circle radius must be finite and nonnegative.");
    const auto style = ShapeStyle();
    m_Renderer.DrawCircle(center, radius, style, DrawingClip(),
                          m_InheritedOpacity * m_State.m_Opacity * style.m_Opacity);
}

void Painter::DrawPolyline(std::span<const Point> points, bool closed)
{
    m_Affinity.VerifyAccess("Painter::DrawPolyline");
    for (const auto point : points)
        Validate(point);
    for (std::size_t i = 1; i < points.size(); ++i)
        DrawLine(points[i - 1], points[i]);
    if (closed && points.size() > 1 && points.back() != points.front())
        DrawLine(points.back(), points.front());
}

void Painter::DrawPath(const PainterPath& path)
{
    m_Affinity.VerifyAccess("Painter::DrawPath");
    const auto& transform = m_State.m_Transform;
    const double scale = std::sqrt(
        static_cast<double>(transform.m_A) * transform.m_A + static_cast<double>(transform.m_B) * transform.m_B +
        static_cast<double>(transform.m_C) * transform.m_C + static_cast<double>(transform.m_D) * transform.m_D);
    const float tolerance = static_cast<float>(std::max(1e-6, 0.35 / std::max(1.0, scale)));
    for (const auto& contour : path.Flatten(tolerance))
        DrawPolyline(contour.m_Points, contour.m_Closed);
}

void Painter::DrawText(const TextLayout& layout, Point baseline)
{
    m_Affinity.VerifyAccess("Painter::DrawText");
    Validate(baseline);
    auto color = m_State.m_Pen;
    color.m_A *= m_State.m_Opacity * m_InheritedOpacity;
    m_Renderer.DrawText(layout, baseline, color, DrawingClip());
}

void Painter::DrawText(std::string_view text, Point baseline, float fontSize)
{
    m_Affinity.VerifyAccess("Painter::DrawText");
    if (!std::isfinite(fontSize) || fontSize <= 0)
        throw std::invalid_argument("Painter font size must be finite and positive.");
    DrawText(m_TextStyle ? m_Font.Shape(text, fontSize, *m_TextStyle) : m_Font.Shape(text, fontSize), baseline);
}

FontFace& Painter::GetFont() const
{
    m_Affinity.VerifyAccess("Painter::GetFont");
    return m_Font;
}
} // namespace EStudioUI
