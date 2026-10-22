#include <EStudioUI/Label.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/State.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

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

bool Near(float left, float right)
{
    return std::abs(left - right) < 0.02f;
}

class Probe final : public Widget
{
  public:
    mutable int m_Measures = 0;
    int m_Layouts = 0;

    void SetContentWidth(float width)
    {
        VerifyAccess();
        if (m_Width == width)
            return;
        m_Width = width;
        Invalidate(Invalidation::Content);
    }

    void LegacyChange()
    {
        Invalidate();
    }

    void SetLegacyContentWidth(float width)
    {
        m_Width = width;
        Invalidate();
    }

  protected:
    Point Measure(FontFace&) const override
    {
        ++m_Measures;
        return {m_Width, GetStyle().m_FontSize};
    }

    void OnLayout() override
    {
        ++m_Layouts;
    }

  private:
    float m_Width = 20;
};

class FloatingOwner final : public Widget
{
  public:
    FloatingOwner()
    {
        m_Child = &Add<Probe>();
    }

    Probe& Child()
    {
        return *m_Child;
    }

  protected:
    void OnLayout() override
    {
        m_Child->SetFloatingBounds(GetContentBounds());
    }

  private:
    Probe* m_Child = nullptr;
};

class StateReadProbe final : public Widget
{
  public:
    explicit StateReadProbe(State<int>& state) : m_State(state)
    {
    }

    mutable bool m_Read = false;
    mutable bool m_SetRejected = false;
    mutable bool m_BatchRejected = false;
    mutable bool m_BatchRan = false;

  protected:
    Point Measure(FontFace&) const override
    {
        m_Read = m_State.Get() == 42 && GetContext()->States().GetRevision() == 0;
        try
        {
            m_State.Set(99);
        }
        catch (const std::logic_error&)
        {
            m_SetRejected = true;
        }
        try
        {
            GetContext()->States().Batch([this] { m_BatchRan = true; });
        }
        catch (const std::logic_error&)
        {
            m_BatchRejected = true;
        }
        return {20, 20};
    }

  private:
    State<int>& m_State;
};

class ThrowingPartProbe final : public Widget
{
  public:
    ThrowingPartProbe()
    {
        RegisterStylePart("probe", 0,
                          [this](const Style& owner, unsigned)
                          {
                              if (m_Throw && owner.m_FontSize == 30)
                              {
                                  m_Throw = false;
                                  throw std::runtime_error("part defaults failed once");
                              }
                              return Style{};
                          });
    }

    bool m_Throw = false;

  protected:
    Point Measure(FontFace&) const override
    {
        return {20, GetStyle().m_FontSize};
    }
};

void TenThousandWidgets(FontFace& font)
{
    UiContext ui(font);
    std::vector<Probe*> probes;
    for (int rowIndex = 0; rowIndex < 99; ++rowIndex)
    {
        auto& row = ui.Root().Add<Widget>();
        row.SetLayoutDirection(Axis::Horizontal);
        row.SetHeight(SizeRule::Fixed(20));
        for (int column = 0; column < 100; ++column)
            probes.push_back(&row.Add<Probe>());
    }
    Check(1 + ui.Root().GetChildren().size() + probes.size() == 10000, "fixture contains exactly 10,000 widgets");
    ui.Prepare(3000, 2200);
    const auto settled = ui.GetStats();
    probes.front()->SetStyleProperty(StyleProperty::Background, Color{1, 0, 0, 1});
    ui.Prepare(3000, 2200);
    const auto painted = ui.GetStats();
    Check(painted.m_StyleResolutions == settled.m_StyleResolutions &&
              painted.m_Measurements == settled.m_Measurements &&
              painted.m_MeasureCalculations == settled.m_MeasureCalculations &&
              painted.m_LayoutPasses == settled.m_LayoutPasses && painted.m_Arrangements == settled.m_Arrangements,
          "isolated noninherited paint edit performs no style, measurement, or arrangement work");
    probes.front()->ClearStyleProperty(StyleProperty::Background);
    ui.Prepare(3000, 2200);
    Check(ui.GetStats().m_StyleResolutions == painted.m_StyleResolutions &&
              ui.GetStats().m_Arrangements == painted.m_Arrangements,
          "isolated paint override release reuses CSS and geometry caches");
    const int unrelatedMeasures = probes.back()->m_Measures;
    const int unrelatedLayouts = probes.back()->m_Layouts;
    const float siblingX = probes[1]->GetBounds().m_X;
    probes.front()->SetContentWidth(50);
    ui.Prepare(3000, 2200);
    const auto content = ui.GetStats();
    Check(content.m_StyleResolutions == painted.m_StyleResolutions, "content edit retains every unrelated style");
    Check(content.m_Measurements == painted.m_Measurements + 1,
          "content edit reuses ancestor and unrelated intrinsic measurements");
    Check(content.m_MeasureCalculations == painted.m_MeasureCalculations + 3,
          "aggregate measurement follows only source, row, and root dependencies");
    Check(content.m_MeasureCacheHits > painted.m_MeasureCacheHits, "unchanged constrained measurements are reused");
    Check(probes.back()->m_Measures == unrelatedMeasures && probes.back()->m_Layouts == unrelatedLayouts,
          "unrelated row keeps intrinsic measurement and arrangement caches");
    Check(Near(probes[1]->GetBounds().m_X, siblingX + 30), "caption growth rearranges its allocation siblings");
    ui.Prepare(3000, 2200);
    Check(ui.GetStats().m_Arrangements == content.m_Arrangements, "settled prepare retains arrangement results");
    std::cout << "10,000 widgets: paint delta style/measure/arrange = 0/0/0; content delta style/intrinsic/aggregate/arrange = "
              << content.m_StyleResolutions - painted.m_StyleResolutions << '/'
              << content.m_Measurements - painted.m_Measurements << '/'
              << content.m_MeasureCalculations - painted.m_MeasureCalculations << '/'
              << content.m_Arrangements - painted.m_Arrangements << '\n';
}

