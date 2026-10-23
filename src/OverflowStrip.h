#pragma once

#include <EStudioUI/Widget.h>
#include <EStudioUI/BoxLayout.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <cmath>

namespace EStudioUI::Detail
{
//! Shares scroll requests with the pure strip layout.
struct OverflowStripState
{
    //! Stable direct-child identity to reveal after allocation.
    std::uint64_t m_RevealTarget = 0;
    //! Requested logical horizontal origin.
    float m_Offset = 0;
};

//! Preserves natural captions, an explicit viewport offset and minimal focus reveal.
class OverflowStripLayout final : public LayoutStrategy
{
  public:
    //! Retains owner-side scroll requests without a widget dependency.
    explicit OverflowStripLayout(std::shared_ptr<OverflowStripState> state) : m_State(std::move(state))
    {
    }

    //! Names allocation reports.
    std::string_view GetName() const override
    {
        return "Scrollable tab strip";
    }

    //! Preserves natural commands inside a zero-minimum viewport.
    LayoutPlan Compute(const LayoutContext& context) const override
    {
        auto natural = context;
        natural.m_Constraints.m_Width.reset();
        natural.m_DefiniteWidth = false;
        natural.m_Justification = LayoutJustification::Start;
        auto plan = BoxLayout(Axis::Horizontal).Compute(natural);
        const float width = context.m_Constraints.m_Width.value_or(plan.m_Measure.m_Preferred.m_X);
        float offset = std::clamp(m_State->m_Offset, 0.0f, std::max(0.0f, plan.m_Measure.m_Preferred.m_X - width));
        for (const auto& slot : plan.m_Slots)
            if (context.m_Children[slot.m_Index].m_Target == m_State->m_RevealTarget)
            {
                const auto& bounds = slot.m_Bounds;
                if (bounds.m_X < offset || bounds.m_Width > width)
                    offset = bounds.m_X;
                else if (bounds.m_X + bounds.m_Width > offset + width)
                    offset = bounds.m_X + bounds.m_Width - width;
                break;
            }
        offset = std::clamp(offset, 0.0f, std::max(0.0f, plan.m_Measure.m_Preferred.m_X - width));
        for (auto& slot : plan.m_Slots)
            slot.m_Bounds.m_X -= offset;
        plan.m_Measure.m_Minimum.m_X = 0;
        return plan;
    }

  private:
    //! Observes the strip's shared finite offset and reveal request.
    std::shared_ptr<OverflowStripState> m_State;
};

//! Invalidates its own arrangement when selection moves inside an unchanged viewport.
class OverflowStrip final : public Widget
{
  public:
    //! Creates a natural-height viewport for ordinary child widgets.
    explicit OverflowStrip(std::string type = "tabbar")
        : Widget(std::move(type)), m_State(std::make_shared<OverflowStripState>())
    {
        SetWidth(SizeRule::Fill());
        SetHeight(SizeRule::Content());
        SetLayout(OverflowStripLayout(m_State));
    }

    //! Minimally reveals a direct-child identity on the next arrangement.
    void Reveal(std::uint64_t selected)
    {
        if (m_State->m_RevealTarget == selected)
            return;
        m_State->m_Offset = GetOffset();
        m_State->m_RevealTarget = selected;
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
    }

    //! Scrolls three quarters of the prepared viewport.
    bool ScrollPage(int direction)
    {
        return ScrollBy(static_cast<float>(direction) * std::max(48.0f, GetContentBounds().m_Width * 0.75f));
    }

    //! Normalizes dominant horizontal/vertical wheel units.
    bool ScrollWheel(Point delta)
    {
        const float amount = std::abs(delta.m_X) > std::abs(delta.m_Y) ? delta.m_X : delta.m_Y;
        return ScrollBy(-amount * 48.0f);
    }

  protected:
    //! Retains the direct command identity across later viewport changes.
    void OnDescendantFocused(Widget& descendant) override
    {
        auto* command = &descendant;
        while (command->GetParent() && command->GetParent() != this)
            command = command->GetParent();
        if (command->GetParent() == this && GetContext())
            Reveal(GetContext()->GetHandle(*command).GetTargetKey());
    }

    //! Reuses the same viewport wheel path for ordinary input.
    bool OnScroll(Point delta, Modifiers) override
    {
        return ScrollWheel(delta);
    }

  private:
    //! Reads the actual bounded offset from the prepared allocation.
    float GetOffset() const
    {
        const auto& children = GetPreparedLayoutReport().m_Children;
        return children.empty()
                   ? 0.0f
                   : std::max(0.0f, children.front().m_Rules.m_Margin.m_Left - children.front().m_Slot.m_Bounds.m_X);
    }

    //! Changes only local arrangement, hit geometry and painting.
    bool ScrollBy(float distance)
    {
        const auto& children = GetPreparedLayoutReport().m_Children;
        if (children.empty())
            return false;
        const float offset = GetOffset();
        const auto& last = children.back();
        const float width =
            last.m_Slot.m_Bounds.m_X + last.m_Slot.m_Bounds.m_Width + last.m_Rules.m_Margin.m_Right + offset;
        const float next = std::clamp(offset + distance, 0.0f, std::max(0.0f, width - GetContentBounds().m_Width));
        if (next == offset)
            return false;
        m_State->m_RevealTarget = 0;
        m_State->m_Offset = next;
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
        return true;
    }

    //! Observes the strip's shared finite offset and reveal request.
    std::shared_ptr<OverflowStripState> m_State;
};

} // namespace EStudioUI::Detail
