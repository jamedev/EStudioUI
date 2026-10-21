#include <EStudioUI/AnimationSystem.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <limits>
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

bool Near(double first, double last, double tolerance = 0.0001)
{
    return std::abs(first - last) < tolerance;
}

template <typename Callback> void Throws(Callback callback, const std::string& message)
{
    bool threw = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    Check(threw, message);
}

AnimationOptions Timing(double duration = 1)
{
    AnimationOptions options;
    options.m_Duration = duration;
    return options;
}

void CurvesAndValues()
{
    for (int i = 0; i < static_cast<int>(EasingCurve::Kind::CubicBezier); ++i)
    {
        EasingCurve curve(static_cast<EasingCurve::Kind>(i));
        Check(curve.Evaluate(0) == 0 && curve.Evaluate(1) == 1, "all curves preserve exact endpoints");
        for (int sample = 1; sample < 100; ++sample)
            Check(std::isfinite(curve.Evaluate(sample / 100.0)), "all curves produce finite intermediate progress");
    }
    const auto bezier = EasingCurve::CubicBezier(0, 0, 1, 1);
    Check(Near(bezier.Evaluate(0.23), 0.23), "Bezier inversion reproduces a linear curve");
    Check(Near(EasingCurve(EasingCurve::Kind::Ease).Evaluate(0.5), 0.802403, 0.00001),
          "CSS ease solves x before evaluating y");
    Check(EasingCurve(EasingCurve::Kind::OutBack).Evaluate(0.7) > 1, "Back preserves deliberate overshoot");
    Throws([] { EasingCurve::CubicBezier(-0.1, 0, 1, 1); }, "invalid Bezier x is rejected");
    Throws([] { EasingCurve().Evaluate(std::numeric_limits<double>::quiet_NaN()); }, "NaN easing time rejected");
    const auto color = Tween(Color{1, 0, 0, 1}, Color{0, 0, 1, 0}, 0.5);
    Check(Near(color.m_R, 1) && Near(color.m_B, 0) && Near(color.m_A, 0.5),
          "transparent endpoint does not introduce a dark or blue fringe");
    Check(Tween(Color{1, 0, 0, 0}, Color{0, 0, 1, 0}, 0) == Color{1, 0, 0, 0},
          "fully transparent colors retain exact endpoints");
    const auto rectangle = Tween(Rect{10, 20, 100, 40}, Rect{50, 60, 200, 80}, 0.5);
    Check(rectangle == Rect{30, 40, 150, 60}, "all rectangle components interpolate");
    Check(Tween(Rect{0, 0, 10, 10}, Rect{0, 0, 0, 0}, 2).m_Width == 0, "overshoot cannot create negative sizes");
    Check(Near(Tween(1.0, 3.0, 0.25), 1.5), "double magnitudes interpolate");
    Throws([] { Tween(1.0, std::numeric_limits<double>::infinity(), 0.0); }, "nonfinite endpoints rejected");
    Throws([] { Animation(Timing(-1)); }, "negative duration rejected");
    auto options = Timing(0);
    options.m_Iterations = 0;
    Throws([&] { Animation invalid(options); }, "infinite instantaneous animation rejected");
    for (int i = 0; i < static_cast<int>(StyleProperty::Count); ++i)
    {
        const auto property = static_cast<StyleProperty>(i);
        Style style;
        const auto value = GetStyleValue(style, property);
        SetStyleValue(style, property, value);
        Check(GetStyleValue(style, property) == value && !GetStylePropertyName(property).empty(),
              "every style property has correctly typed metadata");
    }
    Style style;
    Throws([&] { SetStyleValue(style, StyleProperty::Opacity, Color{}); }, "style type mismatch rejected");
    Throws([&] { SetStyleValue(style, StyleProperty::Align, std::string("unknown")); }, "invalid alignment rejected");
}

