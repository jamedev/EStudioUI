#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace EStudioUI
{
//! Terminal result of one document-close preflight.
enum class DocumentCloseStatus
{
    Closed,
    Cancelled,
    Failed,
    OwnerClosed
};

//! Owns the outcome and stable requested identities of one close operation.
struct DocumentCloseResult
{
    //! Reports whether all requested documents were actually closed.
    DocumentCloseStatus m_Status = DocumentCloseStatus::Cancelled;
    //! Retains the stable requested document identities.
    std::vector<std::string> m_Documents;
    //! Optional bounded application/persistence diagnostic.
    std::string m_Message{};
};

//! An asynchronous owner-side result; it never starts a nested event loop.
class DocumentCloseSession
{
  public:
    //! Creates an empty, nonpending result handle.
    DocumentCloseSession();
    //! Returns whether this valid close workflow is still pending.
    bool IsPending() const;
    //! Returns the owned terminal result, or nullopt while pending/empty.
    std::optional<DocumentCloseResult> GetResult() const;
    //! Runs once after settlement, or immediately for an already completed result.
    void OnFinished(std::function<void(DocumentCloseResult)> callback) const;
    //! Returns the first contained observer exception.
    std::exception_ptr GetCallbackError() const;

  private:
    friend class DocumentWorkspace;
    //! Owns callbacks and terminal state through workspace destruction.
    struct Data;
    //! Creates a pending workflow without retaining its widget owner.
    static DocumentCloseSession Create();
    //! Settles exactly once and contains completion-observer errors.
    void Complete(DocumentCloseResult result) const;
    //! Shares a terminal outcome independently of widget/window resources.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
