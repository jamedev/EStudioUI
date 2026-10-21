#include <EStudioUI/CanvasScene.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace EStudioUI
{
struct CanvasGroupIndex
{
    CanvasScene m_Scene;
    std::weak_ptr<const CanvasGeometryData> m_Source;
    std::size_t m_NodeCount = 0;
    std::size_t m_TextBytes = 0;
    unsigned m_MaxDepth = 0;
};

namespace
{
constexpr double s_Turn = 2 * std::numbers::pi;
constexpr std::size_t s_MaxPoints = 65536;
constexpr std::size_t s_MaxNodes = 1000000;
constexpr std::size_t s_MaxTextBytes = 16 * 1024 * 1024;

bool Finite(CanvasPoint point)
{
    return std::isfinite(point.m_X) && std::isfinite(point.m_Y);
}

void ValidateBounds(CanvasBounds bounds)
{
    if (!std::isfinite(bounds.m_X) || !std::isfinite(bounds.m_Y) || !std::isfinite(bounds.m_Width) ||
        !std::isfinite(bounds.m_Height) || bounds.m_Width < 0 || bounds.m_Height < 0 ||
        !std::isfinite(bounds.m_X + bounds.m_Width) || !std::isfinite(bounds.m_Y + bounds.m_Height))
        throw std::invalid_argument("Canvas bounds must be finite and nonnegative");
}

CanvasPoint Add(CanvasPoint first, CanvasPoint second)
{
    return {first.m_X + second.m_X, first.m_Y + second.m_Y};
}

CanvasPoint Midpoint(CanvasPoint first, CanvasPoint second)
{
    return {first.m_X * 0.5 + second.m_X * 0.5, first.m_Y * 0.5 + second.m_Y * 0.5};
}

CanvasBounds Unite(CanvasBounds first, CanvasBounds second)
{
    const auto x = std::min(first.m_X, second.m_X);
    const auto y = std::min(first.m_Y, second.m_Y);
    return {x, y, std::max(first.m_X + first.m_Width, second.m_X + second.m_Width) - x,
            std::max(first.m_Y + first.m_Height, second.m_Y + second.m_Height) - y};
}

double NormalizeAngle(double angle)
{
    const double result = std::fmod(angle, s_Turn);
    return result < 0 ? result + s_Turn : result;
}

bool OnSweep(double angle, double start, double sweep)
{
    if (std::abs(sweep) >= s_Turn)
        return true;
    const double relative = NormalizeAngle(sweep < 0 ? start - angle : angle - start);
    return relative <= std::abs(sweep) + 8 * std::numeric_limits<double>::epsilon();
}

CanvasPoint ArcPoint(CanvasPoint center, double radius, double angle)
{
    return {center.m_X + radius * std::cos(angle), center.m_Y + radius * std::sin(angle)};
}

bool ValidText(const std::string& text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80)
        {
            if (first < 0x20 && first != '\t')
                return false;
            continue;
        }
        unsigned continuation = first >= 0xC2 && first <= 0xDF   ? 1
                                : first >= 0xE0 && first <= 0xEF ? 2
                                : first >= 0xF0 && first <= 0xF4 ? 3
                                                                 : 0;
        if (!continuation || continuation > text.size() - i)
            return false;
        const auto second = static_cast<unsigned char>(text[i]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) || (first == 0xF0 && second < 0x90) ||
            (first == 0xF4 && second >= 0x90))
            return false;
        while (continuation--)
            if ((static_cast<unsigned char>(text[i++]) & 0xC0) != 0x80)
                return false;
    }
    return true;
}

struct ValidationBudget
{
    std::size_t m_Nodes = 0;
    std::size_t m_TextBytes = 0;
    unsigned m_MaxDepth = 0;
};

const CanvasGroupIndex* GroupIndex(const CanvasGeometryData& data)
{
    const auto* index = data.m_GroupIndex.get();
    return index && index->m_Source.lock().get() == &data && index->m_Scene.Size() == data.m_Children.size() ? index
                                                                                                             : nullptr;
}

