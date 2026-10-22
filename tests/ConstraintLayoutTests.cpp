#include <EStudioUI/ConstraintLayout.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/UiContext.h>

#include <chrono>
#include <algorithm>
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

bool Near(float left, float right)
{
    return std::abs(left - right) < 0.02f;
}

bool Has(const UiContext& ui, LayoutIssue issue, const std::string& name = {})
{
    for (const auto& diagnostic : ui.GetLayoutDiagnostics())
        if (diagnostic.m_Issue == issue && diagnostic.m_Message.find(name) != std::string::npos)
            return true;
    return false;
}

void AnchorsAndResize(FontFace& font)
{
    UiContext ui(font);
    auto& side = ui.Root().Add<Widget>();
    side.SetWidth(SizeRule::Fixed(160));
    side.SetHeight(SizeRule::Fixed(40));
    auto& body = ui.Root().Add<Widget>();
    body.SetWidth(SizeRule::Fill(1, 80));
    body.SetHeight(SizeRule::Fill(1, 30));
    const ConstraintItem a(side), b(body), parent = ConstraintItem::Parent();
    ConstraintLayout layout;
    layout.Add(a.Left() == parent.Left() + 8).Add(a.Top() == 8);
    layout.Add(b.Left() == a.Right() + 12).Add(b.Right() == parent.Right() - 8);
    layout.Add(b.Top() == 8).Add(b.Bottom() == parent.Bottom() - 8);
    ui.Root().SetLayout(std::move(layout));
    ui.Prepare(800, 400);
    Check(Near(side.GetBounds().m_X, 8) && Near(side.GetBounds().m_Width, 160), "fixed sidebar anchors");
    Check(Near(body.GetBounds().m_X, 180) && Near(body.GetBounds().m_Width, 612) &&
              Near(body.GetBounds().m_Height, 384),
          "right/bottom anchors consume available space");
    const auto before = ui.GetStats();
    const auto& strategy = dynamic_cast<const ConstraintLayout&>(*ui.Root().GetLayout());
    const auto initial = strategy.GetStats();
    for (int i = 0; i < 120; ++i)
    {
        const float width = 800 + static_cast<float>(i);
        ui.Prepare(width, 400 + static_cast<float>(i % 7));
        Check(Near(body.GetBounds().m_Width, width - 188), "every intermediate width is arranged immediately");
    }
    const auto after = ui.GetStats();
    const auto final = strategy.GetStats();
    Check(after.m_StyleResolutions == before.m_StyleResolutions && after.m_Measurements == before.m_Measurements,
          "resizes preserve style and intrinsic caches");
    Check(final.m_Updates > initial.m_Updates && final.m_Builds == initial.m_Builds,
          "feasible resize updates tableaux without rebuilding unchanged rules");
    Check(!Has(ui, LayoutIssue::ConstraintConflict), "feasible anchors produce no conflict");
    Check(ui.Root().GetLayoutReport().Explain().find("Constraints") != std::string::npos,
          "ordinary owned layout reports explain constraint allocations");
    std::cout << "120 resize samples: builds " << final.m_Builds << ", incremental updates " << final.m_Updates
              << ", solves " << final.m_Solves << ", cache hits " << final.m_CacheHits << '\n';
}