void Playback(FontFace& font)
{
    UiContext ui(font);
    auto& target = ui.Root().Add<Widget>();
    auto& system = ui.Animations();
    double value = -1;
    int finished = 0;
    int cancelled = 0;
    system.Finished().Connect([&](AnimationId) { ++finished; });
    system.Cancelled().Connect([&](AnimationId) { ++cancelled; });
    ui.SetTime(10);
    auto options = Timing(2);
    options.m_Delay = 1;
    Animation animation(options);
    animation.AddValue(target, "magnitude", 0.0, 100.0, [&](Widget&, double current) { value = current; });
    const auto id = system.Play(std::move(animation));
    Check(value == 0 && ui.HasAnimations(), "initial value is applied immediately including delay");
    ui.SetTime(10.5);
    Check(value == 0, "delay holds the initial value");
    ui.SetTime(12);
    Check(Near(value, 50), "elapsed time rather than frame count drives the timeline");
    Check(system.Pause(id) && !system.HasRunning(), "pause removes continuous frame demand");
    ui.SetTime(20);
    Check(Near(value, 50), "paused wall time does not change properties");
    Check(system.Resume(id), "resume succeeds");
    ui.SetTime(20.5);
    Check(Near(value, 75), "resume retains elapsed position");
    ui.SetTime(21);
    Check(value == 100 && finished == 1 && cancelled == 0 && !system.HasRunning(),
          "completion applies exact endpoint once");
    ui.SetTime(22);
    Check(finished == 1 && system.GetState(id) == AnimationState::Stopped, "completed entries are retired");
    Check(!system.Pause(id) && !system.Seek(id, 0), "stale identities are harmless");
    Throws([&] { ui.SetTime(21); }, "backwards clock rejected");
    Throws([&] { system.Seek(0, -1); }, "negative seek rejected");
    Animation instantaneous(Timing(0));
    instantaneous.AddValue(target, "magnitude", 0.0, 7.0, [&](Widget&, double current) { value = current; });
    system.Play(std::move(instantaneous));
    Check(value == 7 && finished == 2 && !system.HasRunning(), "zero duration completes synchronously");
    options = Timing();
    options.m_Iterations = 2;
    options.m_Alternate = true;
    Animation alternate(options);
    alternate.AddValue(target, "magnitude", 0.0, 10.0, [&](Widget&, double current) { value = current; });
    const auto alternateId = system.Play(std::move(alternate));
    system.Pause(alternateId);
    system.Seek(alternateId, 1);
    Check(value == 10, "alternate direction meets at the endpoint");
    system.Seek(alternateId, 1.5);
    Check(Near(value, 5), "alternate reverse leg interpolates backwards");
    system.Seek(alternateId, 2);
    Check(value == 0 && finished == 3, "even alternate iterations finish at their starting value");
    options.m_Iterations = 0;
    Animation infinite(options);
    infinite.AddValue(target, "magnitude", 0.0, 10.0, [&](Widget&, double current) { value = current; });
    const auto infiniteId = system.Play(std::move(infinite));
    system.Seek(infiniteId, 1000000.25);
    Check(Near(value, 2.5) && system.HasRunning(), "infinite repeats tolerate large frame gaps");
    system.Stop(infiniteId, AnimationStopMode::Finish);
    Check(value == 10 && finished == 4, "explicit Finish terminates infinite playback at the nominal endpoint");
}

