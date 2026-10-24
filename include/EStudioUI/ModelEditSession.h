#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace EStudioUI
{
//! Terminal outcome of a synchronous or asynchronous canonical edit.
enum class ModelEditStatus
{
    Committed,
    Failed,
    OwnerClosed
};

//! Owns an edit outcome independently of its view.
struct ModelEditResult
{

    //! Reports confirmed persistence/publication rather than merely queued work.
    ModelEditStatus m_Status = ModelEditStatus::Failed;

    //! Describes a rejected edit without discarding the editor's proposed value.
    std::string m_Message;
};

//! Owner-side edit result shared by ordinary models, adapters and cell editors.
class ModelEditSession
{
  public:
    //! Creates an empty nonpending handle.
    ModelEditSession();

    //! Returns whether an accepted operation awaits its terminal outcome.
    bool IsPending() const;

    //! Returns an owned outcome, or nullopt while pending/empty.
    std::optional<ModelEditResult> GetResult() const;

    //! Observes settlement once, or immediately for an already settled operation.
    void OnFinished(std::function<void(ModelEditResult)> callback) const;

    //! Returns the first contained completion-observer exception.
    std::exception_ptr GetCallbackError() const;

  private:
    friend class ItemModel;
    friend class AdapterModel;

    //! Owns a receiver-independent, affinity-checked completion record.
    struct Data;

    //! Creates a pending owner-side operation.
    static ModelEditSession Create();

    //! Settles once and contains observer failures.
    void Complete(ModelEditResult result) const;

    //! Shares terminal state without retaining a view/model.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
