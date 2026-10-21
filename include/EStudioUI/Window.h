#pragma once

#include "Renderer.h"
#include "UiContext.h"

namespace EStudioUI
{
//! GLFW window and context creation options.
struct WindowOptions
{
    //! Initial logical width.
    int m_Width = 1180;

    //! Initial logical height.
    int m_Height = 820;

    //! Native title, encoded as UTF-8.
    std::string m_Title = "EStudioUI Prototype";

    //! Maximum presentation rate, enforced independently of vertical synchronization.
    double m_MaxFrameRate = 60;

    //! Allows background presentation when the window does not have input focus.
    bool m_RenderWhenUnfocused = false;

    //! Uses GLFW's null platform and OSMesa for CI rendering.
    bool m_Headless = false;
};

//! Owns a GLFW window, modern GL renderer, font service, and GUI context.
class Window
{
  public:
    //! Creates a window and loads its font, throwing on initialization failure.
    Window(const WindowOptions& options, const std::filesystem::path& fontPath);

    //! Releases resources in context-safe order.
    ~Window();

    //! Native windows have one owner.
    Window(const Window&) = delete;

    //! Native windows have one owner.
    Window& operator=(const Window&) = delete;

    //! Returns the window's GUI context.
    UiContext& Ui();

    //! Returns the window's GL renderer and diagnostics.
    Renderer& Graphics();

    //! Returns the diagnostic identity of the window's OpenGL context.
    std::uintptr_t GetContextIdentity() const;

    //! Returns whether this window uses the null platform and OSMesa.
    bool IsHeadless() const;

    //! Runs the native event loop, sleeping between changes and caret deadlines.
    int Run();

    //! Renders one frame without swapping, useful for screenshots and tests.
    void RenderFrame();

    //! Renders using a deterministic nondecreasing GUI clock, useful for animation captures.
    void RenderFrame(double time);

    //! Requests a new frame and wakes the GLFW event loop.
    void RequestRepaint();

    //! Requests closure without destroying the current callback stack.
    void Close();

    //! Exposes key presses after controls receive them, including F5/F6.
    Signal<Key>& KeyPressed();

    //! Changes logical dimensions and invalidates layout.
    void SetSize(int width, int height);

  private:
    //! Owns native resources and callback adapters.
    struct Impl;

    //! Hides GLFW and GL types from public headers.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
