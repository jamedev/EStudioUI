#include "GpuWorkload.h"
#include "WidgetGallery.h"
#include "CanvasPrimitiveProbe.h"
#include "CanvasProbe.h"
#include "CanvasCurvesProbe.h"
#include "PainterProbe.h"
#include "InputControlsProbe.h"
#include "WidgetStylesProbe.h"

#include <EStudioUI/Button.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Window.h>

#include <algorithm>
#include <chrono>
#include <vector>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
using namespace EStudioUI;
namespace fs = std::filesystem;

void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Verification failed: " + message);
}

Point Center(const Widget& widget)
{
    const auto b = widget.GetBounds();
    return {b.m_X + b.m_Width * 0.5f, b.m_Y + b.m_Height * 0.5f};
}

void Click(UiContext& ui, Widget& widget)
{
    ui.PointerDown(Center(widget));
    ui.PointerUp(Center(widget));
}

//! Measures the rendering backend with a fixed, visible stress scene.
void Benchmark(Window& window, const fs::path& output)
{
    fs::create_directories(output);
    window.SetSize(1600, 900);
    window.RenderFrame();
    auto& gpu = window.Graphics();
    gpu.SetPartialRedrawEnabled(false);
    const auto label = window.Ui().GetFont().Shape("Button", 11);
    const Rect clip{0, 0, 1600, 900};
    Style style;
    style.m_Background = {0.18f, 0.24f, 0.38f, 1};
    style.m_BorderColor = {0.4f, 0.5f, 0.75f, 1};
    style.m_BorderWidth = 1;
    style.m_Radius = 6;
    std::vector<double> samples;
    for (int frame = 0; frame < 35; ++frame)
    {
        const auto start = std::chrono::steady_clock::now();
        gpu.BeginFrame(1600, 900, 1600, 900, {0.04f, 0.06f, 0.1f, 1});
        for (int row = 0; row < 20; ++row)
        {
            for (int column = 0; column < 32; ++column)
            {
                const float x = static_cast<float>(column) * 50 + 1;
                const float y = static_cast<float>(row) * 45 + 1;
                gpu.DrawBox({x, y, 48, 42}, style, clip);
                gpu.DrawText(label, {x + (48 - label.m_Width) * 0.5f, y + 24}, {1, 1, 1, 1}, clip);
            }
        }
        gpu.EndFrame();
        // Readback forces completion. Timings include CPU submission, GPU work,
        // and this synchronization; they are not an isolated GPU timer query.
        (void)gpu.ReadPixel({0, 0});
        const auto end = std::chrono::steady_clock::now();
        if (frame >= 5)
            samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    const auto stats = gpu.GetStats();
    Check(stats.m_DrawCalls == 1 && stats.m_Shapes == 640 && stats.m_Glyphs == 3840, "benchmark scene counts");
    gpu.SavePpm(output / "benchmark.ppm");
    std::ofstream csv(output / "benchmark.csv");
    csv << "frame,milliseconds\n";
    double average = 0;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        csv << i << ',' << samples[i] << '\n';
        average += samples[i];
    }
    average /= static_cast<double>(samples.size());
    std::sort(samples.begin(), samples.end());
    std::ofstream report(output / "benchmark.txt");
    report << "Renderer stress scene: 640 rounded boxes, 3840 analytic glyphs, 1600x900, one draw call.\n"
           << "Driver: " << gpu.GetDriverDescription() << "\n30 measured frames after 5 warm-up frames.\n"
           << "Average: " << average << " ms; median: " << samples[samples.size() / 2] << " ms.\n"
           << "Includes CPU submission and synchronous readback. Excludes widget layout and shaping.\n"
           << "Software OpenGL timings describe this CPU environment, not hardware GPU performance.\n";
    std::cout << "Benchmark: 640 boxes + 3840 glyphs / one draw / " << average << " ms average\n";
}

