#include <EStudioUI/Button.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(double a, double b, double tolerance = 0.0002)
{
    return std::abs(a - b) < tolerance;
}

void Parser()
{
    StyleSheet sheet;
    std::string error;
    Check(sheet.Parse(R"CSS(
        button { background: linear-gradient(to right, #f00, transparent 30%, #00f); transform: translate(50%, -4px) rotate(90deg) scale(2, 1); transform-origin: 50% 50%; box-shadow: 0 4px 8px 2px #1238, inset 1px 2px 3px -1px white; padding: 4px; padding-left: 9px; }
        button:hover::before { content: ""; position: absolute; inset: 2px 3px; background: radial-gradient(ellipse 50% 70% at 20% 40%, white, transparent); }
        button { transition: transform 200ms cubic-bezier(.2, .8, .2, 1), background .3s linear 40ms, border-radius .2s ease; }
        @keyframes pulse { from { opacity: .2; } 25%, 75% { opacity: .8; } to { opacity: .2; } }
    )CSS",
                      error),
          error);
    StyleNode node{"button", "", {}, Hover};
    const auto style = sheet.Compute(node);
    Check(style.m_BackgroundImage.m_Stops.size() == 3 && Near(style.m_BackgroundImage.m_Stops[1].m_Position, .3),
          "compiled gradient stops");
    Check(style.m_Shadow.m_Spread == 2 && style.m_ShadowLayers.m_Layers.size() == 1 &&
              style.m_ShadowLayers.m_Layers[0].m_Inset,
          "multiple inset and spread shadows");
    Check(style.m_Padding == Insets{4, 4, 4, 9}, "compiled partial assignments retain cascade edges");
    Check(style.m_Transitions.size() == 3 && Near(style.m_Transitions[0].m_Duration, .2), "typed transition shorthand");
    const auto before = sheet.Compute(node, &style, 1);
    Check(before.m_Decoration && before.m_Anchors.m_Left.m_Value == 3 &&
              before.m_BackgroundImage.m_Kind == Gradient::Kind::Radial,
          "state-dependent pseudo layer");
    Check(!sheet.Compute(node, &style, 2).m_Decoration, "pseudo layers do not inherit widget rules");
    const auto* frames = sheet.FindKeyframes("pulse");
    Check(frames && frames->size() == 4 && (*frames)[2].m_Offset == .75, "nested keyframe blocks and grouped offsets");
    const auto matrix = style.m_Transform.Resolve({10, 20, 100, 40});
    const auto mapped = matrix.Map({70, 40});
    Check(Near(mapped.m_X, 110) && Near(mapped.m_Y, 56), "ordered transform composition and percentage translation");
    Affine inverse;
    Check(matrix.Inverse(inverse) && Near(inverse.Map(mapped).m_X, 70), "affine inverse agrees with paint mapping");
    const auto revision = sheet.GetRevision();
    for (const auto* invalid :
         {"button { background: linear-gradient(red); }", "@keyframes x { 120% { opacity: 1; } }",
          "button { transform: perspective(2px); }", "button::wrong { content: ''; }",
          "button { transition: opacity -1s; }", "button { animation: x 0s infinite; }", "button { opacity: .5;"})
        Check(!sheet.Parse(invalid, error) && sheet.GetRevision() == revision && sheet.FindKeyframes("pulse"),
              "invalid CSS reload is atomic");
    Check(sheet.Parse("button { transition-property: opacity, transform; transition-duration: 1s; animation-name: "
                      "pulse; animation-duration: 2s; animation-iteration-count: 3; animation-play-state: paused; }",
                      error),
          error);
    const auto longhands = sheet.Compute(node);
    Check(longhands.m_Transitions.size() == 2 && longhands.m_Transitions[1].m_Duration == 1 &&
              longhands.m_Animations[0].m_Paused,
          "timing longhands compile and repeat list values");
    Check(Near(EasingCurve::Steps(4).Evaluate(.49), .25) && Near(EasingCurve::Steps(4, true).Evaluate(0), .25),
          "CSS step easing");
}

void Transitions(FontFace& font)
{
    UiContext ui(font);
    auto& button = ui.Root().Add<Button>("Hover");
    button.SetFloatingBounds({40, 50, 180, 60});
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        button { background: red; transform: translateY(0px) scale(1); transition: background 1s linear .2s, transform 1s linear; }
        button:hover { background: blue; transform: translateY(-8px) scale(1.1); }
    )CSS",
                           error),
          error);
    ui.Prepare(500, 300);
    ui.PointerMove({130, 80});
    ui.Prepare(500, 300);
    const auto bounds = button.GetBounds();
    const auto counters = ui.GetStats();
    ui.SetTime(.1);
    ui.Prepare(500, 300);
    Check(button.GetStyle().m_Background == Color{1, 0, 0, 1}, "CSS transition delay holds presentation");
    ui.SetTime(.7);
    ui.Prepare(500, 300);
    Check(Near(button.GetStyle().m_Background.m_R, .5) && Near(button.GetStyle().m_Background.m_B, .5),
          "state transition interpolates current paint");
    Check(button.GetBounds() == bounds && ui.GetStats().m_LayoutPasses == counters.m_LayoutPasses &&
              ui.GetStats().m_StyleResolutions == counters.m_StyleResolutions,
          "paint and transform ticks retain style/layout caches");
    ui.PointerMove({480, 280});
    ui.Prepare(500, 300);
    ui.SetTime(1.4);
    ui.Prepare(500, 300);
    Check(Near(button.GetStyle().m_Background.m_R, .75) && Near(button.GetStyle().m_Background.m_B, .25),
          "interruption reverses smoothly from current presentation");
    ui.SetTime(2);
    ui.Prepare(500, 300);
    Check(button.GetStyle().m_Background == Color{1, 0, 0, 1} && !ui.HasAnimations(),
          "completed transitions stop requesting frames");
    ui.PointerMove({130, 80});
    ui.Prepare(500, 300);
    button.SetStyleProperty(StyleProperty::Background, Color{0, 1, 0, 1});
    ui.SetTime(2.5);
    ui.Prepare(500, 300);
    Check(button.GetStyle().m_Background == Color{0, 1, 0, 1},
          "explicit C++ tracks retain precedence over automatic CSS");
}

