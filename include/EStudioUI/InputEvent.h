#pragma once

#include "Types.h"

#include <string>
#include <filesystem>
#include <variant>
#include <vector>

namespace EStudioUI
{
//! Committed owning UTF-8 input, independent of shortcut and physical key events.
struct TextInputEvent
{
    //! Owns the committed text; composition is supplied by the F10a client.
    std::string m_Text;

    //! Monotonic source timestamp in seconds.
    double m_Timestamp = 0;
};

//! Wheel input in logical window coordinates, with adapter-defined line deltas.
struct ScrollEvent
{
    //! Position in window coordinates, transformed for the receiving widget.
    Point m_Position;

    //! Horizontal and vertical wheel displacement.
    Point m_Delta;

    //! Keyboard modifier state.
    Modifiers m_Modifiers;

    //! Monotonic source timestamp in seconds.
    double m_Timestamp = 0;
};

//! Owns a native file drop independently of callback-provided path buffers.
struct FileDropEvent
{
    //! Drop position in logical window coordinates, transformed for the receiver.
    Point m_Position;

    //! Copied file or folder paths.
    std::vector<std::filesystem::path> m_Files;

    //! Source modifier state.
    Modifiers m_Modifiers;

    //! Monotonic source timestamp in seconds.
    double m_Timestamp = 0;
};

//! Owns one native-independent input payload suitable for recording or queued publication.
using InputEvent = std::variant<KeyboardEvent, TextInputEvent, PointerEvent, ScrollEvent, FileDropEvent>;

//! Returns compatibility modifiers that cannot treat AltGr as an editing shortcut.
Modifiers EditingModifiers(Modifiers modifiers);

//! Rejects nonfinite coordinates/timestamps, invalid enum values, and invalid pressure.
void ValidateInput(const InputEvent& event);

namespace Detail
{
//! Reports an active input callback boundary on this thread, across UI contexts.
bool IsDeliveringInput() noexcept;

//! Prevents nested input, native event pumping, and frame service during input delivery.
class InputDispatchScope
{
  public:
    //! Enters one owner-side event boundary, rejecting reentrancy.
    InputDispatchScope();

    //! Leaves the boundary even after callback failure.
    ~InputDispatchScope();

    //! Each boundary has exactly one exit.
    InputDispatchScope(const InputDispatchScope&) = delete;

    //! Each boundary has exactly one exit.
    InputDispatchScope& operator=(const InputDispatchScope&) = delete;
};
} // namespace Detail
} // namespace EStudioUI
