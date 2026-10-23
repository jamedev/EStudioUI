#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace EStudioUI
{
//! Portable standard pointer shapes used by widgets and native services.
enum class CursorShape
{
    Arrow,
    Text,
    Crosshair,
    Hand,
    ResizeHorizontal,
    ResizeVertical,
    ResizeAll,
    NotAllowed
};

//! A position or displacement in logical pixels.
struct Point
{
    //! Horizontal coordinate.
    float m_X = 0;

    //! Vertical coordinate.
    float m_Y = 0;

    //! Compares logical coordinates exactly.
    bool operator==(const Point&) const = default;
};

//! An axis-aligned rectangle in logical pixels.
struct Rect
{
    //! Left edge.
    float m_X = 0;

    //! Top edge.
    float m_Y = 0;

    //! Nonnegative width.
    float m_Width = 0;

    //! Nonnegative height.
    float m_Height = 0;

    //! Compares rectangle components exactly.
    bool operator==(const Rect&) const = default;

    //! Tests the half-open rectangle, excluding empty regions.
    bool Contains(Point point) const
    {
        return point.m_X >= m_X && point.m_X < m_X + m_Width && point.m_Y >= m_Y && point.m_Y < m_Y + m_Height;
    }

    //! Computes a nonnegative intersection suitable for nested clipping.
    Rect Intersect(const Rect& other) const
    {
        const float x = std::max(m_X, other.m_X);
        const float y = std::max(m_Y, other.m_Y);
        return {x, y, std::max(0.0f, std::min(m_X + m_Width, other.m_X + other.m_Width) - x),
                std::max(0.0f, std::min(m_Y + m_Height, other.m_Y + other.m_Height) - y)};
    }
};

//! An RGBA color using CSS component values normalized to [0,1].
struct Color
{
    //! Red component.
    float m_R = 0;

    //! Green component.
    float m_G = 0;

    //! Blue component.
    float m_B = 0;

    //! Alpha component.
    float m_A = 1;

    //! Compares normalized color components exactly.
    bool operator==(const Color&) const = default;
};

//! Logical keyboard commands used by controls independently of GLFW.
enum class Key
{
    Unknown,
    Tab,
    Enter,
    Space,
    Left,
    Right,
    Up,
    Down,
    PageUp,
    PageDown,
    Home,
    End,
    Backspace,
    Delete,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Escape,
    F5,
    F6,
    F2,
    Menu,
    Insert,
    D0,
    D1,
    D2,
    D3,
    D4,
    D5,
    D6,
    D7,
    D8,
    D9,
    F1,
    F3,
    F4,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
    Minus,
    Equal,
    Comma,
    Period,
    Slash,
    Backslash,
    Semicolon,
    Apostrophe,
    LeftBracket,
    RightBracket,
    Grave,
    Shift,
    Control,
    Alt,
    Meta
};

//! Keyboard modifiers normalized by the platform backend.
struct Modifiers
{
    //! Shift modifies selection and navigation direction.
    bool m_Shift = false;

    //! Control, or Command on macOS, selects editing shortcuts.
    bool m_Control = false;

    //! Alt is tracked independently of text input.
    bool m_Alt = false;

    //! Super/Command is preserved separately from the platform-primary modifier.
    bool m_Meta = false;

    //! Physical Control, including on platforms where Command is primary.
    bool m_PhysicalControl = false;

    //! Native adapters mark text-producing AltGr so it cannot execute shortcuts.
    bool m_AltGraph = false;
};

//! The phase of a pointer input event.
enum class PointerEventType
{
    Move,
    Down,
    Up,
    Cancel
};

//! The originating pointer device; native pen/touch adapters are separate capabilities.
enum class PointerDevice
{
    Mouse,
    Pen,
    Touch
};

//! The button changing state; held buttons use the corresponding bit in m_Buttons.
enum class PointerButton
{
    None,
    Left,
    Right,
    Middle,
    Button4,
    Button5,
    Button6,
    Button7,
    Button8
};

//! Pointer information delivered in logical window coordinates.
struct PointerEvent
{
    //! Position in window layout coordinates after inverse visual transforms.
    Point m_Position;

    //! Keyboard modifier state.
    Modifiers m_Modifiers;

    //! Successive press count, including double and triple clicks.
    int m_Clicks = 1;

    //! Whether the original window point lies inside every transformed ancestor clip.
    bool m_InsideClip = true;

    //! Input phase; legacy pointer methods populate it automatically.
    PointerEventType m_Type = PointerEventType::Move;

    //! Button changing state, or None during motion.
    PointerButton m_Button = PointerButton::None;

    //! Held button bitmask, using bit (button - 1).
    unsigned m_Buttons = 0;

    //! Stable device-local pointer identity; the mouse uses zero.
    std::uint64_t m_PointerId = 0;

    //! Originating device category.
    PointerDevice m_Device = PointerDevice::Mouse;

    //! Monotonic source timestamp in seconds.
    double m_Timestamp = 0;

    //! Normalized pen pressure when supplied by a capable adapter.
    float m_Pressure = 0;

    //! Leaves client keyboard focus unchanged for native non-client presses.
    bool m_PreserveFocus = false;
};

//! A normalized keyboard event delivered to an embedded scene.
struct KeyboardEvent
{
    //! Logical shortcut key; committed text arrives separately.
    Key m_Key = Key::Unknown;

    //! Whether the key is pressed rather than released.
    bool m_Down = false;

    //! Whether this is an auto-repeat event.
    bool m_Repeat = false;

    //! Modifier state accompanying this event.
    Modifiers m_Modifiers;

    //! Physical key position using the framework's US-layout key names.
    Key m_PhysicalKey = Key::Unknown;

    //! Native scan code, or zero when unavailable.
    int m_ScanCode = 0;

    //! Monotonic source timestamp in seconds.
    double m_Timestamp = 0;

    //! Layout-dependent printable key name, including names outside the named-key enumeration.
    std::string m_KeyName{};
};
} // namespace EStudioUI
