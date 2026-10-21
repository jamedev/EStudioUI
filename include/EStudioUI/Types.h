#pragma once

#include <algorithm>
#include <cstdint>

namespace EStudioUI
{
//! A position or displacement in logical pixels.
struct Point
{
    //! Horizontal coordinate.
    float m_X = 0;

    //! Vertical coordinate.
    float m_Y = 0;
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
};

//! Physical keyboard commands used by controls independently of GLFW.
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
    Menu
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
};

//! Pointer information delivered in logical window coordinates.
struct PointerEvent
{
    //! Absolute pointer position.
    Point m_Position;

    //! Keyboard modifier state.
    Modifiers m_Modifiers;

    //! Successive press count, including double and triple clicks.
    int m_Clicks = 1;
};

//! A normalized keyboard event delivered to an embedded scene.
struct KeyboardEvent
{
    //! Physical key independent of the text input layout.
    Key m_Key = Key::Unknown;

    //! Whether the key is pressed rather than released.
    bool m_Down = false;

    //! Whether this is an auto-repeat event.
    bool m_Repeat = false;

    //! Modifier state accompanying this event.
    Modifiers m_Modifiers;
};
} // namespace EStudioUI
