#include <EStudioUI/CanvasScene.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace EStudioUI;
std::size_t s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

void Near(double actual, double expected, double tolerance, const std::string& message)
{
    Check(std::abs(actual - expected) <= tolerance, message);
}

template <typename Callback> void Throws(Callback callback, const std::string& message)
{
    bool threw = false;
    try
    {
        callback();
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    Check(threw, message);
}

void Curves()
{
    const double pi = std::numbers::pi;
    const auto arc = MakeCanvasArc({10, 20}, 5, -pi / 2, pi);
    const auto bounds = arc.Bounds();
    Near(bounds.m_X, 10, 1e-12, "arc bounds exclude unused left semicircle");
    Near(bounds.m_Y, 15, 1e-12, "arc bounds include start");
    Near(bounds.m_Width, 5, 1e-12, "arc bounds include cardinal extremum");
    Near(bounds.m_Height, 10, 1e-12, "arc bounds include end");
    Near(DistanceToItem(arc, {15, 20}), 0, 1e-12, "arc circumference hit");
    Near(DistanceToItem(arc, {5, 20}), std::sqrt(50.0), 1e-12, "excluded arc region measures endpoint distance");
    const auto reverse = MakeCanvasArc({10, 20}, 5, pi / 2, -pi);
    Near(reverse.Bounds().m_Width, 5, 1e-12, "negative sweep bounds");
    Near(DistanceToItem(reverse, {15, 20}), 0, 1e-12, "negative sweep hit");
    const auto full = MakeCanvasArc({}, 10, 1.2, -2 * pi);
    const auto fullBounds = full.Bounds();
    Near(fullBounds.m_X, -10, 1e-12, "full arc minimum");
    Near(fullBounds.m_Width, 20, 1e-12, "full arc width");
    Throws([&] { MakeCanvasArc({}, 1, 0, 2 * pi + 0.01); }, "arc rejects more than one revolution");

    const auto shortArc = MakeCanvasArc({30, 40}, 100, pi / 12, pi / 12);
    Check(shortArc.Bounds().Contains({30, 40}), "short arc bounds include its indexed center handle");
    CanvasScene scene;
    const auto arcId = scene.Add(shortArc);
    std::vector<CanvasItemId> hits;
    scene.Query({29, 39, 2, 2}, hits);
    Check(hits == std::vector<CanvasItemId>{arcId}, "arc center is discoverable through the scene BVH");
    const auto arcGroup = MakeCanvasGroup({1000, 2000}, {shortArc});
    std::vector<CanvasPoint> centerSnaps;
    GeometrySnapPoints(arcGroup, {1029, 2039, 2, 2}, centerSnaps);
    Check(centerSnaps == std::vector<CanvasPoint>{{1030, 2040}}, "bounded group snap discovers distant arc center");

    std::vector<CanvasPoint> points;
    Check(FlattenGeometry(arc, 0.001, points), "arc tolerance achieved");
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const CanvasPoint midpoint{(points[i - 1].m_X + points[i].m_X) / 2, (points[i - 1].m_Y + points[i].m_Y) / 2};
        Check(DistanceToItem(arc, midpoint) <= 0.00101, "arc chord sagitta respects tolerance");
    }
    Check(!FlattenGeometry(full, 1e-12, points, 8) && points.size() == 8, "arc output has a strict point budget");
    Near(std::hypot(points.front().m_X - points.back().m_X, points.front().m_Y - points.back().m_Y), 0, 1e-12,
         "full arc remains closed when its quality budget is exhausted");

    const auto spline = MakeCanvasSpline({100, 200}, {{0, 0}, {0, 100}, {100, 100}, {100, 0}});
    Check(spline.Bounds() == CanvasBounds{100, 200, 100, 100}, "Bezier bounds conservatively contain control hull");
    Near(DistanceToItem(spline, {150, 275}, 0.0001), 0, 1e-9, "cubic midpoint hit");
    Check(DistanceToItem(spline, {150, 225}) > 40, "curve picking excludes empty control-hull interior");
    Check(FlattenGeometry(spline, 0.01, points), "adaptive cubic subdivision achieves requested tolerance");
    const auto pathApproximation = MakeCanvasPath({}, points);
    for (int i = 0; i <= 1000; ++i)
    {
        const double t = static_cast<double>(i) / 1000;
        const CanvasPoint sample{100 + 300 * t * t - 200 * t * t * t, 200 + 300 * t * (1 - t)};
        Check(DistanceToItem(pathApproximation, sample) <= 0.01001, "dense cubic samples lie near flattened curve");
    }
    Check(!FlattenGeometry(spline, 1e-12, points, 5) && points.size() <= 5, "cubic subdivision honors point cap");
    Check(points.front() == CanvasPoint{100, 200} && points.back() == CanvasPoint{200, 200},
          "budgeted cubic output retains whole-curve endpoints");
    Throws([] { MakeCanvasSpline({}, {{0, 0}, {1, 1}, {2, 2}}); }, "cubic chain requires 3n+1 controls");
    const auto moved = TranslateGeometry(spline, {500, -100});
    Check(moved.m_Data == spline.m_Data && moved.Bounds() == CanvasBounds{600, 100, 100, 100},
          "curve translation shares immutable controls");
    Check(MakeCanvasSpline({100, 200}, {{0, 0}, {0, 100}, {100, 100}, {100, 0}}) == spline,
          "independently allocated curves compare by content");
    GeometrySnapPoints(spline, points);
    Check(std::find(points.begin(), points.end(), CanvasPoint{150, 275}) != points.end(), "cubic midpoint snap");
}

