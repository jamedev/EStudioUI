#pragma once

#include "Renderer.h"
#include "ThreadAffinity.h"
#include "UiHandle.h"
#include "UiContext.h"
#include "ThemeController.h"

namespace EStudioUI
{
class Window;

//! Native non-client behavior selected by prepared title-bar geometry.
enum class WindowHitRegion
{
    //! Ordinary interactive client content.
    Client,

    //! Native caption movement and system gestures.
    Caption,

    //! Native minimize button.
    Minimize,

    //! Native maximize/restore and snap target.
    Maximize,

    //! Native close button.
    Close
};

//! GLFW window and context creation options.
struct WindowOptions
{
    //! Initial logical width.
    int m_Width = 1180;

    //! Initial logical height.
    int m_Height = 820;

    //! Native title, encoded as UTF-8.
    std::string m_Title = "EStudioUI Prototype";

    //! Uses GLFW's null platform and OSMesa for CI rendering.
    bool m_Headless = false;

    //! Maximum presentation rate, enforced independently of vertical synchronization.
    double m_MaxFrameRate = 60;

    //! Allows background presentation when the window does not have input focus.
    bool m_RenderWhenUnfocused = false;

    //! Optional native/logical owner; closing it also closes this window without transferring C++ ownership.
    Window* m_Owner = nullptr;

    //! Additional CSS faces and ordered fallbacks registered before UI construction.
    std::vector<FontSource> m_Fonts;

    //! Admission limits for fonts and retained text resources.
    FontLimits m_FontLimits;

    //! Optional uniquely owned service adapter factory; ordinary applications use the platform default.
    std::function<std::unique_ptr<PlatformServices>(PlatformHost)> m_PlatformFactory;
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

    //! Returns the same injectable native services exposed by this window's UI context.
    PlatformServices& Platform();

    //! Returns the diagnostic identity of the window's OpenGL context.
    std::uintptr_t GetContextIdentity() const;

    //! Returns whether this window uses the null platform and OSMesa.
    bool IsHeadless() const;

    //! Returns the default live theme; use override CSS to customize its cascade.
    ThemeController& Themes();

    //! Runs the native event loop, sleeping between changes and caret deadlines.
    int Run();

    //! Returns a typed producer endpoint; worker code never accesses this Window directly.
    UiHandle<Window> GetHandle();

    //! Returns this window's producer queue endpoint for owned background results.
    UiDispatcher GetDispatcher() const;

    //! Returns whether native or programmatic closure has been requested.
    bool IsClosed() const;

    //! Rejects unsupported direct calls before accessing native/UI state.
    void VerifyAccess() const;

    //! Renders one frame without swapping, useful for screenshots and tests.
    void RenderFrame();

    //! Renders using a deterministic nondecreasing GUI clock, useful for animation captures.
    void RenderFrame(double time);

    //! Requests a new frame and wakes the GLFW event loop.
    void RequestRepaint();

    //! Hides and closes the native surface; owned resources survive active callbacks and live until destruction.
    void Close();

    //! Exposes key presses after controls receive them, including F5/F6.
    Signal<Key>& KeyPressed();

    //! Sets the native UTF-8 title and root accessible name.
    void SetTitle(std::string title);

    //! Returns the owned UTF-8 native title.
    std::string GetTitle() const;

    //! Adopts a prepared widget as an extended native title bar; null restores the standard frame.
    void SetTitleBar(Widget* titleBar);

    //! Reports actual native frame integration, or emulated/unsupported support.
    PlatformSupport GetTitleBarSupport() const;

    //! Moves this window to physical desktop coordinates.
    void SetPosition(Point position);

    //! Sets finite physical desktop bounds, including the platform frame.
    void SetWindowBounds(Rect bounds);

    //! Returns physical desktop bounds, including the native frame where provided.
    Rect GetWindowBounds() const;

    //! Minimizes this native window.
    void Minimize();

    //! Maximizes while retaining the system's work-area and snap behavior.
    void Maximize();

    //! Restores a minimized or maximized window.
    void Restore();

    //! Returns whether the containing native window is maximized.
    bool IsMaximized() const;

    //! Creates an owned window sharing the GPU group, fonts and native/headless mode.
    std::unique_ptr<Window> CreateOwnedWindow(WindowOptions options = {});

    //! Runs on the idle UI owner immediately before a requested frame, without adding an idle timer.
    Signal<>& FramePreparing();

    //! Reports completed native/programmatic closure once while resources remain retained.
    Signal<>& Closed();

    //! Changes logical dimensions and invalidates layout.
    void SetSize(int width, int height);

  private:
    //! Owns native resources and callback adapters.
    struct Impl;

    //! Captures native/UI ownership before an implementation is accessed.
    ThreadAffinity m_Affinity;

    //! Keeps callback execution alive until native resources can be safely retired.
    std::shared_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
