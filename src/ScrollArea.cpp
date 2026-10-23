#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
class ViewportLayout final : public LayoutStrategy
{
  public:
    explicit ViewportLayout(std::function<Point(Point)> gutters) : m_Gutters(std::move(gutters))
    {
    }

    std::string_view GetName() const override
    {
        return "Scroll viewport";
    }

    LayoutPlan Compute(const LayoutContext& c) const override
    {
        LayoutPlan p;
        p.m_Measure.m_Preferred = {320, 220};
        if (c.m_Children.empty())
            return p;
        const Point available{c.m_Constraints.m_Width.value_or(320), c.m_Constraints.m_Height.value_or(220)};
        const auto gutter = m_Gutters(available);
        const float width = std::max(0.0f, available.m_X - gutter.m_X),
                    height = std::max(0.0f, available.m_Y - gutter.m_Y);
        auto measured = c.m_Measure(0, {width, height}, true, true);
        p.m_Slots.push_back({0,
                             {0, 0, std::max(width, measured.m_Minimum.m_X), std::max(height, measured.m_Minimum.m_Y)},
                             measured,
                             true,
                             true});
        return p;
    }

  private:
    std::function<Point(Point)> m_Gutters;
};

class ContentLayout final : public LayoutStrategy
{
  public:
    explicit ContentLayout(std::function<Point()> offset) : m_Offset(std::move(offset))
    {
    }

    std::string_view GetName() const override
    {
        return "Scrollable content";
    }

    LayoutPlan Compute(const LayoutContext& c) const override
    {
        LayoutPlan p;
        if (c.m_Children.empty())
            return p;
        const auto natural = c.m_Measure(0, {}, false, false);
        const auto& rules = c.m_Children[0];
        const float viewportWidth = c.m_Constraints.m_Width.value_or(320),
                    viewportHeight = c.m_Constraints.m_Height.value_or(220);
        float width = rules.m_Width.m_Unit == Length::Unit::Pixels ? rules.m_Width.m_Value : viewportWidth;
        width = std::clamp(std::max(width, natural.m_Minimum.m_X), rules.m_Minimum.m_X, rules.m_Maximum.m_X);
        auto measured = c.m_Measure(0, {width, {}}, true, false);
        float height =
            std::clamp(std::max(viewportHeight, measured.m_Preferred.m_Y), measured.m_Minimum.m_Y, rules.m_Maximum.m_Y);
        if (rules.m_Height.m_Unit == Length::Unit::Pixels)
            height = std::clamp(rules.m_Height.m_Value, measured.m_Minimum.m_Y, rules.m_Maximum.m_Y);
        measured = c.m_Measure(0, {width, height}, true, true);
        auto offset = m_Offset();
        offset.m_X = std::clamp(offset.m_X, 0.0f, std::max(0.0f, width - viewportWidth));
        offset.m_Y = std::clamp(offset.m_Y, 0.0f, std::max(0.0f, height - viewportHeight));
        p.m_Slots.push_back({0, {-offset.m_X, -offset.m_Y, width, height}, measured, true, true});
        p.m_Measure.m_Preferred = {std::min(320.0f, width), std::min(220.0f, height)};
        return p;
    }

  private:
    std::function<Point()> m_Offset;
};
} // namespace

ScrollArea::ScrollArea() : ScrollWidget("scrollarea")
{
    auto& viewport = Add<Widget>("scroll-viewport");
    m_Viewport = &viewport;
    viewport.SetWidth(SizeRule::Fill());
    viewport.SetHeight(SizeRule::Fill());
    m_Content = &viewport.Add<Widget>("scroll-content");
    m_Content->SetWidth(SizeRule::Fill());
    m_Content->SetHeight(SizeRule::Content());
    viewport.SetLayout(ContentLayout([this] { return GetStoredScrollOffset(); }));
    SetLayout(ViewportLayout(
        [this](Point available)
        {
            const auto& vertical = GetPartStyle("scroll-track", Vertical);
            const auto& horizontal = GetPartStyle("scroll-track", Horizontal);
            return Point{vertical.m_Displayed ? std::clamp(vertical.m_Width.Resolve(available.m_X, 12),
                                                           vertical.m_MinWidth, vertical.m_MaxWidth)
                                              : 0,
                         horizontal.m_Displayed ? std::clamp(horizontal.m_Height.Resolve(available.m_Y, 12),
                                                             horizontal.m_MinHeight, horizontal.m_MaxHeight)
                                                : 0};
        }));
}

Widget& ScrollArea::Content() const
{
    VerifyAccess();
    return *m_Content;
}

Point ScrollArea::Measure(FontFace&) const
{
    return {320, 220};
}

Point ScrollArea::GetVirtualSize() const
{
    const auto b = m_Content->GetBounds();
    return {b.m_Width, b.m_Height};
}

void ScrollArea::Paint(Renderer& renderer, FontFace&, double, float opacity) const
{
    renderer.DrawBox(GetBounds(), GetStyle(), GetClip(), opacity);
    PaintScrollBars(renderer, opacity);
}

void ScrollArea::SetScrollOffset(Point offset)
{
    const auto before = GetStoredScrollOffset();
    ScrollWidget::SetScrollOffset(offset);
    if (GetStoredScrollOffset() != before)
        InvalidateChild(*m_Viewport, Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
}

void ScrollArea::Reveal(Widget& descendant)
{
    VerifyStructuralMutation();
    descendant.VerifyAccess();
    auto* node = &descendant;
    for (; node && node != m_Content; node = node->GetParent())
    {
    }
    if (!node)
        throw std::invalid_argument("ScrollArea::Reveal requires a content descendant.");
    if (!GetContext())
        return;
    const auto b = descendant.GetBounds(), v = GetViewport();
    const auto offset = GetScrollOffset();
    Point lo{1000000, 1000000}, hi{-1000000, -1000000};
    for (const Point corner : {Point{b.m_X, b.m_Y}, Point{b.m_X + b.m_Width, b.m_Y}, Point{b.m_X, b.m_Y + b.m_Height},
                               Point{b.m_X + b.m_Width, b.m_Y + b.m_Height}})
    {
        const auto point = GetContext()->ToLayoutPoint(*this, GetContext()->ToWindowPoint(descendant, corner));
        lo.m_X = std::min(lo.m_X, point.m_X);
        lo.m_Y = std::min(lo.m_Y, point.m_Y);
        hi.m_X = std::max(hi.m_X, point.m_X);
        hi.m_Y = std::max(hi.m_Y, point.m_Y);
    }
    EnsureVisible({lo.m_X - v.m_X + offset.m_X, lo.m_Y - v.m_Y + offset.m_Y, hi.m_X - lo.m_X, hi.m_Y - lo.m_Y});
}

void ScrollArea::OnDescendantFocused(Widget& descendant)
{
    Reveal(descendant);
}
} // namespace EStudioUI
