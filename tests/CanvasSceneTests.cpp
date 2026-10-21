#include <EStudioUI/CanvasScene.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
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

bool Less(CanvasItemId a, CanvasItemId b)
{
    return a.m_Index == b.m_Index ? a.m_Generation < b.m_Generation : a.m_Index < b.m_Index;
}

void Oracle(const CanvasScene& scene, CanvasBounds bounds)
{
    std::vector<CanvasItemId> actual;
    std::vector<CanvasItemId> expected;
    scene.Query(bounds, actual);
    for (const auto& item : scene.GetItems())
        if (item.m_Geometry.Bounds().Intersects(bounds))
            expected.push_back(item.m_Id);
    std::sort(actual.begin(), actual.end(), Less);
    std::sort(expected.begin(), expected.end(), Less);
    Check(actual == expected, "BVH query matches brute-force oracle after mutations");
}

void GeometryAndIdentity()
{
    const CanvasGeometry rectangle{CanvasPrimitive::Rectangle, {30, 40}, {10, 20}};
    Check(rectangle.Bounds() == CanvasBounds{10, 20, 20, 20}, "reversed rectangle corners normalize");
    Check(rectangle.Bounds().Contains({30, 40}), "closed bounds contain their maximum edges");
    Check(rectangle.Bounds().Intersects({30, 40, 0, 0}), "point queries include touching boundaries");
    Check(DistanceToItem(rectangle, {20, 30}) == 10, "rectangle hit distance measures its outline");
    Check(DistanceToItem(rectangle, {30, 50}) == 10, "rectangle exterior distance");
    Check(std::abs(DistanceToItem({CanvasPrimitive::Line, {0, 0}, {3, 4}}, {3, 0}) - 2.4) < 1e-12,
          "line picking uses the segment rather than its bounds");
    Check(DistanceToItem({CanvasPrimitive::Line, {}, {1e308, 1e308}}, {5e307, 5e307}) == 0,
          "line distance avoids overflow for large finite bounds");
    Check(DistanceToItem({CanvasPrimitive::Line, {2, 3}, {2, 3}}, {2, 8}) == 5, "degenerate line distance");
    Check(DistanceToItem({CanvasPrimitive::Circle, {10, 10}, {}, 5}, {10, 10}) == 5,
          "circle picking uses the circumference");
    Check(DistanceToItem({CanvasPrimitive::Circle, {10, 10}, {}, 5}, {15, 10}) == 0, "circle boundary distance");
    const auto translated = TranslateGeometry(rectangle, {1e12, -1e12});
    Check(translated.m_A == CanvasPoint{1e12 + 30, -1e12 + 40}, "translation retains double world precision");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    Throws([&] { CanvasGeometry{CanvasPrimitive::Line, {nan, 0}, {}}.Bounds(); }, "NaN geometry rejected");
    Throws([] { CanvasGeometry{CanvasPrimitive::Circle, {}, {}, -1}.Bounds(); }, "negative radius rejected");
    Throws([] { CanvasGeometry{CanvasPrimitive::Circle, {}, {}, 1e308}.Bounds(); }, "overflowing diameter rejected");
    Throws([&] { DistanceToItem(rectangle, {nan, 0}); }, "NaN hit point rejected");
    CanvasScene scene;
    scene.Reserve(5);
    Check(!CanvasItemId{} && scene.Get({}) == nullptr && scene.Size() == 0, "empty identities and empty scene");
    const auto first = scene.Add(rectangle, 5);
    const auto second = scene.Add({CanvasPrimitive::Line, {-10, 0}, {-10, 0}}, 7);
    const auto third = scene.Add({CanvasPrimitive::Circle, {100, 200}, {}, 10});
    Check(scene.Get(first)->m_Style == 5 && scene.Get(second)->m_Style == 7, "shared style indices retained");
    Check(scene.Get(first)->m_Order < scene.Get(second)->m_Order, "painter ordering follows insertion");
    Check(scene.SetOrder(first, 50), "undo can restore painter ordering");
    const auto fourth = scene.Add(rectangle);
    Check(scene.Get(fourth)->m_Order == 51, "new items follow restored painter order");
    Check(scene.GetBounds() == CanvasBounds{-10, 0, 120, 210}, "BVH aggregate bounds");
    Oracle(scene, {-10, 0, 0, 0});
    Check(scene.Remove(second) && !scene.Remove(second) && scene.Get(second) == nullptr, "deleted identities expire");
    Check(scene.Get(first) && scene.Get(third) && scene.Get(fourth), "dense compaction preserves other identities");
    const auto replacement = scene.Add(rectangle);
    Check(replacement.m_Index == second.m_Index && replacement.m_Generation != second.m_Generation,
          "slot reuse changes its lifetime generation");
    Check(!scene.Update(second, rectangle) && !scene.SetStyle(second, 1) && !scene.SetSelected(second, true),
          "stale IDs cannot modify replacements");
    auto revision = scene.GetRevision();
    Check(scene.Update(first, rectangle) && scene.GetRevision() == revision, "unchanged geometry preserves revision");
    Check(scene.SetSelected(first, true) && scene.SetStyle(first, 9),
          "selected state and style can change independently");
    Check(scene.Get(first)->m_Selected && scene.Get(first)->m_Style == 9, "item presentation state retained");
    Check(scene.GetRevision() == revision + 2, "only actual content changes increment revision");
    std::vector<CanvasItemId> result{first};
    scene.Query({1000, 1000, 1, 1}, result);
    Check(result.empty(), "queries replace existing output");
    Throws([&] { scene.Query({0, 0, -1, 1}, result); }, "negative query bounds rejected");
    Throws([&] { scene.Query({0, 0, nan, 1}, result); }, "NaN query bounds rejected");
    revision = scene.GetRevision();
    Throws([&] { scene.Update(first, {CanvasPrimitive::Circle, {}, {}, -1}); }, "invalid update rejected");
    Check(scene.GetRevision() == revision && scene.Get(first)->m_Geometry == rectangle,
          "invalid geometry leaves the item unchanged");
    scene.Clear();
    Check(scene.Size() == 0 && scene.GetBounds() == CanvasBounds{} && !scene.Get(first),
          "clear empties and invalidates");
    const auto afterClear = scene.Add(rectangle);
    Check(afterClear != first && afterClear != replacement && afterClear != third && afterClear != fourth,
          "clear cannot resurrect any previous identity");
    scene.Remove(afterClear);
    scene.Query({}, result);
    Check(result.empty() && scene.GetLastQueryVisits() == 0, "query of empty tree visits no nodes");
}

