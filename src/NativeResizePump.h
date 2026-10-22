#pragma once

#include <exception>
#include <functional>
#include <memory>

namespace EStudioUI::Detail
{
//! Keeps the owner scheduler running inside the Win32 move/size/menu modal loop.
class NativeResizePump
{
  public:
    //! Creates an unattached pump; callbacks return seconds until the next needed frame, or -1.
    explicit NativeResizePump(std::function<double()> tick);
    //! Detaches the native subclass and cancels its timer before window destruction.
    ~NativeResizePump();
    //! Attaches a Win32 HWND; other platforms use GLFW refresh/size callbacks.
    void Attach(void* nativeWindow);
    //! Surfaces exceptions at the ordinary C++ scheduler boundary.
    void RethrowError() const;
    //! Ends an active system sizing/menu loop when its owner requests closure.
    void Cancel();
    //! A callback registration has one native owner.
    NativeResizePump(const NativeResizePump&) = delete;
    //! A callback registration has one native owner.
    NativeResizePump& operator=(const NativeResizePump&) = delete;

  private:
    //! Platform implementation without leaking OS types into UI headers.
    struct Impl;
    //! Owns the callback and native registration.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI::Detail
