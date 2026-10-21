#include "InputControlsProbe.h"

#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/SplitToolButton.h>
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
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string("Input controls GPU: ") + message);
}

Point Center(Rect bounds)
{
    return {bounds.m_X + bounds.m_Width * 0.5f, bounds.m_Y + bounds.m_Height * 0.5f};
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

void Compare(const std::vector<unsigned char>& first, const std::vector<unsigned char>& second)
{
    Check(first.size() == second.size(), "matching framebuffer sizes");
    for (std::size_t index = 0; index < first.size(); ++index)
        Check(std::abs(static_cast<int>(first[index]) - static_cast<int>(second[index])) <= 1,
              "retained and full control pixels match");
}

std::string Theme(bool light)
{
    const std::string common =
        "window { padding: 24px; gap: 12px; font-size: 16px; } "
        "#input-title { font-size: 24px; } "
        "#input-caption { font-size: 13px; } "
        "numericinput { padding: 0; border-width: 0; gap: 3px; } "
        "numericinput textbox, numericinput toolbutton { height: 38px; } "
        "splittoolbutton toolbutton { height: 34px; } "
        "textbox { padding: 8px; border: 1px solid #667399; border-radius: 5px; } "
        "toolbutton { padding: 6px; border: 1px solid #667399; border-radius: 5px; text-align: center; } "
        "toolbutton:hover, toolbutton:focus { border-color: #697cff; } "
        "slider { padding: 0; border-width: 0; selection-background-color: #697cff; } "
        "contextmenu { width: 220px; padding: 6px; border: 1px solid #667399; border-radius: 6px; } "
        "contextmenu toolbutton { height: 32px; text-align: left; } ";
    return common + (light ? "window { background: #f2f5fc; color: #182038; } "
                             "textbox, toolbutton, contextmenu { background: #ffffff; color: #182038; } "
                             "slider { grid-color: #cbd5e4; color: #182038; }"
                           : "window { background: #101828; color: #eef2ff; } "
                             "textbox, toolbutton, contextmenu { background: #202b42; color: #eef2ff; } "
                             "slider { grid-color: #536174; color: #eef2ff; }");
}
} // namespace

