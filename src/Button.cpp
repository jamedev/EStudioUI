#include <EStudioUI/Button.h>

namespace EStudioUI
{
Button::Button(std::string text, std::string type) : Label(std::move(text), std::move(type))
{
}

bool Button::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Signal<>& Button::Clicked()
{
    VerifyAccess();
    return m_Clicked;
}

void Button::SetAction(std::shared_ptr<Action> action)
{
    VerifyStructuralMutation();
    if (action == m_Action)
        return;
    if (action)
        (void)action->GetSnapshot();
    m_ActionStop.reset();
    m_ActionConnection.reset();
    RemoveAction(m_Action);
    m_Action = std::move(action);
    if (!m_Action)
        return;
    AddAction(m_Action);
    const WidgetRef reference(this);
    m_ActionConnection.emplace(m_Action->Properties().Subscribe(
        [reference](const StateChange<ActionState>&)
        {
            if (auto* live = dynamic_cast<Button*>(reference.Get()))
                live->OnActionChanged();
        }));
    m_ActionStop = std::make_unique<std::stop_callback<std::function<void()>>>(m_Action->GetStopToken(),
                                                                               [reference]
                                                                               {
                                                                                   if (auto* live = reference.Get())
                                                                                       live->SetEnabled(false);
                                                                               });
    OnActionChanged();
}

std::shared_ptr<Action> Button::GetAction() const
{
    VerifyAccess();
    return m_Action;
}

void Button::OnActionChanged()
{
    const auto state = m_Action->GetSnapshot();
    SetText(state->m_Text);
    SetEnabled(state->m_Enabled);
    SetVisible(state->m_Visible);
    SetState(Selected, state->m_Checked);
}

void Button::Activate()
{
    const auto action = m_Action;
    const WidgetRef reference(this);
    m_Clicked.Emit();
    if (reference.Get() && action)
        action->Trigger();
}

bool Button::Trigger()
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (!IsEnabled() || !IsVisible())
        return false;
    Activate();
    return true;
}

void Button::OnPointerDown(const PointerEvent&)
{
    m_PointerDown = true;
    SetState(Pressed, true);
}

void Button::OnPointerMove(const PointerEvent& event)
{
    if (m_PointerDown)
        SetState(Pressed, event.m_InsideClip && ContainsRounded(GetBounds(), GetStyle().m_Radius, event.m_Position));
}

void Button::OnPointerUp(const PointerEvent& event)
{
    const bool activate = m_PointerDown && IsEnabled() && event.m_InsideClip &&
                          ContainsRounded(GetBounds(), GetStyle().m_Radius, event.m_Position);
    m_PointerDown = false;
    SetState(Pressed, false);
    if (activate)
        Activate();
}

void Button::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (key == Key::Space && !down)
    {
        const bool activate = m_SpaceDown && !modifiers.m_Control && !modifiers.m_Alt;
        m_SpaceDown = false;
        SetState(Pressed, m_PointerDown);
        if (activate)
            Activate();
        return;
    }
    if (modifiers.m_Control || modifiers.m_Alt)
        return;
    if (key == Key::Enter && down && !repeat)
        Activate();
    else if (key == Key::Space)
    {
        if (down && !repeat)
        {
            m_SpaceDown = true;
            SetState(Pressed, true);
        }
    }
}

void Button::OnFocus(bool focused)
{
    if (!focused)
        OnCaptureLost();
}

void Button::OnCaptureLost()
{
    m_SpaceDown = false;
    m_PointerDown = false;
    SetState(Pressed, false);
}
} // namespace EStudioUI