//! Owns demo state for as long as its signal callbacks remain connected.
class Demo
{
  public:
    Demo(Window& window, fs::path assets, fs::path initialCss)
        : m_Window(window), m_Assets(std::move(assets)), m_CurrentCss(std::move(initialCss))
    {
        auto& root = window.Ui().Root();
        auto& header = root.Add<Widget>();
        header.SetId("header");
        auto& brand = header.Add<Widget>();
        brand.SetId("brand");
        auto& title = brand.Add<Label>("EStudioUI / widget lab");
        title.SetId("title");
        auto& subtitle = brand.Add<Label>("GPU controls. Property animation. Independent 3D contexts.");
        subtitle.AddClass("subtitle");
        auto& themes = header.Add<Widget>();
        themes.SetId("themes");
        auto& dark = themes.Add<Button>("Dark");
        auto& light = themes.Add<Button>("Light");
        auto& reload = themes.Add<Button>("Reload CSS");
        m_Tabs = &root.Add<TabWidget>();
        m_Tabs->SetId("gallery");
        m_Tabs->SetHeight(SizeRule::Fill());
        auto& inputs = m_Tabs->AddTab("Inputs");
        auto& workspace = inputs.Add<Widget>();
        workspace.SetId("workspace");
        auto& left = workspace.Add<Widget>();
        left.AddClass("card");
        AddLabel(left, "01 / SIGNALS & ACTIONS", "eyebrow");
        AddLabel(left, "Buttons that do things", "section-title");
        AddLabel(left, "Hover, press, release, or use your keyboard.\nTyped signals connect the controls to your code.",
                 "description");
        auto& actions = left.Add<Widget>();
        actions.AddClass("row");
        m_Increment = &actions.Add<Button>("Increment");
        m_Increment->AddClass("primary");
        m_Reset = &actions.Add<Button>("Reset");
        m_Disabled = &actions.Add<Button>("Disabled");
        m_Disabled->SetEnabled(false);
        m_CountLabel = &AddLabel(left, "0 clicks / ready for a signal", "");
        m_CountLabel->SetId("click-count");
        left.Add<Widget>().AddClass("divider");
        AddLabel(left, "YOUR NAME", "field-label");
        m_Name = &left.Add<TextBox>();
        m_Name->SetId("name");
        m_Name->SetPlaceholder("Type your name here...");
        auto& formActions = left.Add<Widget>();
        formActions.AddClass("row");
        m_Greet = &formActions.Add<Button>("Say hello");
        m_Greet->AddClass("primary");
        auto& clear = formActions.Add<Button>("Clear");
        m_Message = &left.Add<Label>("Your next idea starts here.\nType a name, then press Enter or Say hello.");
        m_Message->SetId("message");

        auto& right = workspace.Add<Widget>();
        right.AddClass("card");
        AddLabel(right, "02 / TEXT & PRESENTATION", "eyebrow");
        AddLabel(right, "Text, without a bitmap atlas", "section-title");
        AddLabel(right,
                 "Glyph outlines are evaluated in the fragment shader.\nEdit, select, copy, paste, undo, and try "
                 "another theme.",
                 "description");
        m_Editor = &right.Add<TextBox>("Café, naïve, résumé — editable UTF-8");
        m_Editor->SetId("editor");
        AddLabel(right, "PLACEHOLDER & READ-ONLY", "field-label");
        auto& empty = right.Add<TextBox>();
        empty.SetPlaceholder("A placeholder, styled with CSS");
        m_ReadOnly = &right.Add<TextBox>("Read-only text / select and copy me");
        m_ReadOnly->SetReadOnly(true);
        auto& css = right.Add<Label>("textbox:focus {\n  border: 2px solid #8c9bff;\n  background: #171d33;\n}");
        css.SetId("css-sample");
        m_EditStatus = &right.Add<Label>("0 changes / press Enter to submit");
        m_EditStatus->SetId("edit-status");
        auto& footer = root.Add<Label>("Tab / Shift+Tab: focus    Space / Enter: activate    Ctrl/Cmd+A C X V Z: edit  "
                                       "  F5: reload CSS    F6: theme");
        footer.SetId("footer");

        m_Gallery = std::make_unique<WidgetGallery>(window, *m_Tabs, m_Assets);

        m_Increment->Clicked().Connect(
            [this]
            {
                ++m_Clicks;
                m_CountLabel->SetText(std::to_string(m_Clicks) + " clicks / Clicked() signal received");
            });
        m_Reset->Clicked().Connect(
            [this]
            {
                m_Clicks = 0;
                m_CountLabel->SetText("0 clicks / counter reset");
            });
        m_Disabled->Clicked().Connect([this] { ++m_DisabledClicks; });
        m_Greet->Clicked().Connect([this] { Greet(); });
        clear.Clicked().Connect(
            [this]
            {
                m_Name->SetText("");
                m_Window.Ui().SetFocus(m_Name);
            });
        m_Name->Submitted().Connect([this](const std::string&) { Greet(); });
        m_Editor->TextChanged().Connect(
            [this](const std::string& text)
            {
                ++m_Edits;
                m_EditStatus->SetText(std::to_string(m_Edits) + " changes / " + std::to_string(text.size()) +
                                      " UTF-8 bytes");
            });
        m_Editor->Submitted().Connect(
            [this](const std::string&)
            {
                ++m_Submissions;
                m_EditStatus->SetText("Submitted() signal received / " + std::to_string(m_Submissions));
            });
        dark.Clicked().Connect([this] { LoadCss(m_Assets / "dark.css"); });
        light.Clicked().Connect([this] { LoadCss(m_Assets / "light.css"); });
        reload.Clicked().Connect([this] { LoadCss(m_CurrentCss); });
        m_Keys = window.KeyPressed().Connect(
            [this](Key key)
            {
                if (key == Key::F5)
                    LoadCss(m_CurrentCss);
                else if (key == Key::F6)
                    LoadCss(m_Assets / (m_CurrentCss.filename() == "light.css" ? "dark.css" : "light.css"));
            });
        if (!LoadCss(m_CurrentCss))
            throw std::runtime_error("Initial CSS could not be loaded.");
        m_Tabs->SetCurrentIndex(8);
    }

