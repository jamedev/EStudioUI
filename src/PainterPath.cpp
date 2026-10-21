#include <EStudioUI/PainterPath.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
constexpr std::size_t s_MaxCommands = 4096;
constexpr std::size_t s_MaxVertices = 65536;

void Validate(Point point)
{
    if (!std::isfinite(point.m_X) || !std::isfinite(point.m_Y))
        throw std::invalid_argument("Painter path coordinates must be finite.");
}

Point Midpoint(Point a, Point b)
{
    return {a.m_X * 0.5f + b.m_X * 0.5f, a.m_Y * 0.5f + b.m_Y * 0.5f};
}

double Distance(Point point, Point from, Point to)
{
    const double x = static_cast<double>(to.m_X) - from.m_X;
    const double y = static_cast<double>(to.m_Y) - from.m_Y;
    const double px = static_cast<double>(point.m_X) - from.m_X;
    const double py = static_cast<double>(point.m_Y) - from.m_Y;
    const double denominator = x * x + y * y;
    const double t = denominator > 0 ? std::clamp((px * x + py * y) / denominator, 0.0, 1.0) : 0;
    return std::hypot(px - t * x, py - t * y);
}
} // namespace

void PainterPath::Clear()
{
    if (m_Commands.empty())
        return;
    m_Commands.clear();
    m_HasContour = false;
    ++m_Revision;
}

void PainterPath::Append(Command command)
{
    if (m_Commands.size() == s_MaxCommands)
        throw std::length_error("Painter paths support at most 4096 commands.");
    m_Commands.push_back(command);
    ++m_Revision;
}

void PainterPath::RequireContour() const
{
    if (!m_HasContour)
        throw std::logic_error("Start a painter path with MoveTo or ArcTo.");
}

void PainterPath::MoveTo(Point point)
{
    Validate(point);
    Append({Kind::Move, point, {}, {}});
    m_HasContour = true;
}

void PainterPath::LineTo(Point point)
{
    RequireContour();
    Validate(point);
    Append({Kind::Line, point, {}, {}});
}

void PainterPath::QuadTo(Point control, Point end)
{
    RequireContour();
    Validate(control);
    Validate(end);
    Append({Kind::Quadratic, control, end, {}});
}

void PainterPath::CubicTo(Point firstControl, Point secondControl, Point end)
{
    RequireContour();
    Validate(firstControl);
    Validate(secondControl);
    Validate(end);
    Append({Kind::Cubic, firstControl, secondControl, end});
}

void PainterPath::ArcTo(Point center, float radius, float startAngle, float sweepAngle)
{
    Validate(center);
    if (!std::isfinite(radius) || radius < 0 || !std::isfinite(startAngle) || !std::isfinite(sweepAngle) ||
        std::abs(sweepAngle) > 2 * std::numbers::pi_v<float>)
        throw std::invalid_argument("Painter arcs require a finite radius and a sweep within one turn.");
    if (!std::isfinite(center.m_X - radius) || !std::isfinite(center.m_X + radius) ||
        !std::isfinite(center.m_Y - radius) || !std::isfinite(center.m_Y + radius))
        throw std::invalid_argument("Painter arc bounds exceed float coordinates.");
    Append({Kind::Arc, center, {}, {}, radius, std::remainder(startAngle, 2 * std::numbers::pi_v<float>), sweepAngle});
    m_HasContour = true;
}

void PainterPath::Close()
{
    RequireContour();
    Append({Kind::Close, {}, {}, {}});
    m_HasContour = false;
}