void ReplacementAndLifetimes(FontFace& font)
{
    UiContext ui(font);
    auto& system = ui.Animations();
    auto& target = ui.Root().Add<Widget>();
    Animation first(Timing());
    first.AddStyleProperty(target, StyleProperty::Opacity, 0.0f, 1.0f);
    first.AddTranslation(target, {0, 0}, {100, 0});
    const auto firstId = system.Play(std::move(first));
    ui.SetTime(0.5);
    ui.Prepare(500, 200);
    Animation second(Timing());
    second.AddStyleProperty(target, StyleProperty::Opacity, target.GetStyle().m_Opacity, 0.0f);
    const auto secondId = system.Play(std::move(second));
    ui.SetTime(1);
    ui.Prepare(500, 200);
    Check(Near(target.GetStyle().m_Opacity, 0.25) && Near(target.GetStyle().m_TranslateX, 100),
          "new property writer replaces only overlapping tracks");
    Check(system.GetState(firstId) == AnimationState::Stopped && system.GetState(secondId) == AnimationState::Running,
          "unrelated tracks complete normally");
    system.Stop(secondId, AnimationStopMode::ReleaseStyle);
    ui.Prepare(500, 200);
    Check(target.GetStyle().m_Opacity == 1, "release returns a style property to CSS");
    int finished = 0;
    system.Finished().Connect(
        [&](AnimationId)
        {
            ++finished;
            if (finished == 1)
            {
                Animation next(Timing());
                next.AddStyleProperty(target, StyleProperty::Opacity, 0.0f, 1.0f);
                system.Play(std::move(next));
            }
        });
    Animation callback(Timing());
    callback.AddStyleProperty(target, StyleProperty::Radius, 0.0f, 10.0f);
    system.Play(std::move(callback));
    ui.SetTime(2);
    Check(finished == 1 && system.HasRunning(), "finished callbacks can schedule animations during dispatch");
    system.StopAll();
    auto& doomed = ui.Root().Add<Widget>();
    int skipped = 0;
    Animation deletion(Timing());
    deletion.AddCustom(doomed, "delete",
                       [&](Widget& live, double progress)
                       {
                           if (progress > 0)
                               ui.Root().RemoveChild(live);
                       });
    deletion.AddCustom(doomed, "must-skip", [&](Widget&, double) { ++skipped; });
    system.Play(std::move(deletion));
    ui.SetTime(2.5);
    Check(skipped == 1 && !system.HasRunning(), "deleting a target during sampling skips its remaining tracks");
    auto& pausedTarget = ui.Root().Add<Widget>();
    Animation paused(Timing());
    paused.AddTranslation(pausedTarget, {}, {20, 0});
    const auto pausedId = system.Play(std::move(paused));
    system.Pause(pausedId);
    ui.Root().RemoveChild(pausedTarget);
    ui.SetTime(3);
    Check(system.GetState(pausedId) == AnimationState::Stopped, "deleted paused targets do not retain tracks forever");
    Animation throwing(Timing());
    throwing.AddCustom(target, "throw",
                       [](Widget&, double progress)
                       {
                           if (progress > 0)
                               throw std::runtime_error("intentional writer failure");
                       });
    system.Play(std::move(throwing));
    Throws([&] { ui.SetTime(3.5); }, "callback failures propagate");
    ui.SetTime(4);
    Check(!system.HasRunning(), "a throwing callback is retired and scheduler remains usable");
    UiContext other(font);
    Animation foreign(Timing());
    foreign.AddTranslation(other.Root(), {}, {10, 0});
    Throws([&] { system.Play(std::move(foreign)); }, "cross-context targets rejected");
    Animation duplicate(Timing());
    duplicate.AddTranslation(target, {}, {10, 0});
    Throws([&] { duplicate.AddTranslation(target, {}, {20, 0}); }, "duplicate property channels rejected");
}

void StylesAndGeometry(FontFace& font)
{
    UiContext ui(font);
    auto& box = ui.Root().Add<Button>("Animated");
    box.SetId("box");
    std::string error;
    const std::string from = "#box { width: 100px; height: 80px; background: red; border: none; translate-x: 0; }";
    const std::string to = "#box { width: 200px; height: 120px; background: blue; border: none; translate-x: 100px; }";
    ui.Prepare(500, 300);
    ui.PointerMove({20, 20});
    const auto id = ui.AnimateStyleSheet(from, to, Timing(), error);
    Check(id != 0 && error.empty(), "complete stylesheet endpoints parse and schedule");
    ui.Prepare(500, 300);
    Check(box.GetStyle().m_Background == Color{1, 0, 0, 1} && Near(box.GetBounds().m_Width, 100),
          "initial CSS presentation is held at time zero");
    Check(ui.AnimateStyleSheet("#box { unknown: 1; }", Timing(), error) == 0 && !error.empty(),
          "invalid final stylesheet rejected atomically");
    Check(ui.Animations().GetState(id) == AnimationState::Running, "parse failure retains the active transition");
    ui.SetTime(0.5);
    ui.Prepare(500, 300);
    Check(Near(box.GetStyle().m_Background.m_R, 0.5) && Near(box.GetStyle().m_Background.m_B, 0.5),
          "stylesheet colors interpolate automatically");
    Check(Near(box.GetBounds().m_Width, 150) && Near(box.GetBounds().m_Height, 100) && Near(box.GetBounds().m_X, 50),
          "CSS dimensions and position interpolate together");
    Check(!box.IsHovered(), "animated geometry refreshes hover under a stationary pointer");
    int clicks = 0;
    box.Clicked().Connect([&] { ++clicks; });
    ui.PointerDown({20, 20});
    ui.PointerUp({20, 20});
    ui.PointerDown({100, 20});
    ui.PointerUp({100, 20});
    Check(clicks == 1, "hit testing follows animated position");
    ui.SetTime(1);
    ui.Prepare(500, 300);
    Check(box.GetStyle().m_Background == Color{0, 0, 1, 1} && Near(box.GetBounds().m_Width, 200),
          "final CSS is committed without temporary overrides");
    Check(ui.SetStyleSheet("#box { background: green; }", error), "subsequent theme parses");
    ui.Prepare(500, 300);
    Check(Near(box.GetStyle().m_Background.m_G, 0.5), "finished transition releases CSS ownership");
    box.SetWidth(SizeRule::Fixed(123));
    ui.AnimateStyleSheet(from, to, Timing(), error);
    ui.SetTime(1.5);
    ui.Prepare(500, 300);
    Check(Near(box.GetBounds().m_Width, 123), "explicit size contracts survive CSS animation");
    ui.Animations().StopAll(AnimationStopMode::Finish);
    box.ClearStyleProperties();
    Check(ui.SetStyleSheet("#box { translate-x: 0; }", error), "geometry test clears CSS translation");
    Animation geometry(Timing());
    geometry.AddBounds(box, {20, 30, 80, 40}, {100, 90, 160, 80});
    const auto geometryId = ui.Animations().Play(std::move(geometry));
    ui.Animations().Seek(geometryId, 0.5);
    ui.Prepare(500, 300);
    Check(box.GetBounds() == Rect{60, 60, 120, 60}, "absolute geometry interpolates every component");
    ui.Animations().StopAll();
    box.ClearFloatingBounds();
    ui.Prepare(500, 300);
    Check(Near(box.GetBounds().m_Width, 123), "absolute geometry can return to normal layout");
}

