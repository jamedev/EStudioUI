#include "RichCssGallery.h"

#include <EStudioUI/Label.h>
#include <EStudioUI/TextBox.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Rich CSS framebuffer: " + message);
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

Point Center(const Widget& widget)
{
    const auto bounds = widget.GetBounds();
    return {bounds.m_X + bounds.m_Width * .5f, bounds.m_Y + bounds.m_Height * .5f};
}

bool Near(float a, float b)
{
    return std::abs(a - b) < .035f;
}

void PixelProbes(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    WindowOptions options;
    options.m_Headless = headless;
    options.m_Width = 400;
    options.m_Height = 340;
    Window window(options, assets / "NotoSans.ttf");
    auto& ui = window.Ui();
    auto& gpu = window.Graphics();
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        window { background: black; }
        #linear { background: linear-gradient(90deg, red, blue); }
        #radial { background: radial-gradient(ellipse 50% 50% at 50% 50%, white, blue); }
        #inset { background: white; box-shadow: inset 0 0 0 10px black; }
        #stack { background: white; box-shadow: 8px 0 0 red, 16px 0 0 blue; }
        #rounded { background: blue; border-radius: 24px; overflow: hidden; }
        #rounded::before { content: ""; inset: -30px; background: red; }
        #turn { background: #346eff; border: none; transform: translateX(60px) rotate(-20deg); }
        #animated { animation: move 1s linear forwards; }
        @keyframes move { from { background: red; transform: translateX(0px); } to { background: blue; transform: translateX(80px); } }
    )CSS",
                           error),
          error);
    const auto add = [&](const char* id, Rect bounds) -> Widget&
    {
        auto& widget = ui.Root().Add<Widget>();
        widget.SetId(id);
        widget.SetFloatingBounds(bounds);
        return widget;
    };
    add("linear", {10, 10, 180, 60});
    add("radial", {210, 10, 120, 120});
    add("inset", {10, 100, 150, 60});
    add("stack", {200, 150, 70, 35});
    add("rounded", {20, 200, 100, 70});
    auto& turn = ui.Root().Add<Button>("GPU");
    turn.SetId("turn");
    turn.SetFloatingBounds({180, 230, 100, 40});
    int clicks = 0;
    turn.Clicked().Connect([&] { ++clicks; });
    add("animated", {10, 290, 100, 30});
    window.RenderFrame(0);
    const auto left = gpu.ReadPixel({20, 40});
    const auto right = gpu.ReadPixel({180, 40});
    Check(left.m_R > .9f && left.m_B < .1f && right.m_B > .9f && right.m_R < .1f,
          "linear gradient direction and interpolation");
    Check(gpu.ReadPixel({270, 70}).m_R > .97f && gpu.ReadPixel({327, 70}).m_R < .1f, "radial ellipse coordinates");
    Check(gpu.ReadPixel({14, 125}).m_R < .02f && gpu.ReadPixel({80, 130}).m_R > .98f,
          "hard inset shadow and clear interior");
    Check(gpu.ReadPixel({274, 167}).m_R > .98f && gpu.ReadPixel({282, 167}).m_B > .98f,
          "multiple outer shadows preserve CSS order");
    Check(gpu.ReadPixel({21, 201}).m_R < .02f && gpu.ReadPixel({70, 230}).m_R > .98f,
          "oversized pseudo-element obeys rounded owner clip");
    ui.PointerDown({290, 250});
    ui.PointerUp({290, 250});
    Check(clicks == 1, "transformed GPU caption and button remain interactive");
    window.RenderFrame(.5);
    const auto middle = gpu.ReadPixel({80, 305});
    Check(Near(middle.m_R, .5f) && Near(middle.m_B, .5f) && gpu.ReadPixel({20, 305}).m_R < .02f,
          "CSS keyframe geometry and colors reach the actual framebuffer without trails");
    gpu.SavePpm(output / "rich-css-pixels.ppm");
    window.RenderFrame(1);
    Check(gpu.ReadPixel({150, 305}).m_B > .98f && !ui.HasAnimations(),
          "forwards fill commits endpoint and releases frame demand");
}