CanvasBounds Bounds(const CanvasGeometry& geometry, unsigned depth, ValidationBudget& budget)
{
    if (depth > 32 || ++budget.m_Nodes > s_MaxNodes)
        throw std::invalid_argument("Canvas geometry nesting or aggregate size limit exceeded");
    budget.m_MaxDepth = std::max(budget.m_MaxDepth, depth);
    if (!Finite(geometry.m_A) || !Finite(geometry.m_B) || !std::isfinite(geometry.m_Radius) || geometry.m_Radius < 0)
        throw std::invalid_argument("Canvas geometry must have finite coordinates and a nonnegative radius");
    CanvasBounds bounds;
    if (geometry.m_Kind <= CanvasPrimitive::Circle && geometry.m_Data)
        throw std::invalid_argument("Simple canvas primitives do not accept complex payloads");
    if (geometry.m_Kind == CanvasPrimitive::Circle)
    {
        bounds = {geometry.m_A.m_X - geometry.m_Radius, geometry.m_A.m_Y - geometry.m_Radius, 2 * geometry.m_Radius,
                  2 * geometry.m_Radius};
    }
    else if (geometry.m_Kind == CanvasPrimitive::Line || geometry.m_Kind == CanvasPrimitive::Rectangle)
    {
        bounds = {std::min(geometry.m_A.m_X, geometry.m_B.m_X), std::min(geometry.m_A.m_Y, geometry.m_B.m_Y),
                  std::abs(geometry.m_B.m_X - geometry.m_A.m_X), std::abs(geometry.m_B.m_Y - geometry.m_A.m_Y)};
    }
    else
    {
        if (!geometry.m_Data)
            throw std::invalid_argument("Complex canvas geometry requires a payload");
        const auto& data = *geometry.m_Data;
        if ((geometry.m_Kind != CanvasPrimitive::Group &&
             (!data.m_Children.empty() || !data.m_ChildStyles.empty() || data.m_GroupIndex)) ||
            (geometry.m_Kind != CanvasPrimitive::Spline && geometry.m_Kind != CanvasPrimitive::Path &&
             !data.m_Points.empty()) ||
            (geometry.m_Kind != CanvasPrimitive::Text && !data.m_Text.empty()))
            throw std::invalid_argument("Canvas payload contains data for another primitive kind");
        if (!std::isfinite(data.m_StartAngle) || !std::isfinite(data.m_SweepAngle) || !std::isfinite(data.m_FontSize) ||
            data.m_FontSize <= 0 || data.m_Points.size() > s_MaxPoints || data.m_Children.size() > s_MaxNodes ||
            data.m_Text.size() > s_MaxPoints || data.m_Points.size() > s_MaxNodes - budget.m_Nodes ||
            data.m_Text.size() > s_MaxTextBytes - budget.m_TextBytes)
            throw std::invalid_argument("Canvas payload is invalid or exceeds its size limit");
        budget.m_Nodes += data.m_Points.size();
        budget.m_TextBytes += data.m_Text.size();
        if (!data.m_ChildStyles.empty() && data.m_ChildStyles.size() != data.m_Children.size())
            throw std::invalid_argument("Canvas group styles must match its child count");
        if (geometry.m_Kind == CanvasPrimitive::Arc)
        {
            if (std::abs(data.m_SweepAngle) > s_Turn)
                throw std::invalid_argument("Canvas arc sweep exceeds one full turn");
            const double start = NormalizeAngle(data.m_StartAngle);
            const auto first = ArcPoint(geometry.m_A, geometry.m_Radius, start);
            bounds = {first.m_X, first.m_Y, 0, 0};
            const auto include = [&](CanvasPoint point) { bounds = Unite(bounds, {point.m_X, point.m_Y, 0, 0}); };
            // The center is an editable snap handle and must be discoverable through the same BVH.
            include(geometry.m_A);
            include(ArcPoint(geometry.m_A, geometry.m_Radius, start + data.m_SweepAngle));
            for (int cardinal = 0; cardinal < 4; ++cardinal)
            {
                const double angle = cardinal * std::numbers::pi / 2;
                if (OnSweep(angle, start, data.m_SweepAngle))
                    include(ArcPoint(geometry.m_A, geometry.m_Radius, angle));
            }
        }
        else if (geometry.m_Kind == CanvasPrimitive::Spline || geometry.m_Kind == CanvasPrimitive::Path)
        {
            const auto count = data.m_Points.size();
            if ((geometry.m_Kind == CanvasPrimitive::Spline && (count < 4 || (count - 1) % 3 != 0)) ||
                (geometry.m_Kind == CanvasPrimitive::Path && count < (data.m_Closed ? 3U : 2U)))
                throw std::invalid_argument("Invalid canvas curve point count");
            bool first = true;
            for (const auto local : data.m_Points)
            {
                const auto point = Add(geometry.m_A, local);
                if (!Finite(local) || !Finite(point))
                    throw std::invalid_argument("Canvas curve point exceeds finite world coordinates");
                const CanvasBounds pointBounds{point.m_X, point.m_Y, 0, 0};
                bounds = first ? pointBounds : Unite(bounds, pointBounds);
                first = false;
            }
        }
        else if (geometry.m_Kind == CanvasPrimitive::Text)
        {
            if (!ValidText(data.m_Text) || geometry.m_B.m_X < geometry.m_A.m_X || geometry.m_B.m_Y < geometry.m_A.m_Y)
                throw std::invalid_argument("Canvas text requires valid single-line UTF-8 and normalized bounds");
            bounds = {geometry.m_A.m_X, geometry.m_A.m_Y, geometry.m_B.m_X - geometry.m_A.m_X,
                      geometry.m_B.m_Y - geometry.m_A.m_Y};
        }
        else if (geometry.m_Kind == CanvasPrimitive::Group)
        {
            if (data.m_Children.empty())
                throw std::invalid_argument("Canvas group requires at least one child");
            if (const auto* index = GroupIndex(data))
            {
                if (index->m_NodeCount - 1 > s_MaxNodes - budget.m_Nodes ||
                    index->m_TextBytes > s_MaxTextBytes - budget.m_TextBytes || depth + index->m_MaxDepth > 32)
                    throw std::invalid_argument("Canvas geometry nesting or aggregate size limit exceeded");
                budget.m_Nodes += index->m_NodeCount - 1;
                budget.m_TextBytes += index->m_TextBytes;
                budget.m_MaxDepth = std::max(budget.m_MaxDepth, depth + index->m_MaxDepth);
                bounds = index->m_Scene.GetBounds();
                bounds.m_X += geometry.m_A.m_X;
                bounds.m_Y += geometry.m_A.m_Y;
                ValidateBounds(bounds);
                return bounds;
            }
            bool first = true;
            for (const auto& child : data.m_Children)
            {
                auto childBounds = Bounds(child, depth + 1, budget);
                childBounds.m_X += geometry.m_A.m_X;
                childBounds.m_Y += geometry.m_A.m_Y;
                ValidateBounds(childBounds);
                bounds = first ? childBounds : Unite(bounds, childBounds);
                first = false;
            }
        }
        else
        {
            throw std::invalid_argument("Unknown canvas primitive");
        }
    }
    ValidateBounds(bounds);
    return bounds;
}

