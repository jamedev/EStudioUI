#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <optional>

namespace EStudioUI
{
class Widget;
class UiContext;

//! Why one asynchronous popup/modal session ended.
enum class PopupCloseReason
{
    Closed,
    Accepted,
    Rejected,
    Escape,
    OutsideClick,
    ParentClosed,
    Replaced,
    Hidden,
    OwnerExpired,
    Deactivated,
    ContextClosed
};

//! Owns the terminal outcome of one popup presentation.
struct PopupResult
{
    //! Terminal cause; there is no nested event loop or blocking wait.
    PopupCloseReason m_Reason = PopupCloseReason::Closed;

    //! Application-defined response, such as a selected dialog button.
    int m_Value = 0;
};

//! Configuration copied when opening a popup; pointer arguments are observed, not retained.
struct PopupOptions
{
    //! Explicit existing parent popup; null infers a logical owner or replaces the current chain.
    Widget* m_Parent = nullptr;

    //! Prevents pointer, focus, and shortcut access to underlying application content.
    bool m_Modal = false;

    //! Allows outside presses to dismiss this presentation without click-through.
    bool m_DismissOnOutside = true;

    //! Allows Escape to dismiss this presentation.
    bool m_DismissOnEscape = true;

    //! Restores the previous eligible focus target after closure.
    bool m_RestoreFocus = true;

    //! Returns modal defaults that consume outside presses without dismissing.
    static PopupOptions Modal();
};

//! A copyable owner-side asynchronous result independent of the popup widget lifetime.
class PopupSession
{
  public:
    //! Creates an empty handle with no pending presentation.
    PopupSession();

    //! Returns whether this valid presentation still awaits completion.
    bool IsPending() const;

    //! Returns the owned terminal result, or nullopt while pending/empty.
    std::optional<PopupResult> GetResult() const;

    //! Runs once after closure, or immediately for a completed handle; errors remain inspectable.
    void OnFinished(std::function<void(PopupResult)> callback) const;

    //! Returns the first contained completion-observer exception.
    std::exception_ptr GetCallbackError() const;

  private:
    friend class UiContext;

    //! Owns owner-thread completion state and callbacks.
    struct Data;

    //! Creates one pending presentation result.
    static PopupSession Create();

    //! Commits one result and runs all observers, containing their errors.
    void Complete(PopupResult result) const;

    //! Shares a result without keeping the popup, context, or native window alive.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