void Keyframes(FontFace& font)
{
    UiContext ui(font);
    auto& target = ui.Root().Add<Button>();
    target.SetWidth(SizeRule::Fixed(180));
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        button { opacity: .2; animation: pulse 4s linear 1s 2 alternate both; }
        button.paused { animation-play-state: paused; }
        @keyframes pulse { 0% { opacity: .2; transform: rotate(0deg); width: 100px; } 25% { opacity: .6; } 50% { opacity: 1; transform: rotate(180deg); } 100% { opacity: .2; transform: rotate(360deg); width: 300px; } }
    )CSS",
                           error),
          error);
    ui.Prepare(600, 400);
    Check(Near(target.GetStyle().m_Opacity, .2), "backwards fill during delay");
    ui.SetTime(2);
    ui.Prepare(600, 400);
    Check(Near(target.GetStyle().m_Opacity, .6), "keyframes use segment rather than global easing");
    Check(target.GetBounds().m_Width == 180, "Fixed code width survives CSS keyframes");
    target.AddClass("paused");
    ui.Prepare(600, 400);
    Check(!ui.HasAnimations(), "paused CSS animation releases frame demand");
    ui.SetTime(20);
    ui.Prepare(600, 400);
    Check(Near(target.GetStyle().m_Opacity, .6), "paused CSS timeline does not accumulate time");
    target.RemoveClass("paused");
    ui.Prepare(600, 400);
    ui.SetTime(21);
    ui.Prepare(600, 400);
    Check(Near(target.GetStyle().m_Opacity, 1), "resume retains elapsed time");
    ui.SetTime(25);
    ui.Prepare(600, 400);
    Check(Near(target.GetStyle().m_Opacity, 1), "alternate iteration runs backwards");
    ui.SetTime(27);
    ui.Prepare(600, 400);
    Check(Near(target.GetStyle().m_Opacity, .2) && !ui.HasAnimations(), "alternate final fill and completion");
    Check(ui.Root().RemoveChild(target) && !ui.HasAnimations(), "destroyed CSS animation targets are weak");
    ui.SetTime(28);
}