void ConstraintsAndInheritance(FontFace& font)
{
    UiContext ui(font);
    auto& row = ui.Root().Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetHeight(SizeRule::Fixed(80));
    auto& caption = row.Add<Label>("short");
    auto& percent = row.Add<Probe>();
    percent.SetStyleProperty(StyleProperty::Width, Length{Length::Unit::Percent, 50});
    auto& unrelated = ui.Root().Add<Probe>();
    ui.Prepare(400, 300);
    Check(Near(percent.GetBounds().m_Width, 200), "percent allocation uses the actual content constraint");
    const auto beforeText = ui.GetStats();
    const int unrelatedMeasures = unrelated.m_Measures;
    caption.SetText("a longer caption");
    ui.Prepare(400, 300);
    Check(ui.GetStats().m_StyleResolutions == beforeText.m_StyleResolutions && unrelated.m_Measures == unrelatedMeasures,
          "built-in caption setter keeps unrelated CSS and intrinsic measurements");
    Check(Near(percent.GetBounds().m_Width, 200), "arranged percentages do not contaminate persistent measurement");
    const auto beforeResize = ui.GetStats();
    ui.Prepare(600, 300);
    Check(Near(percent.GetBounds().m_Width, 300), "changed real constraints invalidate aggregate measurement keys");
    Check(ui.GetStats().m_Measurements == beforeResize.m_Measurements,
          "constraint changes retain constraint-independent intrinsic results");
    const int beforeFont = unrelated.m_Measures;
    row.SetStyleProperty(StyleProperty::FontSize, 30.0f);
    ui.Prepare(600, 300);
    Check(Near(caption.GetStyle().m_FontSize, 30) && Near(percent.GetStyle().m_FontSize, 30),
          "font override propagates inherited presentation through descendants");
    Check(unrelated.m_Measures == beforeFont, "inherited edit retains measurements outside its subtree");
    row.SetStyleProperty(StyleProperty::Color, Color{1, 0, 0, 1});
    const auto beforeColor = ui.GetStats();
    ui.Prepare(600, 300);
    Check(caption.GetStyle().m_Color == Color{1, 0, 0, 1}, "foreground inheritance reaches descendants");
    Check(ui.GetStats().m_Measurements == beforeColor.m_Measurements,
          "inherited foreground retains intrinsic measurements");
}

void VisibilityAndHooks(FontFace& font)
{
    UiContext ui(font);
    auto& branch = ui.Root().Add<Widget>();
    auto& hidden = branch.Add<Probe>();
    auto& owner = ui.Root().Add<FloatingOwner>();
    owner.SetHeight(SizeRule::Fixed(60));
    ui.Prepare(400, 300);
    ui.Prepare(400, 300);
    Check(Near(owner.Child().GetBounds().m_Width, owner.GetContentBounds().m_Width),
          "OnLayout floating-child updates remain effective");
    branch.SetVisible(false);
    ui.Prepare(400, 300);
    const int hiddenMeasures = hidden.m_Measures;
    hidden.SetContentWidth(90);
    branch.SetStyleProperty(StyleProperty::FontSize, 28.0f);
    ui.Prepare(400, 300);
    Check(hidden.m_Measures == hiddenMeasures, "hidden subtree performs no intrinsic measurement");
    branch.SetVisible(true);
    ui.Prepare(400, 300);
    Check(hidden.m_Measures > hiddenMeasures && Near(hidden.GetStyle().m_FontSize, 28),
          "revealed subtree catches up content and inherited style dependencies");
    branch.SetVisible(false);
    ui.Prepare(400, 300);
    const int beforeHiddenLegacy = hidden.m_Measures;
    hidden.SetLegacyContentWidth(120);
    ui.Prepare(400, 300);
    Check(hidden.m_Measures == beforeHiddenLegacy, "legacy invalidation does not measure hidden content");
    branch.SetVisible(true);
    ui.Prepare(400, 300);
    Check(hidden.m_Measures == beforeHiddenLegacy + 1, "legacy invalidation discards hidden intrinsic cache until reveal");
    const auto beforeLegacy = ui.GetStats();
    hidden.LegacyChange();
    ui.Prepare(400, 300);
    Check(ui.GetStats().m_Measurements > beforeLegacy.m_Measurements + 1,
          "legacy custom-widget invalidation retains the conservative whole-tree path");
}

