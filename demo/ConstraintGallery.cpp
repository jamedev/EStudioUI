#include "ConstraintGallery.h"

#include <EStudioUI/ConstraintLayout.h>
#include <EStudioUI/FormLayout.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/ToolBar.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <thread>

namespace EStudioUI
{
namespace
{
void Check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(std::string("Constraint gallery: ") + message);
}

Widget& Live(const WidgetRef& reference)
{
    auto* widget = reference.Get();
    if (!widget)
        throw std::logic_error("Constraint gallery receiver expired.");
    return *widget;
}

Label& Caption(Widget& parent, std::string text, float size = 14)
{
    auto& label = parent.Add<Label>(std::move(text));
    label.SetStyleProperty(StyleProperty::FontSize, size);
    label.SetWordWrap(true);
    label.SetHeight(SizeRule::Content());
    return label;
}

std::string Bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

class PaintProbe final : public Widget
{
  public:
    mutable int m_Paints = 0;
    mutable std::function<void()> m_OnNextPaint;

  protected:
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override
    {
        ++m_Paints;
        if (m_OnNextPaint)
        {
            auto callback = std::move(m_OnNextPaint);
            callback();
        }
        Widget::Paint(renderer, font, time, opacity);
    }
};

void VerifyCallbackResize(Window& owner, const std::filesystem::path& assets, std::ostream& report)
{
    WindowOptions options;
    options.m_Headless = owner.IsHeadless();
    options.m_RenderWhenUnfocused = true;
    options.m_Width = 320;
    options.m_Height = 180;
    Window probe(options, assets / "NotoSans.ttf");
    auto& box = probe.Ui().Root().Add<PaintProbe>();
    box.SetWidth(SizeRule::Fill());
    box.SetHeight(SizeRule::Fill());
    box.SetStyleProperty(StyleProperty::Background, Color{0.2f, 0.6f, 0.3f, 1});
    probe.RenderFrame();
    auto* native = reinterpret_cast<GLFWwindow*>(probe.GetContextIdentity());
    auto refresh = glfwSetWindowRefreshCallback(native, nullptr);
    glfwSetWindowRefreshCallback(native, refresh);
    Check(refresh != nullptr, "production native refresh callback exists");
    const auto before = probe.Ui().GetStats();
    const auto initial = box.m_Paints;
    const auto started = std::chrono::steady_clock::now();
    for (int step = 0; step < 6; ++step)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const int paints = box.m_Paints;
        probe.SetSize(360 + step * 11, 200 + step * 5);
        refresh(native);
        Check(box.m_Paints > paints, "native callback presents before returning without ProcessEvents or RenderFrame");
        const auto viewport = probe.Platform().GetWindowMetrics().m_LogicalSize;
        Check(std::abs(box.GetBounds().m_Width - viewport.m_X) < 0.02f,
              "callback frame uses the latest logical viewport");
        glfwMakeContextCurrent(native);
        const auto pixel = probe.Graphics().ReadPixel({viewport.m_X - 5, 5});
        Check(pixel.m_G > 0.55f && pixel.m_R < 0.25f, "newly exposed framebuffer area contains actual rendered pixels");
        for (int event = 0; event < 8; ++event)
            refresh(native);
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    Check(box.m_Paints - initial <= static_cast<int>(std::ceil(elapsed * 60)) + 1,
          "bursts of native refresh callbacks respect the same 60 Hz presentation cap");
    const auto after = probe.Ui().GetStats();
    Check(before.m_StyleResolutions == after.m_StyleResolutions && before.m_Measurements == after.m_Measurements,
          "native callbacks preserve style and intrinsic caches");
    report << "Production GLFW callbacks: " << box.m_Paints - initial
           << " actual paint traversals at six intermediate sizes, before returning to the application loop.\n"
           << "Resize style/intrinsic recalculations: 0/0; newly exposed framebuffer pixels: PASS.\n";
    const int beforeBatch = box.m_Paints;
    probe.Ui().States().Batch(
        [&]
        {
            probe.SetSize(450, 240);
            refresh(native);
            Check(box.m_Paints == beforeBatch, "native callbacks cannot expose an uncommitted state batch");
        });
    probe.RenderFrame();
    Check(std::abs(box.GetBounds().m_Width - 450) < 0.02f, "deferred batch resize reaches the next safe frame");
    report << "Refresh burst rate cap and state-batch traversal guard: PASS.\n";
    const auto delivery = probe.GetHandle().Post([](Window& target) { target.Ui().RequestRepaint(); });
    refresh(native);
    Check(delivery.GetStatus() == DispatchStatus::Completed,
          "native scheduler delivers queued UI work without polling events");
    const int beforePaint = box.m_Paints;
    box.m_OnNextPaint = [&]
    {
        probe.SetSize(470, 250);
        refresh(native);
        Check(box.m_Paints == beforePaint + 1, "resize during paint cannot recursively traverse the frame");
    };
    probe.Ui().RequestRepaint();
    probe.RenderFrame();
    probe.RenderFrame();
    Check(std::abs(box.GetBounds().m_Width - 470) < 0.02f, "resize during paint is delivered at the next safe frame");
    report << "Queued UI delivery and resize-during-paint reentrancy guard: PASS.\n";
    probe.Close();
}
} // namespace

