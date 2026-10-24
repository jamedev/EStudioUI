#pragma once
#include "CompletionPopup.h"
#include "TextBox.h"
#include "ToolButton.h"
#include "UiTimer.h"

namespace EStudioUI
{
//! Search editor with debounced committed queries and stale-safe asynchronous completion delivery.
class SearchBox : public Widget
{
  public:
    //! Creates an editor with a clear command.
    SearchBox();
    //! Returns the ordinary text editor, including validators and IME support.
    TextBox& GetEditor() const;
    //! Changes canonical committed text.
    void SetText(std::string_view text);
    //! Returns committed text without composition preedit.
    const std::string& GetText() const;
    //! Sets a finite delay from zero through ten seconds; default is 150 ms.
    void SetDebounce(double seconds);
    //! Sets a bounded static suggestion catalog; matching uses Unicode case folding.
    void SetSuggestions(std::vector<CompletionItem> items);
    //! Installs results for a current query; stale tokens return false. Marshal worker results with UiHandle.
    bool SetCompletions(std::uint64_t request, std::vector<CompletionItem> items);
    //! Returns the last contained debounced query error, if any.
    std::exception_ptr GetQueryError() const;
    //! Returns the current request identity, invalidated immediately on each edit.
    std::uint64_t GetRequestId() const;
    //! Publishes debounced owning query text and a request identity.
    Signal<std::string, std::uint64_t>& QueryRequested();
    //! Reports canonical text changes.
    Signal<const std::string&>& TextChanged();
    //! Reports Enter when no completion is accepted.
    Signal<const std::string&>& Submitted();
    //! Reports a selected completion after its text is committed.
    Signal<CompletionItem>& CompletionAccepted();

  protected:
    //! Routes navigation to suggestions while the editor retains focus.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Invalidates pending results and schedules one query deadline.
    void Edited();
    //! Executes the latest query on the owner thread.
    void Query();
    //! Returns or creates the logically owned suggestion surface.
    CompletionPopup& EnsurePopup();
    //! Embedded input control.
    TextBox* m_Editor = nullptr;
    //! Weak root-owned popup.
    WidgetRef m_Popup;
    //! Bounded pre-folded suggestion catalog.
    std::vector<std::pair<std::string, CompletionItem>> m_Catalog;
    //! Pending debounce callback.
    UiTimer m_Timer;
    //! Delay in seconds.
    double m_Debounce = 0.15;
    //! Current query generation.
    std::uint64_t m_Request = 0;
    //! Prevents selection from scheduling a replacement popup.
    bool m_Accepting = false;
    //! Query observers.
    Signal<std::string, std::uint64_t> m_QueryRequested;
    //! Acceptance observers.
    Signal<CompletionItem> m_CompletionAccepted;
};
} // namespace EStudioUI
