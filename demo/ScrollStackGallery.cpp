#include "ScrollStackGallery.h"

#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/PaintWidget.h>
#include <EStudioUI/ScrollBar.h>
#include <EStudioUI/StackedWidget.h>
#include <EStudioUI/ToolBar.h>
#include <glad/gl.h>

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Scroll/stack GPU: ") + message);
}

Point Center(Rect bounds)
{
    return {bounds.m_X + bounds.m_Width * .5f, bounds.m_Y + bounds.m_Height * .5f};
}

std::vector<std::uint8_t> Pixels()
{
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(viewport[2] * viewport[3] * 4));
    glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    return bytes;
}

//! Uses a fixed logical viewport extent so resizing scales the presentation without changing the scroll range.
class DrawingPreview final : public PaintWidget
{
  public:
    //! Creates a lightweight viewport with application-owned drawing state.
    DrawingPreview() : PaintWidget("drawing-preview")
    {
    }

    //! Pans one logical axis without changing intrinsic size or child layout.
    void SetOffset(bool horizontal, double value)
    {
        VerifyStructuralMutation();
        if (horizontal)
            m_Offset.m_X = static_cast<float>(value);
        else
            m_Offset.m_Y = static_cast<float>(value);
        InvalidatePainting();
    }

  protected:
    //! Draws only grid lines within the current 100 by 75 logical-unit viewport.
    void PaintContent(Painter& painter) const override
    {
        const auto bounds = GetPaintBounds();
        if (bounds.m_Width <= 0 || bounds.m_Height <= 0)
            return;
        painter.Scale(bounds.m_Width / 100, bounds.m_Height / 75);
        painter.SetPen({.18f, .27f, .37f, 1}, .2f);
        for (float x = 10 - std::fmod(m_Offset.m_X, 10); x < 100; x += 10)
            painter.DrawLine({x, 0}, {x, 75});
        for (float y = 10 - std::fmod(m_Offset.m_Y, 10); y < 75; y += 10)
            painter.DrawLine({0, y}, {100, y});
        painter.Translate(-std::fmod(m_Offset.m_X, 100), -std::fmod(m_Offset.m_Y, 75));
        painter.SetPen({.4f, .8f, 1, 1}, .45f);
        painter.SetBrush(Color{.09f, .3f, .45f, .7f});
        for (float x : {0.0f, 100.0f})
            for (float y : {0.0f, 75.0f})
            {
                painter.DrawRoundedRect({x + 12, y + 12, 68, 48}, 6);
                painter.SetBrush(Color{.12f, .45f, .56f, .6f});
                painter.DrawCircle({x + 46, y + 36}, 14);
                painter.DrawLine({x + 4, y + 36}, {x + 88, y + 36});
            }
    }

  private:
    //! Current logical viewport origin.
    Point m_Offset;
};
} // namespace

