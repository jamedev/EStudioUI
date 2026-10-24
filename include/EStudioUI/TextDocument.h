#pragma once

#include "DocumentSnapshot.h"
#include "UndoHistory.h"

namespace EStudioUI
{
//! Owner-thread plain-text story with immutable storage and transactional bounded undo.
class TextDocument
{
  public:
    //! Creates one empty paragraph in the supplied state domain without widgets or GL.
    explicit TextDocument(StateStore& store, DocumentLimits limits = {}, HistoryLimits historyLimits = {});
    //! Expires producer handles before releasing retained story/history data on the owner.
    ~TextDocument();
    //! Documents keep stable owner and state identities.
    TextDocument(const TextDocument&) = delete;
    //! Documents keep stable owner and state identities.
    TextDocument& operator=(const TextDocument&) = delete;
    //! Captures an immutable story value; the returned copy can be read on any thread.
    DocumentSnapshot GetSnapshot() const;
    //! Returns configured story admission limits.
    DocumentLimits GetLimits() const;
    //! Replaces an ordered exact-snapshot range as one undoable state transaction.
    bool Replace(DocumentPosition first, DocumentPosition last, std::string_view text, std::string label = "Edit",
                 std::optional<DocumentSnapshot> expected = {});
    //! Replaces the entire story as one undoable edit, preserving its first paragraph identity.
    bool SetText(std::string_view text, std::string label = "Replace text");
    //! Loads fresh paragraph identities and clears history in one transaction; true marks the value saved.
    void Reset(std::string_view text, bool markSaved = true);
    //! Returns the existing shared command-history service for undo, redo, grouping and saved checkpoints.
    UndoHistory& GetHistory();
    //! Publishes immutable before/after values after coherent document/history commits.
    Connection Subscribe(std::function<void(const StateChange<DocumentSnapshot>&)> callback);
    //! Adds a pure final-candidate validator to the canonical document State.
    Connection AddValidator(std::function<void(const DocumentSnapshot&)> validator);
    //! Captures a lifetime-safe endpoint for explicit queued commands from workers or observers.
    UiHandle<TextDocument> GetHandle() const;

  private:
    //! Retains active state operations through callbacks that delete the public owner.
    struct Data;
    //! Owns state, history, producer metadata and admission limits.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
