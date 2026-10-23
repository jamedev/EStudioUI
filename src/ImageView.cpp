#include <EStudioUI/ImageView.h>
#include <EStudioUI/Renderer.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
ImageView::ImageView(Image image) : Widget("imageview"), m_Image(std::move(image))
{
    SetAccessibleRole(SemanticRole::Image);
}

void ImageView::SetImage(Image image)
{
    VerifyStructuralMutation();
    if (m_Image == image)
        return;
    const auto previous = m_Image.GetSize();
    m_Image = std::move(image);
    Invalidate(previous == m_Image.GetSize() ? Invalidation::Paint : Invalidation::Content);
}

const Image& ImageView::GetImage() const
{
    VerifyAccess();
    return m_Image;
}

void ImageView::SetFit(std::string fit)
{
    VerifyStructuralMutation();
    SetStyleProperty(StyleProperty::ObjectFit, std::move(fit));
}

void ImageView::SetAlignment(float horizontal, float vertical)
{
    VerifyStructuralMutation();
    if (!std::isfinite(horizontal) || !std::isfinite(vertical) || horizontal < 0 || horizontal > 1 || vertical < 0 ||
        vertical > 1)
        throw std::invalid_argument("Image alignment requires finite fractions in [0,1].");
    SetStyleProperty(StyleProperty::ObjectPositionX, horizontal);
    SetStyleProperty(StyleProperty::ObjectPositionY, vertical);
}

Point ImageView::Measure(FontFace&) const
{
    return m_Image.GetSize();
}

MeasureResult ImageView::MeasureConstrained(FontFace&, const MeasureConstraints& constraints) const
{
    auto size = m_Image.GetSize();
    if (constraints.m_Width && size.m_X > 0)
        size.m_Y *= *constraints.m_Width / size.m_X;
    return {{}, size, {}, !m_Image.IsEmpty()};
}

void ImageView::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto content = GetContentBounds();
    if (m_Image.IsEmpty() || content.m_Width <= 0 || content.m_Height <= 0)
        return;
    const auto size = m_Image.GetSize();
    const auto& style = GetStyle();
    Rect target = content, source{0, 0, 1, 1};
    if (style.m_ObjectFit != "fill")
    {
        const float fit = std::min(content.m_Width / size.m_X, content.m_Height / size.m_Y);
        const float scale = style.m_ObjectFit == "none"         ? 1
                            : style.m_ObjectFit == "scale-down" ? std::min(1.0f, fit)
                            : style.m_ObjectFit == "cover"
                                ? std::max(content.m_Width / size.m_X, content.m_Height / size.m_Y)
                                : fit;
        const float width = size.m_X * scale, height = size.m_Y * scale;
        target = {content.m_X + (content.m_Width - width) * style.m_ObjectPositionX,
                  content.m_Y + (content.m_Height - height) * style.m_ObjectPositionY, width, height};
        const auto visible = target.Intersect(content);
        source = {(visible.m_X - target.m_X) / width, (visible.m_Y - target.m_Y) / height, visible.m_Width / width,
                  visible.m_Height / height};
        target = visible;
    }
    renderer.PushState();
    renderer.PushClip(GetBounds(), GetStyle().m_Radius);
    renderer.DrawImage(target, m_Image, GetClip().Intersect(content), opacity, source);
    renderer.PopState();
}
} // namespace EStudioUI
