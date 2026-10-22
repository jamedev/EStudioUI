#pragma once

#include "Types.h"

#include <string>

namespace EStudioUI
{
//! Exact shortcut modifiers; Primary means Control or Command on the current platform.
enum class ShortcutModifier : unsigned
{
    None = 0,
    Shift = 1,
    Control = 2,
    Alt = 4,
    Meta = 8,
    Primary = 16
};

//! Combines shortcut modifier requirements.
constexpr ShortcutModifier operator|(ShortcutModifier left, ShortcutModifier right)
{
    return static_cast<ShortcutModifier>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
}

//! Defines where an associated action participates in shortcut resolution.
enum class ShortcutScope
{
    Widget,
    Subtree,
    Window
};

//! Reports deterministic shortcut resolution without selecting an arbitrary ambiguous command.
enum class ShortcutStatus
{
    Unmatched,
    Triggered,
    Ambiguous
};

//! A logical or physical key gesture with exact modifiers and optional repeat.
struct Shortcut
{
    //! Logical key, or physical key when m_Physical is true.
    Key m_Key = Key::Unknown;

    //! Required modifiers; additional modifiers prevent matching.
    ShortcutModifier m_Modifiers = ShortcutModifier::None;

    //! Matches physical position rather than the layout-dependent logical key.
    bool m_Physical = false;

    //! Allows an auto-repeat event to trigger the action.
    bool m_AutoRepeat = false;

    //! Matches a key-down event, rejecting AltGr and disallowed repeat.
    bool Matches(const KeyboardEvent& event) const;

    //! Rejects unknown keys or unsupported modifier bits.
    void Validate() const;

    //! Returns a readable shortcut hint using platform-primary modifier names.
    std::string ToString() const;

    //! Compares complete gesture contracts.
    bool operator==(const Shortcut&) const = default;
};
} // namespace EStudioUI
