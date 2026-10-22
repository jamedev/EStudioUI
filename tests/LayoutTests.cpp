#include <EStudioUI/BoxLayout.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/FlowLayout.h>
#include <EStudioUI/FormLayout.h>
#include <EStudioUI/GridLayout.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

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

template <typename Callback> void Throws(Callback callback, const std::string& message)
{
    bool rejected = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, message);
}

class WrappedProbe final : public Widget
{
  public:
    mutable int m_Calls = 0;
    bool m_Bad = false;

  protected:
    Point Measure(FontFace&) const override
    {
        return {400, 20};
    }

    MeasureResult MeasureConstrained(FontFace&, const MeasureConstraints& constraints) const override
    {
        ++m_Calls;
        const float width = constraints.m_Width.value_or(400);
        return {{40, 20},
                {400, m_Bad ? std::numeric_limits<float>::quiet_NaN() : std::ceil(400 / std::max(1.0f, width)) * 20},
                15,
                true};
    }
};

class BrokenLayout final : public LayoutStrategy
{
  public:
    explicit BrokenLayout(int mode) : m_Mode(mode)
    {
    }

    std::string_view GetName() const override
    {
        return "Broken";
    }

    LayoutPlan Compute(const LayoutContext& context) const override
    {
        auto plan = BoxLayout().Compute(context);
        if (m_Mode == 0)
            plan.m_Slots.clear();
        else if (m_Mode == 1)
            plan.m_Slots[0].m_Bounds.m_X = std::numeric_limits<float>::infinity();
        else if (m_Mode == 2)
            plan.m_Slots[0].m_Bounds.m_Width = 0;
        else if (m_Mode == 3)
            plan.m_Slots[1].m_Index = 0;
        else if (m_Mode == 4)
            context.m_Measure(999, {}, false, false);
        else if (m_Mode == 5)
            plan.m_Slots[0].m_Bounds.m_Height = -0.001f;
        else
            plan.m_Tracks.push_back(
                {Axis::Horizontal, 0, SizeRule::Content(), std::numeric_limits<float>::infinity(), 0, 0});
        return plan;
    }

  private:
    int m_Mode;
};

class GuardLayout final : public LayoutStrategy
{
  public:
    explicit GuardLayout(Widget& owner) : m_Owner(owner)
    {
    }

    std::string_view GetName() const override
    {
        return "Guard";
    }

    LayoutPlan Compute(const LayoutContext& context) const override
    {
        Throws([&] { m_Owner.SetWidth(SizeRule::Fixed(999)); }, "sizing mutation rejected during strategy traversal");
        Throws([&] { m_Owner.SetLayout(FlowLayout{}); },
               "strategy replacement rejected before destruction of active strategy");
        return BoxLayout().Compute(context);
    }

  private:
    Widget& m_Owner;
};

