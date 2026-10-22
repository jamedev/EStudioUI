#pragma once

#include <memory>

namespace EStudioUI
{
class UiContext;

namespace Detail
{
//! Attaches the initial Windows UI Automation widget provider; other hosts explicitly report unsupported.
class NativeAccessibility
{
  public:
    //! Attaches to a native HWND without retaining the containing window.
    NativeAccessibility(UiContext& ui, void* nativeWindow, std::weak_ptr<void> owner = {});
    //! Disconnects native providers before widget/native teardown.
    ~NativeAccessibility();
    //! Publishes changes only for bounded, previously queried widget nodes while native clients listen.
    void Flush();
    //! Expires providers immediately; retained COM objects cannot access a closed UI.
    void Close() noexcept;
    //! Each bridge owns exactly one native subclass registration.
    NativeAccessibility(const NativeAccessibility&) = delete;
    //! Each bridge owns exactly one native subclass registration.
    NativeAccessibility& operator=(const NativeAccessibility&) = delete;

  private:
    //! Hides all OS and COM types from the UI model.
    struct Impl;
    //! Owns the native attachment and shared provider state.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace Detail
} // namespace EStudioUI
