#include <EStudioUI/SemanticTypes.h>
#include <EStudioUI/Widget.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/NumericInput.h>

#include <cmath>

namespace EStudioUI
{
SemanticAction operator|(SemanticAction left, SemanticAction right) noexcept
{
    return static_cast<SemanticAction>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
}

bool HasSemanticAction(SemanticAction actions, SemanticAction action) noexcept
{
    return action != SemanticAction::None &&
           (static_cast<unsigned>(actions) & static_cast<unsigned>(action)) == static_cast<unsigned>(action);
}

SemanticInfo Widget::DescribeSemantics() const
{
    VerifyAccess();
    SemanticInfo info;
    const auto& type = m_Node.m_Type;
    if (type == "root")
        info.m_Role = SemanticRole::Window;
    else if (type == "toolbar")
        info.m_Role = SemanticRole::ToolBar;
    else if (type == "tabwidget")
        info.m_Role = SemanticRole::TabList;
    else if (type == "listwidget")
        info.m_Role = SemanticRole::List;
    else if (type == "treewidget")
        info.m_Role = SemanticRole::Tree;
    else if (type == "tablewidget")
        info.m_Role = SemanticRole::Table;
    else if (type == "contextmenu")
        info.m_Role = SemanticRole::Menu;
    else if (type == "canvas" || type == "sketch")
        info.m_Role = SemanticRole::Canvas;
    if (const auto* label = dynamic_cast<const Label*>(this))
    {
        info.m_Role = SemanticRole::Text;
        info.m_Name = label->GetText();
    }
    if (const auto* button = dynamic_cast<const Button*>(this))
    {
        info.m_Role = SemanticRole::Button;
        info.m_Actions = SemanticAction::Invoke;
        if (const auto* tool = dynamic_cast<const ToolButton*>(button); tool && tool->IsCheckable())
        {
            info.m_Role = SemanticRole::CheckBox;
            info.m_Checked = tool->IsChecked();
            info.m_Actions = SemanticAction::Toggle;
        }
        else if (const auto action = button->GetAction(); action && action->GetSnapshot()->m_Checkable)
        {
            info.m_Role = SemanticRole::CheckBox;
            info.m_Checked = action->GetSnapshot()->m_Checked;
            info.m_Actions = SemanticAction::Toggle;
        }
    }
    if (const auto* text = dynamic_cast<const TextBox*>(this))
    {
        info.m_Role = SemanticRole::TextBox;
        info.m_Value = text->GetText();
        info.m_ReadOnly = text->IsReadOnly();
        if (!info.m_ReadOnly)
            info.m_Actions = SemanticAction::SetText;
    }
    if (const auto* slider = dynamic_cast<const Slider*>(this))
    {
        info.m_Role = SemanticRole::Slider;
        info.m_Number = slider->GetValue();
        info.m_Minimum = slider->GetMinimum();
        info.m_Maximum = slider->GetMaximum();
        info.m_Step = slider->GetStep();
        info.m_Actions = SemanticAction::SetNumber;
    }
    if (const auto* number = dynamic_cast<const NumericInput*>(this))
    {
        info.m_Role = SemanticRole::SpinBox;
        info.m_Number = number->GetValue();
        info.m_Minimum = number->GetMinimum();
        info.m_Maximum = number->GetMaximum();
        info.m_Step = number->GetStep();
        info.m_ReadOnly = number->GetEditor().IsReadOnly();
        if (!info.m_ReadOnly)
            info.m_Actions = SemanticAction::SetNumber;
    }
    return info;
}

SemanticStatus Widget::OnSemanticAction(const SemanticCommand& command)
{
    if (command.m_Action == SemanticAction::Invoke || command.m_Action == SemanticAction::Toggle)
    {
        if (auto* button = dynamic_cast<Button*>(this))
            return button->Trigger() ? SemanticStatus::Succeeded : SemanticStatus::Disabled;
    }
    else if (command.m_Action == SemanticAction::SetText)
    {
        if (auto* text = dynamic_cast<TextBox*>(this))
        {
            if (!text->CanSetText(command.m_Text))
                return SemanticStatus::InvalidValue;
            text->SetText(command.m_Text);
            return SemanticStatus::Succeeded;
        }
    }
    else if (command.m_Action == SemanticAction::SetNumber)
    {
        if (!std::isfinite(command.m_Number))
            return SemanticStatus::InvalidValue;
        if (auto* slider = dynamic_cast<Slider*>(this))
        {
            if (command.m_Number < slider->GetMinimum() || command.m_Number > slider->GetMaximum())
                return SemanticStatus::InvalidValue;
            slider->SetValue(command.m_Number);
            return SemanticStatus::Succeeded;
        }
        if (auto* number = dynamic_cast<NumericInput*>(this))
        {
            if (command.m_Number < number->GetMinimum() || command.m_Number > number->GetMaximum())
                return SemanticStatus::InvalidValue;
            number->SetValue(command.m_Number);
            return SemanticStatus::Succeeded;
        }
    }
    return SemanticStatus::Unsupported;
}

std::size_t Widget::GetSemanticChildCount(SemanticKey) const
{
    return 0;
}

std::optional<SemanticKey> Widget::GetSemanticChildAt(SemanticKey, std::size_t) const
{
    return {};
}

std::optional<SemanticVirtualNode> Widget::DescribeSemanticChild(SemanticKey) const
{
    return {};
}

std::optional<std::size_t> Widget::GetSemanticChildIndex(SemanticKey) const
{
    return {};
}

SemanticStatus Widget::OnSemanticChildAction(SemanticKey, const SemanticCommand&)
{
    return SemanticStatus::Unsupported;
}

std::optional<SemanticKey> Widget::SemanticHitTest(Point) const
{
    return {};
}

std::size_t Widget::GetSemanticSelectionCount() const
{
    return 0;
}

std::vector<SemanticKey> Widget::GetSemanticSelection(std::size_t, std::size_t) const
{
    return {};
}

std::vector<WidgetRef> Widget::GetSemanticSelectedWidgets(std::size_t, std::size_t) const
{
    return {};
}

std::optional<SemanticKey> Widget::GetSemanticGridItem(std::size_t, std::size_t) const
{
    return {};
}

std::optional<SemanticKey> Widget::GetSemanticFocus() const
{
    return {};
}

} // namespace EStudioUI
