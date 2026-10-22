#include <EStudioUI/InputEvent.h>

#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <algorithm>

namespace EStudioUI
{
namespace Detail
{
namespace
{
thread_local bool s_DeliveringInput = false;
}

bool IsDeliveringInput() noexcept
{
    return s_DeliveringInput;
}

InputDispatchScope::InputDispatchScope()
{
    if (s_DeliveringInput)
        throw std::logic_error("Nested input delivery is unsupported; queue the next event.");
    s_DeliveringInput = true;
}

InputDispatchScope::~InputDispatchScope()
{
    s_DeliveringInput = false;
}
} // namespace Detail

Modifiers EditingModifiers(Modifiers modifiers)
{
#ifdef __APPLE__
    modifiers.m_Control = modifiers.m_Control || modifiers.m_Meta;
#else
    modifiers.m_Control = modifiers.m_Control || modifiers.m_PhysicalControl;
#endif
    if (modifiers.m_AltGraph)
    {
        modifiers.m_Control = false;
        modifiers.m_Alt = false;
    }
    return modifiers;
}

void ValidateInput(const InputEvent& event)
{
    std::visit(
        [](const auto& value)
        {
            using T = std::decay_t<decltype(value)>;
            if (!std::isfinite(value.m_Timestamp) || value.m_Timestamp < 0)
                throw std::invalid_argument("An input timestamp must be finite and nonnegative.");
            if constexpr (std::is_same_v<T, PointerEvent> || std::is_same_v<T, ScrollEvent> ||
                          std::is_same_v<T, FileDropEvent>)
                if (!std::isfinite(value.m_Position.m_X) || !std::isfinite(value.m_Position.m_Y))
                    throw std::invalid_argument("Input coordinates must be finite.");
            if constexpr (std::is_same_v<T, KeyboardEvent>)
            {
                if (value.m_Key < Key::Unknown || value.m_Key > Key::Meta || value.m_PhysicalKey < Key::Unknown ||
                    value.m_PhysicalKey > Key::Meta || (value.m_Repeat && !value.m_Down))
                    throw std::invalid_argument("Invalid keyboard input.");
            }
            if constexpr (std::is_same_v<T, PointerEvent>)
            {
                if (value.m_Type < PointerEventType::Move || value.m_Type > PointerEventType::Cancel ||
                    value.m_Button < PointerButton::None || value.m_Button > PointerButton::Button8 ||
                    value.m_Device < PointerDevice::Mouse || value.m_Device > PointerDevice::Touch ||
                    !std::isfinite(value.m_Pressure) || value.m_Pressure < 0 || value.m_Pressure > 1 ||
                    value.m_Clicks < 1 || value.m_Buttons > 255)
                    throw std::invalid_argument("Invalid pointer input.");
            }
            if constexpr (std::is_same_v<T, ScrollEvent>)
                if (!std::isfinite(value.m_Delta.m_X) || !std::isfinite(value.m_Delta.m_Y))
                    throw std::invalid_argument("Wheel deltas must be finite.");
            if constexpr (std::is_same_v<T, FileDropEvent>)
            {
                if (value.m_Files.empty() || value.m_Files.size() > 512)
                    throw std::invalid_argument("A file drop requires between one and 512 owned paths.");
                std::size_t remaining = 8 * 1024 * 1024;
                for (const auto& path : value.m_Files)
                {
                    const auto& native = path.native();
                    const auto bytes = path.u8string().size();
                    if (path.empty() ||
                        std::find(native.begin(), native.end(), std::filesystem::path::value_type{}) != native.end() ||
                        bytes > remaining)
                        throw std::invalid_argument("File-drop paths must be bounded and cannot contain NULs.");
                    remaining -= bytes;
                }
            }
        },
        event);
}
} // namespace EStudioUI
