#pragma once

#include "Widget.h"

#include <functional>

namespace EStudioUI
{
//! The widget-owned framebuffer supplied to a scene-render callback.
struct OpenGLFrame
{
    //! Physical pixel width, including the window's DPI scale.
    int m_Width = 0;

    //! Physical pixel height.
    int m_Height = 0;

    //! Widget-owned framebuffer, already bound on callback entry.
    std::uint32_t m_Framebuffer = 0;

    //! Monotonic GUI time in seconds.
    double m_Time = 0;
};

//! An embedded GPU surface with a separate shared OpenGL context and depth buffer.
class OpenGLWidget final : public Widget
{
  public:
    //! Creates a lazy surface; a native context is created on its first visible frame.
    OpenGLWidget();

    //! Runs scene cleanup and releases resources with the surface context current.
    ~OpenGLWidget() override;

    //! Sets GUI-thread scene callbacks; they execute with this surface's context current.
    void SetCallbacks(std::function<void()> initialize, std::function<void(const OpenGLFrame&)> render,
                      std::function<void()> release = {});

    //! Enables continuous rendering; otherwise scenes redraw only when requested/resized.
    void SetContinuousRendering(bool enabled);

    //! Invalidates this surface and requests a UI frame on the GUI thread.
    void RequestFrame();

    //! Returns physical framebuffer dimensions, or zero before initialization.
    Point GetFramebufferSize() const;

    //! Returns a diagnostic context identity, distinct from the containing window's context.
    std::uintptr_t GetContextIdentity() const;

    //! Delivers captured pointer presses for scene interaction.
    Signal<PointerEvent>& PointerPressed();

    //! Delivers pointer motion while a press remains captured.
    Signal<PointerEvent>& PointerDragged();

    //! Delivers captured pointer releases.
    Signal<PointerEvent>& PointerReleased();

    //! Delivers wheel deltas for scene zoom/pan.
    Signal<Point>& Scrolled();

    //! Delivers normalized key presses, releases, and repetition to scene controls.
    Signal<KeyboardEvent>& KeyInput();

    //! Allows scene controls to receive keyboard focus.
    bool IsFocusable() const override;

  protected:
    //! Supplies a default preferred viewport size.
    Point Measure(FontFace& font) const override;

    //! Creates/resizes/renders the surface and synchronizes its shared color texture.
    void PrepareFrame(double time, float scaleX, float scaleY) override;

    //! Composites the completed texture between ordinary UI drawing commands.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

    //! Requests animation ticks only for continuous or invalidated surfaces.
    bool NeedsContinuousRendering() const override;

    //! Fences UI sampling before the scene may write its texture again.
    void FrameSubmitted() override;

    //! Starts scene pointer capture.
    void OnPointerDown(const PointerEvent& event) override;

    //! Sends captured scene motion.
    void OnPointerMove(const PointerEvent& event) override;

    //! Ends scene pointer capture.
    void OnPointerUp(const PointerEvent& event) override;

    //! Cancels scene pointer capture.
    void OnCaptureLost() override;

    //! Sends scene zoom/pan wheel input.
    bool OnScroll(Point delta, Modifiers modifiers) override;

    //! Forwards focused scene keyboard input.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Hides GLFW and GL resource declarations from public headers.
    struct Impl;

    //! Owns the hidden context, framebuffer, callbacks, and input signals.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