    ~Demo()
    {
        m_Keys.Disconnect();
        m_Window.Ui().Root().ClearChildren();
    }

    void Profile(const fs::path& output)
    {
        ProfileUiWorkload(m_Window, *m_Tabs, m_Assets, output);
    }

    bool LoadCss(const fs::path& path)
    {
        std::string error;
        std::ifstream theme(path, std::ios::binary);
        std::ifstream animation(m_Assets / "animation-start.css", std::ios::binary);
        std::ifstream rich(m_Assets / "rich-buttons.css", std::ios::binary);
        std::ifstream widgets(m_Assets / "rich-widgets.css", std::ios::binary);
        bool loaded = false;
        if (theme && animation && rich && widgets)
            loaded = m_Window.Ui().SetStyleSheet(std::string(std::istreambuf_iterator<char>(theme), {}) + "\n" +
                                                     std::string(std::istreambuf_iterator<char>(animation), {}) + "\n" +
                                                     std::string(std::istreambuf_iterator<char>(rich), {}) + "\n" +
                                                     std::string(std::istreambuf_iterator<char>(widgets), {}),
                                                 error);
        else
            error = "Cannot read theme, animation-start.css, rich-buttons.css, or rich-widgets.css.";
        if (!loaded)
        {
            m_Message->AddClass("error");
            m_Message->SetText("CSS reload failed; previous theme retained.\n" + error);
            std::cerr << error << '\n';
            return false;
        }
        m_CurrentCss = path;
        const bool recovering = m_Message->GetText().starts_with("CSS reload failed;");
        m_Message->RemoveClass("error");
        if (recovering)
            m_Message->SetText("CSS reloaded successfully.\nYour new styles are now visible.");
        m_Window.RequestRepaint();
        return true;
    }

