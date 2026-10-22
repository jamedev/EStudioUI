#pragma once

#include "ClipboardData.h"
#include "FileDialogSession.h"

namespace EStudioUI
{
//! Owner-side native-service boundary with bounded asynchronous completion delivery.
class PlatformServices
{
  public:
    //! Binds this adapter to one UI owner and optional native window operations.
    explicit PlatformServices(PlatformHost host);

    //! Completes abandoned sessions and releases adapter state on the owner.
    virtual ~PlatformServices();

    //! A native adapter has one owner and one lifecycle.
    PlatformServices(const PlatformServices&) = delete;

    //! A native adapter has one owner and one lifecycle.
    PlatformServices& operator=(const PlatformServices&) = delete;

    //! Returns an owned capability report.
    PlatformCapabilities GetCapabilities() const;

    //! Reads independently owned native clipboard formats.
    ClipboardResult ReadClipboard();

    //! Validates and publishes owned clipboard formats, reporting native contention/errors.
    PlatformResult WriteClipboard(const ClipboardData& data);

    //! Publishes UTF-8 clipboard text through the ordinary format boundary.
    PlatformResult SetClipboardText(std::string text);

    //! Starts a picker with copied options and owner-side asynchronous completion.
    FileDialogSession ShowFileDialog(FileDialogOptions options = {});

    //! Reads coordinate metrics without exposing a native handle.
    WindowMetrics GetWindowMetrics() const;

    //! Selects a standard native pointer shape.
    PlatformResult SetCursor(CursorShape shape);

    //! Publishes a validated native RGBA8 window icon.
    PlatformResult SetWindowIcon(const WindowIcon& icon);

    //! Requests native input focus.
    PlatformResult FocusWindow();

    //! Returns the last synchronous native outcome for compatibility clipboard callers.
    PlatformResult GetLastResult() const;

    //! Returns the adapter's queue for domain validation and producer delivery.
    UiDispatcher GetDispatcher() const;

    //! Delivers ready picker results on the owner; windows call this automatically.
    void Pump();

    //! Cancels sessions and rejects new requests without waiting for a native picker to unwind.
    void Close();

    //! Returns whether this adapter has stopped accepting operations.
    bool IsClosed() const;

    //! Reports native cleanup that must finish before the host window can be destroyed.
    bool HasPendingNativeWork() const;

  protected:
    //! Returns the immutable host configured at construction.
    const PlatformHost& GetHost() const;

    //! Supplies adapter capabilities in addition to the containing window's features.
    virtual PlatformCapabilities DescribeCapabilities() const = 0;

    //! Reads formats on the owner; unavailable implementations report Unsupported.
    virtual ClipboardResult ReadClipboardCore() = 0;

    //! Publishes validated formats on the owner.
    virtual PlatformResult WriteClipboardCore(const ClipboardData& data) = 0;

    //! Starts native work; completion may be invoked from any thread with an owned payload.
    virtual void BeginFileDialog(const FileDialogOptions& options, std::stop_token stop,
                                 std::function<void(FileDialogResult)> complete) = 0;

    //! Requests native cancellation before owner-side sessions are settled.
    virtual void Closing();

    //! Reports unfinished adapter workers without waiting for them.
    virtual bool NativeWorkPending() const;

  private:
    friend class UiContext;
    friend class Window;

    //! Installs the containing UI's phase and transaction precondition.
    void SetOperationGuard(std::function<void()> guard, std::weak_ptr<void> owner);

    //! Records a bounded adapter-side rejection without failing the native callback boundary.
    void RecordNativeResult(PlatformResult result);

    //! Owns bounded completion slots and owner-side state independently of the adapter's address.
    struct Data;

    //! Shares callback-safe service bookkeeping.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