const std::vector<PainterContour>& PainterPath::Flatten(float tolerance) const
{
    if (!std::isfinite(tolerance) || tolerance <= 0)
        throw std::invalid_argument("Painter path tolerance must be finite and positive.");
    if (m_CachedRevision == m_Revision && m_CachedTolerance == tolerance)
        return m_Contours;
    m_Contours.clear();
    m_Limited = false;
    std::size_t vertices = 0;
    Point current;
    const auto add = [&](Point point)
    {
        auto& points = m_Contours.back().m_Points;
        if (!points.empty() && points.back() == point)
            return true;
        if (vertices == s_MaxVertices)
        {
            m_Limited = true;
            return false;
        }
        points.push_back(point);
        ++vertices;
        return true;
    };
    const auto quadratic = [&](auto&& self, Point a, Point b, Point c, unsigned depth) -> void
    {
        if (Distance(b, a, c) <= tolerance || depth == 18 || vertices + 1 >= s_MaxVertices)
        {
            if (depth == 18 || vertices + 1 >= s_MaxVertices)
                m_Limited = true;
            add(c);
            return;
        }
        const auto ab = Midpoint(a, b);
        const auto bc = Midpoint(b, c);
        const auto mid = Midpoint(ab, bc);
        self(self, a, ab, mid, depth + 1);
        self(self, mid, bc, c, depth + 1);
    };
    const auto cubic = [&](auto&& self, Point a, Point b, Point c, Point d, unsigned depth) -> void
    {
        if (std::max(Distance(b, a, d), Distance(c, a, d)) <= tolerance || depth == 18 || vertices + 1 >= s_MaxVertices)
        {
            if (depth == 18 || vertices + 1 >= s_MaxVertices)
                m_Limited = true;
            add(d);
            return;
        }
        const auto ab = Midpoint(a, b);
        const auto bc = Midpoint(b, c);
        const auto cd = Midpoint(c, d);
        const auto abc = Midpoint(ab, bc);
        const auto bcd = Midpoint(bc, cd);
        const auto mid = Midpoint(abc, bcd);
        self(self, a, ab, abc, mid, depth + 1);
        self(self, mid, bcd, cd, d, depth + 1);
    };
    for (const auto& command : m_Commands)
    {
        if (vertices == s_MaxVertices)
        {
            m_Limited = true;
            break;
        }
        switch (command.m_Kind)
        {
        case Kind::Move:
            m_Contours.emplace_back();
            current = command.m_A;
            add(current);
            break;
        case Kind::Line:
            current = command.m_A;
            add(current);
            break;
        case Kind::Quadratic:
            quadratic(quadratic, current, command.m_A, command.m_B, 0);
            current = command.m_B;
            break;
        case Kind::Cubic:
            cubic(cubic, current, command.m_A, command.m_B, command.m_C, 0);
            current = command.m_C;
            break;
        case Kind::Arc:
        {
            const auto pointAt = [&](double angle)
            {
                return Point{static_cast<float>(command.m_A.m_X + command.m_Radius * std::cos(angle)),
                             static_cast<float>(command.m_A.m_Y + command.m_Radius * std::sin(angle))};
            };
            if (m_Contours.empty() || m_Contours.back().m_Closed)
                m_Contours.emplace_back();
            add(pointAt(command.m_Start));
            const double angle =
                command.m_Radius > 0
                    ? 2 * std::acos(std::clamp(1 - static_cast<double>(tolerance) / command.m_Radius, -1.0, 1.0))
                    : std::numbers::pi;
            const double requested = std::max(1.0, std::ceil(std::abs(command.m_Sweep) / std::max(angle, 1e-9)));
            const auto available = std::max<std::size_t>(1, s_MaxVertices - vertices);
            const auto segments = static_cast<std::size_t>(std::min(requested, static_cast<double>(available)));
            if (requested > static_cast<double>(available))
                m_Limited = true;
            for (std::size_t i = 1; i <= segments; ++i)
                add(pointAt(command.m_Start + static_cast<double>(command.m_Sweep) * static_cast<double>(i) /
                                                  static_cast<double>(segments)));
            current = pointAt(static_cast<double>(command.m_Start) + command.m_Sweep);
            break;
        }
        case Kind::Close:
            m_Contours.back().m_Closed = true;
            current = m_Contours.back().m_Points.front();
            if (m_Contours.back().m_Points.size() > 1 && m_Contours.back().m_Points.back() == current)
            {
                m_Contours.back().m_Points.pop_back();
                --vertices;
            }
            break;
        }
    }
    m_CachedRevision = m_Revision;
    m_CachedTolerance = tolerance;
    ++m_BuildCount;
    return m_Contours;
}

bool PainterPath::WasFlatteningLimited() const
{
    return m_Limited;
}

std::uint64_t PainterPath::GetRevision() const
{
    return m_Revision;
}

std::size_t PainterPath::GetFlattenBuildCount() const
{
    return m_BuildCount;
}
} // namespace EStudioUI
