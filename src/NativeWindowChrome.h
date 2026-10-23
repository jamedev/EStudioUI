#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI::Detail
{
//! Extends the Win32 client frame while retaining system non-client input behavior.
class NativeWindowChrome
{
  public:
    //! Binds pure hit testing and optional caption hover delivery to one retained native owner.
    NativeWindowChrome(void* nativeWindow, std::weak_ptr<void> owner, std::function<WindowHitRegion(Point)> hitTest,
                       std::function<void(Point)> hover);

    //! Restores native frame handling before the underlying HWND is destroyed.
    ~NativeWindowChrome();

    //! Enables the extended caption or restores the ordinary native title bar.
    void SetEnabled(bool enabled);

    //! Surfaces contained native callback failures at a normal scheduler boundary.
    void RethrowError() const;

  private:
    //! Owns the native subclass without exposing Windows headers.
    struct Impl;

    //! Keeps callback and native frame state alive for this window.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI::Detail
