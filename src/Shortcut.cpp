#include <EStudioUI/Shortcut.h>

#include <array>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
bool Has(ShortcutModifier value, ShortcutModifier bit)
{
    return (static_cast<unsigned>(value) & static_cast<unsigned>(bit)) != 0;
}
} // namespace

void Shortcut::Validate() const
{
    if (m_Key <= Key::Unknown || m_Key > Key::Meta || (static_cast<unsigned>(m_Modifiers) & ~31u))
        throw std::invalid_argument("A shortcut requires a valid key and modifier combination.");
}

bool Shortcut::Matches(const KeyboardEvent& event) const
{
    if (!event.m_Down || (event.m_Repeat && !m_AutoRepeat) || event.m_Modifiers.m_AltGraph ||
        (m_Physical ? event.m_PhysicalKey : event.m_Key) != m_Key || m_Key == Key::Unknown)
        return false;
    const auto& modifiers = event.m_Modifiers;
    const bool primary = Has(m_Modifiers, ShortcutModifier::Primary);
    const bool control = modifiers.m_PhysicalControl
#ifndef __APPLE__
                         || modifiers.m_Control
#endif
        ;
#ifdef __APPLE__
    const bool requireControl = Has(m_Modifiers, ShortcutModifier::Control);
    const bool requireMeta = Has(m_Modifiers, ShortcutModifier::Meta) || primary;
    const bool meta = modifiers.m_Meta || modifiers.m_Control;
#else
    const bool requireControl = Has(m_Modifiers, ShortcutModifier::Control) || primary;
    const bool requireMeta = Has(m_Modifiers, ShortcutModifier::Meta);
    const bool meta = modifiers.m_Meta;
#endif
    return modifiers.m_Shift == Has(m_Modifiers, ShortcutModifier::Shift) &&
           modifiers.m_Alt == Has(m_Modifiers, ShortcutModifier::Alt) && control == requireControl &&
           meta == requireMeta;
}

std::string Shortcut::ToString() const
{
    Validate();
    std::string result;
    if (Has(m_Modifiers, ShortcutModifier::Primary))
#ifdef __APPLE__
        result += "Cmd+";
#else
        result += "Ctrl+";
#endif
    if (Has(m_Modifiers, ShortcutModifier::Control))
        result += "Ctrl+";
    if (Has(m_Modifiers, ShortcutModifier::Alt))
        result += "Alt+";
    if (Has(m_Modifiers, ShortcutModifier::Shift))
        result += "Shift+";
    if (Has(m_Modifiers, ShortcutModifier::Meta))
        result += "Meta+";
    if (m_Key >= Key::A && m_Key <= Key::Z)
        return result + static_cast<char>('A' + static_cast<int>(m_Key) - static_cast<int>(Key::A));
    if (m_Key >= Key::D0 && m_Key <= Key::D9)
        return result + static_cast<char>('0' + static_cast<int>(m_Key) - static_cast<int>(Key::D0));
    const std::array<Key, 24> functions{Key::F1,  Key::F2,  Key::F3,  Key::F4,  Key::F5,  Key::F6,  Key::F7,  Key::F8,
                                        Key::F9,  Key::F10, Key::F11, Key::F12, Key::F13, Key::F14, Key::F15, Key::F16,
                                        Key::F17, Key::F18, Key::F19, Key::F20, Key::F21, Key::F22, Key::F23, Key::F24};
    for (std::size_t i = 0; i < functions.size(); ++i)
        if (m_Key == functions[i])
            return result + "F" + std::to_string(i + 1);
    switch (m_Key)
    {
    case Key::Enter:
        return result + "Enter";
    case Key::Escape:
        return result + "Escape";
    case Key::Space:
        return result + "Space";
    case Key::Tab:
        return result + "Tab";
    case Key::Left:
        return result + "Left";
    case Key::Right:
        return result + "Right";
    case Key::Up:
        return result + "Up";
    case Key::Down:
        return result + "Down";
    case Key::Home:
        return result + "Home";
    case Key::End:
        return result + "End";
    case Key::PageUp:
        return result + "PageUp";
    case Key::PageDown:
        return result + "PageDown";
    case Key::Insert:
        return result + "Insert";
    case Key::Delete:
        return result + "Delete";
    case Key::Backspace:
        return result + "Backspace";
    case Key::Minus:
        return result + "-";
    case Key::Equal:
        return result + "=";
    case Key::Comma:
        return result + ",";
    case Key::Period:
        return result + ".";
    case Key::Slash:
        return result + "/";
    case Key::Backslash:
        return result + "\\";
    case Key::Semicolon:
        return result + ";";
    case Key::Apostrophe:
        return result + "'";
    case Key::LeftBracket:
        return result + "[";
    case Key::RightBracket:
        return result + "]";
    case Key::Grave:
        return result + "`";
    case Key::Menu:
        return result + "Menu";
    case Key::Shift:
        return result + "Shift";
    case Key::Control:
        return result + "Control";
    case Key::Alt:
        return result + "Alt";
    case Key::Meta:
        return result + "Meta";
    default:
        throw std::invalid_argument("Unsupported shortcut key.");
    }
}
} // namespace EStudioUI
