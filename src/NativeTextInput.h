#pragma once

#include <memory>

namespace EStudioUI
{
class UiContext;

namespace Detail
{
//! Adapts a native Windows input context to the shared text composition protocol.
class NativeTextInput
{
  public:
    //! Attaches to one HWND and preserves its previous input context for restoration.
    NativeTextInput(UiContext& ui, void* nativeWindow, std::weak_ptr<void> owner = {});

    //! Cancels composition and disconnects the native subclass before owner teardown.
    ~NativeTextInput();

    //! Delivers owned native events at an idle owner turn and refreshes caret geometry.
    void Flush();

    //! Rethrows a contained native callback failure at the ordinary C++ service boundary.
    void RethrowError() const;

    //! Cancels the native session and makes any queued native delivery unavailable.
    void Close() noexcept;

    //! Reports a native composition in progress without querying the UI model.
    bool IsComposing() const noexcept;

    //! A native input context has one attachment owner.
    NativeTextInput(const NativeTextInput&) = delete;

    //! A native input context has one attachment owner.
    NativeTextInput& operator=(const NativeTextInput&) = delete;

  private:
    //! Contains all Windows and message-queue state without exposing native types.
    struct Impl;

    //! Keeps callback storage alive during synchronous native message reentrancy.
    std::shared_ptr<Impl> m_Impl;
};
} // namespace Detail
} // namespace EStudioUI
