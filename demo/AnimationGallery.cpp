#include "AnimationGallery.h"

#include <EStudioUI/ToolBar.h>

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
const std::array s_Curves{EasingCurve::Kind::EaseInOut, EasingCurve::Kind::OutBounce, EasingCurve::Kind::OutElastic,
                          EasingCurve::Kind::Linear};
const std::array<std::string, 4> s_CurveNames{"Ease in/out", "Bounce", "Elastic", "Linear"};

void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Animation framebuffer verification: " + message);
}
} // namespace

AnimationGallery::AnimationGallery(Window& window, Widget& page, const std::filesystem::path& assets)
    : m_Window(window), m_Assets(assets)
{
    page.SetGap(8);
    auto& toolbar = page.Add<ToolBar>();
    toolbar.AddButton("CSS endpoints")
        .Clicked()
        .Connect(
            [this]
            {
                AnimationOptions options;
                if (ReadOptions(options))
                    PlayStyle(options);
            });
    toolbar.AddButton("Property group")
        .Clicked()
        .Connect(
            [this]
            {
                AnimationOptions options;
                if (ReadOptions(options))
                    PlayProperties(options);
            });
    toolbar.AddButton("Pause / resume")
        .Clicked()
        .Connect(
            [this]
            {
                auto& system = m_Window.Ui().Animations();
                if (system.GetState(m_Id) == AnimationState::Paused)
                    system.Resume(m_Id);
                else
                    system.Pause(m_Id);
            });
    toolbar.AddButton("Finish").Clicked().Connect(
        [this] { m_Window.Ui().Animations().Stop(m_Id, AnimationStopMode::Finish); });
    toolbar.AddButton("Reset").Clicked().Connect([this] { Reset(); });

    auto& optionsBar = page.Add<ToolBar>();
    optionsBar.Add<Label>("Duration (s)").SetWidth(SizeRule::Fixed(100));
    m_Duration = &optionsBar.Add<TextBox>("1.5");
    m_Duration->SetWidth(SizeRule::Fixed(76));
    m_Duration->SetHeight(SizeRule::Fixed(34));
    m_CurveButton = &optionsBar.AddButton(s_CurveNames[0]);
    m_CurveButton->Clicked().Connect([this] { NextCurve(); });
    m_Repeat = &optionsBar.AddButton("Repeat + alternate");
    m_Repeat->SetCheckable(true);
    auto& caption = page.Add<Label>("One timeline / multiple widgets / editable CSS endpoints\n"
                                    "Controls stay interactive while geometry, colors, spacing and typography change.");
    caption.AddClass("animation-note");
    auto& stage = page.Add<Widget>();
    stage.SetId("animation-stage");
    stage.SetHeight(SizeRule::Fill(1, 320));
    stage.SetLayoutDirection(Axis::Horizontal);
    m_Card = &stage.Add<Widget>();
    m_Card->SetId("animation-card");
    auto& eyebrow = m_Card->Add<Label>("INTERPOLATED PRESENTATION");
    eyebrow.AddClass("animation-eyebrow");
    auto& title = m_Card->Add<Label>("Animated panel");
    title.AddClass("animation-title");
    m_Editor = &m_Card->Add<TextBox>();
    m_Editor->SetPlaceholder("Type while animating...");
    m_Button = &m_Card->Add<Button>("Working button");
    m_Button->Clicked().Connect(
        [this]
        {
            m_Status->SetText("Button activated " + std::to_string(++m_Clicks) +
                              " times. Input follows the animated bounds.");
        });
    m_Badge = &stage.Add<Label>("ABSOLUTE BOUNDS");
    m_Badge->SetId("animation-badge");
    m_Counter = &page.Add<Label>("Custom magnitude: 0 / 100");
    m_Counter->AddClass("animation-counter");
    m_Status = &page.Add<Label>(
        "Choose CSS endpoints or Property group. Edit assets/animation-start.css and animation-end.css.");
    m_Status->AddClass("gallery-status");
    m_Finished = window.Ui().Animations().Finished().Connect(
        [this](AnimationId id)
        {
            if (id == m_Id)
                m_Status->SetText(
                    "Animation completed. CSS endpoint is active; property tracks retain their final values.");
        });
}

AnimationGallery::~AnimationGallery()
{
    m_Finished.Disconnect();
}

