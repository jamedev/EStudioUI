#include <EStudioUI/SplitToolButton.h>
#include <EStudioUI/UiContext.h>

#include <stdexcept>

namespace EStudioUI
{
SplitToolButton::SplitToolButton(std::string text)
    : Widget("splittoolbutton"), m_PendingMenu(std::make_unique<ContextMenu>())
{
    SetLayoutDirection(Axis::Horizontal);
    SetWidth(SizeRule::Content());
    m_PendingMenu->AddClass("split-menu");
    m_Primary = &Add<ToolButton>(std::move(text));
    m_Primary->SetWidth(SizeRule::Content());
    m_Primary->AddClass("split-primary");
    m_Arrow = &Add<ToolButton>("v");
    m_Arrow->AddClass("split-arrow");
    const WidgetRef owner(this);
    m_Primary->Clicked().Connect(
        [owner]
        {
            if (auto* live = dynamic_cast<SplitToolButton*>(owner.Get()))
                live->TriggerDefault();
        });
    m_Arrow->Clicked().Connect(
        [owner]
        {
            if (auto* live = dynamic_cast<SplitToolButton*>(owner.Get()))
                live->OpenMenu();
        });
    ContextMenuRequested().Connect(
        [owner](Point)
        {
            if (auto* live = dynamic_cast<SplitToolButton*>(owner.Get()))
                live->OpenMenu();
        });
}

ContextMenu* SplitToolButton::Menu() const
{
    return m_PendingMenu ? m_PendingMenu.get() : dynamic_cast<ContextMenu*>(m_Menu.Get());
}

ToolButton& SplitToolButton::AddAction(std::string text)
{
    VerifyStructuralMutation();
    auto* menu = Menu();
    if (!menu)
        throw std::logic_error("The tool chooser's context-owned menu was removed.");
    auto& action = menu->AddAction(std::move(text));
    const auto index = m_Actions.size();
    m_Actions.emplace_back(&action);
    const WidgetRef owner(this);
    action.Clicked().Connect(
        [owner, index]
        {
            if (auto* live = dynamic_cast<SplitToolButton*>(owner.Get()))
                live->Choose(index);
        });
    if (m_Default == std::numeric_limits<std::size_t>::max())
        SetDefaultAction(index);
    return action;
}

std::size_t SplitToolButton::GetActionCount() const
{
    VerifyAccess();
    return m_Actions.size();
}

ToolButton& SplitToolButton::AddAction(std::shared_ptr<Action> command)
{
    VerifyStructuralMutation();
    if (!command)
        throw std::invalid_argument("A split-tool action cannot be null.");
    (void)command->GetSnapshot();
    auto& action = AddAction(command->GetSnapshot()->m_Text);
    action.SetAction(command);
    Widget::AddAction(command);
    const auto index = m_Actions.size() - 1;
    const WidgetRef owner(this);
    m_ActionConnections.emplace_back(command->Properties().Subscribe(
        [owner, index](const StateChange<ActionState>&)
        {
            if (auto* live = dynamic_cast<SplitToolButton*>(owner.Get()); live && live->m_Default == index)
                live->SetDefaultAction(index);
        }));
    m_ActionStops.push_back(std::make_unique<std::stop_callback<std::function<void()>>>(
        command->GetStopToken(),
        [owner, index]
        {
            if (auto* live = dynamic_cast<SplitToolButton*>(owner.Get()); live && live->m_Default == index)
                live->m_Primary->SetEnabled(false);
        }));
    if (m_Default == index)
        SetDefaultAction(index);
    return action;
}

ToolButton& SplitToolButton::GetAction(std::size_t index)
{
    VerifyAccess();
    if (index >= m_Actions.size())
        throw std::out_of_range("Tool action index is outside the chooser.");
    auto* action = dynamic_cast<ToolButton*>(m_Actions[index].Get());
    if (!action)
        throw std::logic_error("The tool action was removed from its popup.");
    return *action;
}

void SplitToolButton::SetDefaultAction(std::size_t index)
{
    VerifyAccess();
    auto& action = GetAction(index);
    m_Default = index;
    m_Primary->SetText(action.GetText());
    m_Primary->SetEnabled(action.IsEnabled());
    if (const auto command = action.GetAction())
        m_Primary->SetChecked(command->GetSnapshot()->m_Checked);
    else
        m_Primary->SetChecked(false);
}

std::size_t SplitToolButton::GetDefaultActionIndex() const
{
    VerifyAccess();
    return m_Default;
}

void SplitToolButton::Choose(std::size_t index)
{
    if (index >= m_Actions.size())
        return;
    const auto* action = m_Actions[index].Get();
    if (!IsEnabled() || !action || !action->IsEnabled())
        return;
    const WidgetRef owner(this);
    SetDefaultAction(index);
    if (owner.Get())
        m_Triggered.Emit(index);
}

void SplitToolButton::TriggerDefault()
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    if (!IsEnabled() || m_Default >= m_Actions.size())
        return;
    auto* action = dynamic_cast<ToolButton*>(m_Actions[m_Default].Get());
    if (action && action->IsEnabled())
    {
        if (const auto command = action->GetAction())
        {
            const WidgetRef owner(this);
            Choose(m_Default);
            if (owner.Get())
                command->Trigger();
        }
        else
            action->Clicked().Emit();
    }
}

void SplitToolButton::OpenMenu()
{
    VerifyStructuralMutation();
    auto* context = GetContext();
    if (!context || !IsEnabled() || m_Actions.empty())
        return;
    if (m_PendingMenu)
    {
        auto& popup = context->AddOwnedPopup(std::move(m_PendingMenu), *this);
        m_Menu = WidgetRef(&popup);
    }
    if (auto* menu = Menu())
    {
        const auto bounds = GetBounds();
        menu->Open(context->ToWindowPoint(*this, {bounds.m_X, bounds.m_Y + bounds.m_Height}));
    }
}

bool SplitToolButton::IsMenuOpen() const
{
    VerifyAccess();
    const auto* menu = Menu();
    return menu && menu->IsOpen();
}

ToolButton& SplitToolButton::GetPrimaryButton()
{
    VerifyAccess();
    return *m_Primary;
}

Signal<std::size_t>& SplitToolButton::Triggered()
{
    VerifyAccess();
    return m_Triggered;
}

bool SplitToolButton::OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (!down || repeat || modifiers.m_Control || (key != Key::Down && key != Key::Menu))
        return false;
    OpenMenu();
    return true;
}
} // namespace EStudioUI