void MixedLengthsAndRetargeting(FontFace& font)
{
    UiContext ui(font);
    auto& box = ui.Root().Add<Widget>();
    box.SetId("box");
    ui.Prepare(500, 300);
    std::string error;
    ui.AnimateStyleSheet("#box { width: auto; height: 40px; background: red; }",
                         "#box { width: 200px; height: 40px; background: blue; }", Timing(), error);
    ui.SetTime(0.5);
    ui.Prepare(500, 300);
    Check(Near(box.GetBounds().m_Width, 350), "auto-to-pixel width uses measured endpoint geometry");
    const auto previousColor = box.GetStyle().m_Background;
    const auto replacement =
        ui.AnimateStyleSheet("#box { width: 100px; height: 40px; background: white; }", Timing(), error);
    ui.Prepare(500, 300);
    Check(box.GetStyle().m_Background == previousColor && Near(box.GetBounds().m_Width, 350),
          "retargeting begins at current presentation without snapping");
    ui.Animations().Seek(replacement, 0.5);
    ui.Prepare(500, 300);
    Check(Near(box.GetBounds().m_Width, 225), "replacement geometry interpolates from the interrupted value");
    Check(ui.SetStyleSheet("#box { width: 80px; background: green; }", error),
          "explicit reload interrupts CSS animation");
    ui.Prepare(500, 300);
    Check(Near(box.GetBounds().m_Width, 80) && Near(box.GetStyle().m_Background.m_G, 0.5),
          "explicit reload releases transition overrides");
    ui.AnimateStyleSheet("#box { width: 50%; height: 40px; }", "#box { width: 450px; height: 40px; }", Timing(), error);
    ui.SetTime(1);
    ui.Prepare(500, 300);
    Check(Near(box.GetBounds().m_Width, 350), "mixed percentage and pixel lengths interpolate resolved dimensions");
    ui.Animations().StopAll(AnimationStopMode::Finish);
    auto options = Timing();
    options.m_Easing = EasingCurve(EasingCurve::Kind::OutBack);
    ui.AnimateStyleSheet("#box { display: block; opacity: 0; }", "#box { display: none; opacity: 1; }", options, error);
    ui.SetTime(1.7);
    ui.Prepare(500, 300);
    Check(box.IsVisible() && box.GetStyle().m_Opacity <= 1,
          "discrete visibility waits for endpoint despite easing overshoot");
    ui.SetTime(2);
    ui.Prepare(500, 300);
    Check(!box.IsVisible(), "discrete visibility commits at the endpoint");
    options = Timing();
    options.m_Alternate = true;
    options.m_Iterations = 2;
    const auto alternate = ui.AnimateStyleSheet("#box { display: block; background: red; }",
                                                "#box { display: block; background: blue; }", options, error);
    ui.Animations().Pause(alternate);
    ui.Animations().Seek(alternate, 2);
    ui.Prepare(500, 300);
    Check(box.IsVisible() && box.GetStyle().m_Background == Color{1, 0, 0, 1},
          "even alternating stylesheet iterations restore the starting sheet");
}