void WidthBeforeHeight(FontFace& font)
{
    UiContext ui(font);
    auto& paragraph = ui.Root().Add<Label>(
        "A predictable layout measures a paragraph at its allocated width before positioning the next widget.");
    paragraph.SetWordWrap(true);
    paragraph.SetWidth(SizeRule::Fill());
    paragraph.SetHeight(SizeRule::Content());
    auto& next = ui.Root().Add<Button>("Next");
    next.SetHeight(SizeRule::Fixed(30));
    ui.Root().SetGap(8);
    ui.Prepare(400, 600);
    const auto wide = paragraph.GetBounds();
    Check(Near(wide.m_Width, 400), "wrapping receives the allocated column width");
    Check(Near(next.GetBounds().m_Y, wide.m_Y + wide.m_Height + 8), "following sibling uses measured wrapped height");
    ui.Prepare(160, 600);
    const auto narrow = paragraph.GetBounds();
    Check(narrow.m_Height > wide.m_Height && Near(narrow.m_Width, 160), "narrowing increases content height");
    Check(Near(next.GetBounds().m_Y, narrow.m_Height + 8), "resize moves subsequent widgets without overlap");
    Check(ui.GetOverflowingContainers().empty(), "wrapped content fits without artificial overflow");
    ui.Prepare(400, 600);
    Check(Near(paragraph.GetBounds().m_Height, wide.m_Height), "returning to a prior width restores the same height");
    paragraph.SetStyleProperty(StyleProperty::Padding, Insets{5, 12, 7, 12});
    paragraph.SetStyleProperty(StyleProperty::BorderWidth, 2.0f);
    ui.Prepare(160, 600);
    Check(Near(paragraph.GetContentBounds().m_Width, 132) && paragraph.GetBounds().m_Height > narrow.m_Height,
          "measurement uses the content width after padding and border");

    UiContext rowUi(font);
    auto& row = rowUi.Root().Add<Widget>();
    row.SetLayout(BoxLayout(Axis::Horizontal));
    row.SetAlignment(LayoutAlignment::Start);
    row.SetWidth(SizeRule::Fill());
    row.SetGap(10);
    auto& fixed = row.Add<Widget>();
    fixed.SetWidth(SizeRule::Fixed(100));
    fixed.SetHeight(SizeRule::Fixed(25));
    auto& text = row.Add<Label>(paragraph.GetText());
    text.SetWordWrap(true);
    text.SetWidth(SizeRule::Fill(1, 20));
    rowUi.Prepare(300, 600);
    Check(Near(text.GetBounds().m_Width, 190), "row allocates fixed width and gap before wrapping Fill content");
    Check(Near(row.GetBounds().m_Height, text.GetBounds().m_Height), "content row grows to its wrapped child height");
}