ConstraintGallery::ConstraintGallery(Window& window, Widget& page) : m_Window(window)
{
    Caption(page, "Linear constraints / live resize", 24);
    Caption(page, "Drag any window border. Required anchors follow each intermediate size; soft preferences yield "
                  "when needed. CSS still styles every ordinary widget.");
    auto& toolbar = page.Add<ToolBar>();
    for (const auto& [title, width] :
         std::vector<std::pair<std::string, int>>{{"Narrow", 880}, {"Standard", 1180}, {"Wide", 1520}})
        toolbar.AddButton(title).Clicked().Connect([this, width] { m_Window.SetSize(width, 820); });
    auto& body = page.Add<Widget>();
    body.SetId("constraint-body");
    body.SetHeight(SizeRule::Fill(1, 300));
    m_Body = WidgetRef(&body);
    auto& sidebar = body.Add<Widget>();
    sidebar.SetId("constraint-sidebar");
    sidebar.AddClass("gallery-pane");
    sidebar.SetWidth(SizeRule::Fill(1, 160, 240));
    sidebar.SetHeight(SizeRule::Fill(1, 200));
    m_Sidebar = WidgetRef(&sidebar);
    Caption(sidebar, "PARAMETERS", 18);
    Caption(sidebar, "Preferred width: 220 px\nHard limits: 160 .. 240 px\nBottom follows the footer.");
    auto& form = sidebar.Add<Widget>();
    form.SetLayout(FormLayout{});
    form.SetGap(8);
    for (const auto* title : {"Width", "Height", "Radius"})
    {
        Caption(form, title, 12);
        auto& input = form.Add<NumericInput>();
        input.SetValue(24);
        input.SetHeight(SizeRule::Fixed(32));
        input.SetWidth(SizeRule::Fill(1, 40));
    }
    auto& document = body.Add<Widget>();
    document.SetId("constraint-document");
    document.AddClass("gallery-pane");
    document.SetWidth(SizeRule::Fill(1, 200));
    document.SetHeight(SizeRule::Fill(1, 200));
    m_Document = WidgetRef(&document);
    Caption(document, "DOCUMENT", 21);
    Caption(document, "Left = sidebar.Right + 12\nRight = parent.Right\nBottom = footer.Top - 12", 16);
    Caption(document,
            "This paragraph measures its height after the solver resolves its width. Intermediate resize "
            "frames reuse styles, font outlines, and intrinsic measurements. The solver updates its "
            "existing tableau when only window dimensions change.",
            17);
    auto& editor = document.Add<TextBox>("Edit this field while exploring the layout");
    editor.SetHeight(SizeRule::Fixed(38));
    editor.SetWidth(SizeRule::Fill(1, 100));
    auto& footer = body.Add<Widget>();
    footer.SetId("constraint-footer");
    footer.AddClass("gallery-pane");
    footer.SetWidth(SizeRule::Fill(1, 100));
    footer.SetHeight(SizeRule::Fixed(60));
    Caption(footer,
            "Required anchors + soft preferences + explicit size contracts. Inspect with GetLayoutReport().Explain().");
    auto& badge = body.Add<Widget>();
    badge.SetId("constraint-ratio");
    badge.SetWidth(SizeRule::Fill(1, 0));
    badge.SetHeight(SizeRule::Content());
    badge.SetStyleProperty(StyleProperty::Background, Color{0.25f, 0.53f, 0.85f, 1});
    badge.SetStyleProperty(StyleProperty::Radius, 8.0f);
    badge.SetStyleProperty(StyleProperty::Padding, Insets{10, 12, 10, 12});
    Caption(badge, "2 : 1", 20);
    m_Badge = WidgetRef(&badge);
    const ConstraintItem parent = ConstraintItem::Parent(), side(sidebar), doc(document), bottom(footer), tile(badge);
    ConstraintLayout layout;
    layout.Add(side.Left() == 0).Add(side.Top() == 0).Add(side.Bottom() == bottom.Top() - 12);
    layout.Add(side.Width() == 220, ConstraintStrength::Strong, "sidebar preference");
    layout.Add(doc.Left() == side.Right() + 12).Add(doc.Right() == parent.Right()).Add(doc.Top() == 0);
    layout.Add(doc.Bottom() == bottom.Top() - 12);
    layout.Add(bottom.Left() == 0).Add(bottom.Right() == parent.Right()).Add(bottom.Bottom() == parent.Bottom());
    layout.Add(tile.Right() == doc.Right() - 16).Add(tile.Bottom() == doc.Bottom() - 16);
    layout.Add(tile.Width() == 120).Add(tile.Height() == tile.Width() / 2);
    body.SetLayout(std::move(layout));
}

void ConstraintGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    std::ofstream report(output / "constraint-layout.txt");
    auto& ui = m_Window.Ui();
    auto& gpu = m_Window.Graphics();
    for (const int width : {880, 1180, 1520})
    {
        m_Window.SetSize(width, 860);
        m_Window.RenderFrame(ui.GetTime());
        const auto a = Live(m_Sidebar).GetBounds(), b = Live(m_Document).GetBounds();
        Check(std::abs(b.m_X - a.m_X - a.m_Width - 12) < 0.02f, "sidebar/document spacing survives native resize");
        const auto tile = Live(m_Badge).GetBounds();
        Check(std::abs(tile.m_Width - tile.m_Height * 2) < 0.02f, "aspect ratio survives native resize");
        for (const auto& issue : Live(m_Body).GetLayoutReport().m_Diagnostics)
            Check(issue.m_Issue != LayoutIssue::ConstraintConflict, "gallery constraints remain feasible");
        gpu.SavePpm(output / ("constraints-" + std::to_string(width) + ".ppm"));
        report << "Window " << width << ": sidebar " << a.m_Width << ", document " << b.m_Width << " x " << b.m_Height
               << "; 12 px spacing; badge 2:1.\n";
    }
    m_Window.SetSize(1180, 860);
    m_Window.RenderFrame(ui.GetTime());
    const auto before = ui.GetStats();
    Live(m_Badge).SetStyleProperty(StyleProperty::Background, Color{0.8f, 0.4f, 0.2f, 1});
    m_Window.RenderFrame(ui.GetTime());
    const auto after = ui.GetStats();
    Check(before.m_LayoutPlans == after.m_LayoutPlans && before.m_Arrangements == after.m_Arrangements,
          "paint-only styling does not solve or arrange");
    gpu.SavePpm(output / "constraints-retained.ppm");
    gpu.SetPartialRedrawEnabled(false);
    ui.RequestRepaint();
    m_Window.RenderFrame(ui.GetTime());
    gpu.SavePpm(output / "constraints-full.ppm");
    Check(Bytes(output / "constraints-retained.ppm") == Bytes(output / "constraints-full.ppm"),
          "constraint retained/full framebuffer equality");
    gpu.SetPartialRedrawEnabled(true);
    for (int i = 0; i < 3; ++i)
        m_Window.RenderFrame(ui.GetTime());
    Check(gpu.GetStats().m_DrawCalls == 0, "settled constraints submit zero draws");
    report << "Paint-only layout/arrange delta: 0/0. Retained/full framebuffer equality: PASS. Idle draw calls: 0.\n"
           << Live(m_Body).GetLayoutReport().Explain();
    VerifyCallbackResize(m_Window, assets, report);
    m_Window.RenderFrame(ui.GetTime());
}
} // namespace EStudioUI
