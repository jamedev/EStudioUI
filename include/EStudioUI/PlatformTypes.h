#pragma once

#include "Types.h"
#include "UiDispatcher.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace EStudioUI
{
//! Discoverable native-service families; unavailable services never silently become native.
enum class PlatformFeature
{
    ClipboardText,
    ClipboardHtml,
    ClipboardFiles,
    ClipboardCustom,
    FileDialogs,
    FileDrop,
    DragSource,
    Cursors,
    WindowIcons,
    Scaling,
    ScreenCoordinates,
    OwnedWindows,
    Accessibility,
    TextComposition,
    Count
};

//! Distinguishes an OS integration from a deterministic substitute.
enum class PlatformSupport
{
    Unsupported,
    Emulated,
    Native
};

//! Owns an adapter's feature report.
struct PlatformCapabilities
{
    //! Human-readable adapter identity.
    std::string m_Adapter{};

    //! Per-feature implementation kind.
    std::array<PlatformSupport, static_cast<std::size_t>(PlatformFeature::Count)> m_Features{};

    //! Returns one feature's support, rejecting an invalid feature.
    PlatformSupport Get(PlatformFeature feature) const;

    //! Sets one feature's implementation kind, rejecting invalid enum values.
    void Set(PlatformFeature feature, PlatformSupport support);
};

//! Observable outcomes of synchronous native-service requests.
enum class PlatformStatus
{
    Succeeded,
    Unsupported,
    Busy,
    Closed,
    Failed
};

//! Owns a native operation's status and diagnostic.
struct PlatformResult
{
    //! Status of the requested operation.
    PlatformStatus m_Status = PlatformStatus::Succeeded;

    //! OS error or adapter diagnostic, empty on ordinary success.
    std::string m_Message{};

    //! Native diagnostic code when one is available.
    std::int64_t m_NativeCode = 0;

    //! Returns whether the requested operation succeeded.
    explicit operator bool() const noexcept;
};

//! Owns a tightly packed RGBA8 native icon.
struct WindowIcon
{
    //! Positive pixel width.
    int m_Width = 0;

    //! Positive pixel height.
    int m_Height = 0;

    //! Top-down rows of straight RGBA bytes.
    std::vector<std::uint8_t> m_Pixels{};

    //! Rejects invalid dimensions, excessive storage, or incomplete pixels.
    void Validate() const;
};

//! Maps logical UI coordinates to physical client and desktop pixels.
struct WindowMetrics
{
    //! Client dimensions in logical UI units.
    Point m_LogicalSize{};

    //! Physical framebuffer dimensions.
    Point m_FramebufferSize{};

    //! OS content scale relative to the platform's default DPI.
    Point m_ContentScale{1, 1};

    //! Native client-coordinate units per logical UI unit.
    Point m_InputScale{1, 1};

    //! Physical framebuffer pixels per logical UI unit.
    Point m_PixelScale{1, 1};

    //! Physical desktop position of the client origin when available.
    Point m_ScreenOrigin{};

    //! Reports whether physical desktop conversion is provided by the adapter.
    bool m_HasScreenCoordinates = false;

    //! Validates finite nonnegative sizes and positive scales.
    void Validate() const;

    //! Converts native client input into logical UI coordinates.
    Point FromNative(Point point) const;

    //! Converts a logical UI point to physical desktop coordinates.
    Point ToScreen(Point point) const;

    //! Converts a physical desktop point to logical UI coordinates.
    Point FromScreen(Point point) const;

    //! Converts an axis-aligned logical UI rectangle to physical desktop pixels.
    Rect ToScreen(Rect bounds) const;
};

//! Injects owner-side native window operations without GLFW or OS header dependencies.
struct PlatformHost
{
    //! Queue shared with the containing UI context.
    UiDispatcher m_Dispatcher{};

    //! Opaque native window identity; zero for a headless adapter.
    std::uintptr_t m_NativeWindow = 0;

    //! Reports window features implemented by the containing window backend.
    PlatformCapabilities m_Capabilities{};

    //! Retains enclosing native resources only during owner-side callback-driving calls.
    std::weak_ptr<void> m_OwnerLifetime{};

    //! Reads current owned coordinate metrics on the UI owner.
    std::function<WindowMetrics()> m_GetMetrics{};

    //! Changes a standard native pointer shape on the UI owner.
    std::function<PlatformResult(CursorShape)> m_SetCursor{};

    //! Changes a native window icon on the UI owner.
    std::function<PlatformResult(const WindowIcon&)> m_SetIcon{};

    //! Requests native input focus on the UI owner.
    std::function<PlatformResult()> m_FocusWindow{};

    //! Supplies a text-only OS clipboard on backends without richer native formats.
    std::function<std::string()> m_GetClipboardText{};

    //! Publishes OS clipboard text on a text-only backend.
    std::function<PlatformResult(std::string_view)> m_SetClipboardText{};

    //! Wakes the native owner loop from a worker while native resources are retained.
    std::function<void()> m_WakeNativeLoop{};
};
} // namespace EStudioUI