void GridAndForm(FontFace& font)
{
    UiContext ui(font);
    ui.Root().SetLayout(GridLayout({SizeRule::Fixed(100), SizeRule::Fill(1, 0, 50), SizeRule::Fill(1, 40)}));
    ui.Root().SetGap(10);
    for (int i = 0; i < 6; ++i)
    {
        auto& cell = ui.Root().Add<Widget>();
        cell.SetHeight(SizeRule::Fixed(30));
    }
    ui.Prepare(500, 200);
    const auto& cells = ui.Root().GetChildren();
    Check(Near(cells[0]->GetBounds().m_Width, 100) && Near(cells[1]->GetBounds().m_Width, 50) &&
              Near(cells[2]->GetBounds().m_Width, 330),
          "grid reuses capped Fill redistribution");
    Check(Near(cells[3]->GetBounds().m_Y, 40) && Near(cells[3]->GetBounds().m_X, cells[0]->GetBounds().m_X),
          "row-major cells share tracks and gaps");
    const auto tracks = ui.Root().GetLayoutReport().m_Tracks;
    Check(tracks.size() == 5 && tracks[0].m_Rule.m_Mode == SizeRule::Mode::Fixed && Near(tracks[1].m_Allocated, 50),
          "track report explains shared rules, bounds, and capped allocations");
    cells[1]->SetVisible(false);
    ui.Prepare(500, 200);
    Check(ui.Root().GetLayoutReport().m_Children.size() == 5 && Near(cells[2]->GetBounds().m_X, 110),
          "hidden cells consume neither a slot nor an extra gap");
    ui.Prepare(80, 100);
    Check(Near(cells[0]->GetBounds().m_Width, 100) && cells[3]->GetBounds().m_Width >= 40,
          "small grid preserves track fixed sizes and minima");
    Check(!ui.GetLayoutDiagnostics().empty(), "grid overflow identifies concrete allocations");

    UiContext formUi(font);
    formUi.Root().SetLayout(FormLayout{});
    formUi.Root().SetGap(12);
    auto& small = formUi.Root().Add<Label>("Name");
    small.SetStyleProperty(StyleProperty::FontSize, 12.0f);
    auto& first = formUi.Root().Add<TextBox>("Part 17");
    first.SetHeight(SizeRule::Fixed(44));
    first.SetStyleProperty(StyleProperty::FontSize, 24.0f);
    formUi.Root().Add<Label>("Longer description");
    auto& description =
        formUi.Root().Add<Label>("This field wraps into several lines as the shared field column becomes narrower.");
    description.SetWordWrap(true);
    formUi.Prepare(500, 500);
    const auto report = formUi.Root().GetLayoutReport();
    const auto& a = report.m_Children[0].m_Slot;
    const auto& b = report.m_Children[1].m_Slot;
    Check(a.m_Measure.m_Baseline && b.m_Measure.m_Baseline &&
              Near(a.m_Bounds.m_Y + *a.m_Measure.m_Baseline, b.m_Bounds.m_Y + *b.m_Measure.m_Baseline),
          "form aligns actual label and editor baselines despite different fonts/heights");
    const auto smallLine = font.Shape("Name", 12);
    const auto editorLine = font.Shape("Part 17", 24, true);
    const auto labelContent = small.GetContentBounds();
    const auto editorContent = first.GetContentBounds();
    const float labelBaseline =
        labelContent.m_Y +
        std::max(0.0f,
                 (labelContent.m_Height - std::max(12 * 1.35f, smallLine.m_Ascender + smallLine.m_Descender)) * 0.5f) +
        smallLine.m_Ascender;
    const float editorBaseline = editorContent.m_Y +
                                 (editorContent.m_Height - editorLine.m_Ascender - editorLine.m_Descender) * 0.5f +
                                 editorLine.m_Ascender;
    Check(Near(labelBaseline, editorBaseline), "independently calculated production paint baselines agree");
    Check(Near(first.GetBounds().m_X, description.GetBounds().m_X), "all form fields share a column origin");
    const auto originalHeight = description.GetBounds().m_Height;
    formUi.Prepare(300, 500);
    Check(description.GetBounds().m_Height > originalHeight, "grid measures row height after field column allocation");
    Check(formUi.Root().GetLayoutReport().m_Strategy == "Form", "diagnostics expose the composed strategy name");

    UiContext rowsUi(font);
    rowsUi.Root().SetLayout(
        GridLayout({SizeRule::Fill()}, {SizeRule::Fixed(40), SizeRule::Fill(1, 20, 60), SizeRule::Fill(2, 20)}));
    for (int i = 0; i < 3; ++i)
        rowsUi.Root().Add<Widget>();
    rowsUi.Prepare(200, 300);
    Check(Near(rowsUi.Root().GetChildren()[0]->GetBounds().m_Height, 40) &&
              Near(rowsUi.Root().GetChildren()[1]->GetBounds().m_Height, 60) &&
              Near(rowsUi.Root().GetChildren()[2]->GetBounds().m_Height, 200),
          "row tracks redistribute capped Fill space too");
}