bool Equal(const CanvasGeometry& first, const CanvasGeometry& second, unsigned depth)
{
    if (first.m_Kind != second.m_Kind || first.m_A != second.m_A || first.m_B != second.m_B ||
        first.m_Radius != second.m_Radius)
        return false;
    if (first.m_Data == second.m_Data)
        return true;
    if (!first.m_Data || !second.m_Data || depth > 32)
        return false;
    const auto& a = *first.m_Data;
    const auto& b = *second.m_Data;
    if (a.m_Points != b.m_Points || a.m_Text != b.m_Text || a.m_ChildStyles != b.m_ChildStyles ||
        a.m_StartAngle != b.m_StartAngle || a.m_SweepAngle != b.m_SweepAngle || a.m_FontSize != b.m_FontSize ||
        a.m_Closed != b.m_Closed || a.m_Children.size() != b.m_Children.size())
        return false;
    for (std::size_t i = 0; i < a.m_Children.size(); ++i)
        if (!Equal(a.m_Children[i], b.m_Children[i], depth + 1))
            return false;
    return true;
}

double SegmentDistance(CanvasPoint a, CanvasPoint b, CanvasPoint point)
{
    const auto dx = b.m_X - a.m_X;
    const auto dy = b.m_Y - a.m_Y;
    const auto scale = std::max(std::abs(dx), std::abs(dy));
    if (scale == 0)
        return std::hypot(point.m_X - a.m_X, point.m_Y - a.m_Y);
    const auto ux = dx / scale;
    const auto uy = dy / scale;
    const auto px = (point.m_X - a.m_X) / scale;
    const auto py = (point.m_Y - a.m_Y) / scale;
    const auto projection = std::clamp((px * ux + py * uy) / (ux * ux + uy * uy), 0.0, 1.0);
    if (std::isnan(projection))
        return std::hypot(point.m_X - a.m_X, point.m_Y - a.m_Y);
    return std::hypot(point.m_X - (a.m_X + dx * projection), point.m_Y - (a.m_Y + dy * projection));
}