void VerifyInputControls(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    std::filesystem::create_directories(output);
    WindowOptions options;
    options.m_Width = 560;
    options.m_Height = 390;
    options.m_Headless = headless;
    Window window(options, assets / "NotoSans.ttf");
    auto& ui = window.Ui();
    auto& title = ui.Root().Add<Label>("Sketch input controls");
    title.SetId("input-title");
    title.SetHeight(SizeRule::Fixed(32));
    auto& caption = ui.Root().Add<Label>("Validated values, GPU sliders, remembered tool actions");
    caption.SetId("input-caption");
    caption.SetHeight(SizeRule::Fixed(24));
    auto& numeric = ui.Root().Add<NumericInput>(10);
    numeric.SetWidth(SizeRule::Fixed(360));
    numeric.SetHeight(SizeRule::Fixed(38));
    numeric.SetRange(0, 100);
    numeric.SetStep(.5);
    numeric.SetDecimals(2);
    auto& slider = ui.Root().Add<Slider>();
    slider.SetWidth(SizeRule::Fixed(360));
    slider.SetHeight(SizeRule::Fixed(32));
    slider.SetValue(25);
    auto& validated = ui.Root().Add<TextBox>("125");
    validated.SetWidth(SizeRule::Fixed(360));
    validated.SetHeight(SizeRule::Fixed(38));
    validated.SetValidator(std::make_shared<IntegerValidator>(0, 200));
    auto& chooser = ui.Root().Add<SplitToolButton>("Primitives");
    chooser.SetHeight(SizeRule::Fixed(34));
    chooser.AddAction("Line");
    chooser.AddAction("Circle");
    chooser.AddAction("Arc");
    int numericChanges = 0;
    int releases = 0;
    int submissions = 0;
    int commands = 0;
    numeric.ValueChanged().Connect([&](double) { ++numericChanges; });
    slider.SliderReleased().Connect([&] { ++releases; });
    validated.Submitted().Connect([&](const std::string&) { ++submissions; });
    chooser.Triggered().Connect(
        [&](std::size_t index)
        {
            Check(index == 1, "remembered tool command is Circle");
            ++commands;
        });
    std::string error;
    Check(ui.SetStyleSheet(Theme(false), error), "dark probe CSS parses");
    window.RenderFrame(0);
    Check(numeric.GetBounds().m_Width == 360 && slider.GetBounds().m_Width == 360,
          "explicit control widths survive CSS");
    const auto sliderBounds = slider.GetContentBounds();
    const float trackY = sliderBounds.m_Y + sliderBounds.m_Height * .5f;
    const auto active = window.Graphics().ReadPixel({sliderBounds.m_X + 35, trackY});
    const auto groove = window.Graphics().ReadPixel({sliderBounds.m_X + 300, trackY});
    Check(active.m_B > .9f && active.m_R > .3f && active.m_R < .6f, "GPU slider active track uses CSS selection color");
    Check(groove.m_B > .4f && groove.m_B < .6f && groove.m_R < .4f, "GPU slider remaining groove uses CSS grid color");
    const float handleX = sliderBounds.m_X + 8 + (sliderBounds.m_Width - 16) * .25f;
    Check(window.Graphics().ReadPixel({handleX, trackY}).m_R > .8f,
          "GPU slider draws a filled analytic circular handle");
    window.Graphics().SavePpm(output / "input-controls-dark.ppm");

    auto& editor = numeric.GetEditor();
    ui.SetFocus(&editor);
    editor.SelectAll();
    ui.TextInput("12.5");
    ui.KeyEvent(Key::Enter, true);
    Check(numeric.GetValue() == 12.5 && numericChanges == 1, "numeric Enter commits typed data");
    ui.KeyEvent(Key::Up, true);
    ui.Scroll(Center(editor.GetBounds()), {0, 1});
    Check(numeric.GetValue() == 13.5 && numericChanges == 3, "focused keyboard and wheel apply numeric steps");
    editor.SelectAll();
    ui.TextInput("NaN");
    Check(editor.GetText() == "13.50" && editor.GetSelectedText() == "13.50", "invalid numeric insertion is atomic");
    ui.TextInput("-");
    ui.KeyEvent(Key::Enter, true);
    Check(editor.GetText() == "13.50" && numeric.GetValue() == 13.5,
          "incomplete numeric commit restores the last value");
    ui.SetFocus(&validated);
    validated.SelectAll();
    ui.TextInput("125x");
    Check(validated.GetText() == "125", "integer validator rejects a trailing character");
    ui.TextInput("-");
    ui.KeyEvent(Key::Enter, true);
    Check(submissions == 0 && validated.GetValidationState() == ValidationState::Intermediate,
          "intermediate text remains editable without submitting");
    ui.SetFocus(&chooser.GetPrimaryButton());
    Check(validated.GetText() == "-", "plain validated editor does not promote input on focus loss");
    validated.SetText("125");

    ui.PointerDown({sliderBounds.m_X + 180, trackY});
    ui.PointerMove({sliderBounds.m_X + 800, trackY});
    ui.PointerUp({sliderBounds.m_X + 800, trackY});
    Check(slider.GetValue() == 100 && releases == 1, "captured slider drag reaches its upper endpoint");
    ui.KeyEvent(Key::Home, true);
    ui.KeyEvent(Key::Right, true);
    Check(slider.GetValue() == 1, "slider keyboard Home and Right operate through normal routing");
    slider.SetValue(25);

    ui.SetFocus(&chooser.GetPrimaryButton());
    ui.KeyEvent(Key::Down, true);
    window.RenderFrame(.1);
    Check(chooser.IsMenuOpen(), "split button keyboard command opens its popup");
    window.Graphics().SavePpm(output / "input-controls-popup.ppm");
    const auto circlePoint = Center(chooser.GetAction(1).GetBounds());
    ui.PointerDown(circlePoint);
    ui.PointerUp(circlePoint);
    Check(!chooser.IsMenuOpen() && chooser.GetDefaultActionIndex() == 1 && commands == 1,
          "popup pointer selection remembers and activates its action");
    window.RenderFrame(.2);
    const auto primaryPoint = Center(chooser.GetPrimaryButton().GetBounds());
    ui.PointerDown(primaryPoint);
    ui.PointerUp(primaryPoint);
    Check(commands == 2 && chooser.GetPrimaryButton().GetText() == "Circle",
          "primary split command repeats the remembered action");

    int comparisons = 0;
    for (const bool light : {false, true})
    {
        ui.SetFocus(nullptr);
        ui.PointerMove({550, 380});
        Check(ui.SetStyleSheet(Theme(light), error), "probe theme reload parses");
        window.RenderFrame(.3);
        Check(numeric.GetBounds().m_Width == 360 && slider.GetBounds().m_Width == 360,
              "Fixed control sizes survive a theme reload");
        const auto retained = Pixels();
        window.RenderFrame(.3);
        Check(window.Graphics().GetStats().m_RepaintPixels == 0 &&
                  window.Graphics().GetStats().m_SubmittedInstances == 0,
              "idle input controls submit zero analytic GPU work");
        Compare(retained, Pixels());
        window.Graphics().SetPartialRedrawEnabled(false);
        window.RenderFrame(.3);
        Compare(retained, Pixels());
        ++comparisons;
        window.Graphics().SetPartialRedrawEnabled(true);
        window.Graphics().SavePpm(output / (light ? "input-controls-light.ppm" : "input-controls-dark.ppm"));
    }
    std::ofstream report(output / "input-controls.txt");
    report << "Input controls GPU: PASS\nDriver: " << window.Graphics().GetDriverDescription() << '\n'
           << comparisons << " complete retained/full RGBA comparisons, dark and light themes.\n"
           << "Numeric commit/rollback, strict validators, keyboard/wheel, captured slider drag, CSS track/handle "
              "pixels,\n"
           << "split action selection/repetition, explicit sizing across theme reload, zero-work idle frames.\n";
}
} // namespace EStudioUI
