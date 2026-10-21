#include <EStudioUI/PainterPath.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Exception, typename Callback> void Rejects(Callback callback, const std::string& message)
{
    bool rejected = false;
    try
    {
        callback();
    }
    catch (const Exception&)
    {
        rejected = true;
    }
    Check(rejected, message);
}

double Distance(Point point, const std::vector<Point>& points)
{
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const double x = static_cast<double>(points[i].m_X) - points[i - 1].m_X;
        const double y = static_cast<double>(points[i].m_Y) - points[i - 1].m_Y;
        const double px = static_cast<double>(point.m_X) - points[i - 1].m_X;
        const double py = static_cast<double>(point.m_Y) - points[i - 1].m_Y;
        const double length = x * x + y * y;
        const double fraction = length > 0 ? std::clamp((x * px + y * py) / length, 0.0, 1.0) : 0;
        best = std::min(best, std::hypot(px - x * fraction, py - y * fraction));
    }
    return best;
}

void ContoursAndCache()
{
    PainterPath path;
    Check(path.Flatten().empty(), "empty path has no contours");
    Rejects<std::logic_error>([&] { path.LineTo({1, 2}); }, "segments require a starting point");
    path.MoveTo({0, 0});
    path.LineTo({20, 0});
    path.LineTo({20, 20});
    path.LineTo({0, 0});
    path.Close();
    path.MoveTo({50, 10});
    path.LineTo({70, 10});
    const auto& contours = path.Flatten();
    Check(contours.size() == 2 && contours[0].m_Closed && !contours[1].m_Closed,
          "separate open and closed contours retain topology");
    Check(contours[0].m_Points.size() == 3 && contours[1].m_Points.size() == 2,
          "closed contours omit repeated closing vertices");
    const auto builds = path.GetFlattenBuildCount();
    const auto revision = path.GetRevision();
    path.Flatten();
    Check(path.GetFlattenBuildCount() == builds, "unchanged tolerance and revision reuse geometry");
    path.Flatten(.1f);
    Check(path.GetFlattenBuildCount() == builds + 1 && path.GetRevision() == revision,
          "tolerance changes rebuild geometry without editing source");
    path.LineTo({80, 20});
    path.Flatten(.1f);
    Check(path.GetFlattenBuildCount() == builds + 2 && path.GetRevision() == revision + 1,
          "source changes invalidate flatten cache");
    path.Close();
    Rejects<std::logic_error>([&] { path.LineTo({0, 0}); }, "close ends contour explicitly");
    path.ArcTo({0, 0}, 10, 0, 1);
    Check(path.Flatten().size() == 3, "arc after close starts a new contour");
    path.Clear();
    Check(path.Flatten().empty(), "clear removes old cached geometry");
    const auto clearedRevision = path.GetRevision();
    path.Clear();
    Check(path.GetRevision() == clearedRevision, "redundant clear preserves revision");
}

void AdaptiveCurves()
{
    PainterPath quadratic;
    quadratic.MoveTo({0, 0});
    quadratic.QuadTo({50, 120}, {100, 0});
    const auto quadraticPoints = quadratic.Flatten(.1f)[0].m_Points;
    Check(quadraticPoints.front() == Point{0, 0} && quadraticPoints.back() == Point{100, 0},
          "quadratic endpoints remain exact");
    double error = 0;
    for (int i = 0; i <= 1000; ++i)
    {
        const float t = static_cast<float>(i) / 1000;
        error = std::max(error, Distance({100 * t, 240 * t * (1 - t)}, quadraticPoints));
    }
    Check(error <= .101 && !quadratic.WasFlatteningLimited(), "quadratic sampled approximation meets tolerance");

    PainterPath cubic;
    cubic.MoveTo({0, 0});
    cubic.CubicTo({100, 180}, {-40, -140}, {120, 20});
    const auto cubicPoints = cubic.Flatten(.1f)[0].m_Points;
    error = 0;
    for (int i = 0; i <= 1000; ++i)
    {
        const float t = static_cast<float>(i) / 1000;
        const float s = 1 - t;
        error = std::max(error, Distance({300 * s * s * t - 120 * s * t * t + 120 * t * t * t,
                                          540 * s * s * t - 420 * s * t * t + 20 * t * t * t},
                                         cubicPoints));
    }
    Check(error <= .101 && !cubic.WasFlatteningLimited(), "inflected cubic approximation meets tolerance");

    PainterPath overshoot;
    overshoot.MoveTo({0, 0});
    overshoot.CubicTo({400, 0}, {-400, 0}, {1, 0});
    const auto& overshootPoints = overshoot.Flatten(.1f)[0].m_Points;
    Check(std::any_of(overshootPoints.begin(), overshootPoints.end(), [](Point p) { return p.m_X > 50; }) &&
              std::any_of(overshootPoints.begin(), overshootPoints.end(), [](Point p) { return p.m_X < -50; }),
          "collinear control overshoot is preserved instead of collapsing to an endpoint line");

    PainterPath arc;
    arc.MoveTo({0, 0});
    arc.ArcTo({0, 0}, 80, 0, -std::numbers::pi_v<float>);
    const auto arcPoints = arc.Flatten(.05f)[0].m_Points;
    Check(arcPoints[1] == Point{80, 0}, "arc connects to start from current point");
    Check(std::abs(arcPoints.back().m_X + 80) < .001f && std::abs(arcPoints.back().m_Y) < .001f,
          "negative sweep ends at expected point");
    error = 0;
    for (int i = 0; i <= 1000; ++i)
    {
        const float angle = -std::numbers::pi_v<float> * static_cast<float>(i) / 1000;
        error = std::max(error, Distance({80 * std::cos(angle), 80 * std::sin(angle)}, arcPoints));
    }
    Check(error <= .051, "arc sagitta subdivision meets tolerance");
}

void InvalidDataAndBudgets()
{
    PainterPath path;
    const float infinity = std::numeric_limits<float>::infinity();
    Rejects<std::invalid_argument>([&] { path.MoveTo({infinity, 0}); }, "reject nonfinite points");
    Rejects<std::invalid_argument>([&] { path.Flatten(0); }, "reject zero tolerance");
    Rejects<std::invalid_argument>([&] { path.Flatten(infinity); }, "reject nonfinite tolerance");
    Rejects<std::invalid_argument>([&] { path.ArcTo({}, -1, 0, 1); }, "reject negative radius");
    Rejects<std::invalid_argument>([&] { path.ArcTo({}, 1, 0, 7); }, "reject multi-turn arcs");
    Check(path.GetRevision() == 0, "rejected commands preserve path revision");
    path.MoveTo({0, 0});
    for (int i = 1; i < 4096; ++i)
        path.LineTo({static_cast<float>(i), 0});
    Rejects<std::length_error>([&] { path.LineTo({0, 0}); }, "command budget is enforced");
    PainterPath expensive;
    expensive.ArcTo({}, 1e8f, 0, 2 * std::numbers::pi_v<float>);
    const auto& points = expensive.Flatten(1e-8f)[0].m_Points;
    Check(points.size() <= 65536 && expensive.WasFlatteningLimited(), "vertex budget bounds tiny tolerance work");
    Check(
        std::all_of(points.begin(), points.end(), [](Point p) { return std::isfinite(p.m_X) && std::isfinite(p.m_Y); }),
        "budgeted arc output remains finite");
}
} // namespace

int main()
{
    try
    {
        ContoursAndCache();
        AdaptiveCurves();
        InvalidDataAndBudgets();
        std::cout << "Painter paths PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Painter paths FAIL / " << error.what() << '\n';
        return 1;
    }
}