void Flow(FontFace& font)
{
    UiContext ui(font);
    auto& flow = ui.Root().Add<Widget>();
    flow.SetLayout(FlowLayout{});
    flow.SetGap(10);
    for (int i = 0; i < 5; ++i)
    {
        auto& item = flow.Add<Button>(std::to_string(i));
        item.SetWidth(SizeRule::Fixed(80));
        item.SetHeight(SizeRule::Fixed(20));
    }
    ui.Prepare(200, 400);
    const auto& items = flow.GetChildren();
    Check(Near(items[2]->GetBounds().m_Y - items[0]->GetBounds().m_Y, 30) && Near(flow.GetBounds().m_Height, 80),
          "five items wrap into three rows with exact row spacing");
    ui.Prepare(300, 400);
    Check(Near(items[2]->GetBounds().m_Y, items[0]->GetBounds().m_Y) && Near(flow.GetBounds().m_Height, 50),
          "resizing reflows to two rows and updates content height");
    flow.SetJustification(LayoutJustification::SpaceBetween);
    ui.Prepare(300, 400);
    Check(Near(items[2]->GetBounds().m_X + items[2]->GetBounds().m_Width, 300),
          "each wrapped row supports main-axis justification");
    items[0]->SetWidth(SizeRule::Fixed(400));
    flow.SetWidth(SizeRule::Fixed(300));
    items[0]->SetId("oversized");
    ui.Prepare(300, 400);
    bool found = false;
    for (const auto& issue : ui.GetLayoutDiagnostics())
        found |= issue.m_Issue == LayoutIssue::Overflow && issue.m_ChildId == "oversized";
    Check(found && Near(items[0]->GetBounds().m_Width, 400),
          "oversized flow item retains Fixed and reports its identity");

    UiContext textUi(font);
    auto& panel = textUi.Root().Add<Widget>();
    panel.SetLayout(FlowLayout{});
    auto& text = panel.Add<Label>("An oversized paragraph can occupy one bounded flow row and wrap safely inside it.");
    text.SetWordWrap(true);
    textUi.Prepare(140, 600);
    Check(Near(text.GetBounds().m_Width, 140) && text.GetBounds().m_Height > 30,
          "flow bounds a width-dependent content item before wrapping its text");
    UiContext percentageUi(font);
    auto& percentageFlow = percentageUi.Root().Add<Widget>();
    percentageFlow.SetLayout(FlowLayout{});
    percentageFlow.SetHeight(SizeRule::Fixed(200));
    auto& percentageItem = percentageFlow.Add<Widget>();
    percentageItem.SetWidth(SizeRule::Fixed(80));
    percentageItem.SetStyleProperty(StyleProperty::Height, Length{Length::Unit::Percent, 50});
    percentageUi.Prepare(300, 400);
    Check(Near(percentageItem.GetBounds().m_Height, 100),
          "flow rows retain the independent parent percentage reference");
    for (const auto& issue : percentageUi.GetLayoutDiagnostics())
        Check(issue.m_Issue != LayoutIssue::IndefinitePercentage, "resolved flow height has no spurious cycle warning");
}

void PercentagesAndReports(FontFace& font)
{
    UiContext ui(font);
    ui.Root().SetLayoutDirection(Axis::Horizontal);
    auto& content = ui.Root().Add<Widget>();
    content.SetLayoutDirection(Axis::Horizontal);
    content.SetHeight(SizeRule::Fixed(50));
    content.SetId("content");
    auto& child = content.Add<Button>("Percent");
    child.SetId("percent");
    child.SetStyleProperty(StyleProperty::Width, Length{Length::Unit::Percent, 50});
    ui.Prepare(400, 300);
    bool cycle = false;
    for (const auto& issue : ui.GetLayoutDiagnostics())
        cycle |= issue.m_Issue == LayoutIssue::IndefinitePercentage && issue.m_ChildId == "percent";
    Check(cycle && child.GetBounds().m_Width > 0,
          "percentage/content cycle uses Content sizing with a named diagnostic");
    content.SetWidth(SizeRule::Fixed(200));
    ui.Prepare(400, 300);
    Check(Near(child.GetBounds().m_Width, 100), "independent width resolves percentages deterministically");
    for (const auto& issue : ui.GetLayoutDiagnostics())
        Check(issue.m_Issue != LayoutIssue::IndefinitePercentage, "resolved percentage removes its cycle diagnostic");
    child.SetWidth(SizeRule::Fixed(260));
    ui.Prepare(400, 300);
    const auto owned = content.GetLayoutReport();
    Check(owned.Explain().find("percent") != std::string::npos &&
              owned.Explain().find("Fixed(260)") != std::string::npos,
          "allocation explanation includes the culprit, rule, limits, and dimensions");
    content.ClearChildren();
    ui.Prepare(400, 300);
    Check(owned.m_Children[0].m_ChildId == "percent" && content.GetLayoutReport().m_Diagnostics.empty(),
          "owned report survives child deletion while current diagnostics refresh");
    content.SetVisible(false);
    ui.Prepare(400, 300);
    Check(ui.GetLayoutDiagnostics().empty(), "hidden branches do not expose stale diagnostics");
}