std::string AnimationGallery::ReadAsset(const std::string& name) const
{
    std::ifstream input(m_Assets / name, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read animation demo asset: " + name);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

bool AnimationGallery::ReadOptions(AnimationOptions& options)
{
    const auto& text = m_Duration->GetText();
    double duration = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), duration);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(duration) ||
        duration < 0 || duration > 60 || (duration == 0 && m_Repeat->IsChecked()))
    {
        m_Status->SetText("Enter a duration between 0 and 60 seconds. Repetition requires a positive duration.");
        return false;
    }
    options.m_Duration = duration;
    options.m_Easing = EasingCurve(s_Curves[m_Curve]);
    options.m_Iterations = m_Repeat->IsChecked() ? 0 : 1;
    options.m_Alternate = m_Repeat->IsChecked();
    return true;
}

void AnimationGallery::NextCurve()
{
    m_Curve = (m_Curve + 1) % static_cast<unsigned>(s_Curves.size());
    m_CurveButton->SetText(s_CurveNames[m_Curve]);
}

void AnimationGallery::Reset()
{
    auto& ui = m_Window.Ui();
    ui.Animations().Stop(m_Id, AnimationStopMode::ReleaseStyle);
    const auto clear = [&](auto&& self, Widget& widget) -> void
    {
        widget.ClearStyleProperties();
        for (const auto& child : widget.GetChildren())
            self(self, *child);
    };
    clear(clear, *m_Card);
    m_Badge->ClearStyleProperties();
    m_Badge->ClearFloatingBounds();
    m_Counter->SetText("Custom magnitude: 0 / 100");
    std::string error;
    Check(ui.SetStyleSheet(ReadAsset("dark.css") + ReadAsset("animation-start.css"), error), error);
    m_Status->SetText("Start endpoint restored. All examples share the same GUI-thread animation scheduler.");
}

void AnimationGallery::PlayStyle(const AnimationOptions& options)
{
    Reset();
    const auto base = ReadAsset("dark.css");
    std::string error;
    m_Id = m_Window.Ui().AnimateStyleSheet(base + ReadAsset("animation-start.css"),
                                           base + ReadAsset("animation-end.css"), options, error);
    Check(m_Id != 0, error);
    m_Status->SetText("CSS transition running: widths, colors, corners, padding, shadows, translation and font size.");
    if (m_Window.Ui().Animations().GetState(m_Id) == AnimationState::Stopped)
        m_Status->SetText("Zero-duration CSS transition completed immediately.");
}

void AnimationGallery::PlayProperties(const AnimationOptions& options)
{
    Reset();
    auto& ui = m_Window.Ui();
    const auto rootBounds = ui.Root().GetBounds();
    ui.Prepare(rootBounds.m_Width, rootBounds.m_Height);
    Animation animation(options);
    animation.AddTranslation(*m_Card, {}, {260, 25});
    animation.AddStyleProperty(*m_Card, StyleProperty::Background, m_Card->GetStyle().m_Background,
                               Color{0.05f, 0.44f, 0.36f, 1});
    animation.AddStyleProperty(*m_Card, StyleProperty::Radius, 10.0f, 30.0f);
    animation.AddStyleProperty(*m_Button, StyleProperty::Background, m_Button->GetStyle().m_Background,
                               Color{1, 0.65f, 0.25f, 1});
    animation.AddStyleProperty(*m_Editor, StyleProperty::BorderColor, m_Editor->GetStyle().m_BorderColor,
                               Color{0.3f, 1, 0.8f, 1});
    const auto start = m_Badge->GetBounds();
    animation.AddBounds(*m_Badge, start, {start.m_X + 420, start.m_Y + 12, 230, 60});
    animation.AddValue(*m_Counter, "magnitude", 0.0, 100.0,
                       [](Widget& target, double value)
                       {
                           static_cast<Label&>(target).SetText(
                               "Custom magnitude: " + std::to_string(static_cast<int>(std::lround(value))) + " / 100");
                       });
    m_Id = ui.Animations().Play(std::move(animation));
    m_Status->SetText(
        "Property group running: multiple widgets, Rect bounds, translation, colors and a custom double.");
    if (ui.Animations().GetState(m_Id) == AnimationState::Stopped)
        m_Status->SetText("Zero-duration property group completed immediately.");
}

