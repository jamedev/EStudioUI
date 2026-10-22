#include "LayoutGallery.h"

#include <EStudioUI/Button.h>
#include <EStudioUI/FlowLayout.h>
#include <EStudioUI/FormLayout.h>
#include <EStudioUI/GridLayout.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/TextBox.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Layout foundation: " + message);
}

template <typename T> T& Live(const WidgetRef& reference)
{
    auto* widget = dynamic_cast<T*>(reference.Get());
    if (!widget)
        throw std::logic_error("Layout gallery target expired.");
    return *widget;
}

Label& Caption(Widget& parent, std::string text)
{
    auto& caption = parent.Add<Label>(std::move(text));
    caption.AddClass("pane-title");
    caption.SetHeight(SizeRule::Fixed(20));
    return caption;
}

std::vector<char> Bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Missing layout framebuffer capture.");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace

LayoutGallery::LayoutGallery(Window& window, Widget& page) : m_Window(window)
{
    auto& body = page.Add<Widget>();
    body.SetLayout(GridLayout({SizeRule::Fill(1, 260), SizeRule::Fill(1, 260)}, {SizeRule::Fill()}));
    body.SetHeight(SizeRule::Fill(1, 240));
    body.SetGap(12);
    auto& left = body.Add<Widget>();
    left.AddClass("gallery-pane");
    Caption(left, "FORM / SHARED COLUMNS + BASELINES");
    auto& form = left.Add<Widget>();
    form.SetId("layout-form");
    form.SetLayout(FormLayout(SizeRule::Content(0, 125), SizeRule::Fill(1, 100)));
    form.SetGap(10);
    m_Form = WidgetRef(&form);
    auto& label = form.Add<Label>("Part name");
    label.SetStyleProperty(StyleProperty::FontSize, 13.0f);
    auto& name = form.Add<TextBox>("Chassis bracket");
    name.SetHeight(SizeRule::Fixed(36));
    m_Name = WidgetRef(&name);
    form.Add<Label>("Thickness / mm").SetStyleProperty(StyleProperty::FontSize, 13.0f);
    auto& thickness = form.Add<NumericInput>();
    thickness.SetHeight(SizeRule::Fixed(36));
    thickness.SetValue(2.5);
    form.Add<Label>("Notes").SetStyleProperty(StyleProperty::FontSize, 13.0f);
    auto& notes = form.Add<Label>("Resize the window: this paragraph is measured at the allocated field width. "
                                  "The form row grows automatically and the labels keep their baselines aligned.");
    notes.SetId("layout-notes");
    notes.SetWordWrap(true);
    notes.SetStyleProperty(StyleProperty::FontSize, 13.0f);
    m_Notes = WidgetRef(&notes);
    auto& right = body.Add<Widget>();
    right.AddClass("gallery-pane");
    Caption(right, "FLOW / TOOLS WRAP INTO ROWS");
    auto& flow = right.Add<Widget>();
    flow.SetId("layout-flow");
    flow.SetLayout(FlowLayout{});
    flow.SetGap(7);
    m_Flow = WidgetRef(&flow);
    for (const auto* title : {"Select", "Line", "Arc", "Circle", "Trim", "Measure"})
    {
        auto& button = flow.Add<Button>(title);
        button.SetHeight(SizeRule::Fixed(32));
        button.SetWidth(SizeRule::Content(65, 120));
        button.SetStyleProperty(StyleProperty::Padding, Insets{4, 10, 4, 10});
        button.SetStyleProperty(StyleProperty::FontSize, 13.0f);
        button.Clicked().Connect(
            [target = m_Notes, tool = std::string(title)]
            {
                if (auto* label = dynamic_cast<Label*>(target.Get()))
                    label->SetText(tool +
                                   " selected. Layout strategies compose ordinary widgets; "
                                   "content height follows the current field width without manual geometry updates.");
            });
    }
    Caption(right, "GRID / FIXED + CAPPED FILL + FILL");
    auto& grid = right.Add<Widget>();
    grid.SetId("layout-grid");
    grid.SetLayout(GridLayout({SizeRule::Fixed(60), SizeRule::Fill(1, 30, 90), SizeRule::Fill(2, 40)}));
    grid.SetGap(7);
    m_Grid = WidgetRef(&grid);
    const Color colors[]{{0.3f, 0.55f, 0.85f, 1}, {0.6f, 0.45f, 0.85f, 1}, {0.2f, 0.65f, 0.55f, 1}};
    for (int i = 0; i < 3; ++i)
    {
        auto& bar = grid.Add<Widget>();
        bar.SetId("layout-track-" + std::to_string(i));
        bar.SetHeight(SizeRule::Fixed(26));
        bar.SetStyleProperty(StyleProperty::Background, colors[i]);
        bar.SetStyleProperty(StyleProperty::Radius, 5.0f);
        if (i == 0)
            m_Bar = WidgetRef(&bar);
    }
}

