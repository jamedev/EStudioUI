#include "CanvasProbe.h"
#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/Window.h>
#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
using Clock = std::chrono::steady_clock;

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Canvas GPU: ") + message);
}

double Milliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::vector<unsigned char> Pixels(int width, int height)
{
    std::vector<unsigned char> result(static_cast<std::size_t>(width * height * 4));
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}

void Compare(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b)
{
    Check(a.size() == b.size(), "matching framebuffer sizes");
    for (std::size_t i = 0; i < a.size(); ++i)
        Check(std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) <= 1, "retained/full canvas pixels match");
}

void Populate(CanvasScene& scene, std::size_t count)
{
    scene.Clear();
    scene.Reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const double x = static_cast<double>(i % 1000) * 40;
        const double y = static_cast<double>(i / 1000) * 40;
        scene.Add({CanvasPrimitive::Circle, {x, y}, {}, 8});
    }
}

CanvasWidget& Prepare(Window& window)
{
    std::string error;
    Check(window.Ui().SetStyleSheet(
              "window { background: #101828; } canvas { background: #101828; padding: 0; border-width: 0; }", error),
          "canvas probe window CSS");
    auto& canvas = window.Ui().Root().Add<CanvasWidget>();
    canvas.SetWidth(SizeRule::Fill());
    canvas.SetHeight(SizeRule::Fill());
    canvas.SetGridVisible(false);
    Check(
        canvas.SetItemStyleSheet("canvas-item { background: #263b50; color: #55eebb; border: 2px solid #55eebb; } "
                                 "canvas-item:selected { background: #604a20; border-color: #ffca66; color: #ffca66; }",
                                 error),
        "canvas probe item CSS");
    return canvas;
}
} // namespace

void VerifyCanvas(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    std::filesystem::create_directories(output);
    WindowOptions options;
    options.m_Width = 800;
    options.m_Height = 600;
    options.m_Headless = headless;
    Window window(options, assets / "NotoSans.ttf");
    auto& canvas = Prepare(window);
    auto& scene = canvas.GetScene();
    const auto line = scene.Add({CanvasPrimitive::Line, {-100, 0}, {100, 0}, 0});
    scene.Add({CanvasPrimitive::Circle, {0, -100}, {}, 35});
    scene.Add({CanvasPrimitive::Rectangle, {-80, 70}, {80, 140}, 0});
    canvas.SceneChanged();
    window.RenderFrame();
    auto color = window.Graphics().ReadPixel({400, 300});
    Check(color.m_G > 0.8f && color.m_B > 0.5f, "scene line reaches expected screen point");
    const auto unchanged = window.Graphics().GetStats().m_Shapes;
    window.RenderFrame();
    Check(window.Graphics().GetStats().m_RepaintPixels == 0, "unchanged canvas retains framebuffer");
    for (int frame = 0; frame < 10; ++frame)
    {
        if (frame == 2)
            scene.SetSelected(line, true);
        if (frame == 4)
            scene.Update(line, {CanvasPrimitive::Line, {-100, 20}, {100, 35}, 0});
        if (frame == 6)
            scene.Add({CanvasPrimitive::Circle, {80, 10}, {}, 25});
        if (frame == 8)
            scene.Remove(line);
        canvas.SetCamera({frame * 0.125, frame * 0.25}, 1 + frame * 0.02);
        canvas.SceneChanged();
        window.RenderFrame();
        const auto retained = Pixels(800, 600);
        window.Graphics().SetPartialRedrawEnabled(false);
        window.RenderFrame();
        Compare(retained, Pixels(800, 600));
        window.Graphics().SetPartialRedrawEnabled(true);
    }
    window.Graphics().SavePpm(output / "canvas-primitives.ppm");
    for (const double extent : {1e12, 1e14})
    {
        scene.Clear();
        scene.Add({CanvasPrimitive::Line, {-extent, 0}, {extent, 0}, 0});
        canvas.SetCamera({}, 1e6);
        canvas.SceneChanged();
        window.RenderFrame();
        Check(window.Graphics().ReadPixel({2, 300}).m_G > 0.8f && window.Graphics().ReadPixel({798, 300}).m_G > 0.8f,
              "very long horizontal lines reach both viewport edges at maximum zoom");
        scene.Clear();
        scene.Add({CanvasPrimitive::Line, {0, -extent}, {0, extent}, 0});
        canvas.SceneChanged();
        window.RenderFrame();
        Check(window.Graphics().ReadPixel({400, 2}).m_G > 0.8f && window.Graphics().ReadPixel({400, 598}).m_G > 0.8f,
              "very long vertical lines reach both viewport edges at maximum zoom");
    }
    Populate(scene, 200000);
    canvas.SetCamera({400, 300}, 1);
    canvas.SceneChanged();
    window.RenderFrame();
    Check(scene.Size() == 200000 && canvas.GetVisibleItemCount() > 100 && canvas.GetVisibleItemCount() < 600,
          "200000-item scene submits only viewport candidates");
    Check(canvas.GetQueryVisitCount() < 12000, "BVH prunes far-away items");
    Check(window.Graphics().GetStats().m_Shapes < 700 && unchanged < 20, "GPU stream scales with visible objects");
    const auto far = scene.GetItems().back().m_Id;
    scene.SetSelected(far, true);
    canvas.SceneChanged();
    window.RenderFrame();
    Check(window.Graphics().GetStats().m_RepaintPixels == 0, "offscreen edit produces no analytic GPU work");
    window.Graphics().SavePpm(output / "canvas-200k.ppm");
    std::ofstream report(output / "canvas-verification.txt");
    report << "Canvas GPU PASS\nDriver: " << window.Graphics().GetDriverDescription() << '\n'
           << "10 retained/full RGBA comparisons; unchanged and offscreen edit zero repaint.\n"
           << "200000 items / visible " << canvas.GetVisibleItemCount() << " / BVH nodes "
           << canvas.GetQueryVisitCount() << " / recorded shapes " << window.Graphics().GetStats().m_Shapes << '\n';
}

