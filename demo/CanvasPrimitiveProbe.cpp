#include "CanvasPrimitiveProbe.h"

#include <EStudioUI/Window.h>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Canvas GPU primitives: " + message);
}

std::vector<unsigned char> ReadFrame(int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}
} // namespace

void VerifyCanvasPrimitives(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    WindowOptions options;
    options.m_Width = 640;
    options.m_Height = 360;
    options.m_Headless = headless;
    Window window(options, assets / "NotoSans.ttf");
    window.RenderFrame();
    auto& retained = window.Graphics();
    Renderer reference(window.Ui().GetFont());
    reference.SetPartialRedrawEnabled(false);
    StyleSheet sheet;
    std::string error;
    Check(sheet.Parse("canvas-item { background: #32b88a; border: 3px solid white; "
                      "box-shadow: 0 4px 8px #0008; } canvas-item:selected { background: #fc9c45; }",
                      error),
          error);
    const auto circle = sheet.Compute({"canvas-item", {}, {}, 0, nullptr});
    const auto selected = sheet.Compute({"canvas-item", {}, {}, Selected, nullptr});
    const Rect clip{0, 0, 640, 360};
    const Color clear{0.04f, 0.07f, 0.12f, 1};
    int comparisons = 0;
    for (int scale : {1, 2})
    {
        window.SetSize(640 * scale, 360 * scale);
        window.RenderFrame();
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int width = viewport[2], height = viewport[3];
        const auto render = [&](Renderer& renderer, int frame)
        {
            renderer.BeginFrame(640, 360, width, height, clear);
            // A widget key and a custom item key with subidentity zero must be distinct.
            renderer.SetWidgetPaint(clip, clip, nullptr, false, 0, 41);
            renderer.DrawSolid({540, 10, 70, 30}, {.2f, .4f, .7f, 1}, clip);
            renderer.PushState();
            renderer.SetPaintIdentity(41, 0);
            renderer.DrawLine({40, 40}, {160, 40}, {1, .1f, .1f, 1}, 10, clip);
            renderer.SetPaintIdentity(41, 1);
            renderer.DrawLine({220, 40}, {220, 40}, {.1f, 1, .1f, 1}, 12, clip);
            renderer.SetPaintIdentity(41, 2);
            const float shift = static_cast<float>(frame) * 2;
            renderer.DrawLine({80 + shift, 80}, {180 + shift, 180}, {.1f, .4f, 1, 1}, 4, clip);
            if (frame != 5 && frame != 6)
            {
                renderer.SetPaintIdentity(41, frame >= 7 ? 0x100000003ULL : 3);
                renderer.DrawCircle({300, 100}, 35, frame >= 8 ? selected : circle, clip);
            }
            renderer.SetPaintIdentity(41, 4);
            renderer.DrawLine({20, 230}, {600, 230}, {.9f, .2f, 1, 1}, 5, {250, 210, 150, 60});
            const std::array<int, 3> order = frame < 10 ? std::array<int, 3>{0, 1, 2} : std::array<int, 3>{2, 1, 0};
            for (int index : order)
            {
                renderer.SetPaintIdentity(41, static_cast<std::uint64_t>(10 + index));
                const float offset = static_cast<float>(index);
                renderer.DrawLine({400, 80 + offset * 12}, {540, 160 - offset * 15},
                                  {index == 0 ? 1.0f : .1f, index == 1 ? 1.0f : .1f, index == 2 ? 1.0f : .1f, .6f}, 22,
                                  clip);
            }
            // The same item ID in another canvas must retain its own key.
            renderer.SetPaintIdentity(42, 1);
            renderer.DrawLine({45, 290}, {frame >= 12 ? 130.0f : 110.0f, 315}, {1, .8f, .2f, 1}, 2, clip);
            renderer.PopState();
            renderer.PushState({.94f, .34f, -.34f, .94f, 370, 270});
            renderer.PushClip({-30, -10, 150, 50}, 8);
            renderer.SetPaintIdentity(43, 1);
            renderer.DrawLine({-100, 0}, {200, 20}, {.2f, .9f, .8f, 1}, 6, {-30, -10, 150, 50});
            renderer.PopState();
            renderer.EndFrame();
        };
        for (int frame = 0; frame < 16; ++frame)
        {
            render(retained, frame);
            const auto actual = ReadFrame(width, height);
            if (frame == 0)
            {
                Check(retained.ReadPixel({100, 40}).m_R > .95f, "horizontal line interior");
                Check(retained.ReadPixel({37, 40}).m_R > .95f, "round cap extends beyond endpoint");
                Check(retained.ReadPixel({32, 40}).m_R < .1f, "round cap excludes distant pixels");
                Check(retained.ReadPixel({220, 40}).m_G > .95f, "zero-length line is a round point");
                Check(retained.ReadPixel({130, 130}).m_B > .95f, "diagonal line interior");
                Check(retained.ReadPixel({300, 100}).m_G > .65f, "CSS circle fill");
                Check(retained.ReadPixel({333, 100}).m_R > .8f, "CSS circle border");
                Check(retained.ReadPixel({265, 65}).m_G < .2f, "circle corner remains unfilled");
                Check(retained.ReadPixel({200, 230}).m_R < .1f, "line obeys rectangular clip");
                Check(retained.ReadPixel({300, 230}).m_R > .8f, "line survives inside clip");
            }
            render(reference, frame);
            const auto expected = ReadFrame(width, height);
            for (std::size_t byte = 0; byte < actual.size(); ++byte)
                if (std::abs(static_cast<int>(actual[byte]) - static_cast<int>(expected[byte])) > 1)
                    throw std::runtime_error("Canvas retained/full mismatch at scale " + std::to_string(scale) +
                                             " frame " + std::to_string(frame) + " byte " + std::to_string(byte));
            ++comparisons;
            render(retained, frame);
            Check(retained.GetStats().m_RepaintPixels == 0 && retained.GetStats().m_SubmittedInstances == 0,
                  "unchanged canvas primitive frame submits no analytic work");
        }
    }
    retained.SavePpm(output / "canvas-primitives.ppm");
    std::ofstream report(output / "canvas-primitives.txt");
    report
        << "Canvas analytic GPU primitives: PASS\n"
        << comparisons << " complete retained/full RGBA frame comparisons at 1x and 2x logical scale.\n"
        << "Checked: round caps, zero-length lines, diagonals, CSS circle fill/border/shadow, clips, transforms,\n"
        << "item removal/reinsertion/generation IDs, distinct canvas owners, overlapping painter order, idle reuse.\n";
}
} // namespace EStudioUI