    void Verify(const fs::path& output)
    {
        fs::create_directories(output);
        auto& ui = m_Window.Ui();
        auto& gpu = m_Window.Graphics();
        m_Tabs->SetCurrentIndex(0);
        m_Window.RenderFrame();
        gpu.SavePpm(output / "dark.ppm");
        const auto stats = gpu.GetStats();
        Check(stats.m_DrawCalls == 1 && stats.m_Glyphs > 300, "ordered single-draw rendering");
        Check(stats.m_FontBytes > 0, "GPU outline buffers populated");
        const auto cachedBytes = stats.m_FontBytes;
        m_Window.RenderFrame();
        Check(gpu.GetStats().m_FontBytes == cachedBytes, "stable glyph cache across frames");
        Check(gpu.ReadPixel({4, 4}).m_R < 0.1f, "dark CSS reaches framebuffer");
        const auto point = Center(*m_Increment);
        const Point probe{m_Increment->GetBounds().m_X + 12, m_Increment->GetBounds().m_Y + 12};
        const auto normal = gpu.ReadPixel(probe);
        ui.PointerMove(point);
        m_Window.RenderFrame();
        Check(m_Increment->IsHovered(), "hover dispatch");
        Check(std::abs(gpu.ReadPixel(probe).m_R - normal.m_R) > 0.03f, "hover CSS changes pixels");
        ui.PointerDown(point);
        Check(m_Increment->IsPressed(), "pressed state");
        ui.PointerUp({-10, -10});
        Check(m_Clicks == 0 && !m_Increment->IsPressed(), "release outside cancels activation");
        Click(ui, *m_Increment);
        Check(m_Clicks == 1, "mouse click emits signal");
        ui.KeyEvent(Key::Space, true);
        ui.KeyEvent(Key::Space, false);
        ui.KeyEvent(Key::Enter, true);
        Check(m_Clicks == 3, "keyboard activation emits signals");
        Click(ui, *m_Disabled);
        Check(m_DisabledClicks == 0, "disabled control never activates");
        ui.SetFocus(m_Reset);
        ui.KeyEvent(Key::Tab, true);
        Check(ui.GetFocus() == m_Name, "Tab skips disabled buttons");
        ui.TextInput("Zoë");
        ui.KeyEvent(Key::Enter, true);
        Check(m_Message->GetText().find("Zoë") != std::string::npos, "textbox submission calls application code");
        ui.SetFocus(m_Editor);
        const Modifiers command{false, true, false};
        ui.KeyEvent(Key::A, true, false, command);
        ui.TextInput("Cafe\xCC\x81");
        ui.KeyEvent(Key::Backspace, true);
        Check(m_Editor->GetText() == "Caf", "Backspace removes complete grapheme");
        ui.KeyEvent(Key::Z, true, false, command);
        Check(m_Editor->GetText() == "Cafe\xCC\x81", "undo restores deleted grapheme");
        ui.KeyEvent(Key::A, true, false, command);
        ui.KeyEvent(Key::C, true, false, command);
        ui.KeyEvent(Key::X, true, false, command);
        Check(m_Editor->GetText().empty(), "cut removes selection");
        ui.KeyEvent(Key::V, true, false, command);
        Check(m_Editor->GetText() == "Cafe\xCC\x81", "Unicode clipboard round trip");
        ui.KeyEvent(Key::Y, true, false, command);
        ui.KeyEvent(Key::Enter, true);
        Check(m_Submissions == 1 && m_Edits >= 5, "editing and submission signals");
        ui.SetFocus(m_ReadOnly);
        const auto readOnly = m_ReadOnly->GetText();
        ui.TextInput("should not appear");
        ui.KeyEvent(Key::Backspace, true);
        Check(m_ReadOnly->GetText() == readOnly, "read-only editor rejects modifications");
        m_Editor->SetText("Café, naïve, résumé — editable UTF-8");
        ui.SetFocus(m_Editor);
        m_Editor->SelectAll();
        m_Window.RenderFrame();
        gpu.SavePpm(output / "selection.ppm");
        std::string error;
        Check(!ui.SetStyleSheet("textbox { unsupported-property: 5px; }", error) && !error.empty(),
              "invalid CSS is rejected atomically");
        Check(!LoadCss({}) && m_Message->GetText().starts_with("CSS reload failed;"), "CSS file error is visible");
        Check(LoadCss(m_Assets / "light.css"), "light theme loads");
        Check(m_Message->GetText().starts_with("CSS reloaded successfully."),
              "successful reload clears the error message");
        Greet();
        ui.Deactivate();
        ui.PointerMove({-10, -10});
        m_Window.RenderFrame();
        Check(gpu.ReadPixel({4, 4}).m_R > 0.85f, "theme switch changes framebuffer");
        gpu.SavePpm(output / "light.ppm");
        m_Window.SetSize(1320, 900);
        m_Window.RenderFrame();
        Check(ui.Root().GetBounds().m_Width == 1320, "resize propagates through CSS layout");
        gpu.SavePpm(output / "resized.ppm");
        VerifyPrimitives(output);
        m_Gallery->Verify(m_Assets, output);
        VerifyGpuWorkload(m_Window, m_Assets, output);
        VerifyCanvasPrimitives(m_Assets, output, m_Window.IsHeadless());
        VerifyCanvas(m_Assets, output, m_Window.IsHeadless());
        VerifyCanvasCurves(m_Assets, output, m_Window.IsHeadless());
        VerifyPainter(m_Assets, output, m_Window.IsHeadless());
        VerifyInputControls(m_Assets, output, m_Window.IsHeadless());
        VerifyWidgetStyles(m_Assets, output, m_Window.IsHeadless());
        m_Window.RenderFrame(ui.GetTime());
        std::ofstream report(output / "gpu-report.txt");
        report << "EStudioUI GPU integration: PASS\nDriver: " << gpu.GetDriverDescription()
               << "\nInitial demo: " << stats.m_DrawCalls << " draw call, " << stats.m_Shapes << " shapes, "
               << stats.m_Glyphs << " glyph instances, " << stats.m_CachedGlyphs << " cached glyphs, "
               << stats.m_FontBytes << " outline/band bytes.\n"
               << "Checked: callbacks, capture, keyboard, Tab, Unicode editing, clipboard, undo, read-only, CSS "
                  "reload, theme pixels, resize, clipping, blending, vector glyph coverage, high-DPI.\n";
        std::cout << "GPU integration PASS / " << gpu.GetDriverDescription() << '\n';
        std::cout << "Demo: " << stats.m_DrawCalls << " draw call / " << stats.m_Glyphs << " glyphs / "
                  << stats.m_FontBytes << " outline bytes\n";
    }

