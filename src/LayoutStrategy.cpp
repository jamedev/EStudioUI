#include <EStudioUI/LayoutStrategy.h>

#include <sstream>

namespace EStudioUI
{
namespace
{
std::string RuleName(Length length)
{
    std::ostringstream out;
    switch (length.m_Unit)
    {
    case Length::Unit::Auto:
        return "Content";
    case Length::Unit::Pixels:
        out << "Fixed(" << length.m_Value << ")";
        break;
    case Length::Unit::Fraction:
        out << "Fill(" << length.m_Value << ")";
        break;
    case Length::Unit::Percent:
        out << length.m_Value << '%';
        break;
    }
    return out.str();
}
} // namespace

std::string LayoutReport::Explain() const
{
    std::ostringstream out;
    out << m_Strategy << " #" << m_ContainerId << ": content " << m_ContentSize.m_X << " x " << m_ContentSize.m_Y
        << ", preferred " << m_Measure.m_Preferred.m_X << " x " << m_Measure.m_Preferred.m_Y << '\n';
    out << "  Content hook: preferred " << m_ContentMeasure.m_Preferred.m_X << " x " << m_ContentMeasure.m_Preferred.m_Y
        << '\n';
    for (const auto& track : m_Tracks)
    {
        const Length rule =
            track.m_Rule.m_Mode == SizeRule::Mode::Fixed  ? Length{Length::Unit::Pixels, track.m_Rule.m_Value}
            : track.m_Rule.m_Mode == SizeRule::Mode::Fill ? Length{Length::Unit::Fraction, track.m_Rule.m_Value}
                                                          : Length{};
        out << "  " << (track.m_Axis == Axis::Horizontal ? "Column " : "Row ") << track.m_Index << ' ' << RuleName(rule)
            << ": preferred " << track.m_Preferred << ", minimum " << track.m_Minimum << ", maximum "
            << track.m_Rule.m_Maximum << ", allocated " << track.m_Allocated << '\n';
    }
    for (const auto& child : m_Children)
    {
        const auto& bounds = child.m_Slot.m_Bounds;
        out << "  #" << child.m_ChildId << " [" << child.m_Child << "] " << RuleName(child.m_Rules.m_Width) << " / "
            << RuleName(child.m_Rules.m_Height) << ": preferred " << child.m_Slot.m_Measure.m_Preferred.m_X << " x "
            << child.m_Slot.m_Measure.m_Preferred.m_Y << ", minimum " << child.m_Rules.m_Minimum.m_X << " x "
            << child.m_Rules.m_Minimum.m_Y << ", maximum " << child.m_Rules.m_Maximum.m_X << " x "
            << child.m_Rules.m_Maximum.m_Y << ", allocated (" << bounds.m_X << ", " << bounds.m_Y << ") "
            << bounds.m_Width << " x " << bounds.m_Height << '\n';
    }
    for (const auto& issue : m_Diagnostics)
        out << "  " << (issue.m_Axis == Axis::Horizontal ? "width" : "height") << " #" << issue.m_ChildId << ": "
            << issue.m_Message << " (available " << issue.m_Available << ", required " << issue.m_Required << ")\n";
    return out.str();
}
} // namespace EStudioUI