CanvasPoint CubicPoint(const std::array<CanvasPoint, 4>& points, double t)
{
    const auto lerp = [t](CanvasPoint a, CanvasPoint b)
    { return CanvasPoint{a.m_X * (1 - t) + b.m_X * t, a.m_Y * (1 - t) + b.m_Y * t}; };
    return lerp(lerp(lerp(points[0], points[1]), lerp(points[1], points[2])),
                lerp(lerp(points[1], points[2]), lerp(points[2], points[3])));
}

bool FlattenCubic(const std::array<CanvasPoint, 4>& points, double tolerance, std::size_t segments, unsigned depth,
                  std::vector<CanvasPoint>& output)
{
    const double error =
        std::max(SegmentDistance(points[0], points[3], points[1]), SegmentDistance(points[0], points[3], points[2]));
    if (error <= tolerance || segments <= 1 || depth == 20)
    {
        output.push_back(points[3]);
        return error <= tolerance;
    }
    const auto ab = Midpoint(points[0], points[1]);
    const auto bc = Midpoint(points[1], points[2]);
    const auto cd = Midpoint(points[2], points[3]);
    const auto abc = Midpoint(ab, bc);
    const auto bcd = Midpoint(bc, cd);
    const auto center = Midpoint(abc, bcd);
    const bool left = FlattenCubic({points[0], ab, abc, center}, tolerance, segments / 2, depth + 1, output);
    const bool right =
        FlattenCubic({center, bcd, cd, points[3]}, tolerance, segments - segments / 2, depth + 1, output);
    return left && right;
}

void SnapPoints(const CanvasGeometry& geometry, CanvasPoint offset, std::vector<CanvasPoint>& output)
{
    const auto a = Add(geometry.m_A, offset);
    const auto b = Add(geometry.m_B, offset);
    const auto append = [&](CanvasPoint point) { output.push_back(point); };
    if (geometry.m_Kind == CanvasPrimitive::Group)
    {
        for (const auto& child : geometry.m_Data->m_Children)
            SnapPoints(child, a, output);
        return;
    }
    if (geometry.m_Kind != CanvasPrimitive::Path && geometry.m_Kind != CanvasPrimitive::Spline)
        append(a);
    if (geometry.m_Kind == CanvasPrimitive::Circle || geometry.m_Kind == CanvasPrimitive::Arc)
    {
        const double start = geometry.m_Data ? NormalizeAngle(geometry.m_Data->m_StartAngle) : 0;
        const double sweep = geometry.m_Kind == CanvasPrimitive::Arc ? geometry.m_Data->m_SweepAngle : s_Turn;
        if (geometry.m_Kind == CanvasPrimitive::Arc)
        {
            append(ArcPoint(a, geometry.m_Radius, start));
            append(ArcPoint(a, geometry.m_Radius, start + sweep));
            append(ArcPoint(a, geometry.m_Radius, start + sweep * 0.5));
        }
        for (int i = 0; i < 4; ++i)
        {
            const double angle = i * std::numbers::pi / 2;
            if (OnSweep(angle, start, sweep))
                append(ArcPoint(a, geometry.m_Radius, angle));
        }
    }
    else if (geometry.m_Kind == CanvasPrimitive::Path || geometry.m_Kind == CanvasPrimitive::Spline)
    {
        const auto& points = geometry.m_Data->m_Points;
        for (const auto point : points)
            append(Add(a, point));
        if (geometry.m_Kind == CanvasPrimitive::Spline)
        {
            for (std::size_t i = 0; i + 3 < points.size(); i += 3)
                append(Add(a, CubicPoint({points[i], points[i + 1], points[i + 2], points[i + 3]}, 0.5)));
        }
        else
        {
            for (std::size_t i = 1; i < points.size(); ++i)
                append(Add(a, Midpoint(points[i - 1], points[i])));
            if (geometry.m_Data->m_Closed)
                append(Add(a, Midpoint(points.back(), points.front())));
        }
    }
    else
    {
        append(b);
        append(Midpoint(a, b));
        if (geometry.m_Kind == CanvasPrimitive::Rectangle || geometry.m_Kind == CanvasPrimitive::Text)
        {
            append({a.m_X, b.m_Y});
            append({b.m_X, a.m_Y});
        }
    }
}

