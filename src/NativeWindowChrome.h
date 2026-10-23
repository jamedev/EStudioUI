#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI::Detail
{
//! Owns custom caption input and retains native move, resize and system-command behavior.
class NativeWindowChrome
{
  public:
    //! Binds prepared hit geometry and owning caption pointer events to one native owner.
    NativeWindowChrome(void* nativeWindow, std::weak_ptr<void> owner, std::function<WindowHitRegion(Point)> hitTest,
                       std::function<void(PointerEvent)> pointer);

    //! Restores native frame handling before the underlying HWND is destroyed.
    ~NativeWindowChrome();

    //! Enables the extended caption or restores the ordinary native title bar.
    void SetEnabled(bool enabled);

    //! Resizes an enabled restored frame to native client pixels; returns false for ordinary frames.
    bool ResizeClient(int width, int height);

    //! Surfaces contained native callback failures at a normal scheduler boundary.
    void RethrowError() const;

  private:
    //! Owns the native subclass without exposing Windows headers.
    struct Impl;

    //! Keeps callback and native frame state alive for this window.
    std::shared_ptr<Impl> m_Impl;
};
} // namespace EStudioUI::Detail
