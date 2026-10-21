#include "PainterProbe.h"

#include <EStudioUI/PaintWidget.h>
#include <EStudioUI/Window.h>
#include <glad/gl.h>

#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Painter GPU: " + message);
}

std::vector<unsigned char> ReadFrame(int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

class ProbeWidget : public PaintWidget
{
  public:
    ProbeWidget() : PaintWidget("paint-probe")
    {
    }

  protected:
    void PaintContent(Painter& painter) const override
    {
        painter.SetBrush(Color{1, 0, 0, 1});
        painter.SetPen({0, 0, 0, 0}, 0);
        painter.DrawRect({-100, -100, 500, 500});
        painter.Save();
        painter.Translate(1000, 1000);
        painter.ClipRect({0, 0, 1, 1});
        // An early return with an unmatched Save must not affect the next widget.
    }
};
} // namespace

void VerifyPainter(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    std::filesystem::create_directories(output);
    WindowOptions options;
    options.m_Width = 640;
    options.m_Height = 360;
    options.m_Headless = headless;
    Window window(options, assets / "NotoSans.ttf");
    std::string error;
    Check(window.Ui().SetStyleSheet(
              "window { background: #101828; } paint-probe { background: #00ff00; border: 4px solid #0000ff; "
              "padding: 10px; border-radius: 24px; overflow: hidden; } #sibling { background: #00ff00; }",
              error),
          error);
    auto& custom = window.Ui().Root().Add<ProbeWidget>();
    custom.SetFloatingBounds({20, 20, 120, 100});
    auto& sibling = window.Ui().Root().Add<Widget>();
    sibling.SetId("sibling");
    sibling.SetFloatingBounds({180, 20, 80, 80});
    window.RenderFrame();
    auto& retained = window.Graphics();
    Check(retained.ReadPixel({80, 70}).m_R > .95f, "custom paint uses local content coordinates");
    Check(retained.ReadPixel({27, 70}).m_G > .95f, "custom content respects CSS padding");
    Check(retained.ReadPixel({22, 70}).m_B > .95f, "custom widget keeps CSS border");
    Check(retained.ReadPixel({220, 60}).m_G > .95f, "unbalanced Save is isolated from sibling widget");
    Check(retained.ReadPixel({10, 70}).m_R < .1f, "custom drawing stays inside widget clip");

    // The next case has no padding, making the CSS rounded clip observable in the custom content.
    custom.SetStyleProperty(StyleProperty::Padding, Insets{});
    custom.SetStyleProperty(StyleProperty::BorderWidth, 0.0f);
    window.RenderFrame();
    Check(retained.ReadPixel({21, 21}).m_R < .1f, "custom content respects CSS rounded overflow clip");
    Check(retained.ReadPixel({80, 70}).m_R > .95f, "rounded clip preserves content interior");
    window.Ui().Root().ClearChildren();

    Renderer reference(window.Ui().GetFont());
    reference.SetPartialRedrawEnabled(false);
    auto text = window.Ui().GetFont().Shape("Scoped GPU painter", 22);
    PainterPath curve;
    curve.MoveTo({130, 130});
    curve.CubicTo({180, 60}, {220, 200}, {270, 130});
    curve.MoveTo({300, 140});
    curve.ArcTo({330, 140}, 30, std::numbers::pi_v<float>, std::numbers::pi_v<float>);
    const Color clear{.04f, .06f, .1f, 1};
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
            renderer.SetPaintIdentity(701, 1);
            {
                Painter painter(renderer, window.Ui().GetFont(), {20, 20, 580, 310}, 1, {20, 20});
                painter.SetPen({0, 0, 0, 0}, 0);
                painter.SetBrush(Color{1, 0, 0, 1});
                painter.DrawRect({10 + static_cast<float>(frame) * 3, 10, 80, 50});
                painter.Save();
                painter.Translate(100, 0);
                painter.Rotate(std::numbers::pi_v<float> * .5f);
                painter.ClipRect({0, -40, 30, 30});
                painter.SetBrush(Color{0, 0, 1, 1});
                painter.DrawRect({0, -50, 40, 50});
                painter.Restore();
                painter.SetBrush(Color{0, 1, 0, 1});
                painter.DrawRect({190, 10, 70, 50});
                painter.Save();
                painter.ClipRect({280, 10, 80, 60}, 20);
                painter.SetBrush(Color{1, 1, 0, 1});
                painter.DrawRect({270, 0, 100, 80});
                painter.Restore();
                painter.Save();
                painter.SetOpacity(.5f);
                painter.SetBrush(Color{1, 1, 1, 1});
                painter.DrawRect({380, 10, 60, 60});
                painter.Restore();
                painter.SetBrush(Color{1, 0, 1, 1});
                painter.SetPen({1, 1, 1, 1}, 3);
                painter.DrawCircle({60, 130}, 30);
                painter.SetPen({1, .6f, .1f, 1}, 4);
                painter.DrawPath(curve);
                painter.Save();
                painter.Translate(450, 100);
                painter.Scale(1.5f, 1.5f);
                painter.SetBrush(Color{0, 1, 1, 1});
                painter.SetPen({0, 0, 0, 0}, 0);
                painter.DrawRoundedRect({0, 0, 55, 55}, 12);
                painter.Restore();
                painter.SetPen({1, 1, 1, 1});
                painter.DrawText(text, {180, 260});
                painter.Save();
                painter.ClipRect({0, 0, 1, 1});
                painter.Translate(1000, 1000);
            }
            renderer.SetPaintIdentity(701, 2);
            bool rejected = false;
            try
            {
                Painter painter(renderer, window.Ui().GetFont(), {0, 0, 1, 1});
                painter.Save();
                painter.Scale(0, 1);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            Check(rejected, "singular transform is rejected and unwinds painter state");
            renderer.DrawSolid({540, 260, 40, 40}, {0, 1, 0, 1}, {0, 0, 640, 360});
            renderer.EndFrame();
        };
        for (int frame = 0; frame < 4; ++frame)
        {
            render(retained, frame);
            const auto actual = ReadFrame(width, height);
            if (frame == 0)
            {
                Check(retained.ReadPixel({50, 50}).m_R > .95f, "local origin positions red fill");
                Check(retained.ReadPixel({145, 35}).m_B > .95f, "rotated local clip retains blue interior");
                Check(retained.ReadPixel({165, 35}).m_B < .2f, "rotated local clip rejects outer blue fill");
                Check(retained.ReadPixel({240, 50}).m_G > .95f, "Restore removes transforms and clips");
                Check(retained.ReadPixel({301, 31}).m_R < .1f, "rounded clip excludes its corner");
                Check(retained.ReadPixel({340, 60}).m_G > .95f, "rounded clip preserves yellow interior");
                const auto opacity = retained.ReadPixel({430, 60});
                Check(opacity.m_R > .50f && opacity.m_R < .54f, "local opacity multiplies fill alpha");
                Check(retained.ReadPixel({80, 150}).m_R > .95f && retained.ReadPixel({80, 150}).m_B > .95f,
                      "analytic filled circle");
                Check(retained.ReadPixel({500, 150}).m_G > .95f, "scaled rounded rectangle");
                Check(retained.ReadPixel({560, 280}).m_G > .95f,
                      "RAII restores state through early exit and exceptions");
                Check(retained.GetStats().m_Glyphs > 0, "pre-shaped text submits GPU glyphs");
            }
            render(reference, frame);
            const auto expected = ReadFrame(width, height);
            for (std::size_t byte = 0; byte < actual.size(); ++byte)
                if (std::abs(static_cast<int>(actual[byte]) - static_cast<int>(expected[byte])) > 1)
                    throw std::runtime_error("Painter retained/full mismatch at scale " + std::to_string(scale) +
                                             " frame " + std::to_string(frame) + " byte " + std::to_string(byte));
            ++comparisons;
            render(retained, frame);
            Check(retained.GetStats().m_RepaintPixels == 0 && retained.GetStats().m_SubmittedInstances == 0,
                  "unchanged painter commands submit no GPU work");
        }
    }
    Check(curve.GetFlattenBuildCount() == 1, "unchanged custom path uses one flatten cache build");
    retained.SavePpm(output / "painter.ppm");
    std::ofstream report(output / "painter.txt");
    report << "Scoped GPU painter: PASS\n"
           << comparisons
           << " complete retained/full RGBA comparisons at 1x and 2x logical scale.\n"
              "Checked custom CSS background/border/padding/rounded clipping, local origin, transforms, nested clips,\n"
              "Save/Restore and exception isolation, shape opacity, cached paths/text, idle retained reuse.\n";
}
} // namespace EStudioUI