void ExtensionsAndCaching(FontFace& font)
{
    UiContext ui(font);
    auto& wrapped = ui.Root().Add<WrappedProbe>();
    wrapped.SetWidth(SizeRule::Fill());
    ui.Prepare(100, 500);
    Check(Near(wrapped.GetBounds().m_Height, 80), "custom height-for-width hook controls content height");
    ui.Prepare(200, 500);
    Check(Near(wrapped.GetBounds().m_Height, 40), "custom measurement responds to allocated width");
    const int calls = wrapped.m_Calls;
    for (int i = 0; i < 8; ++i)
        ui.Prepare(i % 2 == 0 ? 100.0f : 200.0f, 500);
    Check(wrapped.m_Calls == calls, "bounded constraint cache reuses natural and two allocated widths");
    const auto settled = ui.GetStats();
    wrapped.SetStyleProperty(StyleProperty::Background, Color{1, 0, 0, 1});
    ui.Prepare(200, 500);
    Check(ui.GetStats().m_LayoutPlans == settled.m_LayoutPlans &&
              ui.GetStats().m_ConstrainedMeasurements == settled.m_ConstrainedMeasurements &&
              ui.GetStats().m_Arrangements == settled.m_Arrangements,
          "paint-only edits preserve strategy and constrained caches");
    wrapped.m_Bad = true;
    ui.Invalidate();
    Throws([&] { ui.Prepare(200, 500); }, "invalid custom measurement rejected");
    Check(ui.GetPhase() == UiPhase::Idle, "measurement failure restores the phase");
    wrapped.m_Bad = false;
    ui.Prepare(200, 500);
    Check(Near(wrapped.GetBounds().m_Height, 40), "failed measurement can be corrected and retried");
    for (int mode = 0; mode < 7; ++mode)
    {
        UiContext broken(font);
        broken.Root().Add<Widget>().SetWidth(SizeRule::Fixed(20));
        broken.Root().Add<Widget>();
        broken.Root().SetLayout(BrokenLayout(mode));
        Throws([&] { broken.Prepare(200, 200); }, "malformed strategy result rejected");
        Check(broken.GetPhase() == UiPhase::Idle, "strategy failure restores the phase");
        broken.Root().SetLayout(nullptr);
        broken.Prepare(200, 200);
        Check(broken.Root().GetLayoutReport().m_Children.size() == 2,
              "restoring default strategy recovers the container");
    }
    UiContext guarded(font);
    guarded.Root().SetLayout(GuardLayout(guarded.Root()));
    guarded.Root().Add<Widget>();
    guarded.Prepare(200, 200);
    Check(Near(guarded.Root().GetBounds().m_Width, 200), "rejected callback mutation preserves normal allocation");
    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                guarded.Root().SetLayout(FlowLayout{});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    Check(rejected, "layout API rejects cross-thread direct access");
}