std::size_t PayloadBytes(const CanvasGeometry& geometry)
{
    if (!geometry.m_Data)
        return 0;
    const auto& data = *geometry.m_Data;
    std::size_t bytes = sizeof(CanvasGeometryData) + data.m_Points.capacity() * sizeof(CanvasPoint) +
                        data.m_Text.capacity() + data.m_Children.capacity() * sizeof(CanvasGeometry) +
                        data.m_ChildStyles.capacity() * sizeof(std::uint32_t);
    if (const auto* index = GroupIndex(data))
        bytes += sizeof(CanvasGroupIndex) + index->m_Scene.GetStorageBytes();
    for (const auto& child : data.m_Children)
    {
        const auto childBytes = PayloadBytes(child);
        if (childBytes > std::numeric_limits<std::size_t>::max() - bytes)
            throw std::length_error("Canvas geometry memory estimate overflow");
        bytes += childBytes;
    }
    return bytes;
}
} // namespace

CanvasBounds CanvasGeometry::Bounds() const
{
    ValidationBudget budget;
    return EStudioUI::Bounds(*this, 0, budget);
}

bool CanvasGeometry::operator==(const CanvasGeometry& other) const
{
    return Equal(*this, other, 0);
}

bool CanvasGeometryData::operator==(const CanvasGeometryData& other) const
{
    return m_Points == other.m_Points && m_Text == other.m_Text && m_Children == other.m_Children &&
           m_ChildStyles == other.m_ChildStyles && m_StartAngle == other.m_StartAngle &&
           m_SweepAngle == other.m_SweepAngle && m_FontSize == other.m_FontSize && m_Closed == other.m_Closed;
}

CanvasGeometry MakeCanvasArc(CanvasPoint center, double radius, double startAngle, double sweepAngle)
{
    auto data = std::make_shared<CanvasGeometryData>();
    data->m_StartAngle = startAngle;
    data->m_SweepAngle = sweepAngle;
    CanvasGeometry result{CanvasPrimitive::Arc, center, {}, radius, std::move(data)};
    result.Bounds();
    return result;
}

CanvasGeometry MakeCanvasSpline(CanvasPoint origin, std::vector<CanvasPoint> controls)
{
    auto data = std::make_shared<CanvasGeometryData>();
    data->m_Points = std::move(controls);
    CanvasGeometry result{CanvasPrimitive::Spline, origin, {}, 0, std::move(data)};
    result.Bounds();
    return result;
}

CanvasGeometry MakeCanvasPath(CanvasPoint origin, std::vector<CanvasPoint> points, bool closed)
{
    auto data = std::make_shared<CanvasGeometryData>();
    data->m_Points = std::move(points);
    data->m_Closed = closed;
    CanvasGeometry result{CanvasPrimitive::Path, origin, {}, 0, std::move(data)};
    result.Bounds();
    return result;
}

CanvasGeometry MakeCanvasText(CanvasBounds bounds, std::string text, double fontSize)
{
    ValidateBounds(bounds);
    auto data = std::make_shared<CanvasGeometryData>();
    data->m_Text = std::move(text);
    data->m_FontSize = fontSize;
    CanvasGeometry result{CanvasPrimitive::Text,
                          {bounds.m_X, bounds.m_Y},
                          {bounds.m_X + bounds.m_Width, bounds.m_Y + bounds.m_Height},
                          0,
                          std::move(data)};
    result.Bounds();
    return result;
}

CanvasGeometry MakeCanvasGroup(CanvasPoint origin, std::vector<CanvasGeometry> children,
                               std::vector<std::uint32_t> styles)
{
    auto data = std::make_shared<CanvasGeometryData>();
    data->m_Children = std::move(children);
    data->m_ChildStyles = std::move(styles);
    CanvasGeometry result{CanvasPrimitive::Group, origin, {}, 0, data};
    ValidationBudget budget;
    Bounds(result, 0, budget);
    auto index = std::make_shared<CanvasGroupIndex>();
    index->m_Scene.Reserve(data->m_Children.size());
    for (const auto& child : data->m_Children)
        index->m_Scene.Add(child);
    index->m_Source = data;
    index->m_NodeCount = budget.m_Nodes;
    index->m_TextBytes = budget.m_TextBytes;
    index->m_MaxDepth = budget.m_MaxDepth;
    data->m_GroupIndex = std::move(index);
    return result;
}

