#include "EssentialGallery.h"
#include <EStudioUI/BusyIndicator.h>
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/Frame.h>
#include <EStudioUI/GroupBox.h>
#include <EStudioUI/RadioButton.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/Separator.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/Splitter.h>
#include <EStudioUI/ToolTip.h>
#include <glad/gl.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Essential GPU: ") + message);
}

std::vector<unsigned char> Pixels()
{
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) *
                                      4);
    glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

std::string Read(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Missing CSS: " + file.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}

Label& Heading(Widget& parent, std::string caption)
{
    auto& label = parent.Add<Label>(std::move(caption));
    label.AddClass("essential-pane-title");
    return label;
}
} // namespace

EssentialGallery::EssentialGallery(Window& window, Widget& page) : m_Window(window)
{
    page.SetId("essential-page");
    auto& scroller = page.Add<ScrollArea>();
    scroller.SetHeight(SizeRule::Fill());
    auto& body = scroller.Content();
    body.SetGap(12);
    auto& title = body.Add<Label>("Essential controls. One GPU pipeline.");
    title.SetStyleProperty(StyleProperty::FontSize, 24.0f);
    title.SetHeight(SizeRule::Content());
    auto& note = body.Add<Label>("Try keyboard navigation, a searchable popup, scrollable forms and draggable panes. "
                                 "Hover or focus a control for help.");
    note.SetWordWrap(true);
    note.SetHeight(SizeRule::Content());
    auto& choices = body.Add<GroupBox>("Choices and text input");
    choices.SetHeight(SizeRule::Fixed(230));
    choices.Content().SetLayoutDirection(Axis::Horizontal);
    choices.Content().SetGap(16);
    auto& left = choices.Content().Add<Widget>();
    left.SetWidth(SizeRule::Fill());
    left.SetGap(5);
    auto& snap = left.Add<CheckBox>("Enable snapping");
    snap.SetChecked(true);
    snap.SetToolTip("Snap geometric points while keeping your sketch editable.");
    auto& mixed = left.Add<CheckBox>("Mixed selection");
    mixed.SetTristate(true);
    mixed.SetCheckState(CheckState::Indeterminate);
    mixed.SetToolTip("Click to cycle unchecked, checked and indeterminate states.");
    auto radio = std::make_shared<RadioGroup>();
    left.Add<RadioButton>("Distance constraint", radio).SetChecked(true);
    left.Add<RadioButton>("Angle constraint", radio);
    auto& right = choices.Content().Add<Widget>();
    right.SetWidth(SizeRule::Fill());
    right.SetGap(8);
    Heading(right, "Virtual choices / editable values");
    auto& combo = right.Add<ComboBox>();
    combo.SetAccessibleName("Material");
    std::vector<std::string> items;
    for (int i = 0; i < 1000; ++i)
        items.push_back("Material " + std::to_string(i));
    combo.SetItems(std::move(items));
    combo.SetToolTip("Type to search. Alt+Down or F4 opens the virtual popup; Escape cancels its preview.");
    auto& editable = right.Add<ComboBox>();
    editable.SetEditable(true);
    editable.SetItems({"Millimeters", "Centimeters", "Meters", "Inches"});
    editable.SetAccessibleName("Units or custom label");
    editable.SetToolTip("Choose a preset or enter your own text. TextBox validators and composition remain available "
                        "through GetEditor().");
    auto& result = right.Add<Label>("Selection: Material 0");
    result.SetHeight(SizeRule::Content());
    const WidgetRef resultRef(&result), comboRef(&combo);
    combo.CurrentChanged().Connect(
        [resultRef, comboRef](int)
        {
            auto* label = dynamic_cast<Label*>(resultRef.Get());
            auto* control = dynamic_cast<ComboBox*>(comboRef.Get());
            if (label && control)
                label->SetText("Selection: " + control->GetCurrentText());
        });
    body.Add<Separator>();
    auto& splitter = body.Add<Splitter>();
    splitter.SetHeight(SizeRule::Fixed(260));
    splitter.SetToolTip("Drag the divider. Focus the splitter and use arrow keys for precise movement.");
    auto& form = splitter.Add<Frame>();
    Heading(form, "Scrollable form");
    auto& area = form.Add<ScrollArea>();
    area.SetHeight(SizeRule::Fill());
    for (int i = 0; i < 12; ++i)
    {
        auto& field = area.Content().Add<TextBox>("Parameter " + std::to_string(i + 1));
        field.SetHeight(SizeRule::Fixed(38));
        field.SetAccessibleName("Parameter " + std::to_string(i + 1));
        field.SetToolTip("Tab moves through the form and reveals the focused field automatically.");
    }
    auto& options = splitter.Add<GroupBox>("Workspace options");
    options.SetCheckable(true);
    options.Content().Add<CheckBox>("Show construction geometry").SetChecked(true);
    options.Content().Add<CheckBox>("Show constraints").SetChecked(true);
    auto& hint =
        options.Content().Add<Label>("Uncheck the title to disable this content. The title stays interactive.");
    hint.SetWordWrap(true);
    hint.SetHeight(SizeRule::Content());
    splitter.SetSizes({1, 1});
    auto& progressFrame = body.Add<Frame>();
    progressFrame.SetHeight(SizeRule::Fixed(120));
    Heading(progressFrame, "Progress that sleeps when it can");
    auto& row = progressFrame.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetGap(12);
    row.SetAlignment(LayoutAlignment::Center);
    row.SetHeight(SizeRule::Fill());
    auto& slider = row.Add<Slider>();
    slider.SetRange(0, 100);
    slider.SetValue(65);
    slider.SetWidth(SizeRule::Fill());
    slider.SetAccessibleName("Task progress");
    auto& progress = row.Add<ProgressBar>();
    progress.SetValue(65);
    progress.SetWidth(SizeRule::Fill());
    progress.SetHeight(SizeRule::Fixed(24));
    progress.SetAccessibleName("Task completion");
    const WidgetRef progressRef(&progress);
    slider.ValueChanged().Connect(
        [progressRef](double value)
        {
            if (auto* live = dynamic_cast<ProgressBar*>(progressRef.Get()))
                live->SetValue(value);
        });
    auto& busy = row.Add<BusyIndicator>();
    busy.SetWidth(SizeRule::Fixed(72));
    busy.SetHeight(SizeRule::Fixed(8));
    busy.SetIndeterminate(false);
    const WidgetRef busyRef(&busy);
    auto& animate = row.Add<Button>("Start / stop busy");
    animate.SetWidth(SizeRule::Content());
    animate.SetToolTip("Busy indicators request frames only while running and visible. This gallery starts idle.");
    animate.Clicked().Connect(
        [busyRef]
        {
            if (auto* live = dynamic_cast<BusyIndicator*>(busyRef.Get()))
                live->SetIndeterminate(!live->IsIndeterminate());
        });
}

void EssentialGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    WindowOptions options;
    options.m_Width = 900;
    options.m_Height = 640;
    options.m_Headless = m_Window.IsHeadless();
    options.m_Fonts.push_back({assets / "NotoSans.ttf", {}, 700});
    Window probe(options, assets / "NotoSans.ttf");
    auto& ui = probe.Ui();
    auto& gpu = probe.Graphics();
    std::string error;
    Check(ui.SetStyleSheet(Read(assets / "essential-widgets.css") +
                               "\nwindow {padding:16px;gap:9px;background:#0b1424;color:#e2edff;} label {height:auto;} "
                               ".probe-row {flex-direction:row;gap:12px;height:38px;} splitter {height:242px;} frame, "
                               "groupbox {min-width:160px;} ",
                           error),
          "rich batch stylesheet parses");
    auto& heading = ui.Root().Add<Label>("Essential widgets / analytic GPU drawing");
    heading.SetStyleProperty(StyleProperty::FontSize, 24.0f);
    auto& row = ui.Root().Add<Widget>();
    row.AddClass("probe-row");
    auto& check = row.Add<CheckBox>("Snapping");
    check.SetChecked(true);
    check.SetToolTip("Shared pointer and focus help without an animation loop.", 0.4);
    auto radio = std::make_shared<RadioGroup>();
    row.Add<RadioButton>("Length", radio).SetChecked(true);
    row.Add<RadioButton>("Angle", radio);
    auto& combo = ui.Root().Add<ComboBox>();
    std::vector<std::string> items;
    items.reserve(100000);
    for (int i = 0; i < 100000; ++i)
        items.push_back("Virtual option " + std::to_string(i));
    combo.SetItems(std::move(items));
    combo.SetCurrentIndex(99999);
    auto& meter = ui.Root().Add<ProgressBar>();
    meter.SetValue(65);
    meter.SetAccessibleName("Import progress");
    auto& split = ui.Root().Add<Splitter>();
    auto& left = split.Add<Frame>();
    Heading(left, "Scrollable parameters");
    auto& area = left.Add<ScrollArea>();
    area.SetHeight(SizeRule::Fill());
    TextBox* last = nullptr;
    for (int i = 0; i < 12; ++i)
    {
        last = &area.Content().Add<TextBox>("Parameter " + std::to_string(i));
        last->SetHeight(SizeRule::Fixed(38));
    }
    auto& group = split.Add<GroupBox>("Display options");
    group.SetCheckable(true);
    group.Content().Add<CheckBox>("Construction lines").SetCheckState(CheckState::Indeterminate);
    group.Content().Add<CheckBox>("Constraints").SetChecked(true);
    group.Content().Add<Separator>();
    auto& note = group.Content().Add<Label>("Rich CSS parts share the renderer, input and layout foundations.");
    note.SetWordWrap(true);
    note.SetHeight(SizeRule::Content());
    split.SetSizes({1, 1});
    probe.RenderFrame(1);
    probe.RenderFrame(2);
    const auto base = Pixels();
    gpu.SavePpm(output / "essential-widgets-0-18.ppm");
    Check(!ui.HasAnimations(), "static essential controls are idle");
    Check(gpu.GetStats().m_RepaintPixels == 0, "unchanged controls reuse retained framebuffer pixels");
    const auto content = meter.GetContentBounds();
    const auto filled = gpu.ReadPixel({content.m_X + content.m_Width * 0.2f, content.m_Y + content.m_Height * 0.5f});
    const auto empty = gpu.ReadPixel({content.m_X + content.m_Width * 0.9f, content.m_Y + content.m_Height * 0.5f});
    Check(filled.m_B + filled.m_G > empty.m_B + empty.m_G + 0.3f, "rich progress fill reaches actual pixels");
    gpu.SetPartialRedrawEnabled(false);
    probe.RenderFrame(2);
    Check(Pixels() == base, "retained and full redraws match for the entire new batch");
    gpu.SetPartialRedrawEnabled(true);
    const auto measurements = ui.GetStats().m_Measurements;
    meter.SetValue(35);
    probe.RenderFrame(2.2);
    Check(ui.GetStats().m_Measurements == measurements, "progress repaint retains intrinsic measurements");
    Check(Pixels() != base, "progress value changes actual GPU output");
    meter.SetValue(65);
    combo.ShowPopup();
    probe.RenderFrame(3);
    auto* popup = dynamic_cast<ListWidget*>(ui.GetPopup());
    Check(popup && popup->GetChildren().empty() && popup->GetItemCount() == 100000,
          "real GPU popup remains virtual at 100,000 records");
    Check(gpu.GetStats().m_Glyphs < 1800, "popup GPU glyph work stays bounded by the viewport");
    gpu.SavePpm(output / "essential-popup-0-18.ppm");
    ui.KeyEvent(Key::Escape, true);
    Check(combo.GetCurrentIndex() == 99999, "real popup cancellation preserves canonical selection");
    ui.SetFocus(&check);
    probe.RenderFrame(3.1);
    const auto beforeHelp = Pixels();
    probe.RenderFrame(3.6);
    Check(dynamic_cast<ToolTip*>(ui.GetPopup()) && ui.GetFocus() == &check, "GPU tooltip is nonactivating");
    Check(Pixels() != beforeHelp, "tooltip paints actual GPU pixels");
    gpu.SavePpm(output / "essential-tooltip-0-18.ppm");
    ui.KeyEvent(Key::Escape, true);
    probe.RenderFrame(3.6);
    Check(Pixels() == beforeHelp, "tooltip dismissal restores exact underlying pixels");
    area.SetScrollOffset({0, 0});
    probe.RenderFrame(3.7);
    const auto scrollMeasurements = ui.GetStats().m_Measurements;
    const auto thumb = area.GetScrollThumbBounds(false);
    const Point thumbStart{thumb.m_X + thumb.m_Width * 0.5f, thumb.m_Y + thumb.m_Height * 0.5f};
    ui.PointerDown(thumbStart);
    ui.PointerMove({thumbStart.m_X, thumbStart.m_Y + 48});
    ui.PointerUp({thumbStart.m_X, thumbStart.m_Y + 48});
    probe.RenderFrame(3.8);
    Check(area.GetScrollOffset().m_Y > 80 && area.Content().GetBounds().m_Y < area.GetViewport().m_Y - 80,
          "dragging the production ScrollArea thumb moves the actual clipped form");
    Check(ui.GetStats().m_Measurements == scrollMeasurements,
          "production scrollbar dragging retains unrelated intrinsic measurements");
    const auto dragged = Pixels();
    gpu.SetPartialRedrawEnabled(false);
    probe.RenderFrame(3.8);
    Check(Pixels() == dragged, "dragged ScrollArea retained pixels match a complete redraw");
    gpu.SetPartialRedrawEnabled(true);
    gpu.SavePpm(output / "essential-scroll-drag-0-19.ppm");
    ui.SetFocus(last);
    probe.RenderFrame(4);
    Check(last->GetBounds().Intersect(area.GetViewport()).m_Height > 30,
          "focused scrolling field is visible in the actual GPU form");
    gpu.SavePpm(output / "essential-scroll-0-18.ppm");
    const auto old = split.GetSizes();
    const auto h = split.GetHandleBounds(0);
    const Point center{h.m_X + h.m_Width * 0.5f, h.m_Y + 20};
    ui.PointerDown(center);
    ui.PointerMove({center.m_X + 70, center.m_Y});
    ui.PointerUp({center.m_X + 70, center.m_Y});
    probe.RenderFrame(5);
    Check(split.GetSizes()[0] > old[0] + 60, "real input resizes GPU panes");
    probe.SetSize(680, 520);
    probe.RenderFrame(6);
    Check(split.GetSizes()[0] >= 160 && split.GetSizes()[1] >= 160, "resizing preserves pane hard minima");
    gpu.SavePpm(output / "essential-resize-0-18.ppm");
    std::ofstream report(output / "essential-widgets-0-18.txt");
    report
        << "Essential widget GPU integration: PASS\nRich progress pixels; static retained reuse; full/retained exact "
           "equality;\n100,000-record virtual popup and viewport-bounded glyphs; popup cancellation;\nnonactivating "
           "tooltip pixels and exact dismissal; focus reveal; captured splitter drag; resize hard minima.\nDriver: "
        << gpu.GetDriverDescription() << "\n";
}
} // namespace EStudioUI
