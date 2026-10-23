#include <EStudioUI/Window.h>
#include <EStudioUI/Application.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/FramePacer.h>
#include <EStudioUI/MemoryPlatformServices.h>

#include "ApplicationRuntime.h"
#include "NativeResizePump.h"
#include "NativeAccessibility.h"
#include "NativeTextInput.h"
#include "NativePlatformServices.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
int s_GlfwUsers = 0;
bool s_Headless = false;

Modifiers TranslateModifiers(int value, bool altGraph = false)
{
#ifdef __APPLE__
    const bool control = (value & GLFW_MOD_SUPER) != 0;
#else
    const bool control = (value & GLFW_MOD_CONTROL) != 0;
#endif
    return {(value & GLFW_MOD_SHIFT) != 0,   control, (value & GLFW_MOD_ALT) != 0, (value & GLFW_MOD_SUPER) != 0,
            (value & GLFW_MOD_CONTROL) != 0, altGraph};
}

Key TranslateKey(int key)
{
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        return static_cast<Key>(static_cast<int>(Key::A) + key - GLFW_KEY_A);
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        return static_cast<Key>(static_cast<int>(Key::D0) + key - GLFW_KEY_0);
    const Key functions[]{Key::F1,  Key::F2,  Key::F3,  Key::F4,  Key::F5,  Key::F6,  Key::F7,  Key::F8,
                          Key::F9,  Key::F10, Key::F11, Key::F12, Key::F13, Key::F14, Key::F15, Key::F16,
                          Key::F17, Key::F18, Key::F19, Key::F20, Key::F21, Key::F22, Key::F23, Key::F24};
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F24)
        return functions[key - GLFW_KEY_F1];
    switch (key)
    {
    case GLFW_KEY_TAB:
        return Key::Tab;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        return Key::Enter;
    case GLFW_KEY_SPACE:
        return Key::Space;
    case GLFW_KEY_LEFT:
        return Key::Left;
    case GLFW_KEY_RIGHT:
        return Key::Right;
    case GLFW_KEY_UP:
        return Key::Up;
    case GLFW_KEY_DOWN:
        return Key::Down;
    case GLFW_KEY_PAGE_UP:
        return Key::PageUp;
    case GLFW_KEY_PAGE_DOWN:
        return Key::PageDown;
    case GLFW_KEY_F2:
        return Key::F2;
    case GLFW_KEY_MENU:
        return Key::Menu;
    case GLFW_KEY_HOME:
        return Key::Home;
    case GLFW_KEY_END:
        return Key::End;
    case GLFW_KEY_BACKSPACE:
        return Key::Backspace;
    case GLFW_KEY_DELETE:
        return Key::Delete;
    case GLFW_KEY_ESCAPE:
        return Key::Escape;
    case GLFW_KEY_F5:
        return Key::F5;
    case GLFW_KEY_F6:
        return Key::F6;
    case GLFW_KEY_INSERT:
        return Key::Insert;
    case GLFW_KEY_MINUS:
        return Key::Minus;
    case GLFW_KEY_EQUAL:
        return Key::Equal;
    case GLFW_KEY_COMMA:
        return Key::Comma;
    case GLFW_KEY_PERIOD:
        return Key::Period;
    case GLFW_KEY_SLASH:
        return Key::Slash;
    case GLFW_KEY_BACKSLASH:
        return Key::Backslash;
    case GLFW_KEY_SEMICOLON:
        return Key::Semicolon;
    case GLFW_KEY_APOSTROPHE:
        return Key::Apostrophe;
    case GLFW_KEY_LEFT_BRACKET:
        return Key::LeftBracket;
    case GLFW_KEY_RIGHT_BRACKET:
        return Key::RightBracket;
    case GLFW_KEY_GRAVE_ACCENT:
        return Key::Grave;
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT:
        return Key::Shift;
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL:
        return Key::Control;
    case GLFW_KEY_LEFT_ALT:
    case GLFW_KEY_RIGHT_ALT:
        return Key::Alt;
    case GLFW_KEY_LEFT_SUPER:
    case GLFW_KEY_RIGHT_SUPER:
        return Key::Meta;
    default:
        return Key::Unknown;
    }
}

// GLFW key tokens identify US-layout positions; printable names carry the current layout.
Key LogicalKey(int key, int scanCode)
{
    const char* name = glfwGetKeyName(key, scanCode);
    if (name && name[0] && !name[1])
    {
        const unsigned char value = static_cast<unsigned char>(name[0]);
        if (value >= 'a' && value <= 'z')
            return static_cast<Key>(static_cast<int>(Key::A) + value - 'a');
        if (value >= 'A' && value <= 'Z')
            return static_cast<Key>(static_cast<int>(Key::A) + value - 'A');
        if (value >= '0' && value <= '9')
            return static_cast<Key>(static_cast<int>(Key::D0) + value - '0');
        switch (value)
        {
        case '-':
            return Key::Minus;
        case '=':
            return Key::Equal;
        case ',':
            return Key::Comma;
        case '.':
            return Key::Period;
        case '/':
            return Key::Slash;
        case '\\':
            return Key::Backslash;
        case ';':
            return Key::Semicolon;
        case '\'':
            return Key::Apostrophe;
        case '[':
            return Key::LeftBracket;
        case ']':
            return Key::RightBracket;
        case '`':
            return Key::Grave;
        default:
            break;
        }
    }
    return TranslateKey(key);
}

