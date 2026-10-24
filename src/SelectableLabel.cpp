#include <EStudioUI/SelectableLabel.h>

namespace EStudioUI
{
SelectableLabel::SelectableLabel(std::string text) : TextBox(std::move(text))
{
    AddClass("selectable-label");
    SetReadOnly(true);
}
} // namespace EStudioUI
