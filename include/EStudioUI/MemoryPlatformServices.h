#pragma once

#include "PlatformServices.h"

#include <deque>

namespace EStudioUI
{
//! Deterministic injectable clipboard and scripted picker adapter for headless applications/tests.
class MemoryPlatformServices final : public PlatformServices
{
  public:
    //! Creates a bounded in-memory adapter for one UI dispatcher.
    explicit MemoryPlatformServices(UiDispatcher dispatcher);

    //! Creates an adapter with injected window operations.
    explicit MemoryPlatformServices(PlatformHost host);

    //! Settles outstanding results before releasing deterministic state.
    ~MemoryPlatformServices() override;

    //! Queues one owned picker result; an unscripted request cancels at its next owner turn.
    void QueueFileDialogResult(FileDialogResult result);

  protected:
    //! Reports emulated clipboard and picker support.
    PlatformCapabilities DescribeCapabilities() const override;

    //! Copies the in-memory clipboard.
    ClipboardResult ReadClipboardCore() override;

    //! Replaces the in-memory clipboard atomically after validation.
    PlatformResult WriteClipboardCore(const ClipboardData& data) override;

    //! Publishes the next scripted result through the shared asynchronous delivery path.
    void BeginFileDialog(const FileDialogOptions& options, std::stop_token stop,
                         std::function<void(FileDialogResult)> complete) override;

  private:
    //! Owns clipboard formats independently of caller storage.
    ClipboardData m_Clipboard;

    //! Retains at most sixteen deterministic picker outcomes.
    std::deque<FileDialogResult> m_Results;
};
} // namespace EStudioUI
