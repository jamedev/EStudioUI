#include "WidgetStylesProbe.h"

#include <EStudioUI/ListWidget.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/Window.h>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
constexpr float Width = 820;
constexpr float Height = 560;
constexpr int RowCount = 10000;

void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Widget styles GPU: " + message);
}

Point Center(Rect bounds)
{
    return {bounds.m_X + bounds.m_Width * .5f, bounds.m_Y + bounds.m_Height * .5f};
}

std::vector<unsigned char> Pixels(int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

void Compare(const std::vector<unsigned char>& retained, const std::vector<unsigned char>& full,
             const std::string& label)
{
    Check(retained.size() == full.size(), label + ": matching framebuffer dimensions");
    for (std::size_t index = 0; index < retained.size(); ++index)
        if (std::abs(static_cast<int>(retained[index]) - static_cast<int>(full[index])) > 1)
            throw std::runtime_error("Widget styles GPU: retained/full RGBA mismatch in " + label + " at byte " +
                                     std::to_string(index));
}

bool Green(Color color)
{
    return color.m_G > .8f && color.m_R < .25f && color.m_B < .3f;
}

bool Magenta(Color color)
{
    return color.m_R > .8f && color.m_B > .6f && color.m_G < .3f;
}

std::string Theme(bool alternate)
{
    return std::string(R"CSS(
        window { background: #101828; color: #ffffff; font-size: 14px; padding: 0; }
        slider { padding: 0; border-width: 0; background: transparent; }
        slider::groove { height: 10px; background: linear-gradient(to right, #ff0000, #0000ff);
                          border-radius: 5px; }
        slider::fill { height: 10px; background: #00ff00; border-radius: 5px; }
        slider::handle { width: 28px; height: 28px; border: 2px solid #ffffff; border-radius: 14px;
                          background: linear-gradient(to right, #ff0000, #0000ff);
                          box-shadow: 0px 2px 3px #000000; }
        textbox { background: #202b42; color: #ffffff; padding: 8px; border: 2px solid #4a5b80;
                  border-radius: 0; }
        textbox:focus { border-color: #00ff00; }
        textbox::selection { background: #ff00cc; color: #ffffff; }
        textbox::caret { width: 4px; background: #00ff00; }
        textbox::placeholder { color: #00ffff; font-size: 20px; text-align: right; }
        numericinput { padding: 0; border: 3px solid #4a5b80; gap: 2px; background: #202b42; }
        numericinput:focus-within { border-color: #ff00cc; }
        numericinput textbox { border-width: 0; padding: 4px; }
        numericinput toolbutton { background: #324669; color: #ffffff; border-width: 0; }
        listwidget, treewidget, tablewidget { padding: 0; border: 2px solid #ffffff;
            background: #101828; row-height: 32px; grid-color: #344057; }
        listwidget::item, treewidget::item { background: linear-gradient(to right, #ff0000, #0000ff);
            color: #ffffff; padding: 5px; border-width: 0; }
        listwidget::item:alternate, treewidget::item:alternate { background: #182848; }
        listwidget::item:hover, treewidget::item:hover { background: #ff8000; }
        listwidget::item:selected, treewidget::item:selected { background: #00ff00; color: #102018;
            box-shadow: inset 0px 0px 2px #ffffff, 0px 2px 3px #000000; }
        treewidget::branch { width: 28px; background: #ff0000; color: #ffffff; border-width: 0; }
        treewidget::branch:expanded { background: #00ffff; }
        tablewidget::cell { background: #182848; color: #ffffff; padding: 4px; border-width: 0; }
        tablewidget::cell:alternate { background: #243354; }
        tablewidget::cell:hover { background: #ff8000; }
        tablewidget::cell:selected { background: #ff00cc; box-shadow: inset 0px 0px 2px #ffffff;
                                    color: #ffffff; }
        tablewidget::header { background: linear-gradient(to right, #203060, #4050a0); padding: 4px; }
        tablewidget::header:sorted { background: #ff8000; }
        tablewidget::header:hover { border: 2px solid #00ffff; }
        listwidget::scroll-track:vertical, treewidget::scroll-track:vertical,
        tablewidget::scroll-track:vertical { width: 16px; background: #182848; }
        listwidget::scroll-thumb:vertical, treewidget::scroll-thumb:vertical,
        tablewidget::scroll-thumb:vertical { width: 10px; background: #ffcc00; border-radius: 4px; }
        tablewidget::scroll-track:horizontal { height: 16px; background: #182848; }
        tablewidget::scroll-thumb:horizontal { height: 10px; background: #00ffff; border-radius: 4px; }
        listwidget.moved { transform: translate(8px, -6px) rotate(2deg); }
    )CSS") +
           (alternate ? "listwidget::item:selected { background: #00cc33; } "
                        "tablewidget::cell:selected { background: #cc00ff; } "
                        "slider::fill { background: #00cc33; }"
                      : "");
}

// A fixed registered paint part exercises the same cache/timeline path as slider handles,
// without adding pointer or geometry changes to the animation workload.
class AnimatedPartProbe final : public Widget
{
  public:
    AnimatedPartProbe() : Widget("animated-part-probe")
    {
        RegisterStylePart(
            "face", 0,
            [](const Style&, unsigned)
            {
                Style style;
                style.m_Background = {1, 0, 0, 1};
                return style;
            },
            true);
    }

  protected:
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override
    {
        Widget::Paint(renderer, font, time, opacity);
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(this), 1);
        const auto& style = GetPartStyle("face");
        renderer.DrawBox(GetContentBounds(), style, GetClip(), opacity * style.m_Opacity);
    }
};
} // namespace

void VerifyWidgetStyles(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    std::filesystem::create_directories(output);
    WindowOptions options;
    options.m_Width = static_cast<int>(Width);
    options.m_Height = static_cast<int>(Height);
    options.m_Headless = headless;
    Window window(options, assets / "NotoSans.ttf");
    auto& ui = window.Ui();
    auto& retained = window.Graphics();
    auto& slider = ui.Root().Add<Slider>();
    slider.SetFloatingBounds({20, 24, 320, 48});
    slider.SetValue(25);
    auto& editor = ui.Root().Add<TextBox>("Selected text");
    editor.SetFloatingBounds({20, 92, 320, 48});
    auto& placeholder = ui.Root().Add<TextBox>();
    placeholder.SetPlaceholder("Placeholder");
    placeholder.SetFloatingBounds({20, 160, 320, 48});
    auto& numeric = ui.Root().Add<NumericInput>(12.5);
    numeric.SetFloatingBounds({400, 28, 240, 44});
    numeric.SetRange(0, 100);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetFloatingBounds({20, 240, 240, 280});
    auto& tree = ui.Root().Add<TreeWidget>();
    tree.SetFloatingBounds({280, 240, 230, 280});
    auto& table = ui.Root().Add<TableWidget>();
    table.SetFloatingBounds({530, 240, 270, 280});
    table.SetColumns({{"Name", SizeRule::Fixed(120)}, {"Value", SizeRule::Fixed(180)}});
    std::vector<std::string> items;
    std::vector<std::vector<std::string>> rows;
    items.reserve(RowCount);
    rows.reserve(RowCount);
    for (int row = 0; row < RowCount; ++row)
    {
        items.push_back("Item " + std::to_string(row));
        rows.push_back({"Row " + std::to_string(row), std::to_string(row)});
    }
    list.SetItems(std::move(items));
    table.SetRows(std::move(rows));
    const auto branch = tree.AddItem(0, "Geometry");
    for (int item = 0; item < 100; ++item)
        tree.AddItem(branch, "Primitive " + std::to_string(item));
    tree.SetExpanded(branch, true);
    std::string error;
    Check(ui.SetStyleSheet(Theme(false), error), "rich part stylesheet parses: " + error);
    window.RenderFrame(0);

    Renderer reference(ui.GetFont());
    reference.SetPartialRedrawEnabled(false);
    int comparisons = 0;
    std::size_t maxVisibleGlyphs = 0;
    double time = ui.GetTime();
    for (const int scale : {1, 2})
    {
        window.SetSize(static_cast<int>(Width) * scale, static_cast<int>(Height) * scale);
        // Native resize callbacks may already have advanced the GUI clock.
        time = std::max(time, ui.GetTime());
        window.RenderFrame(time);
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int width = viewport[2], height = viewport[3];
        Check(width == static_cast<int>(Width) * scale && height == static_cast<int>(Height) * scale,
              "native framebuffer matches requested DPI test scale");
        const auto render = [&](Renderer& renderer)
        {
            ui.SetTime(time);
            ui.Prepare(Width, Height);
            renderer.BeginFrame(Width, Height, width, height, {0.04f, 0.06f, 0.1f, 1});
            ui.Paint(renderer, time);
            renderer.EndFrame();
        };
        const auto compareFrame = [&](const std::string& label)
        {
            Check(time >= ui.GetTime(), label + ": deterministic frame time follows native callbacks");
            render(retained);
            const auto pixels = Pixels(width, height);
            maxVisibleGlyphs = std::max(maxVisibleGlyphs, retained.GetStats().m_Glyphs);
            Check(retained.GetStats().m_Glyphs < 1500 && retained.GetStats().m_Shapes < 500,
                  "large collections paint only visible cells and rows");
            render(retained);
            Check(retained.GetStats().m_RepaintPixels == 0 && retained.GetStats().m_SubmittedInstances == 0,
                  label + ": unchanged frame submits zero analytic instances");
            Compare(pixels, Pixels(width, height), label + " idle");
            render(reference);
            Compare(pixels, Pixels(width, height), label + " at " + std::to_string(scale) + "x");
            ++comparisons;
        };

        list.RemoveClass("moved");
        list.SetScrollOffset({});
        table.SetScrollOffset({});
        tree.SetScrollOffset({});
        tree.SetExpanded(branch, true);
        list.SetCurrentIndex(2);
        table.SortByColumn(0);
        table.SetCurrentCell(1, 1);
        table.SetScrollOffset({});
        ui.SetFocus(nullptr);
        ui.PointerMove({810, 550});
        Check(ui.SetStyleSheet(Theme(false), error), error);
        compareFrame("initial parts");

        const auto sliderBounds = slider.GetContentBounds();
        const float sliderY = Center(sliderBounds).m_Y;
        Check(Green(retained.ReadPixel({sliderBounds.m_X + 40, sliderY})), "slider fill paints its own color");
        const auto groove = retained.ReadPixel({sliderBounds.m_X + 270, sliderY});
        Check(groove.m_B > .7f && groove.m_R < .3f, "slider groove paints its independent gradient");
        const float handleX = sliderBounds.m_X + 14 + (sliderBounds.m_Width - 28) * .25f;
        const auto handleLeft = retained.ReadPixel({handleX - 7, sliderY});
        const auto handleRight = retained.ReadPixel({handleX + 7, sliderY});
        Check(handleLeft.m_R > handleRight.m_R + .3f && handleRight.m_B > handleLeft.m_B + .3f,
              "CSS handle dimensions contain a real GPU gradient");
        const auto listViewport = list.GetViewport();
        const float rowHeight = list.GetStyle().m_RowHeight;
        Check(Green(retained.ReadPixel({listViewport.m_X + 160, listViewport.m_Y + rowHeight * 2 + 8})),
              "selected list row uses its part palette");
        const auto gradientLeft = retained.ReadPixel({listViewport.m_X + 12, listViewport.m_Y + 4});
        const auto gradientRight = retained.ReadPixel({listViewport.m_X + 190, listViewport.m_Y + 4});
        Check(gradientLeft.m_R > gradientRight.m_R + .6f && gradientRight.m_B > gradientLeft.m_B + .6f,
              "collection item gradient spans the allocated row");
        const auto cell = table.GetCellBounds(1, 1);
        Check(Magenta(retained.ReadPixel({cell.m_X + 30, cell.m_Y + 5})), "selected table cell uses rich part fill");
        const auto tableContent = table.GetContentBounds();
        const auto sorted = retained.ReadPixel({tableContent.m_X + 90, tableContent.m_Y + 6});
        Check(sorted.m_R > .9f && sorted.m_G > .4f && sorted.m_G < .6f && sorted.m_B < .1f,
              "sorted header has its own state palette");
        const auto treeViewport = tree.GetViewport();
        const auto expanded = retained.ReadPixel({treeViewport.m_X + 3, treeViewport.m_Y + 3});
        Check(expanded.m_R < .1f && expanded.m_G > .9f && expanded.m_B > .9f,
              "expanded tree branch paints its named part");
        const auto thumb = list.GetScrollThumbBounds(false);
        const auto thumbPixel = retained.ReadPixel(Center(thumb));
        Check(thumb.m_Width == 10 && thumbPixel.m_R > .9f && thumbPixel.m_G > .7f && thumbPixel.m_B < .1f,
              "styled scrollbar width and fill reach the framebuffer");
        Check(list.GetScrollTrackBounds(false).m_Width == 16, "CSS track width reserves the actual gutter");
        Check(list.GetChildren().empty() && tree.GetChildren().empty() && table.GetChildren().size() == 1,
              "collection rows remain model data without per-row widgets");
        retained.SavePpm(output / ("widget-styles-" + std::to_string(scale) + "x.ppm"));

        ui.SetFocus(&editor);
        editor.SelectAll();
        compareFrame("text selection and caret");
        const auto textContent = editor.GetContentBounds();
        Check(Magenta(retained.ReadPixel({textContent.m_X + 3, textContent.m_Y + 3})),
              "textbox selection uses its named part fill");
        const auto textWidth = ui.GetFont().Shape(editor.GetText(), editor.GetStyle().m_FontSize).m_Width;
        Check(Green(retained.ReadPixel({textContent.m_X + textWidth + 1, textContent.m_Y + 3})),
              "textbox caret uses its CSS color and width");
        ui.SetFocus(&numeric.GetEditor());
        compareFrame("composed control focus within");
        const auto numericBounds = numeric.GetBounds();
        Check(Magenta(retained.ReadPixel({numericBounds.m_X + 1, numericBounds.m_Y + 20})),
              "focus-within styles the composed numeric control");
        ui.SetFocus(nullptr);

        list.SetCurrentIndex(4);
        table.SetCurrentCell(3, 0);
        ui.PointerMove({listViewport.m_X + 150, listViewport.m_Y + rowHeight + 8});
        compareFrame("hover and selection shadow replacement");
        const auto hovered = retained.ReadPixel({listViewport.m_X + 160, listViewport.m_Y + rowHeight + 8});
        Check(hovered.m_R > .9f && hovered.m_G > .4f && hovered.m_B < .1f,
              "item hover changes only the hovered row palette");
        ui.PointerMove({tableContent.m_X + 80, tableContent.m_Y + 12});
        compareFrame("header hover border");
        tree.SetExpanded(branch, false);
        compareFrame("collapsed branch");
        const auto collapsed = retained.ReadPixel({treeViewport.m_X + 3, treeViewport.m_Y + 3});
        Check(collapsed.m_R > .9f && collapsed.m_G < .1f && collapsed.m_B < .1f,
              "collapsed branch changes palette without changing its row contract");
        tree.SetExpanded(branch, true);

        ui.PointerMove({listViewport.m_X + 150, listViewport.m_Y + 8});
        compareFrame("row hover before hide");
        list.SetVisible(false);
        ui.PointerMove({listViewport.m_X + 150, listViewport.m_Y + rowHeight + 8});
        list.SetVisible(true);
        compareFrame("row hover after reshow");
        const auto reshowHover = retained.ReadPixel({listViewport.m_X + 160, listViewport.m_Y + rowHeight + 8});
        Check(reshowHover.m_R > .9f && reshowHover.m_G > .4f && reshowHover.m_B < .1f,
              "reshown collection uses the current pointer without another move event");
        list.SetScrollOffset({0, rowHeight});
        compareFrame("stationary pointer after scrolling");
        const auto scrolledHover = retained.ReadPixel({listViewport.m_X + 160, listViewport.m_Y + rowHeight + 8});
        Check(scrolledHover.m_R > .9f && scrolledHover.m_G > .4f && scrolledHover.m_B < .1f,
              "stationary pointer follows its viewport row after programmatic scrolling");
        list.SetScrollOffset({});

        ui.PointerMove({810, 550});
        list.AddClass("moved");
        compareFrame("transformed collection");
        list.RemoveClass("moved");
        compareFrame("transform removed");
        Check(ui.SetStyleSheet(Theme(true), error), error);
        compareFrame("theme replacement");
        Check(slider.GetBounds().m_Width == 320 && list.GetBounds().m_Width == 240 &&
                  table.GetColumnWidths() == std::vector<float>{120, 180},
              "theme replacement preserves explicit control and column dimensions");

        // Pointer mapping must use the painted handle's CSS half-width, not the legacy 8px radius.
        ui.PointerDown({sliderBounds.m_X + 14, sliderY});
        ui.PointerUp({sliderBounds.m_X + 14, sliderY});
        Check(slider.GetValue() == 0, "styled slider pointer range starts at the painted handle center");
        ui.PointerDown({sliderBounds.m_X + sliderBounds.m_Width - 14, sliderY});
        ui.PointerUp({sliderBounds.m_X + sliderBounds.m_Width - 14, sliderY});
        Check(slider.GetValue() == 100, "styled slider pointer range ends at the painted handle center");
        slider.SetValue(25);
        ui.SetFocus(nullptr);
        const auto track = list.GetScrollTrackBounds(false);
        ui.PointerDown(Center(list.GetScrollThumbBounds(false)));
        ui.PointerMove({Center(track).m_X, track.m_Y + track.m_Height + 100});
        ui.PointerUp({Center(track).m_X, track.m_Y + track.m_Height + 100});
        Check(std::abs(list.GetScrollOffset().m_Y - (RowCount * rowHeight - list.GetViewport().m_Height)) < .1f,
              "CSS scrollbar drag clamps exactly to the final row");
        table.SetScrollOffset({10000, 10000000});
        compareFrame("scroll endpoints");
        Check(table.GetScrollOffset().m_X <= 300 - table.GetViewport().m_Width + .1f,
              "horizontal scrollbar clamps against styled viewport width");
        list.SetScrollOffset({0, 12345});
        table.SetScrollOffset({24, 23456});
        compareFrame("virtualized middle rows");
        ui.PointerMove({810, 550});
    }

    ui.SetFocus(nullptr);
    ui.Root().ClearChildren();
    auto& animatedPage = ui.Root().Add<Widget>();
    animatedPage.SetFloatingBounds({0, 0, Width, Height});
    std::vector<AnimatedPartProbe*> animated;
    for (int index = 0; index < 500; ++index)
    {
        auto& item = animatedPage.Add<AnimatedPartProbe>();
        item.SetFloatingBounds(
            {10 + static_cast<float>(index % 25) * 32, 10 + static_cast<float>(index / 25) * 26, 24, 18});
        animated.push_back(&item);
    }
    Check(ui.SetStyleSheet(R"CSS(
        window { background: #101828; padding: 0; }
        @keyframes part-pulse { from { background: #ff0000; } to { background: #0000ff; } }
        animated-part-probe::face { animation: part-pulse 1s linear infinite alternate; }
    )CSS",
                           error),
          error);
    window.SetSize(static_cast<int>(Width), static_cast<int>(Height));
    time = std::max(time, ui.GetTime());
    window.RenderFrame(time);
    Check(ui.HasAnimations(), "visible registered parts request their animation frames");
    const auto before = ui.GetStats();
    const auto startColor = animated.front()->GetPartStyle("face").m_Background;
    for (int tick = 1; tick <= 240; ++tick)
    {
        ui.SetTime(time + static_cast<double>(tick) / 240);
        ui.Prepare(Width, Height);
    }
    const auto after = ui.GetStats();
    Check(before.m_StyleResolutions == after.m_StyleResolutions,
          "500 paint-only part animations evaluate zero selectors over 240 ticks");
    Check(before.m_LayoutPasses == after.m_LayoutPasses,
          "500 paint-only part animations cause zero layout passes over 240 ticks");
    const auto endColor = animated.front()->GetPartStyle("face").m_Background;
    Check(startColor.m_R > .9f && endColor.m_B > .9f,
          "fixed part presentations interpolate to the specified keyframe endpoint");
    window.RenderFrame(time + 1);
    const auto animatedPixel = retained.ReadPixel(Center(animated.front()->GetBounds()));
    Check(animatedPixel.m_B > .9f && animatedPixel.m_R < .1f,
          "animated part presentation reaches the real GPU framebuffer");
    retained.SavePpm(output / "widget-styles-animation.ppm");
    animatedPage.SetVisible(false);
    window.RenderFrame(time + 1.1);
    Check(!ui.HasAnimations(), "hidden page animations do not request continuous frames");
    window.RenderFrame(time + 1.2);
    Check(retained.GetStats().m_RepaintPixels == 0 && retained.GetStats().m_SubmittedInstances == 0,
          "hidden animated page settles to zero analytic GPU work");

    std::ofstream report(output / "widget-styles.txt");
    report << "Widget styles GPU: PASS\nDriver: " << retained.GetDriverDescription() << '\n'
           << comparisons << " complete retained/full RGBA comparisons at 1x and 2x.\n"
           << "Real pixels: slider gradient/fill/handle, selection/caret, focus-within, row hover/selection,\n"
           << "tree branch expansion, sorted table header, selected cells, styled scrollbars.\n"
           << "Selection shadows, outer transforms, theme replacement, pointer endpoints and scroll clamps.\n"
           << RowCount << " list items and table rows: peak visible glyphs " << maxVisibleGlyphs << ".\n"
           << "500 animated fixed parts, 240 paint-only ticks: selector resolutions "
           << after.m_StyleResolutions - before.m_StyleResolutions << ", layout passes "
           << after.m_LayoutPasses - before.m_LayoutPasses << ".\n"
           << "Idle and hidden animated frames submit zero analytic work.\n";
}
} // namespace EStudioUI