class MeasuredWidget final : public Widget
{
  public:
    //! Counts intrinsic measurements for cache invalidation checks.
    mutable int m_Measurements = 0;

  protected:
    //! Records actual layout work instead of inferring it from repaint requests.
    Point Measure(FontFace&) const override
    {
        ++m_Measurements;
        return {100, 40};
    }
};

void CachesAndInputOwnership(FontFace& font)
{
    UiContext ui(font);
    auto& box = ui.Root().Add<MeasuredWidget>();
    ui.Prepare(400, 300);
    const int measured = box.m_Measurements;
    Animation paint(Timing());
    paint.AddStyleProperty(box, StyleProperty::Background, Color{1, 0, 0, 1}, Color{0, 0, 1, 1});
    paint.AddStyleProperty(box, StyleProperty::Opacity, 0.0f, 1.0f);
    paint.AddStyleProperty(box, StyleProperty::Radius, 0.0f, 20.0f);
    ui.Animations().Play(std::move(paint));
    ui.SetTime(0.5);
    ui.Prepare(400, 300);
    Check(box.m_Measurements == measured, "paint-only animation reuses style and layout caches");
    Check(Near(box.GetStyle().m_Background.m_B, 0.5) && Near(box.GetStyle().m_Radius, 10),
          "cached geometry still paints updated presentation");
    ui.Animations().StopAll();
    auto& parent = ui.Root().Add<Widget>();
    auto& child = parent.Add<Widget>();
    Animation inherited(Timing());
    inherited.AddStyleProperty(parent, StyleProperty::Color, Color{1, 0, 0, 1}, Color{0, 0, 1, 1});
    ui.Animations().Play(std::move(inherited));
    ui.SetTime(1);
    ui.Prepare(400, 300);
    Check(Near(child.GetStyle().m_Color.m_R, 0.5) && Near(child.GetStyle().m_Color.m_B, 0.5),
          "inherited properties propagate their animated value to descendants");
    ui.Animations().StopAll();
    auto& editor = ui.Root().Add<TextBox>("keep focus");
    editor.SetId("editor");
    std::string error;
    Check(ui.SetStyleSheet("#editor { display: block; }", error), "focus test style parses");
    ui.Prepare(400, 300);
    ui.SetFocus(&editor);
    ui.AnimateStyleSheet("#editor { display: none; }", Timing(), error);
    Check(ui.GetFocus() == &editor, "measuring a hidden endpoint does not prematurely remove input focus");
    ui.SetTime(1.5);
    ui.Prepare(400, 300);
    Check(ui.GetFocus() == &editor && editor.IsVisible(), "focus survives until the visibility endpoint");
    ui.SetTime(2);
    ui.Prepare(400, 300);
    Check(ui.GetFocus() == nullptr && !editor.IsVisible(), "focus is removed when final CSS actually hides its target");

    Animation nested(Timing());
    nested.AddCustom(box, "nested-clock",
                     [&](Widget&, double progress)
                     {
                         if (progress > 0)
                             ui.SetTime(10);
                     });
    ui.Animations().Play(std::move(nested));
    Throws([&] { ui.SetTime(2.5); }, "nested animation clock updates are rejected safely");
    ui.SetTime(3);
    Check(Near(ui.GetTime(), 3) && !ui.Animations().HasRunning(),
          "nested callback failure cannot corrupt the GUI clock");

    bool chained = false;
    ui.Animations().Finished().Connect(
        [&](AnimationId)
        {
            if (!chained)
            {
                chained = true;
                ui.AnimateStyleSheet("#editor { display: block; background: blue; }", Timing(), error);
            }
        });
    ui.AnimateStyleSheet("#editor { display: block; background: red; }", Timing(0), error);
    Check(chained && ui.Animations().HasRunning(), "synchronous CSS completion can start a following transition");
    Check(ui.SetStyleSheet("#editor { background: green; }", error) && !ui.Animations().HasRunning(),
          "reload cancels the newer transition started by an immediate completion listener");
}
} // namespace

int main()
{
    try
    {
        CurvesAndValues();
        FontFace font(ESTUDIOUI_FONT_PATH);
        Playback(font);
        ReplacementAndLifetimes(font);
        StylesAndGeometry(font);
        MixedLengthsAndRetargeting(font);
        CachesAndInputOwnership(font);
        std::cout << "Animation checks passed: " << s_Checks << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Animation check failed: " << exception.what() << '\n';
        return 1;
    }
}