bool FlattenGeometry(const CanvasGeometry& geometry, double tolerance, std::vector<CanvasPoint>& output,
                     std::size_t maxPoints)
{
    geometry.Bounds();
    if (!std::isfinite(tolerance) || tolerance <= 0 || maxPoints < 2 || maxPoints > s_MaxNodes)
        throw std::invalid_argument("Canvas flattening requires a finite positive tolerance and 2..1000000 points");
    output.clear();
    if (geometry.m_Kind == CanvasPrimitive::Line)
    {
        output = {geometry.m_A, geometry.m_B};
        return true;
    }
    if (geometry.m_Kind == CanvasPrimitive::Rectangle)
    {
        const std::array<CanvasPoint, 5> points{geometry.m_A, CanvasPoint{geometry.m_B.m_X, geometry.m_A.m_Y},
                                                geometry.m_B, CanvasPoint{geometry.m_A.m_X, geometry.m_B.m_Y},
                                                geometry.m_A};
        const auto count = std::min(maxPoints, points.size());
        for (std::size_t i = 0; i < count; ++i)
            output.push_back(points[i * (points.size() - 1) / (count - 1)]);
        return count == points.size();
    }
    if (geometry.m_Kind == CanvasPrimitive::Circle || geometry.m_Kind == CanvasPrimitive::Arc)
    {
        const double start =
            geometry.m_Kind == CanvasPrimitive::Arc ? NormalizeAngle(geometry.m_Data->m_StartAngle) : 0;
        const double sweep = geometry.m_Kind == CanvasPrimitive::Arc ? geometry.m_Data->m_SweepAngle : s_Turn;
        const double fraction = geometry.m_Radius > 0 ? std::min(1.0, tolerance / geometry.m_Radius) : 1;
        const double step = 2 * std::asin(std::sqrt(fraction * (2 - fraction)));
        const double needed =
            step > 0 ? std::max(1.0, std::ceil(std::abs(sweep) / step)) : std::numeric_limits<double>::infinity();
        const auto segments = static_cast<std::size_t>(std::min(static_cast<double>(maxPoints - 1), needed));
        output.reserve(segments + 1);
        for (std::size_t i = 0; i <= segments; ++i)
            output.push_back(ArcPoint(geometry.m_A, geometry.m_Radius,
                                      start + sweep * (static_cast<double>(i) / static_cast<double>(segments))));
        return needed <= static_cast<double>(maxPoints - 1);
    }
    if (geometry.m_Kind == CanvasPrimitive::Path)
    {
        const auto& points = geometry.m_Data->m_Points;
        const std::size_t required = points.size() + (geometry.m_Data->m_Closed ? 1 : 0);
        const std::size_t count = std::min(required, maxPoints);
        output.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto index = i * (required - 1) / (count - 1);
            output.push_back(Add(geometry.m_A, points[index == points.size() ? 0 : index]));
        }
        return required <= maxPoints;
    }
    if (geometry.m_Kind == CanvasPrimitive::Spline)
    {
        const auto& points = geometry.m_Data->m_Points;
        const auto count = (points.size() - 1) / 3;
        output.push_back(points.front());
        bool complete = true;
        if (maxPoints <= count)
        {
            for (std::size_t i = 1; i < maxPoints; ++i)
            {
                const double parameter =
                    static_cast<double>(i) * static_cast<double>(count) / static_cast<double>(maxPoints - 1);
                const auto segment = std::min(count - 1, static_cast<std::size_t>(parameter));
                const auto index = segment * 3;
                output.push_back(CubicPoint({points[index], points[index + 1], points[index + 2], points[index + 3]},
                                            parameter - static_cast<double>(segment)));
            }
            complete = false;
        }
        else
        {
            const auto segments = (maxPoints - 1) / count;
            for (std::size_t i = 0; i + 3 < points.size(); i += 3)
                complete = FlattenCubic({points[i], points[i + 1], points[i + 2], points[i + 3]}, tolerance, segments,
                                        0, output) &&
                           complete;
        }
        for (auto& point : output)
            point = Add(geometry.m_A, point);
        return complete;
    }
    throw std::invalid_argument("Text and groups cannot be flattened as one contour");
}