void RandomizedMutationOracle()
{
    CanvasScene scene;
    std::mt19937 random(0xCA5CADE);
    std::uniform_real_distribution<double> coordinate(-10000, 10000);
    std::uniform_real_distribution<double> extent(0, 100);
    std::vector<CanvasItemId> ids;
    const auto geometry = [&]()
    {
        const CanvasPoint first{coordinate(random), coordinate(random)};
        return CanvasGeometry{static_cast<CanvasPrimitive>(random() % 3),
                              first,
                              {first.m_X + extent(random), first.m_Y + extent(random)},
                              extent(random)};
    };
    for (int i = 0; i < 5000; ++i)
        ids.push_back(scene.Add(geometry()));
    for (int mutation = 0; mutation < 3000; ++mutation)
    {
        const auto slot = random() % ids.size();
        if (mutation % 3 == 0)
        {
            const auto retired = ids[slot];
            Check(scene.Remove(retired), "random remove finds live identity");
            ids[slot] = scene.Add(geometry());
            Check(!scene.Get(retired), "random replacement does not resurrect old handle");
        }
        else
            Check(scene.Update(ids[slot], geometry()), "random update finds live identity");
        if (mutation % 5 == 0)
            Oracle(scene, {coordinate(random), coordinate(random), 350, 350});
    }
    Oracle(scene, {-20000, -20000, 40000, 40000});
    for (const auto id : ids)
        Check(scene.Remove(id), "complete random removal preserves remaining handles");
    Check(scene.Size() == 0, "removing all randomized items empties the tree");
}

void OverlappingAndDegenerateGeometry()
{
    CanvasScene scene;
    scene.Reserve(20000);
    std::vector<CanvasItemId> ids;
    for (int i = 1; i <= 20000; ++i)
        ids.push_back(scene.Add({CanvasPrimitive::Rectangle,
                                 {-static_cast<double>(i), -static_cast<double>(i)},
                                 {static_cast<double>(i), static_cast<double>(i)}}));
    std::vector<CanvasItemId> result;
    scene.Query({}, result);
    Check(result.size() == 20000 && scene.GetLastQueryVisits() == 39999,
          "overlapping shapes return every candidate without truncation");
    for (std::size_t i = 0; i < ids.size(); i += 2)
        scene.Remove(ids[i]);
    Oracle(scene, {-10, -10, 20, 20});
    scene.Clear();
    for (int i = 0; i < 20000; ++i)
        scene.Add({CanvasPrimitive::Line, {1e12, -1e12}, {1e12, -1e12}});
    scene.Query({1e12, -1e12, 0, 0}, result);
    Check(result.size() == 20000, "coincident degenerate lines stay queryable at large origins");
    scene.Query({1e12 + 1, -1e12, 0, 0}, result);
    Check(result.empty() && scene.GetLastQueryVisits() == 1, "root rejects an outside point in one visit");
    scene.Clear();
    scene.Add({CanvasPrimitive::Line, {-1e308, 0}, {-1e308, 0}});
    const auto revision = scene.GetRevision();
    Throws([&] { scene.Add({CanvasPrimitive::Line, {1e308, 0}, {1e308, 0}}); },
           "overflowing aggregate scene bounds rejected before insertion");
    Check(scene.Size() == 1 && scene.GetRevision() == revision, "invalid aggregate leaves the scene unchanged");
}

