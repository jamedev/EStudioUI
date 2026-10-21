#include <EStudioUI/Window.h>
#include <EStudioUI/TextBox.h>

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

struct Window::Impl
{
    GLFWwindow* m_Window = nullptr;
    std::unique_ptr<FontFace> m_Font;
    std::unique_ptr<UiContext> m_Ui;
    std::unique_ptr<Renderer> m_Renderer;
    Signal<Key> m_KeyPressed;
    std::exception_ptr m_CallbackError;
    bool m_Initialized = false;
    bool m_Headless = false;
    bool m_Focused = true;
    double m_LastClick = -100;
    Point m_LastClickPosition;
    int m_Clicks = 0;
    std::string m_TestClipboard;

    ~Impl()
    {
        if (m_Window)
            glfwMakeContextCurrent(m_Window);
        m_Ui.reset();
        m_Renderer.reset();
        m_Font.reset();
        if (m_Window)
            glfwDestroyWindow(m_Window);
        if (m_Initialized && --s_GlfwUsers == 0)
            glfwTerminate();
    }

    template <typename Callback> static void Dispatch(GLFWwindow* window, Callback callback)
    {
        auto& self = *static_cast<Impl*>(glfwGetWindowUserPointer(window));
        try
        {
            self.m_Ui->SetTime(glfwGetTime());
            callback(self);
        }
        catch (...)
        {
            self.m_CallbackError = std::current_exception();
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
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

Window::Window(const WindowOptions& options, const std::filesystem::path& fontPath) : m_Impl(std::make_unique<Impl>())
{
    auto& self = *m_Impl;
    self.m_Headless = options.m_Headless;
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
    self.m_Renderer = std::make_unique<Renderer>(*self.m_Font);
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
                               if (action == GLFW_PRESS)
                                   state.m_KeyPressed.Emit(translated);
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
                                                  });
                               });
    self.m_Ui->Prepare(static_cast<float>(options.m_Width), static_cast<float>(options.m_Height));
}

Window::~Window() = default;

UiContext& Window::Ui()
{
    return *m_Impl->m_Ui;
}

Renderer& Window::Graphics()
{
    return *m_Impl->m_Renderer;
}

std::uintptr_t Window::GetContextIdentity() const
{
    return reinterpret_cast<std::uintptr_t>(m_Impl->m_Window);
}

bool Window::IsHeadless() const
{
    return m_Impl->m_Headless;
}

Signal<Key>& Window::KeyPressed()
{
    return m_Impl->m_KeyPressed;
}

void Window::RenderFrame()
{
    RenderFrame(glfwGetTime());
}

void Window::RenderFrame(double time)
{
    auto& self = *m_Impl;
    glfwMakeContextCurrent(self.m_Window);
    self.m_Ui->SetTime(time);
    int width = 0;
    int height = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(self.m_Window, &width, &height);
    glfwGetFramebufferSize(self.m_Window, &framebufferWidth, &framebufferHeight);
    if (width <= 0 || height <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0)
        return;
    self.m_Ui->Prepare(static_cast<float>(width), static_cast<float>(height));
    const double now = time;
    self.m_Ui->PrepareSurfaces(now, static_cast<float>(framebufferWidth) / static_cast<float>(width),
                               static_cast<float>(framebufferHeight) / static_cast<float>(height));
    self.m_Renderer->BeginFrame(static_cast<float>(width), static_cast<float>(height), framebufferWidth,
                                framebufferHeight, self.m_Ui->Root().GetStyle().m_Background);
    self.m_Ui->Paint(*self.m_Renderer, now);
    self.m_Renderer->EndFrame();
    self.m_Ui->FrameSubmitted();
}

int Window::Run()
{
    auto& self = *m_Impl;
    while (!glfwWindowShouldClose(self.m_Window))
    {
        const double animationDeadline = glfwGetTime() + 1.0 / 60.0;
        if (self.m_Ui->NeedsRepaint() || self.m_Ui->HasAnimations())
        {
            RenderFrame();
            glfwSwapBuffers(self.m_Window);
        }
        if (self.m_Ui->HasAnimations())
        {
            glfwWaitEventsTimeout(std::max(0.0005, animationDeadline - glfwGetTime()));
            self.m_Ui->RequestRepaint();
        }
        else if (const auto* textBox = dynamic_cast<TextBox*>(self.m_Ui->GetFocus());
                 textBox && !textBox->IsReadOnly() && self.m_Focused)
        {
            const auto now = glfwGetTime();
            glfwWaitEventsTimeout(std::clamp(textBox->NextCaretDeadline(now) - now, 0.001, 0.51));
            self.m_Ui->RequestRepaint();
        }
        else
            glfwWaitEvents();
        if (self.m_CallbackError)
            std::rethrow_exception(self.m_CallbackError);
    }
    return 0;
}

void Window::RequestRepaint()
{
    m_Impl->m_Ui->RequestRepaint();
    glfwPostEmptyEvent();
}

void Window::Close()
{
    glfwSetWindowShouldClose(m_Impl->m_Window, GLFW_TRUE);
    glfwPostEmptyEvent();
}

void Window::SetSize(int width, int height)
{
    glfwSetWindowSize(m_Impl->m_Window, std::max(1, width), std::max(1, height));
    m_Impl->m_Ui->Invalidate();
}
} // namespace EStudioUI