void Input(FontFace& font)
{
    UiContext ui(font);
    auto& parent = ui.Root().Add<Widget>();
    parent.SetFloatingBounds({30, 30, 400, 300});
    auto& button = parent.Add<Button>("Rotated");
    button.SetFloatingBounds({100, 100, 100, 40});
    int clicks = 0;
    button.Clicked().Connect([&] { ++clicks; });
    std::string error;
    Check(ui.SetStyleSheet(
              "panel { transform: translateX(30px); } button { transform: translateX(80px) rotate(90deg) scale(1.2); }",
              error),
          error);
    ui.Prepare(600, 400);
    ui.PointerDown({260, 120});
    ui.PointerUp({260, 120});
    Check(clicks == 1 && Near(ui.ToLayoutPoint(button, {260, 120}).m_X, 150),
          "nested transformed pointer capture activates the painted button");
    ui.PointerDown({150, 120});
    ui.PointerUp({150, 120});
    Check(clicks == 1, "original layout location is no longer a click target");
    button.SetStyleProperty(StyleProperty::Transform, Transform{{{TransformOperation::Kind::Scale, {0, 0}}}});
    ui.PointerDown({260, 120});
    ui.PointerUp({260, 120});
    Check(clicks == 1, "singular transforms do not capture input");
    button.ClearStyleProperties();
    parent.SetFloatingBounds({100, 80, 100, 120});
    button.SetFloatingBounds({110, 100, 80, 40});
    Check(ui.SetStyleSheet("button { transform: translateX(60px); }", error), error);
    ui.Prepare(600, 400);
    ui.PointerDown({180, 120});
    ui.PointerUp({230, 120});
    Check(clicks == 1, "capture release outside the world-space ancestor clip does not activate");
    ui.PointerDown({180, 120});
    ui.PointerUp({180, 120});
    Check(clicks == 2, "visible part of a transformed clipped button remains interactive");
}

void CallbackClock(FontFace& font)
{
    UiContext ui(font);
    auto& target = ui.Root().Add<Button>();
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        button { opacity: .2; transition: opacity 1s linear; }
        button.on { opacity: 1; animation: shift 1s linear forwards; }
        @keyframes shift { from { transform: translateX(0px); } to { transform: translateX(10px); } }
    )CSS",
                           error),
          error);
    ui.Prepare(300, 200);
    AnimationOptions options;
    options.m_Duration = 1;
    Animation trigger(options);
    trigger.AddCustom(target, "callback-clock", [](Widget&, double) {});
    ui.Animations().Finished().Connect(
        [&](AnimationId)
        {
            target.AddClass("on");
            ui.Prepare(300, 200);
        });
    ui.Animations().Play(std::move(trigger));
    ui.SetTime(1);
    ui.Prepare(300, 200);
    Check(Near(target.GetStyle().m_Opacity, .2) && Near(target.GetStyle().m_Transform.m_Operations[0].m_Values[0], 0),
          "CSS started inside a C++ completion callback uses the current GUI clock");
    ui.SetTime(1.5);
    ui.Prepare(300, 200);
    Check(Near(target.GetStyle().m_Opacity, .6) && Near(target.GetStyle().m_Transform.m_Operations[0].m_Values[0], 5),
          "callback-started transitions and keyframes share elapsed time");
}

void CacheStress(FontFace& font)
{
    UiContext ui(font);
    std::string error;
    Check(ui.SetStyleSheet(R"CSS(
        button { animation: wave 2s linear infinite alternate; background: linear-gradient(90deg, #2587ff, #ac4dff); }
        @keyframes wave { from { opacity: .4; transform: translateY(0px) scale(1); } 50% { opacity: 1; } to { opacity: .7; transform: translateY(-5px) scale(1.03); } }
    )CSS",
                           error),
          error);
    for (int i = 0; i < 500; ++i)
        ui.Root().Add<Button>("B").SetFloatingBounds(
            {static_cast<float>(i % 25 * 32), static_cast<float>(i / 25 * 28), 30, 26});
    ui.Prepare(800, 600);
    const auto stats = ui.GetStats();
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 1; frame <= 240; ++frame)
    {
        ui.SetTime(frame / 120.0);
        ui.Prepare(800, 600);
        Check(ui.GetStats().m_LayoutPasses == stats.m_LayoutPasses &&
                  ui.GetStats().m_StyleResolutions == stats.m_StyleResolutions,
              "500 animated widgets retain layout and compiled CSS caches");
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "500-widget CSS clock: " << elapsed / 240
              << " ms/tick; 0 style resolutions and 0 layout passes during 240 ticks.\n";
}
} // namespace

int main()
{
    try
    {
        Parser();
        FontFace font(ESTUDIOUI_FONT_PATH);
        Transitions(font);
        Keyframes(font);
        Input(font);
        CallbackClock(font);
        CacheStress(font);
        std::cout << s_Checks << " rich CSS checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Rich CSS: " << error.what() << '\n';
        return 1;
    }
}