void TransactionalReplacement()
{
    CanvasScene scene;
    auto* observed = &scene;
    std::vector<CanvasItemId> retired;
    for (int i = 0; i < 100; ++i)
        retired.push_back(scene.Add({CanvasPrimitive::Line, {static_cast<double>(i), 0}, {}}));
    for (int i = 0; i < 20; ++i)
    {
        const auto id = scene.GetItems().back().m_Id;
        scene.Remove(id);
        retired.push_back(scene.Add({CanvasPrimitive::Circle, {10, 20}, {}, 30}));
    }
    scene.Clear();
    retired.push_back(scene.Add({CanvasPrimitive::Line, {5, 6}, {7, 8}}));
    const auto revision = scene.GetRevision();

    CanvasScene candidate;
    const CanvasGeometry geometry{CanvasPrimitive::Rectangle, {100, 200}, {300, 400}};
    const auto id = candidate.Add(geometry, 7);
    candidate.SetSelected(id, true);
    candidate.SetOrder(id, 42);
    scene.ReplaceWith(candidate);
    Check(observed == &scene && observed->Size() == 1, "scene replacement preserves the observed object address");
    Check(scene.GetRevision() == revision + 1, "replacement advances the existing scene revision");
    const auto& restored = scene.GetItems().front();
    Check(restored.m_Geometry == geometry && restored.m_Style == 7 && restored.m_Selected && restored.m_Order == 42,
          "replacement retains geometry, styles, selection and painter order");
    Check(scene.Get(restored.m_Id) == &restored, "replacement remaps dense and slot identities together");
    Oracle(scene, {150, 250, 10, 10});
    for (int i = 0; i < 150; ++i)
        scene.Add({CanvasPrimitive::Line, {}, {10, 10}});
    for (const auto old : retired)
        Check(!scene.Get(old), "growing a smaller replacement never revives an old or cleared identity");

    const auto oldReplacement = scene.GetItems().front().m_Id;
    const auto secondRevision = scene.GetRevision();
    CanvasScene empty;
    scene.ReplaceWith(empty);
    Check(scene.Size() == 0 && scene.GetRevision() == secondRevision + 1, "empty replacement advances revision");
    for (int i = 0; i < 150; ++i)
        scene.Add({CanvasPrimitive::Line, {}, {10, 10}});
    Check(!scene.Get(oldReplacement), "empty replacement preserves the generation floor for future slots");
    for (const auto old : retired)
        Check(!scene.Get(old), "repeated replacement cannot revive historical identities");
    const auto beforeSelf = scene.GetRevision();
    const auto live = scene.GetItems().front().m_Id;
    Throws([&] { scene.ReplaceWith(scene); }, "self replacement rejected");
    Check(scene.Size() == 150 && scene.GetRevision() == beforeSelf && scene.Get(live),
          "rejected replacement leaves content and identity unchanged");
}

void LargeScene()
{
    using Clock = std::chrono::steady_clock;
    CanvasScene scene;
    constexpr std::size_t count = 200000;
    scene.Reserve(count);
    const auto start = Clock::now();
    for (std::size_t i = 0; i < count; ++i)
    {
        const double x = static_cast<double>(i % 1000) * 20;
        const double y = static_cast<double>(i / 1000) * 20;
        scene.Add({CanvasPrimitive::Rectangle, {x, y}, {x + 8, y + 8}}, static_cast<std::uint32_t>(i % 4));
    }
    const auto built = Clock::now();
    Check(scene.Size() == count, "200,000 lightweight items stored");
    std::vector<CanvasItemId> visible;
    std::size_t totalVisits = 0;
    std::size_t maxVisits = 0;
    constexpr std::size_t queries = 1000;
    for (std::size_t i = 0; i < queries; ++i)
    {
        const double x = static_cast<double>((i * 37) % 980) * 20;
        const double y = static_cast<double>((i * 17) % 180) * 20;
        scene.Query({x, y, 199, 199}, visible);
        Check(visible.size() == 100, "local viewport returns its 100 grid items");
        const auto visits = scene.GetLastQueryVisits();
        totalVisits += visits;
        maxVisits = std::max(maxVisits, visits);
        Check(visits < 5000, "local viewport prunes the 200,000-item BVH");
    }
    const auto queried = Clock::now();
    for (int i = 0; i < 1000; ++i)
    {
        const auto id = scene.GetItems()[static_cast<std::size_t>(i) * 137].m_Id;
        scene.Update(id, TranslateGeometry(scene.Get(id)->m_Geometry, {-1e12, 1e12}));
    }
    Oracle(scene, {-1e12, 1e12, 1000, 1000});
    Oracle(scene, {500, 500, 1000, 1000});
    std::cout << "200000-item BVH: build_ms=" << std::chrono::duration<double, std::milli>(built - start).count()
              << " query_mean_us=" << std::chrono::duration<double, std::micro>(queried - built).count() / queries
              << " mean_visits=" << totalVisits / queries << " max_visits=" << maxVisits << '\n';
}
} // namespace

int main()
{
    try
    {
        GeometryAndIdentity();
        RandomizedMutationOracle();
        OverlappingAndDegenerateGeometry();
        TransactionalReplacement();
        LargeScene();
        std::cout << "Canvas scene: " << s_Checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Canvas scene failure: " << exception.what() << '\n';
        return 1;
    }
}