ScrollStackGallery::ScrollStackGallery(Window& window, Widget& page) : m_Window(window)
{
    page.AddClass("scroll-stack-gallery");
    page.SetGap(12);
    auto& title = page.Add<Label>("One workspace, persistent pages");
    title.AddClass("stack-heading");
    auto& note = page.Add<Label>("Switch pages without losing your inputs. Pan the preview with the scrollbars, "
                                 "or focus a bar and use arrows, Page Up / Down, Home and End.");
    note.SetWordWrap(true);
    note.SetHeight(SizeRule::Content());
    auto& choice = page.Add<ComboBox>();
    choice.SetItems({"Drawing preview", "Properties", "Short note"});
    choice.SetWidth(SizeRule::Fixed(260));
    choice.SetAccessibleName("Workspace page");
    auto& stack = page.Add<StackedWidget>();
    stack.SetHeight(SizeRule::Fill(1, 180));
    stack.SetAccessibleName("Drawing workspace");
    auto& drawing = stack.AddPage();
    drawing.SetGap(8);
    auto& row = drawing.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetHeight(SizeRule::Fill());
    row.SetGap(8);
    auto& preview = row.Add<DrawingPreview>();
    preview.SetWidth(SizeRule::Fill());
    preview.SetHeight(SizeRule::Fill());
    preview.SetAccessibleName("Pannable drawing preview");
    auto& vertical = row.Add<ScrollBar>();
    vertical.SetWidth(SizeRule::Content());
    vertical.SetHeight(SizeRule::Fill());
    vertical.SetRange(0, 675);
    vertical.SetPageStep(75);
    vertical.SetSingleStep(2);
    vertical.SetAccessibleName("Drawing vertical position");
    auto& horizontal = drawing.Add<ScrollBar>(Axis::Horizontal);
    horizontal.SetHeight(SizeRule::Content());
    horizontal.SetRange(0, 900);
    horizontal.SetPageStep(100);
    horizontal.SetSingleStep(2);
    horizontal.SetAccessibleName("Drawing horizontal position");
    const WidgetRef target(&preview);
    const auto pan = [target](bool horizontalAxis, double value)
    {
        if (auto* live = dynamic_cast<DrawingPreview*>(target.Get()))
            live->SetOffset(horizontalAxis, value);
    };
    vertical.ValueChanged().Connect([pan](double value) { pan(false, value); });
    horizontal.ValueChanged().Connect([pan](double value) { pan(true, value); });
    auto& properties = stack.AddPage();
    properties.SetGap(12);
    properties.Add<Label>("Drawing properties");
    properties.Add<CheckBox>("Snap to grid").SetChecked(true);
    auto& length = properties.Add<NumericInput>(42);
    length.SetRange(0, 1000);
    length.SetAccessibleName("Drawing length");
    properties.Add<Label>("These values remain here when you return to the page.").SetWordWrap(true);
    auto& notes = stack.AddPage();
    notes.SetGap(12);
    notes.Add<Label>("Short note");
    notes.Add<TextBox>("A persistent sketch note").SetAccessibleName("Sketch note");
    notes.Add<Label>("Document editing will use the separate document layer planned in F10b.").SetWordWrap(true);
    const WidgetRef pages(&stack);
    choice.CurrentChanged().Connect(
        [pages](int index)
        {
            if (auto* live = dynamic_cast<StackedWidget*>(pages.Get()); live && index >= 0)
                live->SetCurrentIndex(index);
        });
}

void ScrollStackGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    WindowOptions options;
    options.m_Width = 640;
    options.m_Height = 480;
    options.m_Headless = m_Window.IsHeadless();
    Window probe(options, assets / "NotoSans.ttf");
    auto& ui = probe.Ui();
    auto& gpu = probe.Graphics();
    std::string error;
    Check(ui.SetStyleSheet("window { background:#101820; padding:16px; gap:12px; } "
                           "stackedwidget, stackpage { padding:0; border-width:0; } "
                           "#first { background:linear-gradient(90deg,#cf3525,#f07030); } "
                           "#second { background:linear-gradient(90deg,#2548d0,#30a0ef); } "
                           "scrollbar { padding:0; border:0px solid transparent; } "
                           "scrollbar::scroll-track { background:#202d40; padding:2px; border-radius:8px; } "
                           "scrollbar::scroll-track:horizontal { height:20px; } "
                           "scrollbar::scroll-thumb { background:#30c090; border-radius:6px; min-width:24px; } "
                           "scrollbar::scroll-thumb:hover { background:#e8b030; width:30px; } "
                           "scrollbar::scroll-thumb:pressed { background:#e030a0; }",
                           error),
          "Probe CSS compiles");
    auto& stack = ui.Root().Add<StackedWidget>();
    stack.SetHeight(SizeRule::Fill());
    stack.AddPage().SetId("first");
    stack.AddPage().SetId("second");
    auto& bar = ui.Root().Add<ScrollBar>(Axis::Horizontal);
    bar.SetHeight(SizeRule::Fixed(20));
    bar.SetRange(0, 900);
    bar.SetPageStep(100);
    probe.RenderFrame();
    Check(gpu.ReadPixel({40, 40}).m_R > .7f, "Only the first selected page reaches the framebuffer");
    auto thumb = Center(bar.GetThumbBounds());
    Check(gpu.ReadPixel(thumb).m_G > .6f, "Proportional thumb uses its GPU CSS fill");
    const auto initial = ui.GetStats();
    bar.SetValue(450);
    probe.RenderFrame(ui.GetTime());
    Check(ui.GetStats().m_Measurements == initial.m_Measurements &&
              ui.GetStats().m_Arrangements == initial.m_Arrangements &&
              ui.GetStats().m_StyleResolutions == initial.m_StyleResolutions,
          "Value-only redraw retains CSS and layout work");
    const auto moved = Pixels();
    gpu.SetPartialRedrawEnabled(false);
    probe.RenderFrame(ui.GetTime());
    Check(moved == Pixels(), "Moved thumb retained pixels exactly match a full redraw");
    gpu.SetPartialRedrawEnabled(true);
    const auto normalThumb = bar.GetThumbBounds();
    ui.PointerMove({bar.GetTrackBounds().m_X + 4, Center(normalThumb).m_Y});
    probe.RenderFrame(ui.GetTime());
    Check(std::abs(bar.GetThumbBounds().m_Width - normalThumb.m_Width) < .01f &&
              gpu.ReadPixel(Center(normalThumb)).m_R < .3f,
          "Track hover leaves the thumb's proportional geometry and neutral color intact");
    thumb = Center(normalThumb);
    ui.PointerMove({normalThumb.m_X + 2, thumb.m_Y});
    probe.RenderFrame(ui.GetTime());
    Check(std::abs(bar.GetThumbBounds().m_Width - 30) < .01f && gpu.ReadPixel(thumb).m_R > .8f &&
              gpu.ReadPixel(thumb).m_G > .5f,
          "Local thumb hover paints its CSS variant");
    ui.PointerDown(thumb);
    probe.RenderFrame(ui.GetTime());
    Check(gpu.ReadPixel(thumb).m_R > .8f && gpu.ReadPixel(thumb).m_B > .5f,
          "Captured thumb paints its pressed CSS variant");
    ui.PointerMove({thumb.m_X + 100, thumb.m_Y});
    ui.PointerUp({thumb.m_X + 100, thumb.m_Y});
    probe.RenderFrame(ui.GetTime());
    Check(bar.GetValue() > 600, "Actual production pointer routing drags the standalone scrollbar");
    stack.SetCurrentIndex(1);
    probe.RenderFrame(ui.GetTime());
    Check(gpu.ReadPixel({40, 40}).m_B > .7f && gpu.ReadPixel({40, 40}).m_R < .3f,
          "Switching pages removes the previous styled pixels");
    ui.PointerMove({1, 1});
    probe.RenderFrame(ui.GetTime());
    probe.RenderFrame(ui.GetTime());
    Check(gpu.GetStats().m_RepaintPixels == 0 && !ui.HasAnimations(),
          "Settled pages and scrollbars reuse the framebuffer without animation deadlines");
    for (const Point size : {Point{360, 300}, Point{760, 520}})
    {
        probe.SetSize(static_cast<int>(size.m_X), static_cast<int>(size.m_Y));
        probe.RenderFrame(ui.GetTime());
        Check(stack.GetCurrentPage()->GetBounds().m_Width == stack.GetContentBounds().m_Width,
              "The active fill page follows narrow and wide resize allocations");
        const auto resized = Pixels();
        gpu.SetPartialRedrawEnabled(false);
        probe.RenderFrame(ui.GetTime());
        Check(resized == Pixels(), "Resized stack/scrollbar retained pixels match a full redraw");
        gpu.SetPartialRedrawEnabled(true);
    }
    gpu.SavePpm(output / "scroll-stack-widgets.ppm");
    std::ofstream report(output / "scroll-stack-report.txt");
    report << "ScrollBar / StackedWidget actual GPU: PASS\nProportional CSS thumb; hover/pressed; captured drag; "
              "active-page gradients; narrow/wide resize; exact retained/full pixels; zero settled repaint; "
              "value-only CSS/intrinsic/layout retention.\n";
}
} // namespace EStudioUI
