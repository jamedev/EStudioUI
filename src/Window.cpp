#include <EStudioUI/Window.h>
#include <EStudioUI/Application.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/FramePacer.h>

#include "ApplicationRuntime.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
int s_GlfwUsers = 0;
bool s_Headless = false;

Modifiers TranslateModifiers(int value)
{
#ifdef __APPLE__
    const bool control = (value & GLFW_MOD_SUPER) != 0;
#else
    const bool control = (value & GLFW_MOD_CONTROL) != 0;
#endif
    return {(value & GLFW_MOD_SHIFT) != 0, control, (value & GLFW_MOD_ALT) != 0};
}

Key TranslateKey(int key)
{
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        return static_cast<Key>(static_cast<int>(Key::A) + key - GLFW_KEY_A);
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
    default:
        return Key::Unknown;
    }
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
    Signal<Key> m_KeyPressed;
    std::exception_ptr m_CallbackError;
    bool m_Initialized = false;
    bool m_Headless = false;
    bool m_Focused = true;
    bool m_RenderWhenUnfocused = false;
    bool m_Closed = false;
    bool m_Rendering = false;
    FramePacer m_Pacer;
    WidgetRef m_CaretTarget;
    double m_CaretDeadline = -1;
    double m_TimeOffset = 0;
    double m_LastClick = -100;
    Point m_LastClickPosition;
    int m_Clicks = 0;
    std::string m_TestClipboard;

    ~Impl()
    {
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
        if (m_Window)
            glfwDestroyWindow(m_Window);
        if (m_Initialized && --s_GlfwUsers == 0)
            glfwTerminate();
    }

    template <typename Callback> static void Dispatch(GLFWwindow* window, Callback callback)
    {
        auto self = static_cast<Impl*>(glfwGetWindowUserPointer(window))->shared_from_this();
        Detail::ApplicationDispatchScope scope;
        try
        {
            if (!self->m_Owner || self->m_Closed)
                return;
            glfwMakeContextCurrent(self->m_Window);
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
        if (m_CallbackError)
            std::rethrow_exception(m_CallbackError);
        return m_Owner && !m_Closed && !glfwWindowShouldClose(m_Window);
    }

    void RequestClose()
    {
        m_Closed = true;
        m_TargetLifetime.request_stop();
        if (m_Ui)
            m_Ui->GetDispatcher().Close();
        if (m_Window)
            glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
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
            m_Ui->GetDispatcher().Drain();
        if (!m_Owner || m_Closed)
            return;
        glfwMakeContextCurrent(m_Window);
        int width = 0, height = 0, framebufferWidth = 0, framebufferHeight = 0;
        glfwGetWindowSize(m_Window, &width, &height);
        glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
        if (width <= 0 || height <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0)
            return;
        m_Ui->Prepare(static_cast<float>(width), static_cast<float>(height));
        m_Ui->PrepareSurfaces(time, static_cast<float>(framebufferWidth) / static_cast<float>(width),
                              static_cast<float>(framebufferHeight) / static_cast<float>(height));
        m_Renderer->BeginFrame(static_cast<float>(width), static_cast<float>(height), framebufferWidth,
                               framebufferHeight, m_Ui->Root().GetStyle().m_Background);
        m_Ui->Paint(*m_Renderer, time);
        m_Renderer->EndFrame();
        m_Ui->FrameSubmitted();
    }

    double Service()
    {
        if (!IsOpen())
            return -1;
        glfwMakeContextCurrent(m_Window);
        m_Ui->SetTime(GuiTime());
        if (!IsOpen())
            return -1;
        glfwMakeContextCurrent(m_Window);
        auto dispatcher = m_Ui->GetDispatcher();
        dispatcher.Drain();
        if (!IsOpen())
            return -1;
        glfwMakeContextCurrent(m_Window);
        const double now = glfwGetTime();
        const bool active =
            m_Headless || (glfwGetWindowAttrib(m_Window, GLFW_VISIBLE) &&
                           !glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) && (m_Focused || m_RenderWhenUnfocused));
        double wake = dispatcher.HasPending() ? now : -1;
        if (!active)
            return wake;

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
        return wake;
    }

    Point Cursor() const
    {
        double x = 0;
        double y = 0;
        glfwGetCursorPos(m_Window, &x, &y);
        return {static_cast<float>(x), static_cast<float>(y)};
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
        return TranslateModifiers(flags);
    }
};