void PrioritiesAndConflicts(FontFace& font)
{
    UiContext ui(font);
    auto& child = ui.Root().Add<Widget>();
    child.SetWidth(SizeRule::Fill(1, 40, 400));
    child.SetHeight(SizeRule::Fixed(30));
    const ConstraintItem item(child), parent = ConstraintItem::Parent();
    ConstraintLayout layout;
    layout.Add(item.Width() == 200, ConstraintStrength::Strong, "preferred width")
        .Add(item.Width() == 300, ConstraintStrength::Medium, "secondary width")
        .Add(item.CenterX() == parent.CenterX());
    ui.Root().SetLayout(layout);
    ui.Prepare(700, 100);
    Check(Near(child.GetBounds().m_Width, 200) && Near(child.GetBounds().m_X, 250), "weighted strengths and centers");
    Check(Has(ui, LayoutIssue::ConstraintRelaxed, "secondary width"), "soft relaxation is named");
    const auto owned = ui.Root().GetLayoutReport();
    Check(owned.m_Constraints.size() == 3 && owned.m_Constraints[1].m_Status == "relaxed" &&
              owned.Explain().find("secondary width") != std::string::npos,
          "owned report includes named expressions, strength, status and residual");
    layout = ConstraintLayout{};
    layout.Add(item.Width() == 100, ConstraintStrength::Required, "first width")
        .Add(item.Width() == 120, ConstraintStrength::Required, "conflicting width")
        .Add(item.Width() >= 150, ConstraintStrength::Required, "conflicting minimum");
    ui.Root().SetLayout(layout);
    ui.Prepare(700, 100);
    Check(Near(child.GetBounds().m_Width, 100), "earlier required rules win deterministically");
    Check(Has(ui, LayoutIssue::ConstraintConflict, "conflicting width") &&
              Has(ui, LayoutIssue::ConstraintConflict, "conflicting minimum"),
          "all rejected required rules named");
    child.SetWidth(SizeRule::Fixed(75));
    ui.Prepare(700, 100);
    Check(Near(child.GetBounds().m_Width, 75) && Has(ui, LayoutIssue::ConstraintConflict, "first width"),
          "explicit fixed contract precedes user constraints");
    child.SetWidth(SizeRule::Fill(1, 40));
    layout = ConstraintLayout{};
    layout.Add(item.Left() == 0).Add(item.Right() == parent.Right(), ConstraintStrength::Required, "fit right");
    ui.Root().SetLayout(layout);
    ui.Prepare(20, 100);
    Check(child.GetBounds().m_Width >= 40 && Has(ui, LayoutIssue::ConstraintConflict, "fit right") &&
              Has(ui, LayoutIssue::Overflow),
          "infeasible viewport preserves hard minimum with overflow");
    ui.Prepare(500, 100);
    Check(Near(child.GetBounds().m_Width, 500) && !Has(ui, LayoutIssue::ConstraintConflict),
          "previously rejected required rule recovers when resize becomes feasible");
    layout = ConstraintLayout{};
    layout.Add(parent.Width() == 999, ConstraintStrength::Required, "impossible parent width");
    ui.Root().SetLayout(std::move(layout));
    ui.Prepare(500, 100);
    Check(Has(ui, LayoutIssue::ConstraintConflict, "impossible parent width") &&
              Near(ui.Root().GetLayoutReport().m_ContentSize.m_X, 500),
          "speculative viewport edit cannot displace the real viewport");

    layout = ConstraintLayout{};
    layout.Add(item.Left() == 0).Add(item.Right() == parent.Right());
    for (int i = 0; i < 110; ++i)
        layout.Add(item.Width() == 600, ConstraintStrength::Strong);
    ui.Root().SetLayout(std::move(layout));
    ui.Prepare(100, 100);
    Check(Near(child.GetBounds().m_Width, 100) && !Has(ui, LayoutIssue::ConstraintConflict),
          "aggregate soft objective cannot move a hard viewport target");
    Check(dynamic_cast<const ConstraintLayout&>(*ui.Root().GetLayout()).GetStats().m_Builds >= 2,
          "competing soft objectives exercise verified exact recovery");
}

void Lifetimes(FontFace& font)
{
    UiContext ui(font);
    auto& first = ui.Root().Add<Widget>();
    auto& second = ui.Root().Add<Widget>();
    first.SetWidth(SizeRule::Fixed(100));
    second.SetWidth(SizeRule::Fixed(50));
    const ConstraintItem a(first), b(second);
    ConstraintLayout layout;
    layout.Add(a.Left() == 20).Add(b.Left() == a.Right() + 10, ConstraintStrength::Required, "linked child");
    ui.Root().SetLayout(std::move(layout));
    ui.Prepare(500, 200);
    Check(Near(second.GetBounds().m_X, 130), "initial linked position");
    first.SetVisible(false);
    ui.Prepare(500, 200);
    Check(Has(ui, LayoutIssue::ConstraintInactive, "linked child"),
          "hidden anchor deactivates rule without retargeting");
    first.SetVisible(true);
    ui.Prepare(500, 200);
    Check(Near(second.GetBounds().m_X, 130) && !Has(ui, LayoutIssue::ConstraintInactive),
          "show restores stable anchors");
    ui.Root().RemoveChild(first);
    auto& replacement = ui.Root().Add<Widget>();
    replacement.SetWidth(SizeRule::Fixed(200));
    ui.Prepare(500, 200);
    Check(Has(ui, LayoutIssue::ConstraintInactive, "linked child") && !Near(second.GetBounds().m_X, 230),
          "deleted anchor cannot bind a replacement widget");
}