double DistanceToItem(const CanvasGeometry& geometry, CanvasPoint point, double curveTolerance)
{
    const auto bounds = geometry.Bounds();
    if (!Finite(point) || !std::isfinite(curveTolerance) || curveTolerance <= 0)
        throw std::invalid_argument("Canvas hit testing requires a finite point and positive curve tolerance");
    if (geometry.m_Kind == CanvasPrimitive::Circle)
        return std::abs(std::hypot(point.m_X - geometry.m_A.m_X, point.m_Y - geometry.m_A.m_Y) - geometry.m_Radius);
    if (geometry.m_Kind == CanvasPrimitive::Line)
        return SegmentDistance(geometry.m_A, geometry.m_B, point);
    if (geometry.m_Kind == CanvasPrimitive::Arc)
    {
        const double start = NormalizeAngle(geometry.m_Data->m_StartAngle);
        const double sweep = geometry.m_Data->m_SweepAngle;
        const double angle = std::atan2(point.m_Y - geometry.m_A.m_Y, point.m_X - geometry.m_A.m_X);
        if (OnSweep(angle, start, sweep))
            return std::abs(std::hypot(point.m_X - geometry.m_A.m_X, point.m_Y - geometry.m_A.m_Y) - geometry.m_Radius);
        const auto first = ArcPoint(geometry.m_A, geometry.m_Radius, start);
        const auto last = ArcPoint(geometry.m_A, geometry.m_Radius, start + sweep);
        return std::min(std::hypot(point.m_X - first.m_X, point.m_Y - first.m_Y),
                        std::hypot(point.m_X - last.m_X, point.m_Y - last.m_Y));
    }
    if (geometry.m_Kind == CanvasPrimitive::Group)
    {
        const CanvasPoint local{point.m_X - geometry.m_A.m_X, point.m_Y - geometry.m_A.m_Y};
        if (!Finite(local))
            return std::numeric_limits<double>::infinity();
        double distance = std::numeric_limits<double>::infinity();
        if (!GroupIndex(*geometry.m_Data))
        {
            for (const auto& child : geometry.m_Data->m_Children)
                distance = std::min(distance, DistanceToItem(child, local, curveTolerance));
            return distance;
        }
        const double right = bounds.m_X + bounds.m_Width;
        const double bottom = bounds.m_Y + bounds.m_Height;
        const double lowerBound = std::hypot(std::max({bounds.m_X - point.m_X, 0.0, point.m_X - right}),
                                             std::max({bounds.m_Y - point.m_Y, 0.0, point.m_Y - bottom}));
        double radius = std::max(curveTolerance, lowerBound);
        std::vector<std::size_t> candidates;
        // Expanding square queries find a witness, then its exact distance bounds the final search.
        for (unsigned iteration = 0; iteration < 66; ++iteration)
        {
            if (iteration == 64)
                radius = std::numeric_limits<double>::infinity();
            const double left = std::clamp(point.m_X - radius, bounds.m_X, right);
            const double top = std::clamp(point.m_Y - radius, bounds.m_Y, bottom);
            const double queryRight = std::clamp(point.m_X + radius, bounds.m_X, right);
            const double queryBottom = std::clamp(point.m_Y + radius, bounds.m_Y, bottom);
            QueryCanvasGroup(geometry, {left, top, queryRight - left, queryBottom - top}, candidates);
            for (const auto index : candidates)
                distance =
                    std::min(distance, DistanceToItem(geometry.m_Data->m_Children[index], local, curveTolerance));
            if (!candidates.empty() && distance <= radius)
                return distance;
            if (!candidates.empty())
                radius = distance;
            else
                radius *= 2;
        }
        return distance;
    }
    if (geometry.m_Kind == CanvasPrimitive::Path)
    {
        const auto& points = geometry.m_Data->m_Points;
        double distance = std::numeric_limits<double>::infinity();
        for (std::size_t i = 1; i < points.size(); ++i)
            distance = std::min(distance,
                                SegmentDistance(Add(geometry.m_A, points[i - 1]), Add(geometry.m_A, points[i]), point));
        if (geometry.m_Data->m_Closed)
            distance = std::min(
                distance, SegmentDistance(Add(geometry.m_A, points.back()), Add(geometry.m_A, points.front()), point));
        return distance;
    }
    if (geometry.m_Kind == CanvasPrimitive::Spline)
    {
        std::vector<CanvasPoint> points;
        FlattenGeometry(geometry, curveTolerance, points);
        double distance = std::numeric_limits<double>::infinity();
        for (std::size_t i = 1; i < points.size(); ++i)
            distance = std::min(distance, SegmentDistance(points[i - 1], points[i], point));
        return distance;
    }
    const auto right = bounds.m_X + bounds.m_Width;
    const auto bottom = bounds.m_Y + bounds.m_Height;
    if (bounds.Contains(point))
    {
        if (geometry.m_Kind == CanvasPrimitive::Text)
            return 0;
        return std::min({point.m_X - bounds.m_X, right - point.m_X, point.m_Y - bounds.m_Y, bottom - point.m_Y});
    }
    return std::hypot(std::max({bounds.m_X - point.m_X, 0.0, point.m_X - right}),
                      std::max({bounds.m_Y - point.m_Y, 0.0, point.m_Y - bottom}));
}