  private:
    static Label& AddLabel(Widget& parent, std::string text, const std::string& className)
    {
        auto& label = parent.Add<Label>(std::move(text));
        if (!className.empty())
            label.AddClass(className);
        return label;
    }

    void Greet()
    {
        m_Message->RemoveClass("error");
        m_Message->SetText("Hello, " + (m_Name->GetText().empty() ? std::string("friend") : m_Name->GetText()) +
                           ".\nSubmitted text is now in your C++ application.");
    }

    void VerifyPrimitives(const fs::path& output)
    {
        auto& gpu = m_Window.Graphics();
        auto& font = m_Window.Ui().GetFont();
        const Rect clip{0, 0, 400, 300};
        gpu.BeginFrame(400, 300, 800, 600, {0, 0, 0, 1});
        gpu.DrawSolid({10, 10, 100, 100}, {1, 0, 0, 1}, clip);
        gpu.DrawSolid({20, 20, 100, 100}, {0, 1, 0, 1}, {40, 40, 30, 30});
        gpu.DrawText(font.Shape("O", 72), {160, 95}, {1, 1, 1, 1}, clip);
        gpu.DrawSolid({10, 150, 100, 50}, {1, 0, 0, 1}, clip);
        gpu.DrawSolid({10, 150, 100, 50}, {0, 0, 1, 0.5f}, clip);
        Style rounded;
        rounded.m_Background = {0, 1, 1, 1};
        rounded.m_Radius = 20;
        gpu.DrawBox({240, 20, 100, 100}, rounded, clip);
        gpu.DrawText(font.Shape("Small café / GPU outlines", 13), {150, 180}, {1, 1, 1, 1}, clip);
        gpu.EndFrame();
        Check(gpu.GetStats().m_DrawCalls == 1, "shape/glyph interleaving remains one draw call");
        Check(gpu.ReadPixel({25, 25}).m_R > 0.95f, "clipped draw preserves earlier primitive");
        Check(gpu.ReadPixel({50, 50}).m_G > 0.95f, "clip interior draws");
        Check(gpu.ReadPixel({80, 80}).m_R > 0.95f, "clip excludes outside fragments");
        const auto blend = gpu.ReadPixel({50, 170});
        Check(blend.m_R > 0.45f && blend.m_R < 0.55f && blend.m_B > 0.45f, "painter order alpha blending");
        Check(gpu.ReadPixel({241, 21}).m_G < 0.1f && gpu.ReadPixel({290, 60}).m_G > 0.95f, "analytic rounded corners");
        const auto run = font.Shape("O", 72);
        const auto& outline = font.GetGlyph(run.m_Glyphs.front().m_Id);
        const float x = 160 + (outline.m_MinX + outline.m_MaxX) * 36;
        const float y = 95 - (outline.m_MinY + outline.m_MaxY) * 36;
        Check(gpu.ReadPixel({x, y}).m_R < 0.05f, "glyph counter remains empty");
        bool foundInk = false;
        for (float sample = 160; sample < 220; sample += 1)
            foundInk = foundInk || gpu.ReadPixel({sample, y}).m_R > 0.8f;
        Check(foundInk, "analytic glyph contains visible filled outline");
        gpu.SavePpm(output / "primitives-2x.ppm");
    }