Window::Window(const WindowOptions& options, const std::filesystem::path& fontPath)
{
    Detail::VerifyApplicationAccess();
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
    self.m_Font = std::make_unique<FontFace>(fontPath);
    self.m_Ui = std::make_unique<UiContext>(*self.m_Font);
    self.m_Ui->SetOwnerLifetime(m_Impl);
    self.m_Renderer = std::make_unique<Renderer>(*self.m_Font);
    self.m_Ui->GetDispatcher().SetWakeup([] { glfwPostEmptyEvent(); });
    self.m_Ui->SetClipboard(
        [&self]
        {
            if (self.m_Headless)
                return self.m_TestClipboard;
            const auto* text = glfwGetClipboardString(self.m_Window);
            return std::string(text ? text : "");
        },
        [&self](std::string_view text)
        {
            if (self.m_Headless)
                self.m_TestClipboard = text;
            else
            {
                const std::string copy(text);
                glfwSetClipboardString(self.m_Window, copy.c_str());
            }
        });

    glfwSetWindowSizeCallback(self.m_Window,
                              [](GLFWwindow* window, int width, int height)
                              {
                                  Impl::Dispatch(
                                      window, [&](Impl& state)
                                      { state.m_Ui->Prepare(static_cast<float>(width), static_cast<float>(height)); });
                              });
    glfwSetFramebufferSizeCallback(self.m_Window, [](GLFWwindow* window, int, int)
                                   { Impl::Dispatch(window, [](Impl& state) { state.m_Ui->Invalidate(); }); });
    glfwSetWindowContentScaleCallback(self.m_Window, [](GLFWwindow* window, float, float)
                                      { Impl::Dispatch(window, [](Impl& state) { state.m_Ui->Invalidate(); }); });
    glfwSetWindowRefreshCallback(self.m_Window, [](GLFWwindow* window)
                                 { Impl::Dispatch(window, [](Impl& state) { state.m_Ui->RequestRepaint(); }); });
    glfwSetCursorPosCallback(
        self.m_Window,
        [](GLFWwindow* window, double x, double y)
        {
            Impl::Dispatch(
                window, [&](Impl& state)
                { state.m_Ui->PointerMove({static_cast<float>(x), static_cast<float>(y)}, state.CurrentModifiers()); });
        });
    glfwSetCursorEnterCallback(self.m_Window,
                               [](GLFWwindow* window, int entered)
                               {
                                   Impl::Dispatch(window,
                                                  [&](Impl& state)
                                                  {
                                                      if (!entered)
                                                          state.m_Ui->PointerMove({-100000, -100000});
                                                  });
                               });
    glfwSetMouseButtonCallback(
        self.m_Window,
        [](GLFWwindow* window, int button, int action, int modifiers)
        {
            if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT)
                return;
            Impl::Dispatch(window,
                           [&](Impl& state)
                           {
                               const auto point = state.Cursor();
                               if (button == GLFW_MOUSE_BUTTON_RIGHT)
                               {
                                   if (action == GLFW_PRESS)
                                       state.m_Ui->ContextMenu(point);
                                   return;
                               }
                               if (action == GLFW_PRESS)
                               {
                                   const double now = glfwGetTime();
                                   const float distance = std::hypot(point.m_X - state.m_LastClickPosition.m_X,
                                                                     point.m_Y - state.m_LastClickPosition.m_Y);
                                   state.m_Clicks =
                                       now - state.m_LastClick <= 0.45 && distance <= 5 ? state.m_Clicks % 3 + 1 : 1;
                                   state.m_LastClick = now;
                                   state.m_LastClickPosition = point;
                                   state.m_Ui->PointerDown(point, TranslateModifiers(modifiers), state.m_Clicks);
                               }
                               else if (action == GLFW_RELEASE)
                                   state.m_Ui->PointerUp(point, TranslateModifiers(modifiers));
                           });
        });
    glfwSetScrollCallback(self.m_Window,
                          [](GLFWwindow* window, double x, double y)
                          {
                              Impl::Dispatch(window,
                                             [&](Impl& state)
                                             {
                                                 state.m_Ui->Scroll(state.Cursor(),
                                                                    {static_cast<float>(x), static_cast<float>(y)},
                                                                    state.CurrentModifiers());
                                             });
                          });
    glfwSetKeyCallback(
        self.m_Window,
        [](GLFWwindow* window, int key, int, int action, int modifiers)
        {
            Impl::Dispatch(window,
                           [&](Impl& state)
                           {
                               const auto translated =
                                   key == GLFW_KEY_F10 && (modifiers & GLFW_MOD_SHIFT) ? Key::Menu : TranslateKey(key);
                               state.m_Ui->KeyEvent(translated, action != GLFW_RELEASE, action == GLFW_REPEAT,
                                                    TranslateModifiers(modifiers));
                               if (action == GLFW_PRESS && state.m_Owner && !state.m_Closed)
                               {
                                   glfwMakeContextCurrent(state.m_Window);
                                   state.m_KeyPressed.Emit(translated);
                               }
                           });
        });
    glfwSetCharCallback(
        self.m_Window, [](GLFWwindow* window, unsigned codepoint)
        { Impl::Dispatch(window, [&](Impl& state) { state.m_Ui->TextInput(EncodeUtf8(codepoint)); }); });
    glfwSetWindowFocusCallback(self.m_Window,
                               [](GLFWwindow* window, int focused)
                               {
                                   Impl::Dispatch(window,
                                                  [&](Impl& state)
                                                  {
                                                      state.m_Focused = focused != 0;
                                                      if (!focused)
                                                          state.m_Ui->Deactivate();
                                                      else
                                                          state.m_Ui->RequestRepaint();
                                                  });
                               });
    glfwSetWindowCloseCallback(self.m_Window, [](GLFWwindow* window)
                               { Impl::Dispatch(window, [](Impl& state) { state.RequestClose(); }); });
    self.m_Ui->Prepare(static_cast<float>(options.m_Width), static_cast<float>(options.m_Height));
    self.m_Owner = this;
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
    Detail::RegisterApplicationWindow(self.m_Registration);
}

Window::~Window()
{
    if (!m_Affinity.IsOwnerThread())
        std::terminate();
    if (m_Impl)
    {
        m_Impl->m_Owner = nullptr;
        m_Impl->RequestClose();
        Detail::RetireApplicationWindow(std::move(m_Impl));
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
        glfwSetWindowSize(self->m_Window, std::max(1, width), std::max(1, height));
        if (self->m_Owner && !self->m_Closed)
            self->m_Ui->Invalidate();
    }
    Detail::FlushRetiredWindows();
}
} // namespace EStudioUI
