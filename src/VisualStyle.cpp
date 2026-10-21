#include <EStudioUI/VisualStyle.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace EStudioUI
{
Point Affine::Map(Point point) const
{
    return {m_A * point.m_X + m_C * point.m_Y + m_X, m_B * point.m_X + m_D * point.m_Y + m_Y};
}

Affine Affine::operator*(const Affine& local) const
{
    const auto offset = Map({local.m_X, local.m_Y});
    return {m_A * local.m_A + m_C * local.m_B,
            m_B * local.m_A + m_D * local.m_B,
            m_A * local.m_C + m_C * local.m_D,
            m_B * local.m_C + m_D * local.m_D,
            offset.m_X,
            offset.m_Y};
}

bool Affine::Inverse(Affine& inverse) const
{
    const float determinant = m_A * m_D - m_B * m_C;
    if (!std::isfinite(determinant) || std::abs(determinant) < 0.00000001f)
        return false;
    inverse = {m_D / determinant,
               -m_B / determinant,
               -m_C / determinant,
               m_A / determinant,
               (m_C * m_Y - m_D * m_X) / determinant,
               (m_B * m_X - m_A * m_Y) / determinant};
    return true;
}

Rect Affine::MapBounds(Rect bounds) const
{
    const auto p0 = Map({bounds.m_X, bounds.m_Y});
    const auto p1 = Map({bounds.m_X + bounds.m_Width, bounds.m_Y});
    const auto p2 = Map({bounds.m_X, bounds.m_Y + bounds.m_Height});
    const auto p3 = Map({bounds.m_X + bounds.m_Width, bounds.m_Y + bounds.m_Height});
    const float x = std::min({p0.m_X, p1.m_X, p2.m_X, p3.m_X});
    const float y = std::min({p0.m_Y, p1.m_Y, p2.m_Y, p3.m_Y});
    return {x, y, std::max({p0.m_X, p1.m_X, p2.m_X, p3.m_X}) - x, std::max({p0.m_Y, p1.m_Y, p2.m_Y, p3.m_Y}) - y};
}

Affine Transform::Resolve(Rect bounds) const
{
    const Point origin{bounds.m_X + m_Origin.m_X * bounds.m_Width, bounds.m_Y + m_Origin.m_Y * bounds.m_Height};
    Affine matrix{1, 0, 0, 1, origin.m_X, origin.m_Y};
    for (const auto& operation : m_Operations)
    {
        const auto& v = operation.m_Values;
        Affine local;
        const float angle = v[0] * std::numbers::pi_v<float> / 180.0f;
        switch (operation.m_Kind)
        {
        case TransformOperation::Kind::Translate:
            local.m_X = v[0] * (operation.m_PercentX ? bounds.m_Width / 100 : 1);
            local.m_Y = v[1] * (operation.m_PercentY ? bounds.m_Height / 100 : 1);
            break;
        case TransformOperation::Kind::Scale:
            local.m_A = v[0];
            local.m_D = v[1];
            break;
        case TransformOperation::Kind::Rotate:
            local = {std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle), 0, 0};
            break;
        case TransformOperation::Kind::SkewX:
            local.m_C = std::tan(angle);
            break;
        case TransformOperation::Kind::SkewY:
            local.m_B = std::tan(angle);
            break;
        case TransformOperation::Kind::Matrix:
            local = {v[0], v[1], v[2], v[3], v[4], v[5]};
            break;
        }
        matrix = matrix * local;
        for (float component : {matrix.m_A, matrix.m_B, matrix.m_C, matrix.m_D, matrix.m_X, matrix.m_Y})
            if (!std::isfinite(component) || std::abs(component) > 1.0e12f)
                return {0, 0, 0, 0, 0, 0};
    }
    return matrix * Affine{1, 0, 0, 1, -origin.m_X, -origin.m_Y};
}

bool ContainsRounded(Rect bounds, float radius, Point point)
{
    if (!bounds.Contains(point))
        return false;
    radius = std::clamp(radius, 0.0f, std::min(bounds.m_Width, bounds.m_Height) * 0.5f);
    const float x =
        std::max(std::abs(point.m_X - bounds.m_X - bounds.m_Width * 0.5f) - bounds.m_Width * 0.5f + radius, 0.0f);
    const float y =
        std::max(std::abs(point.m_Y - bounds.m_Y - bounds.m_Height * 0.5f) - bounds.m_Height * 0.5f + radius, 0.0f);
    return x * x + y * y <= radius * radius;
}
} // namespace EStudioUI