void Benchmark(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    WindowOptions options;
    options.m_Headless = headless;
    options.m_Width = 800;
    options.m_Height = 600;
    Window window(options, assets / "NotoSans.ttf");
    auto& ui = window.Ui();
    for (int i = 0; i < 160; ++i)
        ui.Root().Add<Button>("GPU").SetFloatingBounds(
            {static_cast<float>(i % 16 * 49 + 8), static_cast<float>(i / 16 * 57 + 10), 43, 42});
    std::ofstream report(output / "rich-css-performance.txt");
    report << "Driver: " << window.Graphics().GetDriverDescription() << "\n"
           << "160 buttons / 800x600 / 24 timed frames per case / synchronous framebuffer completion.\n"
           << "Host timings are observations; software GL does not predict hardware performance.\n";
    double clock = 0;
    const std::string base = "window { background: #080e1a; } button { padding: 2px; border: none; border-radius: 7px; "
                             "font-size: 11px; background: #29475f; }";
    for (bool rich : {false, true})
    {
        std::string error;
        Check(ui.SetStyleSheet(base + (rich ? R"CSS(
            button { background: linear-gradient(110deg, #2089e4, #a347c2); box-shadow: 0 3px 5px #0006, inset 0 1px 2px #fff3; overflow: hidden; animation: hover 2s linear infinite alternate; }
            button::before { content: ""; inset: 0; background: linear-gradient(90deg, transparent, #ffffff18, transparent); }
            @keyframes hover { from { transform: translateY(0px); opacity: .75; } to { transform: translateY(-3px); opacity: 1; } }
        )CSS"
                                            : ""),
                               error),
              error);
        for (int i = 0; i < 4; ++i)
        {
            clock += 1.0 / 120;
            window.RenderFrame(clock);
            window.Graphics().ReadPixel({0, 0});
        }
        const auto counters = ui.GetStats();
        std::vector<double> times;
        for (int i = 0; i < 24; ++i)
        {
            const auto start = std::chrono::steady_clock::now();
            clock += 1.0 / 120;
            window.RenderFrame(clock);
            window.Graphics().ReadPixel({0, 0});
            times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::sort(times.begin(), times.end());
        const auto& stats = window.Graphics().GetStats();
        Check(rich ? stats.m_DrawCalls == 1 : stats.m_DrawCalls == 0,
              "unchanged frames reuse pixels and dense animation stays batched");
        Check(ui.GetStats().m_StyleResolutions == counters.m_StyleResolutions &&
                  ui.GetStats().m_LayoutPasses == counters.m_LayoutPasses,
              "paint-only rich animation does not resolve CSS or arrange layout");
        Check(!rich || (stats.m_Gradients == 320 && stats.m_Shadows == 320),
              "all gradient and shadow layers actually reach the batch");
        report << (rich ? "Rich + animated" : "Simple baseline") << ": median " << times[12] << " ms; p95 " << times[22]
               << " ms; draws " << stats.m_DrawCalls << "; shapes " << stats.m_Shapes << "; glyphs " << stats.m_Glyphs
               << "; instance bytes " << stats.m_InstanceBytes << "; effect bytes " << stats.m_EffectBytes
               << "; timed style resolutions 0; timed layout passes 0.\n";
    }
}
} // namespace

RichCssGallery::RichCssGallery(Window& window, Widget& page) : m_Window(window)
{
    page.SetId("rich-css");
    auto& title = page.Add<Label>("Rich paint. Working controls.");
    title.AddClass("rich-heading");
    auto& description =
        page.Add<Label>("Hover, press, Tab, and type. Gradients, layers, transforms, shadows, and keyframes are CSS.");
    description.AddClass("rich-description");
    const std::array<const char*, 12> classes{"aurora", "neon",         "press",  "shine", "sweep",    "jelly",
                                              "pulse",  "border-trace", "radial", "morph", "spectrum", "progress"};
    const std::array<const char*, 12> names{"Aurora lift",      "Neon circuit",  "Press me", "Light sweep",
                                            "Color reveal",     "Jelly motion",  "Breathe",  "Living outline",
                                            "Follow the light", "Soft geometry", "Spectrum", "In progress"};
    const std::array<const char*, 12> notes{
        "Gradient / transform", "Outer + inset glow",     "Press / depth",          "Before / rounded clip",
        "Transform origin",     "Multi-stage keyframes",  "Infinite / alternate",   "After / animated border",
        "Radial interpolation", "Inset shadows / radius", "Animated gradient axis", "After / progress track"};
    for (std::size_t rowIndex = 0; rowIndex < 3; ++rowIndex)
    {
        auto& row = page.Add<Widget>();
        row.AddClass("rich-row");
        row.SetLayoutDirection(Axis::Horizontal);
        row.SetHeight(SizeRule::Fixed(124));
        for (std::size_t column = 0; column < 4; ++column)
        {
            const auto index = rowIndex * 4 + column;
            auto& card = row.Add<Widget>();
            card.AddClass("rich-card");
            card.SetWidth(SizeRule::Fill());
            auto& label =
                card.Add<Label>(std::string(index < 9 ? "0" : "") + std::to_string(index + 1) + " / " + classes[index]);
            label.AddClass("rich-label");
            auto& button = card.Add<Button>(names[index]);
            button.AddClass("rich-button");
            button.AddClass(classes[index]);
            button.Clicked().Connect([this] { ++m_Clicks; });
            m_Buttons[index] = &button;
            auto& note = card.Add<Label>(notes[index]);
            note.AddClass("rich-note");
        }
    }
    auto& editor = page.Add<TextBox>();
    editor.SetPlaceholder("Focus this text box: its border and shadows transition through CSS.");
    auto& status = page.Add<Label>(
        "12 working buttons / 2 decorative layers per widget / no extra effect windows or runtime libraries");
    status.AddClass("rich-status");
}

void RichCssGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    m_Window.RenderFrame();
    const auto bounds = m_Buttons.back()->GetBounds();
    const auto clip = m_Buttons.back()->GetClip();
    Check(bounds.m_Y + bounds.m_Height <= clip.m_Y + clip.m_Height, "all twelve buttons fit the native gallery tab");
    m_Window.Graphics().SavePpm(output / "rich-css-tab.ppm");
    WindowOptions options;
    options.m_Headless = m_Window.IsHeadless();
    options.m_Width = 1180;
    options.m_Height = 620;
    {
        Window probe(options, assets / "NotoSans.ttf");
        RichCssGallery gallery(probe, probe.Ui().Root());
        std::string error;
        Check(probe.Ui().SetStyleSheet(
                  Read(assets / "rich-buttons.css") + "\nwindow { background: #080e1a; padding: 24px; }", error),
              error);
        probe.RenderFrame(0);
        probe.Graphics().SavePpm(output / "rich-buttons-rest.ppm");
        const auto shineBounds = gallery.m_Buttons[3]->GetBounds();
        const Point shinePoint{shineBounds.m_X + shineBounds.m_Width * .6f,
                               shineBounds.m_Y + shineBounds.m_Height * .5f};
        const auto restPixel = probe.Graphics().ReadPixel(shinePoint);
        probe.Ui().PointerMove(Center(*gallery.m_Buttons[3]));
        probe.RenderFrame(0);
        probe.RenderFrame(.18);
        Check(probe.Graphics().ReadPixel(shinePoint).m_R > restPixel.m_R + .15f,
              "hover transition moves the decorative shine across the actual button");
        probe.Graphics().SavePpm(output / "rich-buttons-hover.ppm");
        probe.RenderFrame(.9);
        const auto point = Center(*gallery.m_Buttons[2]);
        probe.Ui().PointerDown(point);
        probe.Ui().PointerUp(point);
        Check(gallery.m_Clicks == 1, "gallery buttons emit working activation signals");
        Check(probe.Graphics().GetStats().m_RepaintPixels < probe.Graphics().GetStats().m_FramebufferPixels / 2,
              "rich gallery animation repaints a bounded region");
    }
    PixelProbes(assets, output, options.m_Headless);
    Benchmark(assets, output, options.m_Headless);
}
} // namespace EStudioUI