void AnimationGallery::Verify(const std::filesystem::path& output)
{
    AnimationOptions options;
    options.m_Duration = 1;
    PlayStyle(options);
    auto& ui = m_Window.Ui();
    ui.Animations().Pause(m_Id);
    m_Window.RenderFrame();
    const auto start = m_Card->GetBounds();
    const auto firstColor = m_Card->GetStyle().m_Background;
    for (double progress : {0.0, 0.5, 1.0})
    {
        ui.Animations().Seek(m_Id, progress);
        m_Window.RenderFrame();
        const auto bounds = m_Card->GetBounds();
        const auto styleColor = m_Card->GetStyle().m_Background;
        const auto pixel =
            m_Window.Graphics().ReadPixel({bounds.m_X + bounds.m_Width * 0.5f, bounds.m_Y + bounds.m_Height - 16});
        Check(std::abs(pixel.m_R - styleColor.m_R) < 0.025f && std::abs(pixel.m_G - styleColor.m_G) < 0.025f &&
                  std::abs(pixel.m_B - styleColor.m_B) < 0.025f,
              "framebuffer matches interpolated card fill");
        Check(std::abs(bounds.m_Width - (280 + 150 * progress)) < 0.05, "CSS width follows normalized time");
        const auto name = progress == 0 ? "start" : progress == 1 ? "end" : "middle";
        m_Window.Graphics().SavePpm(output / (std::string("animation-css-") + name + ".ppm"));
    }
    const auto lastColor = m_Card->GetStyle().m_Background;
    Check(firstColor != lastColor && m_Card->GetBounds().m_X > start.m_X + 200,
          "CSS endpoint visibly changes color and position");
    PlayProperties(options);
    ui.Animations().Pause(m_Id);
    ui.Animations().Seek(m_Id, 0.5);
    m_Window.RenderFrame();
    Check(m_Counter->GetText() == "Custom magnitude: 50 / 100", "custom setter shares the same timeline");
    const auto button = m_Button->GetBounds();
    ui.PointerDown({button.m_X + button.m_Width * 0.5f, button.m_Y + button.m_Height * 0.5f});
    ui.PointerUp({button.m_X + button.m_Width * 0.5f, button.m_Y + button.m_Height * 0.5f});
    Check(m_Clicks == 1, "translated button remains interactive");
    m_Window.RenderFrame();
    m_Window.Graphics().SavePpm(output / "animation-properties-middle.ppm");
    ui.Animations().Stop(m_Id, AnimationStopMode::Finish);
    Reset();
    WindowOptions probeOptions;
    probeOptions.m_Width = 200;
    probeOptions.m_Height = 120;
    probeOptions.m_Headless = m_Window.IsHeadless();
    {
        Window probe(probeOptions, m_Assets / "NotoSans.ttf");
        auto& probeUi = probe.Ui();
        std::string error;
        Check(probeUi.SetStyleSheet("window { background: black; }", error), error);
        auto& box = probeUi.Root().Add<Widget>();
        Animation blend(options);
        blend.AddBounds(box, {10, 20, 70, 60}, {90, 20, 90, 60});
        blend.AddTranslation(box, {}, {20, 0});
        blend.AddStyleProperty(box, StyleProperty::Background, Color{1, 0, 0, 1}, Color{0, 0, 1, 1});
        blend.AddStyleProperty(box, StyleProperty::Opacity, 0.25f, 0.75f);
        const auto blendId = probeUi.Animations().Play(std::move(blend));
        probeUi.Animations().Pause(blendId);
        probeUi.Animations().Seek(blendId, 0.5);
        probe.RenderFrame();
        const auto pixel = probe.Graphics().ReadPixel({100, 50});
        const auto previous = probe.Graphics().ReadPixel({20, 50});
        Check(std::abs(pixel.m_R - 0.25f) < 0.015f && std::abs(pixel.m_B - 0.25f) < 0.015f,
              "interpolated color and opacity blend into the actual framebuffer");
        Check(previous.m_R < 0.01f && previous.m_B < 0.01f, "old bounds do not leave painted trails");
        Check(box.GetBounds() == Rect{60, 20, 80, 60}, "absolute geometry and translation compose in GPU frames");
        probe.Graphics().SavePpm(output / "animation-blending.ppm");
    }
    std::ofstream report(output / "animation-report.txt");
    report << "Animation framebuffer checks: PASS\n"
           << "CSS start/middle/end: actual framebuffer colors, width, position, and endpoint ownership.\n"
           << "Typed group: synchronized custom double, absolute Rect bounds, colors, translation, and button input.\n"
           << "Pixel probe: interpolated color/opacity blending, composed geometry, and clearing old bounds.\n";
}
} // namespace EStudioUI