void PathsAndText()
{
    const auto open = MakeCanvasPath({10, 20}, {{0, 0}, {10, 0}, {10, 10}});
    const auto closed = MakeCanvasPath({10, 20}, {{0, 0}, {10, 0}, {10, 10}}, true);
    Near(DistanceToItem(closed, {15, 25}), 0, 1e-12, "closed polyline hits its closing segment");
    Check(DistanceToItem(open, {15, 25}) == 5, "open polyline excludes closing segment");
    std::vector<CanvasPoint> points;
    Check(FlattenGeometry(closed, 1, points) && points.size() == 4 && points.front() == points.back(),
          "closed paths explicitly repeat first vertex");
    Throws([] { MakeCanvasPath({}, {{0, 0}, {1, 0}}, true); }, "closed path requires at least three vertices");
    Throws([] { MakeCanvasPath({}, std::vector<CanvasPoint>(65537)); }, "curve point limit is enforced");
    const auto text = MakeCanvasText({10, 20, 100, 40}, "Diameter \xCE\xA6 = 12", 18);
    Check(text.Bounds() == CanvasBounds{10, 20, 100, 40}, "text uses explicit clipping bounds");
    Check(DistanceToItem(text, {50, 40}) == 0, "text box is selectable throughout its content area");
    Check(DistanceToItem(text, {115, 40}) == 5, "text exterior hit distance");
    const auto moved = TranslateGeometry(text, {5, 10});
    Check(moved.m_Data == text.m_Data && moved.Bounds() == CanvasBounds{15, 30, 100, 40},
          "text translation moves both bounds corners and shares content");
    Throws([] { MakeCanvasText({}, "\xED\xA0\x80"); }, "UTF-8 surrogate rejected");
    Throws([] { MakeCanvasText({}, "\xF4\x90\x80\x80"); }, "UTF-8 code point above Unicode maximum rejected");
    Throws([] { MakeCanvasText({}, "\xC0\x80"); }, "overlong UTF-8 rejected");
    Throws([] { MakeCanvasText({}, "first\nsecond"); }, "annotations are single line");
    Throws([] { MakeCanvasText({}, "valid", 0); }, "font size must be positive");
    Check(GeometryBytes({CanvasPrimitive::Line, {}, {1, 1}}) == 0, "simple geometry retains no payload allocation");
    Check(GeometryBytes(text) >= sizeof(CanvasGeometryData), "text payload is included in memory accounting");
}