    TabWidget* m_Tabs = nullptr;
    std::unique_ptr<WidgetGallery> m_Gallery;
    Window& m_Window;
    fs::path m_Assets;
    fs::path m_CurrentCss;
    Connection m_Keys;
    Button* m_Increment = nullptr;
    Button* m_Reset = nullptr;
    Button* m_Disabled = nullptr;
    Button* m_Greet = nullptr;
    TextBox* m_Name = nullptr;
    TextBox* m_Editor = nullptr;
    TextBox* m_ReadOnly = nullptr;
    Label* m_CountLabel = nullptr;
    Label* m_Message = nullptr;
    Label* m_EditStatus = nullptr;
    int m_Clicks = 0;
    int m_DisabledClicks = 0;
    int m_Edits = 0;
    int m_Submissions = 0;
};
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto executable = fs::absolute(argv[0]);
        const auto assets = executable.parent_path() / "assets";
        fs::path font = assets / "NotoSans.ttf";
        fs::path css = assets / "dark.css";
        fs::path output = "verification";
        WindowOptions options;
        bool selfTest = false;
        bool benchmark = false;
        bool profile = false;
        bool profileCanvas = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string argument = argv[i];
            const auto value = [&]() -> std::string
            {
                if (++i == argc)
                    throw std::runtime_error("Missing value for " + argument);
                return argv[i];
            };
            if (argument == "--headless")
                options.m_Headless = true;
            else if (argument == "--self-test")
                selfTest = true;
            else if (argument == "--profile-ui")
                profile = true;
            else if (argument == "--profile-canvas")
                profileCanvas = true;
            else if (argument == "--benchmark")
                benchmark = true;
            else if (argument == "--font")
                font = value();
            else if (argument == "--css")
                css = value();
            else if (argument == "--output")
                output = value();
            else if (argument == "--help")
            {
                std::cout << "EStudioUIDemo [--css FILE] [--font FILE] [--self-test] [--benchmark] [--profile-ui] "
                             "[--profile-canvas] "
                             "[--headless] "
                             "[--output DIR]\n"
                          << "F5 reloads CSS; F6 switches themes. Headless mode requires OSMesa (Linux).\n";
                return 0;
            }
            else
                throw std::runtime_error("Unknown argument: " + argument);
        }
        if (profileCanvas)
        {
            ProfileCanvas(assets, output, options.m_Headless);
            return 0;
        }
        Window window(options, font);
        Demo demo(window, assets, css);
        if (selfTest)
        {
            demo.Verify(output);
            if (!benchmark && !profile)
                return 0;
        }
        if (profile)
        {
            demo.Profile(output);
            return 0;
        }
        if (benchmark)
        {
            Benchmark(window, output);
            return 0;
        }
        if (options.m_Headless)
            throw std::runtime_error(
                "Use --self-test, --benchmark, --profile-ui, or --profile-canvas with --headless.");
        return window.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "EStudioUI: " << error.what() << '\n';
        return 1;
    }
}
