#include "GpuWorkload.h"
#include "GpuProbeWindow.h"

#include <EStudioUI/Button.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/TableWidget.h>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
void Check(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error("GPU workload: " + message);
}

std::vector<unsigned char> ReadFrame(int width, int height)
{
    std::vector<unsigned char> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    return bytes;
}

void VerifyPixels(Window& owner, const std::filesystem::path& assets, const std::filesystem::path& output)
{
    WindowOptions options;
    options.m_Width = 640;
    options.m_Height = 360;
    options.m_Headless = owner.IsHeadless();
    Window window(options, assets / "NotoSans.ttf");
    AllowOversizedGpuProbe(window);
    auto& ui = window.Ui();
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        window { background: #131c2b; }
        button { background: linear-gradient(110deg, #258bee, #b149b8); border: 1px solid #a8caff; border-radius: 15px; box-shadow: 0 7px 12px #0008, inset 0 1px 3px #fff5; }
        button::before { content: ""; inset: 0; background: linear-gradient(90deg, transparent, #fff3, transparent); border-radius: 15px; }
        button.no-layer::before { content: none; }
        #group { background: #27414a; border-radius: 25px; overflow: hidden; }
        label { font-size: 17px; }
    )CSS",
                           error),
          error);
    auto& back = ui.Root().Add<Widget>();
    back.SetFloatingBounds({40, 70, 180, 120});
    back.SetStyleProperty(StyleProperty::Background, Color{.2f, .7f, .5f, .4f});
    auto& button = ui.Root().Add<Button>("Moving GPU text");
    button.SetFloatingBounds({60, 90, 175, 52});
    auto& label = ui.Root().Add<Label>("Overlapping translucent caption");
    label.SetFloatingBounds({90, 125, 260, 50});
    label.SetStyleProperty(StyleProperty::Opacity, .65f);
    auto& group = ui.Root().Add<Widget>();
    group.SetId("group");
    group.SetFloatingBounds({370, 90, 220, 190});
    group.Add<Button>("Clipped").SetFloatingBounds({360, 120, 230, 50});
    group.Add<Label>("Rotated clip").SetFloatingBounds({390, 200, 160, 50});
    window.RenderFrame(ui.GetTime());
    Renderer reference(ui.GetFont());
    reference.SetPartialRedrawEnabled(false);
    int comparisons = 0;
    for (int phase = 0; phase < 2; ++phase)
    {
        if (phase)
        {
            window.SetSize(1280, 720);
            window.RenderFrame(ui.GetTime());
        }
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int width = viewport[2], height = viewport[3];
        for (int frame = 0; frame < 20; ++frame)
        {
            const double time = ui.GetTime() + 1.0 / 60;
            button.SetFloatingBounds({60 + frame * 4.3f, 90 + frame * .37f, 175, 52});
            button.SetStyleProperty(StyleProperty::Opacity, .4f + frame * .025f);
            Transform transform;
            transform.m_Operations.push_back({TransformOperation::Kind::Rotate, {static_cast<float>(frame - 10)}});
            group.SetStyleProperty(StyleProperty::Transform, transform);
            if (frame == 4)
                label.SetText("New glyphs @ # and a shorter caption");
            if (frame == 6)
                button.AddClass("no-layer");
            if (frame == 9)
                button.RemoveClass("no-layer");
            if (frame == 12)
                label.SetVisible(false);
            if (frame == 15)
                label.SetVisible(true);
            ui.SetTime(time);
            ui.Prepare(640, 360);
            const auto render = [&](Renderer& renderer)
            {
                renderer.BeginFrame(640, 360, width, height, ui.Root().GetStyle().m_Background);
                ui.Paint(renderer, time);
                renderer.EndFrame();
                ui.FrameSubmitted();
            };
            render(window.Graphics());
            const auto pixels = ReadFrame(width, height);
            const auto stats = window.Graphics().GetStats();
            render(reference);
            const auto expected = ReadFrame(width, height);
            for (std::size_t index = 0; index < pixels.size(); ++index)
                if (std::abs(static_cast<int>(pixels[index]) - static_cast<int>(expected[index])) > 1)
                    throw std::runtime_error("Retained/full pixel mismatch at phase " + std::to_string(phase) +
                                             " frame " + std::to_string(frame) + " byte " + std::to_string(index));
            if (frame > 0)
                Check(stats.m_RepaintPixels < stats.m_FramebufferPixels,
                      "local animation does not repaint the entire framebuffer");
            ++comparisons;
        }
    }
    window.Graphics().SavePpm(output / "retained-redraw.ppm");
    const std::array<std::array<int, 4>, 4> orders{{{{0, 1, 2, 3}}, {{2, 3, 0, 1}}, {{3, 1, 0, 2}}, {{0, 1, 2, 3}}}};
    const std::array<Color, 4> colors{{{1, .1f, .2f, .7f}, {.1f, 1, .2f, .5f}, {.1f, .3f, 1, .6f}, {1, .6f, .1f, .8f}}};
    for (const auto& order : orders)
    {
        const auto render = [&](Renderer& renderer)
        {
            renderer.BeginFrame(640, 360, 1280, 720, {.04f, .07f, .12f, 1});
            for (const int ownerIndex : order)
            {
                const Rect box{40 + ownerIndex * 48.0f, 50 + ownerIndex * 24.0f, 160, 130};
                renderer.PushState();
                renderer.SetWidgetPaint(box, {0, 0, 640, 360}, nullptr, false, 0,
                                        static_cast<std::uintptr_t>(ownerIndex + 1));
                renderer.DrawSolid(box, colors[static_cast<std::size_t>(ownerIndex)], {0, 0, 640, 360});
                renderer.PopState();
            }
            renderer.EndFrame();
        };
        render(window.Graphics());
        const auto pixels = ReadFrame(1280, 720);
        render(reference);
        Check(pixels == ReadFrame(1280, 720), "reordered translucent owners preserve painter order");
        ++comparisons;
    }
    for (const int selected : {0, 9, 1, 8})
    {
        const auto render = [&](Renderer& renderer)
        {
            const Rect clip{0, 0, 640, 360};
            renderer.BeginFrame(640, 360, 1280, 720, {.04f, .07f, .12f, 1});
            renderer.SetWidgetPaint(clip, clip, nullptr, false, 0, 5);
            for (int row = 0; row < 10; ++row)
            {
                const float y = 10 + row * 32.0f;
                renderer.DrawSolid({40, y, 460, 30}, {0, .4f, 1, row == selected ? .7f : 0}, clip);
                renderer.DrawSolid({40, y + 30, 460, 1}, {.4f, .4f, .4f, 1}, clip);
            }
            renderer.EndFrame();
        };
        render(window.Graphics());
        const auto pixels = ReadFrame(1280, 720);
        if (selected != 0)
            Check(window.Graphics().GetStats().m_RepaintPixels < 1280 * 720 / 2,
                  "selection changes retain grid pixels around transparent paint slots");
        render(reference);
        Check(pixels == ReadFrame(1280, 720), "transparent paint slots preserve selection pixels");
        ++comparisons;
    }
    std::ofstream report(output / "retained-pixels.txt");
    report << comparisons << " full-frame RGBA comparisons passed (maximum permitted channel difference: 1/255).\n"
           << "Moving geometry/shadows, translucent overlap and reordered owners, selection/transparent slots, "
              "changing glyphs, insertion/removal "
              "of pseudo layers, hidden controls, rotated rounded clips, and 1x/2x logical-to-physical scaling.\n";
}

void VerifyHidden(Window& owner, const std::filesystem::path& assets)
{
    WindowOptions options;
    options.m_Headless = owner.IsHeadless();
    options.m_Width = 640;
    options.m_Height = 360;
    Window window(options, assets / "NotoSans.ttf");
    auto& ui = window.Ui();
    auto& tabs = ui.Root().Add<TabWidget>();
    tabs.SetHeight(SizeRule::Fill());
    auto& animated = tabs.AddTab("Animated").Add<Button>("Hidden loop");
    animated.SetId("hidden-loop");
    auto& normal = tabs.AddTab("Static").Add<Button>("Static button");
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        window { background: #121b2b; }
        #hidden-loop { animation: loop 2s linear infinite alternate; }
        @keyframes loop { from { opacity: .3; } to { opacity: 1; } }
    )CSS",
                           error),
          error);
    window.RenderFrame(0);
    tabs.SetCurrentIndex(1);
    window.RenderFrame(.1);
    ui.SetTime(.2);
    Check(!ui.HasAnimations() && !ui.NeedsRepaint(), "hidden CSS timelines do not request frames on clock/input ticks");
    const auto b = normal.GetBounds();
    const Point point{b.m_X + b.m_Width * .5f, b.m_Y + b.m_Height * .5f};
    ui.PointerMove(point);
    window.RenderFrame(.2);
    const auto counters = ui.GetStats();
    for (int event = 1; event <= 60; ++event)
    {
        ui.SetTime(.2 + event / 1000.0);
        ui.PointerMove(point);
        Check(!ui.NeedsRepaint(), "motion inside an unchanged static control does not wake hidden CSS paint");
    }
    Check(ui.GetStats().m_StyleResolutions == counters.m_StyleResolutions &&
              ui.GetStats().m_LayoutPasses == counters.m_LayoutPasses,
          "hidden ticks retain style/layout caches");
    window.RenderFrame(.3);
    const auto& stats = window.Graphics().GetStats();
    Check(stats.m_DrawCalls == 0 && stats.m_InstanceBytes == 0 && stats.m_RepaintPixels == 0,
          "unchanged frame performs no analytic redraw or instance upload");
    tabs.SetCurrentIndex(0);
    window.RenderFrame(1);
    Check(std::abs(animated.GetStyle().m_Opacity - .65f) < .001f,
          "revealed CSS animation catches up to logical elapsed time");
}