CanvasGeometry TranslateGeometry(const CanvasGeometry& geometry, CanvasPoint delta)
{
    if (!Finite(delta))
        throw std::invalid_argument("Canvas displacement must be finite");
    auto result = geometry;
    result.m_A = Add(result.m_A, delta);
    if (geometry.m_Kind == CanvasPrimitive::Line || geometry.m_Kind == CanvasPrimitive::Rectangle ||
        geometry.m_Kind == CanvasPrimitive::Text)
        result.m_B = Add(result.m_B, delta);
    result.Bounds();
    return result;
}

void GeometrySnapPoints(const CanvasGeometry& geometry, std::vector<CanvasPoint>& output)
{
    geometry.Bounds();
    output.clear();
    SnapPoints(geometry, {}, output);
}

void QueryCanvasGroup(const CanvasGeometry& geometry, const CanvasBounds& worldBounds, std::vector<std::size_t>& output)
{
    ValidateBounds(worldBounds);
    const auto bounds = geometry.Bounds();
    if (geometry.m_Kind != CanvasPrimitive::Group)
        throw std::invalid_argument("Group spatial queries require group geometry");
    output.clear();
    if (!bounds.Intersects(worldBounds))
        return;
    // Clip before shifting to local coordinates to avoid overflowing far-away queries.
    const double left = std::max(bounds.m_X, worldBounds.m_X);
    const double top = std::max(bounds.m_Y, worldBounds.m_Y);
    const double right = std::min(bounds.m_X + bounds.m_Width, worldBounds.m_X + worldBounds.m_Width);
    const double bottom = std::min(bounds.m_Y + bounds.m_Height, worldBounds.m_Y + worldBounds.m_Height);
    const CanvasBounds local{left - geometry.m_A.m_X, top - geometry.m_A.m_Y, right - left, bottom - top};
    const auto& data = *geometry.m_Data;
    if (const auto* index = GroupIndex(data))
    {
        std::vector<CanvasItemId> candidates;
        index->m_Scene.Query(local, candidates);
        output.reserve(candidates.size());
        for (const auto id : candidates)
            output.push_back(static_cast<std::size_t>(index->m_Scene.Get(id)->m_Order));
        std::sort(output.begin(), output.end());
        return;
    }
    for (std::size_t i = 0; i < data.m_Children.size(); ++i)
        if (data.m_Children[i].Bounds().Intersects(local))
            output.push_back(i);
}

void GeometrySnapPoints(const CanvasGeometry& geometry, const CanvasBounds& worldBounds,
                        std::vector<CanvasPoint>& output)
{
    ValidateBounds(worldBounds);
    geometry.Bounds();
    output.clear();
    const auto visit = [&](const auto& self, const CanvasGeometry& value) -> void
    {
        if (value.m_Kind == CanvasPrimitive::Group)
        {
            std::vector<std::size_t> candidates;
            QueryCanvasGroup(value, worldBounds, candidates);
            for (const auto index : candidates)
                self(self, TranslateGeometry(value.m_Data->m_Children[index], value.m_A));
        }
        else
        {
            const auto begin = output.size();
            SnapPoints(value, {}, output);
            output.erase(std::remove_if(output.begin() + static_cast<std::ptrdiff_t>(begin), output.end(),
                                        [&](CanvasPoint point) { return !worldBounds.Contains(point); }),
                         output.end());
        }
    };
    visit(visit, geometry);
}

std::size_t GeometryBytes(const CanvasGeometry& geometry)
{
    geometry.Bounds();
    return PayloadBytes(geometry);
}
} // namespace EStudioUI