void ProfileCanvas(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    std::filesystem::create_directories(output);
    WindowOptions options;
    options.m_Width = 800;
    options.m_Height = 600;
    options.m_Headless = headless;
    Window window(options, assets / "NotoSans.ttf");
    auto& canvas = Prepare(window);
    window.Graphics().SetGpuTimingEnabled(true);
    std::ofstream csv(output / "canvas-profile.csv");
    csv << "items,build_ms,query_median_us,node_visits,visible_items,completed_frame_median_ms,shapes,draws,repaint_"
           "pixels\n";
    for (const auto count : {20000u, 200000u})
    {
        auto start = Clock::now();
        Populate(canvas.GetScene(), count);
        const double buildMs = Milliseconds(start);
        canvas.SceneChanged();
        std::vector<double> queryTimes;
        std::vector<CanvasItemId> hits;
        std::size_t visits = 0;
        for (int i = 0; i < 100; ++i)
        {
            start = Clock::now();
            canvas.GetScene().Query({i * 0.25, 0, 800, 600}, hits);
            queryTimes.push_back(Milliseconds(start) * 1000);
            visits = canvas.GetScene().GetLastQueryVisits();
        }
        std::vector<double> times;
        for (int i = 0; i < 36; ++i)
        {
            canvas.SetCamera({400 + i * 1.25, 300 + i * 0.5}, 1);
            start = Clock::now();
            window.RenderFrame();
            window.Graphics().ReadPixel({1, 1});
            if (i >= 4)
                times.push_back(Milliseconds(start));
        }
        std::sort(queryTimes.begin(), queryTimes.end());
        std::sort(times.begin(), times.end());
        const auto& stats = window.Graphics().GetStats();
        csv << count << ',' << buildMs << ',' << queryTimes[queryTimes.size() / 2] << ',' << visits << ','
            << canvas.GetVisibleItemCount() << ',' << times[times.size() / 2] << ',' << stats.m_Shapes << ','
            << stats.m_DrawCalls << ',' << stats.m_RepaintPixels << '\n';
    }
    std::ofstream report(output / "canvas-profile.txt");
    report << "Driver: " << window.Graphics().GetDriverDescription() << '\n'
           << "800x600, 4 warmup + 32 completed pan frames; 100 isolated CPU BVH queries per scene.\n"
           << "Same visible density for 20k/200k. Times are host measurements, not Windows GPU utilization.\n"
           << "No full-scene-overview/LOD throughput claim: drawing all visible geometry remains O(visible).\n";
    std::cout << "Canvas profile written to " << output << '\n';
}
} // namespace EStudioUI