void LayoutGallery::Verify(const std::filesystem::path& output, const std::function<void()>& inspect)
{
    auto& ui = m_Window.Ui();
    auto& gpu = m_Window.Graphics();
    auto& notes = Live<Label>(m_Notes);
    float narrowHeight = 0, wideHeight = 0;
    std::ofstream report(output / "layout-foundation.txt");
    for (const int width : {880, 1180, 1520})
    {
        m_Window.SetSize(width, 900);
        m_Window.RenderFrame(ui.GetTime());
        inspect();
        m_Window.RenderFrame(ui.GetTime());
        const auto form = Live<Widget>(m_Form).GetLayoutReport();
        const auto& label = form.m_Children[0].m_Slot;
        const auto& field = form.m_Children[1].m_Slot;
        Check(label.m_Measure.m_Baseline && field.m_Measure.m_Baseline &&
                  std::abs(label.m_Bounds.m_Y + *label.m_Measure.m_Baseline - field.m_Bounds.m_Y -
                           *field.m_Measure.m_Baseline) < 0.02f,
              "label/editor painted baselines align");
        Check(notes.GetBounds().m_Y + notes.GetBounds().m_Height <=
                  notes.GetClip().m_Y + notes.GetClip().m_Height + 0.02f,
              "form notes remain inside the allocated pane after resize");
        const auto& tools = Live<Widget>(m_Flow).GetChildren();
        if (width == 880)
        {
            narrowHeight = notes.GetBounds().m_Height;
            Check(tools.back()->GetBounds().m_Y > tools.front()->GetBounds().m_Y,
                  "narrow tools wrap into multiple rows");
        }
        if (width == 1520)
            wideHeight = notes.GetBounds().m_Height;
        report << "Window width " << width << ": notes " << notes.GetBounds().m_Width << " x "
               << notes.GetBounds().m_Height << "; flow height " << Live<Widget>(m_Flow).GetBounds().m_Height << '\n';
        gpu.SavePpm(output / ("layout-foundation-" + std::to_string(width) + ".ppm"));
    }
    Check(narrowHeight > wideHeight, "actual native resize recomputes height for width");
    auto& name = Live<TextBox>(m_Name);
    ui.SetFocus(&name);
    name.SelectAll();
    ui.TextInput("Bracket / edited");
    Check(name.GetText() == "Bracket / edited", "form uses working existing text input");
    ui.SetFocus(nullptr);
    m_Window.SetSize(1280, 900);
    for (int frame = 0; frame < 3; ++frame)
        m_Window.RenderFrame(ui.GetTime());
    inspect();
    m_Window.RenderFrame(ui.GetTime());
    auto& bar = Live<Widget>(m_Bar);
    const auto before = ui.GetStats();
    bar.SetStyleProperty(StyleProperty::Background, Color{0.9f, 0.4f, 0.2f, 1});
    m_Window.RenderFrame(ui.GetTime());
    const auto after = ui.GetStats();
    Check(before.m_LayoutPlans == after.m_LayoutPlans &&
              before.m_ConstrainedMeasurements == after.m_ConstrainedMeasurements &&
              before.m_Arrangements == after.m_Arrangements,
          "paint-only color change retains all layout work");
    const auto bounds = bar.GetBounds();
    const auto pixel = gpu.ReadPixel({bounds.m_X + bounds.m_Width * 0.5f, bounds.m_Y + bounds.m_Height * 0.5f});
    Check(std::abs(pixel.m_R - 0.9f) < 0.02f && std::abs(pixel.m_G - 0.4f) < 0.02f,
          "actual framebuffer reflects paint-only change");
    gpu.SavePpm(output / "layout-foundation.ppm");
    gpu.SetPartialRedrawEnabled(false);
    ui.RequestRepaint();
    m_Window.RenderFrame(ui.GetTime());
    gpu.SavePpm(output / "layout-full-reference.ppm");
    Check(Bytes(output / "layout-foundation.ppm") == Bytes(output / "layout-full-reference.ppm"),
          "retained and full layout framebuffers match byte for byte");
    gpu.SetPartialRedrawEnabled(true);
    for (int frame = 0; frame < 3; ++frame)
        m_Window.RenderFrame(ui.GetTime());
    Check(gpu.GetStats().m_DrawCalls == 0, "settled layout gallery submits zero draw calls");
    report << "Native resize, working editor, mixed-font baseline alignment: PASS\n"
           << "Paint-only layout/constraint/arrange delta: 0/0/0\n"
           << "Retained/full framebuffer equality: PASS; idle draw calls: 0\n"
           << Live<Widget>(m_Form).GetLayoutReport().Explain() << Live<Widget>(m_Flow).GetLayoutReport().Explain()
           << Live<Widget>(m_Grid).GetLayoutReport().Explain();
}
} // namespace EStudioUI
