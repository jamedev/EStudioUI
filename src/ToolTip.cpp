#include <EStudioUI/ToolTip.h>

namespace EStudioUI
{
ToolTip::ToolTip(std::string text) : Label(std::move(text), "tooltip")
{
    SetWordWrap(true);
}

SemanticInfo ToolTip::DescribeSemantics() const
{
    auto info = Label::DescribeSemantics();
    info.m_Role = SemanticRole::ToolTip;
    return info;
}
} // namespace EStudioUI