void CompositionBaselines(FontFace& font)
{
    UiContext ui(font);
    ui.Root().SetLayout(FormLayout{});
    auto& label = ui.Root().Add<Label>("Value");
    auto& number = ui.Root().Add<NumericInput>(7.5);
    number.SetHeight(SizeRule::Fixed(48));
    std::string error;
    Check(ui.SetStyleSheet("numericinput textbox { font-size: 24px; height: 40px; padding: 4px; } "
                           "numericinput toolbutton { font-size: 10px; height: 40px; }",
                           error),
          "composition baseline CSS parses");
    ui.Prepare(400, 200);
    const auto smallLine = font.Shape(label.GetText(), label.GetStyle().m_FontSize);
    const auto& editor = number.GetEditor();
    const auto editorLine = font.Shape(editor.GetText(), editor.GetStyle().m_FontSize, true);
    const auto a = label.GetContentBounds(), b = editor.GetContentBounds();
    const float labelHeight = std::max(smallLine.m_FontSize * 1.35f, smallLine.m_Ascender + smallLine.m_Descender);
    const float labelBaseline = a.m_Y + std::max(0.0f, (a.m_Height - labelHeight) * 0.5f) + smallLine.m_Ascender;
    const float editorBaseline =
        b.m_Y + (b.m_Height - editorLine.m_Ascender - editorLine.m_Descender) * 0.5f + editorLine.m_Ascender;
    Check(Near(labelBaseline, editorBaseline),
          "numeric composition uses its editor baseline rather than a differently styled tool button");
    number.GetEditor().SetVisible(false);
    ui.Prepare(400, 200);
    Check(!ui.Root().GetLayoutReport().m_Children[1].m_Slot.m_Measure.m_Baseline,
          "hidden chosen baseline source does not select an unrelated child");

    UiContext sourceUi(font);
    auto& group = sourceUi.Root().Add<Widget>();
    auto& first = group.Add<Label>("First");
    auto& second = group.Add<Label>("Second");
    group.SetBaselineChild(&second);
    sourceUi.Prepare(200, 200);
    const auto chosen = group.GetLayoutReport().m_Measure.m_Baseline;
    Check(chosen && *chosen > first.GetBounds().m_Height, "composition may explicitly choose a later child baseline");
    Throws([&] { group.SetBaselineChild(&sourceUi.Root()); }, "baseline source must be an immediate child");
    group.RemoveChild(second);
    sourceUi.Prepare(200, 200);
    Check(group.GetLayoutReport().m_Measure.m_Baseline && *group.GetLayoutReport().m_Measure.m_Baseline < *chosen,
          "deleting the chosen child safely restores the default source");
    UiContext clipped(font);
    auto& row = clipped.Root().Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetAlignment(LayoutAlignment::Center);
    row.SetHeight(SizeRule::Fixed(10));
    row.Add<Label>("one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten").SetHeight(SizeRule::Fixed(300));
    clipped.Prepare(300, 200);
    Check(row.GetLayoutReport().m_Measure.m_Baseline && *row.GetLayoutReport().m_Measure.m_Baseline < 0 &&
              !clipped.GetOverflowingContainers().empty(),
          "clipped centered content may export a finite baseline outside its parent bounds");
}

void Styling(FontFace& font)
{
    UiContext ui(font);
    auto& text = ui.Root().Add<Label>("UTF-8: cafe\xCC\x81 / \xE2\x98\x95 / a-long-unbroken-grapheme-safe-caption");
    text.SetWidth(SizeRule::Fill());
    std::string error;
    Check(ui.SetStyleSheet("window { text-wrap: wrap; } label { font-size: 18px; }", error),
          "inherited text-wrap CSS parses");
    ui.Prepare(35, 1000);
    Check(text.IsWordWrap() && text.GetBounds().m_Height > 100,
          "inherited wrap uses grapheme boundaries and long-token fallback");
    const auto before = text.GetBounds().m_Height;
    text.SetStyleProperty(StyleProperty::FontSize, 26.0f);
    ui.Prepare(35, 2000);
    Check(text.GetBounds().m_Height > before, "font change invalidates width-specific text and measurement caches");
    text.SetWordWrap(false);
    ui.Prepare(35, 2000);
    Check(text.GetBounds().m_Height < 50, "explicit wrap override takes precedence over inherited CSS");
    text.ClearStyleProperty(StyleProperty::WordWrap);
    ui.Prepare(35, 2000);
    Check(text.IsWordWrap(), "clearing wrap override restores inherited CSS");
    Check(ui.SetStyleSheet("window { align-items: baseline; }", error), "baseline CSS parses");
    Check(!ui.SetStyleSheet("label { text-wrap: imaginary; }", error), "unsupported wrap mode is rejected atomically");
    Throws([] { GridLayout(std::vector<SizeRule>{}); }, "empty grid columns rejected");
    Throws([] { MeasureConstraints{std::numeric_limits<float>::infinity(), {}}.Validate(); },
           "infinite measure input rejected");
    Throws([] { MeasureResult{{20, 0}, {10, 0}}.Validate(); }, "inverted custom requirements rejected");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        WidthBeforeHeight(font);
        GridAndForm(font);
        Flow(font);
        PercentagesAndReports(font);
        ExtensionsAndCaching(font);
        CompositionBaselines(font);
        Styling(font);
        std::cout << "Layout foundation PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Layout foundation FAIL / " << error.what() << '\n';
        return 1;
    }
}