void WrappingAndRatios(FontFace& font)
{
    UiContext ui(font);
    auto& paragraph = ui.Root().Add<Label>("The constraint layout measures wrapped paragraphs at their solved width "
                                           "before positioning the following control, including every resize sample.");
    paragraph.SetWordWrap(true);
    paragraph.SetWidth(SizeRule::Fill(1, 10));
    paragraph.SetHeight(SizeRule::Content());
    auto& next = ui.Root().Add<Widget>();
    next.SetWidth(SizeRule::Fixed(40));
    next.SetHeight(SizeRule::Fixed(20));
    const ConstraintItem text(paragraph), button(next), parent = ConstraintItem::Parent();
    ConstraintLayout layout;
    layout.Add(text.Left() == 0).Add(text.Right() == parent.Right()).Add(text.Top() == 0);
    layout.Add(button.Top() == text.Bottom() + 7);
    ui.Root().SetLayout(std::move(layout));
    ui.Prepare(500, 400);
    const float wide = paragraph.GetBounds().m_Height;
    ui.Prepare(160, 400);
    Check(paragraph.GetBounds().m_Height > wide && Near(next.GetBounds().m_Y, paragraph.GetBounds().m_Height + 7),
          "height-for-width resolves once before vertical anchor allocation");
    UiContext ratioUi(font);
    auto& tile = ratioUi.Root().Add<Widget>();
    tile.SetWidth(SizeRule::Fill(1, 0));
    tile.SetHeight(SizeRule::Content());
    const ConstraintItem square(tile);
    ConstraintLayout ratio;
    ratio.Add(square.Width() == 120).Add(square.Height() == square.Width() / 2);
    ratioUi.Root().SetLayout(std::move(ratio));
    ratioUi.Prepare(400, 200);
    Check(Near(tile.GetBounds().m_Width, 120) && Near(tile.GetBounds().m_Height, 60), "linear aspect ratio");
    ConstraintLayout scaled;
    scaled.Add(square.Width() * 1e-12 == 120e-12);
    scaled.Add(square.Height() == square.Width() / 2);
    ratioUi.Root().SetLayout(std::move(scaled));
    ratioUi.Prepare(400, 200);
    Check(Near(tile.GetBounds().m_Width, 120) && Near(tile.GetBounds().m_Height, 60),
          "equivalent tiny scalar coefficients normalize before solving");
}

void Validation()
{
    for (const auto value : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1e20})
    {
        bool failed = false;
        try
        {
            ConstraintExpression expression(value);
        }
        catch (const std::invalid_argument&)
        {
            failed = true;
        }
        Check(failed, "invalid coefficient rejected before reaching solver");
    }
    bool failed = false;
    try
    {
        const auto bad = ConstraintItem::Parent().Width() / 0;
        (void)bad;
    }
    catch (const std::invalid_argument&)
    {
        failed = true;
    }
    Check(failed, "division by zero diagnosed");
}

class WrappedCell final : public Widget
{
  protected:
    MeasureResult MeasureConstrained(FontFace&, const MeasureConstraints& constraints) const override
    {
        const float width = std::max(1.0f, constraints.m_Width.value_or(200));
        return {{4, 20}, {200, std::ceil(200 / width) * 20}, {}, true};
    }
};

void ResizeWork(FontFace& font, bool wrapped)
{
    UiContext ui(font);
    ConstraintLayout layout;
    std::vector<ConstraintItem> items;
    for (int i = 0; i < 100; ++i)
    {
        Widget& child = wrapped ? static_cast<Widget&>(ui.Root().Add<WrappedCell>()) : ui.Root().Add<Widget>();
        child.SetWidth(SizeRule::Fill(1, 4));
        child.SetHeight(wrapped ? SizeRule::Content() : SizeRule::Fixed(20));
        items.emplace_back(child);
        layout.Add(items.back().Top() == 0);
        layout.Add(items.back().Left() == (i == 0 ? ConstraintExpression(0) : items[items.size() - 2].Right() + 2));
        if (i > 0)
            layout.Add(items.back().Width() == items.front().Width());
    }
    layout.Add(items.back().Right() == ConstraintItem::Parent().Right());
    ui.Root().SetLayout(std::move(layout));
    ui.Prepare(1200, 600);
    const auto& solver = dynamic_cast<const ConstraintLayout&>(*ui.Root().GetLayout());
    const auto before = solver.GetStats();
    std::vector<double> milliseconds;
    for (int frame = 1; frame <= 64; ++frame)
    {
        const auto start = std::chrono::steady_clock::now();
        ui.Prepare(1200 + static_cast<float>(frame * 3), 600);
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        const auto& child = *ui.Root().GetChildren().back();
        Check(Near(child.GetBounds().m_X + child.GetBounds().m_Width, 1200 + static_cast<float>(frame * 3)),
              "100-child chain follows every intermediate width");
    }
    const auto after = solver.GetStats();
    Check(after.m_Builds == before.m_Builds && after.m_Updates > before.m_Updates,
          "100-child chain retains its solver topology during resizing");
    std::sort(milliseconds.begin(), milliseconds.end());
    std::cout << "100 " << (wrapped ? "height-for-width" : "fixed-height")
              << " children / 300 user rules / 64 resize samples: CPU Prepare median " << milliseconds[32]
              << " ms, p95 " << milliseconds[60] << " ms, max " << milliseconds.back() << " ms; tableau rebuild delta "
              << after.m_Builds - before.m_Builds << '\n';
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        AnchorsAndResize(font);
        PrioritiesAndConflicts(font);
        Lifetimes(font);
        WrappingAndRatios(font);
        Validation();
        ResizeWork(font, false);
        ResizeWork(font, true);
        std::cout << "Constraint layout: " << s_Checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Constraint layout failed: " << error.what() << '\n';
        return 1;
    }
}
