#include "CanvasCurvesProbe.h"

#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/SketchDocument.h>
#include <EStudioUI/Window.h>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Canvas curves GPU: ") + message);
}

std::vector<unsigned char> Pixels(int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

void Compare(const std::vector<unsigned char>& actual, const std::vector<unsigned char>& expected)
{
    Check(actual.size() == expected.size(), "matching framebuffer sizes");
    for (std::size_t index = 0; index < actual.size(); ++index)
        Check(std::abs(static_cast<int>(actual[index]) - static_cast<int>(expected[index])) <= 1,
              "retained and full curved-geometry pixels match");
}
} // namespace

void VerifyCanvasCurves(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    std::filesystem::create_directories(output);
    WindowOptions options;
    options.m_Width = 800;
    options.m_Height = 600;
    options.m_Headless = headless;
    Window window(options, assets / "NotoSans.ttf");
    std::string error;
    Check(window.Ui().SetStyleSheet(
              "window { background: black; } canvas { background: black; padding: 0; border-width: 0; }", error),
          "window CSS");
    auto& canvas = window.Ui().Root().Add<CanvasWidget>();
    canvas.SetGridVisible(false);
    const auto red = canvas.RegisterItemStyle("red");
    const auto blue = canvas.RegisterItemStyle("blue");
    const auto green = canvas.RegisterItemStyle("green");
    Check(canvas.SetItemStyleSheet("* { background: transparent; color: #44eeaa; border: 3px solid #44eeaa; } "
                                   "red { background: #ef3344; border-width: 0; } "
                                   "blue { background: #2266ee; border-width: 0; } "
                                   "green { background: #44dd77; border-width: 0; } "
                                   "*:selected { color: white; border-color: white; }",
                                   error),
          "item CSS");
    auto& scene = canvas.GetScene();
    const auto arc = scene.Add(MakeCanvasArc({-240, -160}, 50, 0, std::numbers::pi));
    scene.Add(MakeCanvasSpline({-40, -190}, {{-70, 60}, {-70, -60}, {70, -60}, {70, 60}}));
    scene.Add(MakeCanvasPath({120, -190}, {{0, 0}, {90, 0}, {90, 80}}));
    scene.Add(MakeCanvasText({-300, 30, 115, 32}, "Canvas annotation clipped", 24));
    const auto group = scene.Add(
        MakeCanvasGroup({180, 65},
                        {{CanvasPrimitive::Circle, {-40, 0}, {}, 22, {}},
                         {CanvasPrimitive::Circle, {40, 0}, {}, 22, {}},
                         MakeCanvasGroup({0, 80}, {{CanvasPrimitive::Rectangle, {-25, -15}, {25, 15}, 0, {}}})},
                        {red, blue, green}));
    // These would be expensive to shape or subdivide if broad-phase culling were bypassed.
    for (int index = 0; index < 10000; ++index)
    {
        const double x = 100000 + index * 30.0;
        scene.Add(MakeCanvasArc({x, 0}, 10, 0, std::numbers::pi));
        if (index < 100)
            scene.Add(MakeCanvasText({x, 30, 100, 25}, "Offscreen annotation", 18));
    }
    canvas.SceneChanged();
    window.RenderFrame();
    const auto colorAt = [&](CanvasPoint point) { return window.Graphics().ReadPixel(canvas.ToScreen(point)); };
    Check(colorAt({-240, -110}).m_G > .8f, "arc follows its signed sweep");
    Check(colorAt({-240, -210}).m_G < .05f, "arc gap remains empty");
    Check(colorAt({-40, -220}).m_G > .8f, "cubic spline passes through its expected midpoint");
    Check(colorAt({165, -190}).m_G > .8f && colorAt({210, -150}).m_G > .8f, "path contains both line segments");
    Check(colorAt({165, -150}).m_G < .05f, "open path does not add a closing segment");
    Check(colorAt({140, 65}).m_R > .8f && colorAt({220, 65}).m_B > .8f,
          "group preserves individual child palette indices");
    Check(colorAt({180, 145}).m_G > .8f, "nested group inherits its containing child style");
    GLint initialViewport[4]{};
    glGetIntegerv(GL_VIEWPORT, initialViewport);
    const auto initialPixels = Pixels(initialViewport[2], initialViewport[3]);
    const auto greenAt = [&](int x, int y)
    {
        const int pixelX = x * initialViewport[2] / 800;
        const int pixelY = initialViewport[3] - 1 - y * initialViewport[3] / 600;
        return initialPixels[(static_cast<std::size_t>(pixelY) * initialViewport[2] + pixelX) * 4 + 1] / 255.0f;
    };
    std::size_t glyphPixels = 0;
    for (int y = 331; y < 361; ++y)
        for (int x = 100; x < 215; ++x)
            if (greenAt(x, y) > .3f)
                ++glyphPixels;
    Check(glyphPixels > 100, "annotation contains actual shaped glyph outlines");
    for (int y = 330; y < 363; ++y)
        for (int x = 216; x < 320; x += 4)
            Check(greenAt(x, y) < .05f, "text is clipped to its explicit annotation bounds");
    const auto warm = canvas.GetCacheStats();
    Check(warm.m_CurveMisses == 3 && warm.m_TextMisses == 1,
          "offscreen curves and annotations do not enter the paint caches");
    window.Graphics().SavePpm(output / "canvas-curves.ppm");
    window.RenderFrame();
    Check(window.Graphics().GetStats().m_RepaintPixels == 0 && window.Graphics().GetStats().m_SubmittedInstances == 0,
          "unchanged curves and annotations submit no analytic GPU work");
    Check(canvas.GetCacheStats().m_CurveMisses == warm.m_CurveMisses &&
              canvas.GetCacheStats().m_TextMisses == warm.m_TextMisses,
          "unchanged frames reuse tessellation and shaping");
    canvas.SetCamera({.25, .5}, 1);
    window.RenderFrame();
    Check(canvas.GetCacheStats().m_CurveMisses == warm.m_CurveMisses &&
              canvas.GetCacheStats().m_TextMisses == warm.m_TextMisses,
          "panning reuses local curve vertices and shaped runs");
    for (int frame = 0; frame < 12; ++frame)
    {
        if (frame == 2)
            scene.SetSelected(arc, true);
        if (frame == 4)
            scene.Update(group, TranslateGeometry(scene.Get(group)->m_Geometry, {-130, 0}));
        if (frame == 6)
            scene.Update(group, TranslateGeometry(scene.Get(group)->m_Geometry, {450, 0}));
        if (frame == 8)
            scene.Update(group, TranslateGeometry(scene.Get(group)->m_Geometry, {-320, 0}));
        canvas.SetCamera({frame * 2.25, frame * .75}, 1 + frame * .05);
        canvas.SceneChanged();
        window.RenderFrame();
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const auto retained = Pixels(viewport[2], viewport[3]);
        window.Graphics().SetPartialRedrawEnabled(false);
        window.RenderFrame();
        Compare(retained, Pixels(viewport[2], viewport[3]));
        window.Graphics().SetPartialRedrawEnabled(true);
    }
    Check(canvas.GetCacheStats().m_TextMisses == warm.m_TextMisses,
          "zoom changes transform glyph outlines without reshaping text");
    scene.Clear();
    std::vector<CanvasGeometry> children;
    children.reserve(200000);
    const auto sharedArc = MakeCanvasArc({}, 8, 0, std::numbers::pi);
    for (std::size_t index = 0; index < 200000; ++index)
        children.push_back(TranslateGeometry(
            sharedArc, {static_cast<double>(index % 1000) * 40, static_cast<double>(index / 1000) * 40}));
    scene.Add(MakeCanvasGroup({}, std::move(children)));
    canvas.SetCamera({400, 300}, 1);
    canvas.SceneChanged();
    const auto beforeGroup = canvas.GetCacheStats();
    window.RenderFrame();
    const auto afterGroup = canvas.GetCacheStats();
    Check(canvas.GetVisibleItemCount() == 1 && afterGroup.m_GroupCandidates > 100 && afterGroup.m_GroupCandidates < 700,
          "200000-child group queries only visible child bounds");
    Check(afterGroup.m_CurveMisses == beforeGroup.m_CurveMisses + 1,
          "translated group children share one cached local tessellation");
    Check(window.Graphics().GetStats().m_Shapes < 10000,
          "group GPU stream scales with visible curves rather than total child count");
    window.RenderFrame();
    Check(window.Graphics().GetStats().m_RepaintPixels == 0, "large unchanged groups retain their framebuffer");
    window.Graphics().SavePpm(output / "canvas-curves-group-200k.ppm");
    const auto groupShapes = window.Graphics().GetStats().m_Shapes;

    // GroupSelection assigns palette zero to its new parent while retaining each
    // child's complete palette. Parent opacity must not be multiplied into those leaves.
    auto document = std::make_shared<SketchDocument>();
    canvas.SetScene(std::shared_ptr<CanvasScene>(document, &document->GetScene()));
    canvas.SetCamera({}, 1);
    Check(canvas.SetItemStyleSheet("* { background: #44eeaa; color: #44eeaa; opacity: 0.5; border-width: 0; } "
                                   "red { background: #ef3344; opacity: 0.7; } *:selected { background: white; }",
                                   error),
          "translucent group CSS");
    const auto first = document->AddShape({CanvasPrimitive::Rectangle, {-100, -30}, {20, 40}, 0, {}});
    const auto second = document->AddShape({CanvasPrimitive::Rectangle, {-30, -60}, {100, 10}, 0, {}}, red);
    document->ClearSelection();
    canvas.SceneChanged();
    window.RenderFrame();
    GLint opacityViewport[4]{};
    glGetIntegerv(GL_VIEWPORT, opacityViewport);
    const auto ungroupedPixels = Pixels(opacityViewport[2], opacityViewport[3]);
    document->Select(first);
    document->Select(second, true);
    const auto translucentGroup = document->GroupSelection();
    Check(static_cast<bool>(translucentGroup), "grouping translucent leaves succeeds");
    document->ClearSelection();
    Check(!document->GetScene().Get(translucentGroup)->m_Selected, "group selection is cleared before comparing CSS");
    canvas.SceneChanged();
    window.RenderFrame();
    Compare(Pixels(opacityViewport[2], opacityViewport[3]), ungroupedPixels);
    const auto groupedGeometry = document->GetScene().Get(translucentGroup)->m_Geometry;
    Check(document->UpdateShape(translucentGroup, MakeCanvasGroup({}, {groupedGeometry})),
          "nested translucent group edit succeeds");
    canvas.SceneChanged();
    window.RenderFrame();
    Compare(Pixels(opacityViewport[2], opacityViewport[3]), ungroupedPixels);
    Check(document->UpdateShape(translucentGroup, groupedGeometry), "restoring the original group succeeds");
    document->Select(translucentGroup);
    Check(document->UngroupSelection(), "ungrouping translucent leaves succeeds");
    document->ClearSelection();
    for (const auto& item : document->GetScene().GetItems())
        Check(!item.m_Selected, "ungrouped leaf selection is cleared before comparing CSS");
    canvas.SceneChanged();
    window.RenderFrame();
    Compare(Pixels(opacityViewport[2], opacityViewport[3]), ungroupedPixels);

    std::ofstream report(output / "canvas-curves.txt");
    report << "Canvas curves and annotations GPU: PASS\nDriver: " << window.Graphics().GetDriverDescription()
           << "\n12 retained/full RGBA comparisons covering selection, group movement/culling, pan and zoom.\n"
           << "Arc gaps, cubic midpoint, open paths, clipped shaped text, nested child CSS, idle caches.\n"
           << "Grouping, nesting, and ungrouping preserve translucent leaf RGBA after clearing selection.\n"
           << "200000 grouped arcs / visible child candidates " << afterGroup.m_GroupCandidates << " / recorded shapes "
           << groupShapes << "\n"
           << "Curve entries " << afterGroup.m_CurveEntries << " / text entries " << afterGroup.m_TextEntries
           << " / retained cache bytes " << afterGroup.m_Bytes << "\n";
}
} // namespace EStudioUI
