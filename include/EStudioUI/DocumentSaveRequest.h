#pragma once

#include "UndoHistory.h"

namespace EStudioUI
{
//! The terminal outcome of application-defined document persistence.
enum class DocumentSaveStatus
{
    Saved,
    Cancelled,
    Failed
};

//! Owned persistence result; a failed/cancelled save never approves document closure.
struct DocumentSaveResult
{
    //! Reports whether the captured data was successfully persisted.
    DocumentSaveStatus m_Status = DocumentSaveStatus::Cancelled;
    //! Optional bounded UTF-8 diagnostic for application presentation.
    std::string m_Message{};
};

//! A reserved, exactly-once save completion usable from an application worker.
class DocumentSaveRequest
{
  public:
    //! Creates an empty request which cannot accept completion.
    DocumentSaveRequest();
    //! Returns immutable document identity without accessing widgets.
    const std::string& GetDocumentId() const;
    //! Returns the historical value which this persistence operation must capture.
    HistoryCheckpoint GetCheckpoint() const;
    //! Returns cancellation requested when the document/workspace owner expires.
    std::stop_token GetStopToken() const;
    //! Returns whether completion has not yet been accepted and the owner is live.
    bool IsPending() const;
    //! Stores one owned result and wakes owner delivery, even when its normal queue is saturated.
    bool Complete(DocumentSaveResult result) const;

  private:
    friend class DocumentWorkspace;
    //! Owns an independently reserved, mutex-protected completion slot.
    struct Data;
    //! Creates a slot whose notification performs no worker-side UI access.
    static DocumentSaveRequest Create(std::string id, HistoryCheckpoint checkpoint, UiDispatcher dispatcher,
                                      std::function<void()> notify);
    //! Takes an accepted result once on the owner.
    std::optional<DocumentSaveResult> TakeResult() const;
    //! Expires retained worker requests without joining their workers.
    void Expire() const;
    //! Shares immutable identity and reserved result storage with the worker.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