void Groups()
{
    const std::vector<CanvasGeometry> children{{CanvasPrimitive::Line, {0, 0}, {10, 0}},
                                               {CanvasPrimitive::Circle, {100, 100}, {}, 10},
                                               MakeCanvasText({5, 10, 20, 10}, "Label")};
    const auto group = MakeCanvasGroup({1000, 2000}, children, {2, 3, 4});
    Check(group.m_Data->m_GroupIndex != nullptr, "group factory builds immutable spatial index");
    Check(group.Bounds() == CanvasBounds{1000, 2000, 110, 110}, "group bounds include local children and offset");
    Near(DistanceToItem(group, {1005, 2000}), 0, 1e-12, "group hit translates into child coordinates");
    Near(DistanceToItem(group, {1100, 2100}), 10, 1e-12, "group nearest-distance search honors hollow shapes");
    Near(DistanceToItem(group, {1010, 2015}), 0, 1e-12, "group text content is selectable");
    Near(DistanceToItem(group, {900, 2000}), 100, 1e-12, "group nearest search handles exterior points");
    std::vector<std::size_t> indices;
    QueryCanvasGroup(group, {995, 1995, 120, 120}, indices);
    Check(indices == std::vector<std::size_t>{0, 1, 2}, "group queries preserve original painter order");
    QueryCanvasGroup(group, {1089, 2089, 22, 22}, indices);
    Check(indices == std::vector<std::size_t>{1}, "group child BVH excludes distant items");
    const auto nested = MakeCanvasGroup({-100, -200}, {group}, {8});
    Check(nested.Bounds() == CanvasBounds{900, 1800, 110, 110}, "nested group offsets compose");
    Near(DistanceToItem(nested, {905, 1800}), 0, 1e-12, "nested group distance composes transforms");
    std::vector<CanvasPoint> snaps;
    GeometrySnapPoints(nested, {899, 1799, 12, 2}, snaps);
    Check(snaps.size() == 3 && std::find(snaps.begin(), snaps.end(), CanvasPoint{905, 1800}) != snaps.end(),
          "bounded group snapping traverses local BVHs and filters points");
    Check(group == MakeCanvasGroup({1000, 2000}, children, {2, 3, 4}),
          "group equality ignores distinct index allocations");
    auto copiedData = std::make_shared<CanvasGeometryData>(*group.m_Data);
    auto manual = group;
    manual.m_Data = copiedData;
    Check(group == manual && *group.m_Data == *manual.m_Data, "copied payload is logically equal");
    QueryCanvasGroup(manual, {1089, 2089, 22, 22}, indices);
    Check(indices == std::vector<std::size_t>{1}, "copied raw payload safely falls back from stale index");
    copiedData->m_Children[1] = {CanvasPrimitive::Circle, {500, 500}, {}, 10};
    Check(manual.Bounds() == CanvasBounds{1000, 2000, 510, 510},
          "copied raw payload never reuses foreign cached bounds");
    QueryCanvasGroup(manual, {1489, 2489, 22, 22}, indices);
    Check(indices == std::vector<std::size_t>{1}, "raw group fallback queries updated unsubmitted children");
    copiedData->m_GroupIndex.reset();
    Check(manual.Bounds() == CanvasBounds{1000, 2000, 510, 510}, "raw group without an index remains validated");
    Check(GeometryBytes(group) > children.size() * sizeof(CanvasGeometry) + sizeof(CanvasGeometryData),
          "group memory accounting includes index and child payloads");
    Throws([] { MakeCanvasGroup({}, {}); }, "empty group rejected");
    Throws([&] { MakeCanvasGroup({}, children, {1}); }, "group child styles must match child count");
    auto deep = children.front();
    for (int i = 0; i < 32; ++i)
        deep = MakeCanvasGroup({}, {deep});
    Throws([&] { MakeCanvasGroup({}, {deep}); }, "cached groups preserve recursive depth limits");
    auto invalidData = std::make_shared<CanvasGeometryData>();
    CanvasGeometry invalid{CanvasPrimitive::Line, {}, {1, 1}, 0, invalidData};
    Throws([&] { invalid.Bounds(); }, "simple primitives cannot hide an unvalidated recursive payload");
    auto recursiveData = std::make_shared<CanvasGeometryData>();
    CanvasGeometry recursive{CanvasPrimitive::Group, {}, {}, 0, recursiveData};
    recursiveData->m_Children.push_back(recursive);
    Throws([&] { recursive.Bounds(); }, "cyclic raw group payload is rejected by the depth bound");
    recursiveData->m_Children.clear();
}

void LargeGroup()
{
    constexpr std::size_t count = 200000;
    std::vector<CanvasGeometry> children;
    children.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const double x = static_cast<double>(i % 1000) * 10;
        const double y = static_cast<double>(i / 1000) * 10;
        children.push_back({CanvasPrimitive::Line, {x, y}, {x + 2, y}});
    }
    const auto start = std::chrono::steady_clock::now();
    const auto group = MakeCanvasGroup({100, 200}, std::move(children));
    const auto built = std::chrono::steady_clock::now();
    std::vector<std::size_t> indices;
    for (int i = 0; i < 1000; ++i)
    {
        QueryCanvasGroup(group, {100, 200, 2, 0}, indices);
        Check(indices == std::vector<std::size_t>{0}, "200k group small query returns one child");
        Check(DistanceToItem(group, {101, 200}) == 0, "200k group hit uses local candidates");
        Check(TranslateGeometry(group, {1, 1}).m_Data == group.m_Data, "200k group movement retains immutable index");
    }
    const auto queried = std::chrono::steady_clock::now();
    std::vector<CanvasPoint> snaps;
    GeometrySnapPoints(group, {99, 199, 4, 2}, snaps);
    Check(snaps.size() == 3, "200k group bounded snap sees only nearby endpoints and midpoint");
    Check(GeometryBytes(group) < 128 * 1024 * 1024, "200k group index and geometry fit one command budget");
    CanvasScene scene;
    const auto id = scene.Add(group);
    std::vector<CanvasItemId> hits;
    scene.Query({100, 200, 2, 0}, hits);
    Check(hits == std::vector<CanvasItemId>{id}, "group remains one outer-scene identity");
    std::cout << "200k group build " << std::chrono::duration<double>(built - start).count()
              << " s; 1000 bounded queries/hits/translations " << std::chrono::duration<double>(queried - built).count()
              << " s\n";
}
} // namespace

int main()
{
    try
    {
        Curves();
        PathsAndText();
        Groups();
        LargeGroup();
        std::cout << "Canvas geometry: " << s_Checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Canvas geometry failure: " << error.what() << '\n';
        return 1;
    }
}