std::string EncodeUtf8(unsigned codepoint)
{
    std::string text;
    if (codepoint <= 0x7F)
        text += static_cast<char>(codepoint);
    else if (codepoint <= 0x7FF)
    {
        text += static_cast<char>(0xC0 | (codepoint >> 6));
        text += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else if (codepoint <= 0xFFFF)
    {
        text += static_cast<char>(0xE0 | (codepoint >> 12));
        text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else if (codepoint <= 0x10FFFF)
    {
        text += static_cast<char>(0xF0 | (codepoint >> 18));
        text += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return text;
}
} // namespace

struct Window::Impl : std::enable_shared_from_this<Window::Impl>
{
    Window* m_Owner = nullptr;
    const std::uint64_t m_TargetKey = UiDispatcher::AllocateTargetKey();
    std::stop_source m_TargetLifetime;
    std::shared_ptr<Detail::ApplicationWindow> m_Registration;
    GLFWwindow* m_Window = nullptr;
    std::unique_ptr<FontFace> m_Font;
    std::unique_ptr<UiContext> m_Ui;
    std::unique_ptr<Renderer> m_Renderer;
    std::unique_ptr<Detail::NativeResizePump> m_ResizePump;
    std::unique_ptr<Detail::NativeAccessibility> m_Accessibility;
    std::unique_ptr<Detail::NativeTextInput> m_TextInput;
    Signal<Key> m_KeyPressed;
    std::exception_ptr m_CallbackError;
    bool m_Initialized = false;
    bool m_Headless = false;
    bool m_Focused = true;
    bool m_RenderWhenUnfocused = false;
    bool m_Closed = false;
    bool m_Rendering = false;
    bool m_Servicing = false;
    FramePacer m_Pacer;
    WidgetRef m_CaretTarget;
    double m_CaretDeadline = -1;
    double m_TimeOffset = 0;
    double m_LastClick = -100;
    Point m_LastClickPosition;
    int m_Clicks = 0;
    std::uint64_t m_InputRevision = 0;
    WindowMetrics m_Metrics;
    std::array<GLFWcursor*, 8> m_Cursors{};
    std::unique_ptr<std::stop_callback<std::function<void()>>> m_ParentStop;
    std::optional<CursorShape> m_LastUiCursor;

    void UpdateCursor()
    {
        const auto desired = m_Ui->GetPointerCursor();
        if (!m_LastUiCursor || *m_LastUiCursor != desired)
        {
            m_LastUiCursor = desired;
            m_Ui->Platform().SetCursor(desired);
        }
    }

    void UpdateMetrics()
    {
        int width = 0, height = 0, framebufferWidth = 0, framebufferHeight = 0;
        glfwGetWindowSize(m_Window, &width, &height);
        glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
        float scaleX = 1, scaleY = 1;
        if (!m_Headless)
            glfwGetWindowContentScale(m_Window, &scaleX, &scaleY);
        const int platform = glfwGetPlatform();
        const bool pixelCoordinates = platform == GLFW_PLATFORM_WIN32 || platform == GLFW_PLATFORM_X11;
        m_Metrics.m_ContentScale = {scaleX, scaleY};
        m_Metrics.m_InputScale = pixelCoordinates ? Point{scaleX, scaleY} : Point{1, 1};
        m_Metrics.m_LogicalSize = {static_cast<float>(width) / m_Metrics.m_InputScale.m_X,
                                   static_cast<float>(height) / m_Metrics.m_InputScale.m_Y};
        m_Metrics.m_FramebufferSize = {static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight)};
        m_Metrics.m_PixelScale = {
            width > 0 && framebufferWidth > 0 ? static_cast<float>(framebufferWidth) / m_Metrics.m_LogicalSize.m_X : 1,
            height > 0 && framebufferHeight > 0 ? static_cast<float>(framebufferHeight) / m_Metrics.m_LogicalSize.m_Y
                                                : 1};
        m_Metrics.m_HasScreenCoordinates = false;
#ifdef _WIN32
        if (!m_Headless)
        {
            POINT origin{};
            if (ClientToScreen(glfwGetWin32Window(m_Window), &origin))
            {
                m_Metrics.m_ScreenOrigin = {static_cast<float>(origin.x), static_cast<float>(origin.y)};
                m_Metrics.m_HasScreenCoordinates = true;
            }
        }
#else
        if (platform == GLFW_PLATFORM_X11)
        {
            int x = 0, y = 0;
            glfwGetWindowPos(m_Window, &x, &y);
            m_Metrics.m_ScreenOrigin = {static_cast<float>(x), static_cast<float>(y)};
            m_Metrics.m_HasScreenCoordinates = true;
        }
#endif
        m_Metrics.Validate();
    }

    PlatformResult SelectCursor(CursorShape shape)
    {
        if (m_Closed)
            return {PlatformStatus::Closed, "The native window is closed."};
        if (m_Headless)
            return {};
        static constexpr int shapes[]{GLFW_ARROW_CURSOR,         GLFW_IBEAM_CURSOR,      GLFW_CROSSHAIR_CURSOR,
                                      GLFW_POINTING_HAND_CURSOR, GLFW_RESIZE_EW_CURSOR,  GLFW_RESIZE_NS_CURSOR,
                                      GLFW_RESIZE_ALL_CURSOR,    GLFW_NOT_ALLOWED_CURSOR};
        const auto index = static_cast<std::size_t>(shape);
        glfwGetError(nullptr);
        if (!m_Cursors[index])
            m_Cursors[index] = glfwCreateStandardCursor(shapes[index]);
        if (m_Cursors[index])
            glfwSetCursor(m_Window, m_Cursors[index]);
        const char* message = nullptr;
        const int error = glfwGetError(&message);
        return error || !m_Cursors[index] ? PlatformResult{PlatformStatus::Unsupported,
                                                           message ? message : "Native cursor unavailable.", error}
                                          : PlatformResult{};
    }

    PlatformHost MakePlatformHost()
    {
        PlatformHost host;
        host.m_Dispatcher = m_Ui->GetDispatcher();
        host.m_OwnerLifetime = weak_from_this();
        for (const auto feature : {PlatformFeature::FileDrop, PlatformFeature::Cursors, PlatformFeature::Scaling})
            host.m_Capabilities.Set(feature, m_Headless ? PlatformSupport::Emulated : PlatformSupport::Native);
        host.m_Capabilities.Set(PlatformFeature::OwnedWindows,
#ifdef _WIN32
                                m_Headless ? PlatformSupport::Emulated : PlatformSupport::Native
#else
                                PlatformSupport::Emulated
#endif
        );
        if (m_Metrics.m_HasScreenCoordinates)
            host.m_Capabilities.Set(PlatformFeature::ScreenCoordinates, PlatformSupport::Native);
#ifdef _WIN32
        if (!m_Headless)
        {
            host.m_NativeWindow = reinterpret_cast<std::uintptr_t>(glfwGetWin32Window(m_Window));
            host.m_Capabilities.Set(PlatformFeature::Accessibility, PlatformSupport::Native);
            host.m_Capabilities.Set(PlatformFeature::TextComposition, PlatformSupport::Native);
        }
#endif
        const std::weak_ptr<Impl> weak = weak_from_this();
        host.m_GetMetrics = [weak]
        {
            if (const auto state = weak.lock())
                return state->m_Metrics;
            return WindowMetrics{};
        };
        host.m_SetCursor = [weak](CursorShape shape)
        {
            const auto state = weak.lock();
            return state ? state->SelectCursor(shape) : PlatformResult{PlatformStatus::Closed};
        };
        host.m_SetIcon = [weak](const WindowIcon& icon)
        {
            const auto state = weak.lock();
            if (!state || state->m_Closed)
                return PlatformResult{PlatformStatus::Closed};
            if (state->m_Headless || glfwGetPlatform() == GLFW_PLATFORM_COCOA ||
                glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
                return PlatformResult{PlatformStatus::Unsupported, "Window icons are unavailable on this backend."};
            GLFWimage image{icon.m_Width, icon.m_Height, const_cast<unsigned char*>(icon.m_Pixels.data())};
            glfwGetError(nullptr);
            glfwSetWindowIcon(state->m_Window, 1, &image);
            const char* message = nullptr;
            const int error = glfwGetError(&message);
            return error ? PlatformResult{PlatformStatus::Failed, message ? message : "Native icon failed.", error}
                         : PlatformResult{};
        };
        if (!m_Headless && glfwGetPlatform() != GLFW_PLATFORM_COCOA && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
            host.m_Capabilities.Set(PlatformFeature::WindowIcons, PlatformSupport::Native);
        host.m_FocusWindow = [weak]
        {
            const auto state = weak.lock();
            if (!state || state->m_Closed)
                return PlatformResult{PlatformStatus::Closed};
            glfwGetError(nullptr);
            if (!state->m_Headless)
                glfwFocusWindow(state->m_Window);
            const char* message = nullptr;
            const int error = glfwGetError(&message);
            return error ? PlatformResult{PlatformStatus::Failed, message ? message : "Native focus request failed.",
                                          error}
                         : PlatformResult{};
        };
        host.m_GetClipboardText = [weak]
        {
            const auto state = weak.lock();
            const auto* text = state && !state->m_Closed ? glfwGetClipboardString(state->m_Window) : nullptr;
            return std::string(text ? text : "");
        };
        host.m_SetClipboardText = [weak](std::string_view text)
        {
            const auto state = weak.lock();
            if (!state || state->m_Closed)
                return PlatformResult{PlatformStatus::Closed};
            const std::string owned(text);
            glfwGetError(nullptr);
            glfwSetClipboardString(state->m_Window, owned.c_str());
            const char* message = nullptr;
            const int error = glfwGetError(&message);
            return error ? PlatformResult{PlatformStatus::Failed, message ? message : "Native clipboard failed.", error}
                         : PlatformResult{};
        };
        host.m_WakeNativeLoop = [] { glfwPostEmptyEvent(); };
        return host;
    }

    ~Impl()
    {
        m_TextInput.reset();
        m_Accessibility.reset();
        m_ResizePump.reset();
        m_TargetLifetime.request_stop();
        if (m_Ui)
            m_Ui->GetDispatcher().Close();
        if (m_Window)
            glfwMakeContextCurrent(m_Window);
        m_Ui.reset();
        // Widget destruction can run lifetime observers that visit other windows.
        if (m_Window)
            glfwMakeContextCurrent(m_Window);
        m_Renderer.reset();
        m_Font.reset();
        for (auto* cursor : m_Cursors)
            if (cursor)
                glfwDestroyCursor(cursor);
        if (m_Window)
            glfwDestroyWindow(m_Window);
        if (m_Initialized && --s_GlfwUsers == 0)
            glfwTerminate();
    }

    template <typename Callback> static void Dispatch(GLFWwindow* window, Callback callback)
    {
        auto self = static_cast<Impl*>(glfwGetWindowUserPointer(window))->shared_from_this();
        Detail::ApplicationDispatchScope scope;

        struct RestoreContext
        {
            GLFWwindow* m_Previous = glfwGetCurrentContext();

            ~RestoreContext()
            {
                glfwMakeContextCurrent(m_Previous);
            }
        } restore;

        try
        {
            if (!self->m_Owner || self->m_Closed)
                return;
            glfwMakeContextCurrent(self->m_Window);
            // Synchronous resize/focus callbacks may occur inside a state batch.
            // Route their invalidation now; advance animation time at the next safe boundary.
            if (!StateStore::IsUpdatingOnThisThread() && !self->m_Rendering && !self->m_Servicing &&
                self->m_Ui->GetPhase() == UiPhase::Idle && Detail::CanServiceNativeFrame())
                self->m_Ui->SetTime(self->GuiTime());
            if (!self->m_Owner || self->m_Closed)
                return;
            glfwMakeContextCurrent(self->m_Window);
            callback(*self);
        }
        catch (...)
        {
            self->m_CallbackError = std::current_exception();
            self->RequestClose();
        }
    }

    double GuiTime() const
    {
        return std::max(glfwGetTime() + m_TimeOffset, m_Ui->GetTime());
    }

    bool IsOpen() const
    {
        if (m_ResizePump)
            m_ResizePump->RethrowError();
        if (m_CallbackError)
            std::rethrow_exception(m_CallbackError);
        return m_Owner && !m_Closed && !glfwWindowShouldClose(m_Window);
    }

    void RequestClose()
    {
        if (m_Closed)
            return;
        m_Closed = true;
        if (m_TextInput)
            m_TextInput->Close();
        if (m_Ui && m_Ui->GetPhase() == UiPhase::Idle)
        {
            try
            {
                m_Ui->CancelTextComposition();
            }
            catch (...)
            {
                m_CallbackError = std::current_exception();
            }
        }
        if (m_Accessibility)
            m_Accessibility->Close();
        if (m_Ui)
            m_Ui->ClosePlatformServices();
        if (m_ResizePump)
            m_ResizePump->Cancel();
        if (m_Window)
        {
#ifdef _WIN32
            if (!m_Headless)
                SetWindowLongPtrW(glfwGetWin32Window(m_Window), GWLP_HWNDPARENT, 0);
#endif
            glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
            // The C++ owner may retain a closed Window. Remove its native surface
            // immediately; release context resources when that owner is destroyed.
            glfwHideWindow(m_Window);
        }
        m_TargetLifetime.request_stop();
        if (m_Ui)
            m_Ui->GetDispatcher().Close();
    }

    void Render(double time, bool drain)
    {
        if (m_Rendering)
            throw std::logic_error("A window cannot render recursively from its own callback.");

        struct RenderingScope
        {
            bool& m_Rendering;

            explicit RenderingScope(bool& rendering) : m_Rendering(rendering)
            {
                rendering = true;
            }

            ~RenderingScope()
            {
                m_Rendering = false;
            }
        } rendering(m_Rendering);

        glfwMakeContextCurrent(m_Window);
        m_Ui->SetTime(time);
        if (!m_Owner || m_Closed)
            return;
        glfwMakeContextCurrent(m_Window);
        if (drain)
        {
            m_Ui->PumpPlatformServices();
            if (!m_Owner || m_Closed)
                return;
            m_Ui->GetDispatcher().Drain();
        }
        if (!m_Owner || m_Closed)
            return;
        glfwMakeContextCurrent(m_Window);
        int width = 0, height = 0, framebufferWidth = 0, framebufferHeight = 0;
        glfwGetWindowSize(m_Window, &width, &height);
        glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
        if (width <= 0 || height <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0)
            return;
        UpdateMetrics();
        m_Ui->Prepare(m_Metrics.m_LogicalSize.m_X, m_Metrics.m_LogicalSize.m_Y);
        if (m_TextInput)
        {
            m_TextInput->Flush();
            m_TextInput->RethrowError();
        }
        if (!m_Owner || m_Closed)
            return;
        m_Ui->PrepareSurfaces(time, m_Metrics.m_PixelScale.m_X, m_Metrics.m_PixelScale.m_Y);
        m_Renderer->BeginFrame(m_Metrics.m_LogicalSize.m_X, m_Metrics.m_LogicalSize.m_Y, framebufferWidth,
                               framebufferHeight, m_Ui->Root().GetStyle().m_Background);
        m_Ui->Paint(*m_Renderer, time);
        m_Renderer->EndFrame();
        m_Ui->FrameSubmitted();
        if (m_Owner && !m_Closed)
        {
            UpdateCursor();
            if (m_Accessibility)
                m_Accessibility->Flush();
        }
    }

    double Service()
    {
        if (m_Servicing)
            return -1;

        struct ServiceScope
        {
            bool& m_Flag;

            explicit ServiceScope(bool& flag) : m_Flag(flag)
            {
                flag = true;
            }

            ~ServiceScope()
            {
                m_Flag = false;
            }
        } servicing(m_Servicing);

        if (!IsOpen())
            return -1;
        if (m_TextInput)
        {
            m_TextInput->Flush();
            m_TextInput->RethrowError();
        }
        if (!IsOpen())
            return -1;
        glfwMakeContextCurrent(m_Window);
        m_Ui->SetTime(GuiTime());
        if (!IsOpen())
            return -1;
        glfwMakeContextCurrent(m_Window);
        m_Ui->PumpPlatformServices();
        if (!IsOpen())
            return -1;
        glfwMakeContextCurrent(m_Window);
        auto dispatcher = m_Ui->GetDispatcher();
        dispatcher.Drain();
        if (!IsOpen())
            return -1;
        UpdateCursor();
        glfwMakeContextCurrent(m_Window);
        const double now = glfwGetTime();
        const bool active =
            m_Headless || (glfwGetWindowAttrib(m_Window, GLFW_VISIBLE) &&
                           !glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) && (m_Focused || m_RenderWhenUnfocused));
        double wake = dispatcher.HasPending() ? now : -1;
        if (!active)
        {
            if (m_Accessibility)
                m_Accessibility->Flush();
            return wake;
        }

        m_Ui->UpdateToolTip(GuiTime());
        const double helpDeadline = m_Ui->NextToolTipDeadline();
        if (helpDeadline >= 0 && (wake < 0 || helpDeadline - m_TimeOffset < wake))
            wake = helpDeadline - m_TimeOffset;
        const auto* editor = dynamic_cast<TextBox*>(m_Ui->GetFocus());
        if (editor && !editor->IsReadOnly())
        {
            if (m_CaretTarget.Get() == editor && m_CaretDeadline >= 0 && now >= m_CaretDeadline)
                m_Ui->RequestRepaint();
            m_CaretTarget = WidgetRef(const_cast<TextBox*>(editor));
            m_CaretDeadline = editor->NextCaretDeadline(GuiTime()) - m_TimeOffset;
            if (wake < 0 || m_CaretDeadline < wake)
                wake = m_CaretDeadline;
        }
        else
        {
            m_CaretTarget = {};
            m_CaretDeadline = -1;
        }
        if ((m_Ui->NeedsRepaint() || m_Ui->HasAnimations()) && m_Pacer.IsDue(now))
        {
            Render(GuiTime(), false);
            if (!IsOpen())
                return -1;
            glfwSwapBuffers(m_Window);
            m_Pacer.Presented(now, std::max(now, glfwGetTime()));
        }
        if (m_Ui->NeedsRepaint() || m_Ui->HasAnimations())
        {
            const double frame = m_Pacer.GetDeadline();
            if (wake < 0 || frame < wake)
                wake = frame;
        }
        if (m_TextInput && IsOpen())
        {
            m_TextInput->Flush();
            m_TextInput->RethrowError();
        }
        if (m_Accessibility && IsOpen())
            m_Accessibility->Flush();
        return wake;
    }

    double NativeFrame()
    {
        if (!Detail::CanServiceNativeFrame() || m_Rendering || m_Servicing || StateStore::IsUpdatingOnThisThread() ||
            m_Ui->GetPhase() != UiPhase::Idle)
            return 0.01;
        const double deadline = Detail::ServiceNativeWindows();
        return deadline < 0 ? -1 : std::max(0.0, deadline - glfwGetTime());
    }

    void ObserveNativeDelivery(const DispatchReceipt& receipt)
    {
        const auto weak = weak_from_this();
        receipt.OnFinished(
            [weak](DispatchStatus status, std::exception_ptr error)
            {
                if (status != DispatchStatus::Failed && status != DispatchStatus::Rejected)
                    return;
                if (const auto self = weak.lock(); self && self->m_Owner && !self->m_Closed)
                {
                    self->m_CallbackError = error ? error
                                                  : std::make_exception_ptr(std::runtime_error(
                                                        "The native delivery queue rejected an ordered callback."));
                    self->RequestClose();
                }
            });
    }

    void SetFocused(bool focused)
    {
        m_Focused = focused;
        if (focused)
        {
            m_Ui->RequestRepaint();
            return;
        }
        const auto revision = ++m_InputRevision;
        if (!Detail::IsDeliveringInput() && m_Ui->GetPhase() == UiPhase::Idle && !StateStore::IsUpdatingOnThisThread())
        {
            m_Ui->Deactivate();
            return;
        }
        const auto weak = weak_from_this();
        auto receipt = m_Ui->Defer(
            [weak, revision](UiContext& ui)
            {
                if (const auto self = weak.lock();
                    self && self->m_Owner && !self->m_Closed && self->m_InputRevision == revision && !self->m_Focused)
                    ui.Deactivate();
            });
        ObserveNativeDelivery(receipt);
    }

    void DeliverInput(InputEvent event)
    {
        if (Detail::IsDeliveringInput() || m_Ui->GetPhase() != UiPhase::Idle || StateStore::IsUpdatingOnThisThread())
        {
            const auto weak = weak_from_this();
            const auto revision = m_InputRevision;
            auto receipt = m_Ui->Defer(
                [weak, revision, event = std::move(event)](UiContext&) mutable
                {
                    if (const auto self = weak.lock();
                        self && self->m_Owner && !self->m_Closed && self->m_InputRevision == revision)
                        self->DeliverInput(std::move(event));
                });
            ObserveNativeDelivery(receipt);
            return;
        }
        const auto* key = std::get_if<KeyboardEvent>(&event);
        const bool notify = key && key->m_Down && !key->m_Repeat;
        const auto pressed = key ? key->m_Key : Key::Unknown;
        m_Ui->DispatchInput(event);
        if (m_Owner && !m_Closed)
            UpdateCursor();
        if (notify && m_Owner && !m_Closed)
        {
            glfwMakeContextCurrent(m_Window);
            m_KeyPressed.Emit(pressed);
        }
    }

    Point Cursor() const
    {
        double x = 0;
        double y = 0;
        glfwGetCursorPos(m_Window, &x, &y);
        return m_Metrics.FromNative({static_cast<float>(x), static_cast<float>(y)});
    }

    Modifiers InputModifiers(int flags) const
    {
        const bool altGraph = (flags & GLFW_MOD_CONTROL) != 0 && glfwGetKey(m_Window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        return TranslateModifiers(flags, altGraph);
    }

    unsigned PointerButtons() const
    {
        unsigned result = 0;
        for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_8; ++button)
            if (glfwGetMouseButton(m_Window, button) == GLFW_PRESS)
                result |= 1u << static_cast<unsigned>(button);
        return result;
    }

    Modifiers CurrentModifiers() const
    {
        int flags = 0;
        if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(m_Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
            flags |= GLFW_MOD_SHIFT;
        if (glfwGetKey(m_Window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
            glfwGetKey(m_Window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
            flags |= GLFW_MOD_CONTROL;
        if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
            glfwGetKey(m_Window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
            flags |= GLFW_MOD_SUPER;
        if (glfwGetKey(m_Window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
            glfwGetKey(m_Window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
            flags |= GLFW_MOD_ALT;
        return InputModifiers(flags);
    }
};

Window::Window(const WindowOptions& options, const std::filesystem::path& fontPath)
{
    Detail::VerifyWindowCreation();
    std::shared_ptr<Impl> parent;
    if (options.m_Owner)
    {
        options.m_Owner->VerifyAccess();
        if (options.m_Owner->IsClosed())
            throw std::invalid_argument("A native owner must be an open window.");
        parent = options.m_Owner->m_Impl;
    }
    m_Impl = std::make_shared<Impl>();
    auto& self = *m_Impl;
    self.m_Headless = options.m_Headless;
    self.m_RenderWhenUnfocused = options.m_RenderWhenUnfocused;
    self.m_Pacer = FramePacer(options.m_MaxFrameRate);
    if (s_GlfwUsers > 0 && s_Headless != options.m_Headless)
        throw std::runtime_error("Native and null-platform windows require separate processes.");
    if (s_GlfwUsers == 0)
    {
        glfwSetErrorCallback([](int code, const char* message)
                             { std::cerr << "GLFW " << code << ": " << message << '\n'; });
        glfwInitHint(GLFW_PLATFORM, options.m_Headless ? GLFW_PLATFORM_NULL : GLFW_ANY_PLATFORM);
        if (!glfwInit())
            throw std::runtime_error(
                "GLFW initialization failed. Use --headless on a machine without a desktop display.");
        s_Headless = options.m_Headless;
    }
    ++s_GlfwUsers;
    self.m_Initialized = true;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (!options.m_Headless)
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    if (options.m_Headless)
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_OSMESA_CONTEXT_API);
    }
    self.m_Window = glfwCreateWindow(std::max(1, options.m_Width), std::max(1, options.m_Height),
                                     options.m_Title.c_str(), nullptr, nullptr);
    if (!self.m_Window)
        throw std::runtime_error("Could not create an OpenGL 3.3 window/context.");
    glfwMakeContextCurrent(self.m_Window);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
        throw std::runtime_error("Could not load OpenGL entry points.");
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(self.m_Window, &self);
    self.m_Font = std::make_unique<FontFace>(fontPath, options.m_FontLimits);
    for (const auto& source : options.m_Fonts)
        self.m_Font->AddFont(source);
    self.m_Ui = std::make_unique<UiContext>(*self.m_Font);
    self.m_Ui->SetOwnerLifetime(m_Impl);
    self.m_Ui->Root().SetAccessibleName(options.m_Title);
    self.m_Renderer = std::make_unique<Renderer>(*self.m_Font);
    self.m_Ui->GetDispatcher().SetWakeup([] { glfwPostEmptyEvent(); });
    self.UpdateMetrics();
    auto host = self.MakePlatformHost();
    auto services = options.m_PlatformFactory ? options.m_PlatformFactory(std::move(host))
                    : options.m_Headless      ? std::make_unique<MemoryPlatformServices>(std::move(host))
                                              : Detail::CreateNativePlatformServices(std::move(host));
    self.m_Ui->SetPlatformServices(std::move(services));
#ifdef _WIN32
    if (!self.m_Headless)
    {
        self.m_Accessibility =
            std::make_unique<Detail::NativeAccessibility>(*self.m_Ui, glfwGetWin32Window(self.m_Window), m_Impl);
        self.m_TextInput =
            std::make_unique<Detail::NativeTextInput>(*self.m_Ui, glfwGetWin32Window(self.m_Window), m_Impl);
    }
#endif

    glfwSetWindowSizeCallback(self.m_Window,
                              [](GLFWwindow* window, int, int)
                              {
                                  Impl::Dispatch(window,
                                                 [&](Impl& state)
                                                 {
                                                     state.UpdateMetrics();
                                                     state.m_Ui->RequestRepaint();
                                                     state.NativeFrame();
                                                 });
                              });
    glfwSetFramebufferSizeCallback(self.m_Window,
                                   [](GLFWwindow* window, int, int)
                                   {
                                       Impl::Dispatch(window,
                                                      [](Impl& state)
                                                      {
                                                          state.UpdateMetrics();
                                                          state.m_Ui->RequestRepaint();
                                                      });
                                   });
    glfwSetWindowContentScaleCallback(self.m_Window,
                                      [](GLFWwindow* window, float, float)
                                      {
                                          Impl::Dispatch(window,
                                                         [](Impl& state)
                                                         {
                                                             state.UpdateMetrics();
                                                             state.m_Ui->RequestRepaint();
                                                         });
                                      });
    glfwSetWindowPosCallback(self.m_Window, [](GLFWwindow* window, int, int)
                             { Impl::Dispatch(window, [](Impl& state) { state.UpdateMetrics(); }); });
    glfwSetWindowRefreshCallback(self.m_Window,
                                 [](GLFWwindow* window)
                                 {
                                     Impl::Dispatch(window,
                                                    [](Impl& state)
                                                    {
                                                        state.m_Ui->RequestRepaint();
                                                        state.NativeFrame();
                                                    });
                                 });
    glfwSetCursorPosCallback(self.m_Window,
                             [](GLFWwindow* window, double x, double y)
                             {
                                 Impl::Dispatch(
                                     window,
                                     [&](Impl& state)
                                     {
                                         PointerEvent event{
                                             state.m_Metrics.FromNative({static_cast<float>(x), static_cast<float>(y)}),
                                             state.CurrentModifiers()};
                                         event.m_Buttons = state.PointerButtons();
                                         event.m_Timestamp = glfwGetTime();
                                         state.DeliverInput(event);
                                     });
                             });
    glfwSetCursorEnterCallback(self.m_Window,
                               [](GLFWwindow* window, int entered)
                               {
                                   Impl::Dispatch(window,
                                                  [&](Impl& state)
                                                  {
                                                      PointerEvent event{entered ? state.Cursor()
                                                                                 : Point{-100000, -100000},
                                                                         state.CurrentModifiers()};
                                                      event.m_Buttons = state.PointerButtons();
                                                      event.m_Timestamp = glfwGetTime();
                                                      state.DeliverInput(event);
                                                  });
                               });
    glfwSetMouseButtonCallback(
        self.m_Window,
        [](GLFWwindow* window, int button, int action, int modifiers)
        {
            Impl::Dispatch(
                window,
                [&](Impl& state)
                {
                    if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_8)
                        return;
                    PointerEvent event{state.Cursor(), state.InputModifiers(modifiers)};
                    event.m_Type = action == GLFW_PRESS ? PointerEventType::Down : PointerEventType::Up;
                    event.m_Button = button == GLFW_MOUSE_BUTTON_LEFT     ? PointerButton::Left
                                     : button == GLFW_MOUSE_BUTTON_RIGHT  ? PointerButton::Right
                                     : button == GLFW_MOUSE_BUTTON_MIDDLE ? PointerButton::Middle
                                                                          : static_cast<PointerButton>(button + 1);
                    event.m_Buttons = state.PointerButtons();
                    event.m_Timestamp = glfwGetTime();
                    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT)
                    {
                        const double now = event.m_Timestamp;
                        const float distance = std::hypot(event.m_Position.m_X - state.m_LastClickPosition.m_X,
                                                          event.m_Position.m_Y - state.m_LastClickPosition.m_Y);
                        state.m_Clicks = now - state.m_LastClick <= 0.45 && distance <= 5 ? state.m_Clicks % 3 + 1 : 1;
                        state.m_LastClick = now;
                        state.m_LastClickPosition = event.m_Position;
                        event.m_Clicks = state.m_Clicks;
                    }
                    state.DeliverInput(event);
                });
        });
    glfwSetScrollCallback(self.m_Window,
                          [](GLFWwindow* window, double x, double y)
                          {
                              Impl::Dispatch(window,
                                             [&](Impl& state)
                                             {
                                                 state.DeliverInput(
                                                     ScrollEvent{state.Cursor(),
                                                                 {static_cast<float>(x), static_cast<float>(y)},
                                                                 state.CurrentModifiers(),
                                                                 glfwGetTime()});
                                             });
                          });
    glfwSetKeyCallback(self.m_Window,
                       [](GLFWwindow* window, int key, int scanCode, int action, int modifiers)
                       {
                           Impl::Dispatch(window,
                                          [&](Impl& state)
                                          {
                                              const auto translated = LogicalKey(key, scanCode);
                                              if (state.m_TextInput && state.m_TextInput->IsComposing())
                                              {
                                                  if (action == GLFW_PRESS && translated == Key::Escape)
                                                      state.m_Ui->CancelTextComposition();
                                                  return;
                                              }
                                              KeyboardEvent event{translated,
                                                                  action != GLFW_RELEASE,
                                                                  action == GLFW_REPEAT,
                                                                  state.InputModifiers(modifiers),
                                                                  TranslateKey(key),
                                                                  scanCode,
                                                                  glfwGetTime()};
                                              if (const char* name = glfwGetKeyName(key, scanCode))
                                                  event.m_KeyName = name;
                                              state.DeliverInput(event);
                                          });
                       });
    glfwSetCharCallback(self.m_Window,
                        [](GLFWwindow* window, unsigned codepoint)
                        {
                            Impl::Dispatch(
                                window, [&](Impl& state)
                                { state.DeliverInput(TextInputEvent{EncodeUtf8(codepoint), glfwGetTime()}); });
                        });
    glfwSetWindowFocusCallback(self.m_Window, [](GLFWwindow* window, int focused)
                               { Impl::Dispatch(window, [&](Impl& state) { state.SetFocused(focused != 0); }); });
    glfwSetWindowCloseCallback(self.m_Window, [](GLFWwindow* window)
                               { Impl::Dispatch(window, [](Impl& state) { state.RequestClose(); }); });
    glfwSetDropCallback(self.m_Window,
                        [](GLFWwindow* window, int count, const char** paths)
                        {
                            Impl::Dispatch(window,
                                           [&](Impl& state)
                                           {
                                               FileDropEvent drop;
                                               drop.m_Position = state.Cursor();
                                               drop.m_Modifiers = state.CurrentModifiers();
                                               drop.m_Timestamp = glfwGetTime();
                                               std::size_t remaining = 8 * 1024 * 1024;
                                               bool valid = count > 0 && count <= 512 && paths;
                                               for (int i = 0; valid && i < count; ++i)
                                               {
                                                   if (!paths[i])
                                                   {
                                                       valid = false;
                                                       break;
                                                   }
                                                   const std::string_view path(paths[i]);
                                                   if (path.empty() || path.size() > remaining)
                                                   {
                                                       valid = false;
                                                       break;
                                                   }
                                                   remaining -= path.size();
                                                   drop.m_Files.emplace_back(std::u8string(
                                                       reinterpret_cast<const char8_t*>(path.data()), path.size()));
                                               }
                                               if (!valid)
                                               {
                                                   state.m_Ui->Platform().RecordNativeResult(
                                                       {PlatformStatus::Failed,
                                                        "The native file drop exceeds its path/payload budget."});
                                                   return;
                                               }
                                               state.DeliverInput(std::move(drop));
                                           });
                        });
    self.m_Ui->Prepare(self.m_Metrics.m_LogicalSize.m_X, self.m_Metrics.m_LogicalSize.m_Y);
    self.m_Owner = this;
#ifdef _WIN32
    if (!options.m_Headless)
    {
        self.m_ResizePump = std::make_unique<Detail::NativeResizePump>(
            [window = self.m_Window]
            {
                double remaining = -1;
                Impl::Dispatch(window, [&](Impl& state) { remaining = state.NativeFrame(); });
                return remaining;
            });
        self.m_ResizePump->Attach(glfwGetWin32Window(self.m_Window));
    }
#endif
    self.m_Registration = std::make_shared<Detail::ApplicationWindow>();
    const std::weak_ptr<Impl> weak = m_Impl;
    self.m_Registration->m_IsOpen = [weak]
    {
        const auto state = weak.lock();
        return state && state->IsOpen();
    };
    self.m_Registration->m_Service = [weak]
    {
        const auto state = weak.lock();
        return state ? state->Service() : -1;
    };
    self.m_Registration->m_Close = [weak]
    {
        if (const auto state = weak.lock())
            state->RequestClose();
    };
    self.m_Registration->m_HasPendingNativeWork = [weak]
    {
        const auto state = weak.lock();
        return state && state->m_Ui && state->m_Ui->HasPendingPlatformWork();
    };
    Detail::RegisterApplicationWindow(self.m_Registration);
    if (parent)
    {
#ifdef _WIN32
        if (!self.m_Headless)
        {
            SetLastError(0);
            if (!SetWindowLongPtrW(glfwGetWin32Window(self.m_Window), GWLP_HWNDPARENT,
                                   reinterpret_cast<LONG_PTR>(glfwGetWin32Window(parent->m_Window))) &&
                GetLastError())
                throw std::runtime_error("Could not set the native window owner.");
        }
#endif
        self.m_ParentStop =
            std::make_unique<std::stop_callback<std::function<void()>>>(parent->m_TargetLifetime.get_token(),
                                                                        [weak]
                                                                        {
                                                                            if (const auto state = weak.lock())
                                                                                state->RequestClose();
                                                                        });
    }
}

Window::~Window()
{
    if (!m_Affinity.IsOwnerThread())
        std::terminate();
    if (m_Impl)
    {
        m_Impl->m_Owner = nullptr;
        m_Impl->RequestClose();
        const std::weak_ptr<Impl> weak = m_Impl;
        Detail::RetireApplicationWindow(std::move(m_Impl),
                                        [weak]
                                        {
                                            const auto state = weak.lock();
                                            return !state || !state->m_Ui || !state->m_Ui->HasPendingPlatformWork();
                                        });
    }
}

void Window::VerifyAccess() const
{
    m_Affinity.VerifyAccess("Window access");
    Detail::VerifyApplicationAccess();
}

UiContext& Window::Ui()
{
    VerifyAccess();
    return *m_Impl->m_Ui;
}

Renderer& Window::Graphics()
{
    VerifyAccess();
    return *m_Impl->m_Renderer;
}

PlatformServices& Window::Platform()
{
    VerifyAccess();
    return m_Impl->m_Ui->Platform();
}

std::uintptr_t Window::GetContextIdentity() const
{
    VerifyAccess();
    return reinterpret_cast<std::uintptr_t>(m_Impl->m_Window);
}

bool Window::IsHeadless() const
{
    VerifyAccess();
    return m_Impl->m_Headless;
}

Signal<Key>& Window::KeyPressed()
{
    VerifyAccess();
    return m_Impl->m_KeyPressed;
}

void Window::RenderFrame()
{
    VerifyAccess();
    RenderFrame(m_Impl->GuiTime());
}

void Window::RenderFrame(double time)
{
    VerifyAccess();
    {
        const auto self = m_Impl;
        Detail::ApplicationDispatchScope scope;
        self->Render(time, true);
        self->m_TimeOffset = time - glfwGetTime();
    }
    Detail::FlushRetiredWindows();
}

int Window::Run()
{
    VerifyAccess();
    return Application::RunWindows();
}

UiHandle<Window> Window::GetHandle()
{
    VerifyAccess();
    const std::weak_ptr<Impl> weak = m_Impl;
    return UiHandle<Window>::ForTarget(
        m_Impl->m_Ui->GetDispatcher(), m_Impl->m_TargetKey,
        [weak]() -> Window*
        {
            const auto self = weak.lock();
            if (!self || self->m_Closed || !self->m_Owner)
                return nullptr;
            glfwMakeContextCurrent(self->m_Window);
            return self->m_Owner;
        },
        m_Impl->m_TargetLifetime.get_token());
}

UiDispatcher Window::GetDispatcher() const
{
    VerifyAccess();
    return m_Impl->m_Ui->GetDispatcher();
}

bool Window::IsClosed() const
{
    VerifyAccess();
    return m_Impl->m_Closed || glfwWindowShouldClose(m_Impl->m_Window);
}

void Window::RequestRepaint()
{
    VerifyAccess();
    m_Impl->m_Ui->RequestRepaint();
    glfwPostEmptyEvent();
}

void Window::Close()
{
    VerifyAccess();
    {
        const auto self = m_Impl;
        Detail::ApplicationDispatchScope scope;
        self->RequestClose();
    }
    Detail::FlushRetiredWindows();
}

void Window::SetSize(int width, int height)
{
    VerifyAccess();
    {
        const auto self = m_Impl;
        Detail::ApplicationDispatchScope scope;
        const auto dimension = [](int value, float scale)
        {
            return static_cast<int>(std::clamp(std::round(static_cast<double>(std::max(1, value)) * scale), 1.0,
                                               static_cast<double>(std::numeric_limits<int>::max())));
        };
        glfwSetWindowSize(self->m_Window, dimension(width, self->m_Metrics.m_InputScale.m_X),
                          dimension(height, self->m_Metrics.m_InputScale.m_Y));
        self->UpdateMetrics();
        if (self->m_Owner && !self->m_Closed)
            self->m_Ui->RequestRepaint();
    }
    Detail::FlushRetiredWindows();
}
} // namespace EStudioUI