std::string ReadCss(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

double Median(std::vector<double> values)
{
    if (values.empty())
        return -1;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

TableWidget* FindTable(Widget& widget)
{
    if (auto* table = dynamic_cast<TableWidget*>(&widget))
        return table;
    for (const auto& child : widget.GetChildren())
        if (auto* table = FindTable(*child))
            return table;
    return nullptr;
}
} // namespace

void VerifyGpuWorkload(Window& owner, const std::filesystem::path& assets, const std::filesystem::path& output)
{
    VerifyHidden(owner, assets);
    VerifyPixels(owner, assets, output);
    owner.RenderFrame(owner.Ui().GetTime());
}

void ProfileUiWorkload(Window& window, TabWidget& tabs, const std::filesystem::path& assets,
                       const std::filesystem::path& output)
{
    std::filesystem::create_directories(output);
    window.SetSize(1180, 820);
    auto& ui = window.Ui();
    auto& gpu = window.Graphics();
    gpu.SetGpuTimingEnabled(true);
    const auto css =
        ReadCss(assets / "dark.css") + ReadCss(assets / "animation-start.css") + ReadCss(assets / "rich-buttons.css");
    std::ofstream csv(output / "gpu-workload.csv");
    csv << "case,redraw,median_completed_frame_ms,median_gpu_query_ms,mean_repaint_fraction,mean_glyph_instances,mean_"
           "draw_calls\n";
    std::ofstream report(output / "gpu-workload.txt");
    report << "Driver: " << gpu.GetDriverDescription() << "\n"
           << "Actual gallery / 1180x820 / four warmup and 32 measured frames per case.\n"
           << "GL_TIME_ELAPSED queries are polled asynchronously; completed-frame CPU timings include readback.\n"
           << "Unpaced profiling measures per-frame cost, not utilization percent. Software GL does not predict "
              "hardware load.\n";
    double clock = ui.GetTime();
    for (int scenario : {0, 1, 5})
    {
        for (bool retained : {false, true})
        {
            ui.Deactivate();
            tabs.SetCurrentIndex(scenario);
            std::string error;
            Check(ui.SetStyleSheet(css, error), error);
            gpu.SetPartialRedrawEnabled(retained);
            std::vector<double> times, gpuTimes;
            double area = 0, glyphs = 0, draws = 0;
            std::uint64_t lastTiming = 0;
            for (int frame = 0; frame < 36; ++frame)
            {
                clock = std::max(clock, ui.GetTime()) + 1.0 / 60;
                ui.SetTime(clock);
                ui.Prepare(1180, 820);
                if (scenario == 0)
                    ui.PointerMove({30, 30});
                if (scenario == 1)
                {
                    auto* table = FindTable(tabs.GetPage(1));
                    Check(table != nullptr, "profile table exists");
                    const auto cell = table->GetCellBounds(frame % 8, 1);
                    const Point point{cell.m_X + 30, cell.m_Y + cell.m_Height * .5f};
                    ui.PointerDown(point);
                    ui.PointerUp(point);
                    Check(table->GetCurrentCell() == std::pair{frame % 8, 1}, "profile clicks change table selection");
                }
                const auto start = std::chrono::steady_clock::now();
                window.RenderFrame(clock);
                gpu.ReadPixel({0, 0});
                const auto stats = gpu.GetStats();
                if (frame >= 4)
                {
                    times.push_back(
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                    if (stats.m_TimingSamples > lastTiming && stats.m_GpuMilliseconds >= 0)
                        gpuTimes.push_back(stats.m_GpuMilliseconds);
                    area += static_cast<double>(stats.m_RepaintPixels) / static_cast<double>(stats.m_FramebufferPixels);
                    glyphs += static_cast<double>(stats.m_ReplayedGlyphs);
                    draws += static_cast<double>(stats.m_DrawCalls);
                }
                lastTiming = stats.m_TimingSamples;
            }
            const auto name = scenario == 0 ? "idle-inputs" : scenario == 1 ? "table-selection" : "rich-css";
            const auto mode = retained ? "retained" : "full";
            csv << name << ',' << mode << ',' << Median(times) << ',' << Median(gpuTimes) << ',' << area / 32 << ','
                << glyphs / 32 << ',' << draws / 32 << '\n';
            report << name << " / " << mode << ": completed-frame median " << Median(times) << " ms; GPU query median "
                   << Median(gpuTimes) << " ms; repaint " << area / 32 * 100 << "%; glyph instances " << glyphs / 32
                   << "; draws " << draws / 32 << ".\n";
            if (retained && scenario == 0)
                Check(area == 0 && glyphs == 0 && draws == 0, "idle tab reuses all analytic pixels");
            if (retained && scenario == 5)
                Check(area / 32 < .45, "actual rich tab limits analytic repaint area");
            if (retained && scenario == 1)
                Check(area / 32 < .3, "table selection retains unrelated rows and panels");
        }
    }
    gpu.SetPartialRedrawEnabled(true);
    gpu.SetGpuTimingEnabled(false);
}
} // namespace EStudioUI
