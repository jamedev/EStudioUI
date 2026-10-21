#include <EStudioUI/Button.h>

namespace EStudioUI
{
Button::Button(std::string text) : Label(std::move(text), "button")
{
}

bool Button::IsFocusable() const
{
    return true;
}

Signal<>& Button::Clicked()
{
    return m_Clicked;
}

void Button::OnPointerDown(const PointerEvent&)
{
    m_PointerDown = true;
    SetState(Pressed, true);
}

void Button::OnPointerMove(const PointerEvent& event)
{
    if (m_PointerDown)
        SetState(Pressed, GetBounds().Contains(event.m_Position) && GetClip().Contains(event.m_Position));
}

void Button::OnPointerUp(const PointerEvent& event)
{
    const bool activate =
        m_PointerDown && IsEnabled() && GetBounds().Contains(event.m_Position) && GetClip().Contains(event.m_Position);
    m_PointerDown = false;
    SetState(Pressed, false);
    if (activate)
        m_Clicked.Emit();
}

void Button::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (key == Key::Space && !down)
    {
        const bool activate = m_SpaceDown && !modifiers.m_Control && !modifiers.m_Alt;
        m_SpaceDown = false;
        SetState(Pressed, m_PointerDown);
        if (activate)
            m_Clicked.Emit();
        return;
    }
    if (modifiers.m_Control || modifiers.m_Alt)
        return;
    if (key == Key::Enter && down && !repeat)
        m_Clicked.Emit();
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