void OverflowAndContracts(FontFace& font)
{
    UiContext ui(font);
    auto& overflowing = ui.Root().Add<Widget>();
    overflowing.SetLayoutDirection(Axis::Horizontal);
    overflowing.SetWidth(SizeRule::Fixed(100));
    overflowing.SetHeight(SizeRule::Fixed(30));
    auto& fixed = overflowing.Add<Probe>();
    fixed.SetWidth(SizeRule::Fixed(200));
    auto& unrelated = ui.Root().Add<Probe>();
    ui.Prepare(500, 300);
    Check(!ui.GetOverflowingContainers().empty(), "hard allocation overflow is reported");
    unrelated.SetContentWidth(40);
    ui.Prepare(500, 300);
    Check(!ui.GetOverflowingContainers().empty(), "skipped layout subtree retains its overflow diagnostics");
    std::string error;
    Check(ui.SetStyleSheet("panel { width: 900px; min-width: 800px; }", error), "contract theme parses");
    ui.Prepare(500, 300);
    Check(Near(overflowing.GetBounds().m_Width, 100) && Near(fixed.GetBounds().m_Width, 200),
          "explicit hard size contracts retain precedence after theme reload");
}

void CachedMotionRelease(FontFace& font)
{
    UiContext ui(font);
    auto& target = ui.Root().Add<Probe>();
    target.SetId("target");
    std::string error;
    Check(ui.SetStyleSheet(R"(
        #target { opacity: .2; animation: pulse 2s linear infinite; }
        @keyframes pulse { from { opacity: .2; } to { opacity: 1; } }
    )", error), "paint keyframe theme parses");
    ui.Prepare(400, 300);
    target.SetStyleProperty(StyleProperty::Opacity, 0.9f);
    target.SetStyleProperty(StyleProperty::Background, Color{0, 1, 0, 1});
    ui.SetTime(0.5);
    Check(Near(target.GetStyle().m_Opacity, 0.9f), "explicit override stays above running CSS keyframes");
    const auto keyed = ui.GetStats();
    target.ClearStyleProperty(StyleProperty::Opacity);
    ui.Prepare(400, 300);
    Check(Near(target.GetStyle().m_Opacity, 0.4f), "paint release reveals the current compiled keyframe sample");
    Check(target.GetStyle().m_Background == Color{0, 1, 0, 1}, "releasing one override retains other explicit overrides");
    Check(ui.GetStats().m_StyleResolutions == keyed.m_StyleResolutions &&
              ui.GetStats().m_Measurements == keyed.m_Measurements &&
              ui.GetStats().m_Arrangements == keyed.m_Arrangements,
          "keyframe paint release performs no style, measure, or arrangement work");
    Check(ui.SetStyleSheet(R"(
        #target { opacity: .2; transition: opacity 1s linear; }
        #target.on { opacity: .8; }
    )", error), "paint transition theme parses");
    ui.Prepare(400, 300);
    target.AddClass("on");
    ui.Prepare(400, 300);
    target.SetStyleProperty(StyleProperty::Opacity, 0.95f);
    ui.SetTime(1.0);
    const auto transitioned = ui.GetStats();
    target.ClearStyleProperty(StyleProperty::Opacity);
    ui.Prepare(400, 300);
    Check(Near(target.GetStyle().m_Opacity, 0.5f), "paint release reveals the current CSS transition sample");
    Check(ui.GetStats().m_StyleResolutions == transitioned.m_StyleResolutions,
          "transition release does not repeat CSS selector evaluation");
    Check(ui.SetStyleSheet("#target { opacity: .65; background: red; }", error), "replacement paint theme parses");
    ui.Prepare(400, 300);
    target.SetStyleProperty(StyleProperty::Opacity, 0.1f);
    target.ClearStyleProperty(StyleProperty::Opacity);
    target.ClearStyleProperty(StyleProperty::Background);
    ui.Prepare(400, 300);
    Check(Near(target.GetStyle().m_Opacity, 0.65f) && target.GetStyle().m_Background == Color{1, 0, 0, 1},
          "paint release uses the latest stylesheet and selector cascade");
}

void StateReadDuringMeasurement(FontFace& font)
{
    UiContext ui(font);
    State<int> state(ui.States(), 42);
    auto& probe = ui.Root().Add<StateReadProbe>(state);
    ui.Prepare(400, 300);
    Check(probe.m_Read, "custom measurement may read committed observable state and context state metadata");
    Check(probe.m_SetRejected && probe.m_BatchRejected && !probe.m_BatchRan && state.Get() == 42,
          "state writes and batches are rejected before mutation during measurement");
}

void AtomicFrameBoundary(FontFace& font)
{
    UiContext ui(font);
    State<int> state(ui.States(), 42);
    ui.Prepare(400, 300);
    bool prepareRejected = false;
    bool timeRejected = false;
    bool drainRejected = false;
    bool deferredRan = false;
    bool bodyThrew = false;
    try
    {
        ui.States().Batch(
            [&]
            {
                state.Set(84);
                ui.Defer([&] { deferredRan = true; });
                try
                {
                    ui.Prepare(400, 300);
                }
                catch (const std::logic_error&)
                {
                    prepareRejected = true;
                }
                try
                {
                    ui.SetTime(1);
                }
                catch (const std::logic_error&)
                {
                    timeRejected = true;
                }
                try
                {
                    ui.GetDispatcher().Drain();
                }
                catch (const std::logic_error&)
                {
                    drainRejected = true;
                }
                throw std::runtime_error("rollback frame-boundary probe");
            });
    }
    catch (const std::runtime_error&)
    {
        bodyThrew = true;
    }
    Check(bodyThrew && prepareRejected && timeRejected && drainRejected && !deferredRan,
          "frame preparation, animation clock, and queue drain cannot observe staged batch values");
    Check(state.Get() == 42 && ui.States().GetRevision() == 0 && ui.GetTime() == 0,
          "throwing batch body restores canonical state without advancing frame time");
    ui.GetDispatcher().Drain();
    Check(deferredRan, "queued work remains deliverable after batch rollback ends");
}

void FailedStyleRetry(FontFace& font)
{
    UiContext ui(font);
    auto& parent = ui.Root().Add<Widget>();
    auto& probe = parent.Add<ThrowingPartProbe>();
    ui.Prepare(400, 300);
    probe.m_Throw = true;
    parent.SetStyleProperty(StyleProperty::FontSize, 30.0f);
    bool failed = false;
    try
    {
        ui.Prepare(400, 300);
    }
    catch (const std::runtime_error&)
    {
        failed = true;
    }
    Check(failed && ui.GetPhase() == UiPhase::Idle, "failed part style resolution restores the traversal phase");
    ui.Prepare(400, 300);
    Check(Near(probe.GetBounds().m_Height, 30), "style retry discards intrinsic results from the previous font");
}

void CappedConstraintClosure(FontFace& font)
{
    UiContext ui(font);
    auto& wrapper = ui.Root().Add<Widget>();
    wrapper.SetWidth(SizeRule::Content(0, 200));
    wrapper.SetHeight(SizeRule::Fixed(100));
    auto& capped = wrapper.Add<Widget>();
    capped.SetWidth(SizeRule::Content(0, 200));
    capped.SetHeight(SizeRule::Fixed(100));
    capped.SetLayoutDirection(Axis::Horizontal);
    auto& content = capped.Add<Widget>();
    auto& percent = content.Add<Probe>();
    percent.SetStyleProperty(StyleProperty::Width, Length{Length::Unit::Percent, 50});
    ui.Prepare(400, 300);
    ui.Prepare(600, 300);
    const auto retained = percent.GetBounds();
    ui.Invalidate();
    ui.Prepare(600, 300);
    const auto rebuilt = percent.GetBounds();
    Check(Near(retained.m_X, rebuilt.m_X) && Near(retained.m_Y, rebuilt.m_Y) &&
              Near(retained.m_Width, rebuilt.m_Width) && Near(retained.m_Height, rebuilt.m_Height),
          "constraint misses rearrange descendants even when a capped ancestor keeps identical bounds and clip");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        TenThousandWidgets(font);
        ConstraintsAndInheritance(font);
        VisibilityAndHooks(font);
        OverflowAndContracts(font);
        CachedMotionRelease(font);
        StateReadDuringMeasurement(font);
        AtomicFrameBoundary(font);
        FailedStyleRetry(font);
        CappedConstraintClosure(font);
        std::cout << "Invalidation PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Invalidation FAIL / " << error.what() << '\n';
        return 1;
    }
}
