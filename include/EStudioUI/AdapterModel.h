#pragma once

#include "ItemModel.h"
#include "ModelAdapter.h"

namespace EStudioUI
{
class UiContext;
class BackgroundTasks;

//! Describes one owner-side adapter request without confusing old rows with requested data.
enum class AdapterOperation
{
    Idle,
    Catalog,
    Loading,
    Committing
};

//! Owns coherent status for an asynchronously loaded result window.
struct AdapterStatus
{

    //! Reports current owner-side work; no I/O occurs from view data accessors.
    AdapterOperation m_Operation = AdapterOperation::Idle;

    //! Describes the last successfully published page.
    ModelQuery m_Query;

    //! Counts original and filtered source rows, independently of loaded-window size.
    std::uint64_t m_TotalRows = 0, m_MatchedRows = 0;

    //! Counts the immutable rows currently exposed to ordinary views.
    std::size_t m_LoadedRows = 0;

    //! Owns the most recent request/publication failure.
    std::string m_Error;

    //! Compares coherent status values for no-op suppression.
    bool operator==(const AdapterStatus&) const = default;
};

//! Bridges any worker-safe ModelAdapter into the existing snapshot/model/selection boundary.
class AdapterModel final : public ItemModel
{
  public:
    //! Creates a catalog-loading model; the application executor must outlive it.
    static std::shared_ptr<AdapterModel> Create(UiContext& ui, std::shared_ptr<const ModelAdapter> adapter,
                                                BackgroundTasks& workers);

    //! Expires requests/results and settles edit sessions without joining application workers.

    ~AdapterModel() override;

    //! Returns discovered datasets as an owned immutable catalog.
    std::shared_ptr<const std::vector<ModelDataSet>> GetCatalog() const;

    //! Returns current query, operation, counts and error.
    AdapterStatus GetStatus() const;

    //! Returns the most recent contained status/publication observer exception.
    std::exception_ptr GetCallbackError() const;

    //! Observes owned status after coherent page/model publication.
    Connection SubscribeStatus(std::function<void(AdapterStatus)> callback);

    //! Selects/query-loads a dataset; superseded reads cannot replace a newer page.
    void SetQuery(ModelQuery query);

    //! Rediscovers schemas while retaining a still-existing dataset/query.
    void RefreshCatalog();

    //! Reloads the current page or the initial catalog if no dataset was selected.
    void Refresh();

    //! Cancels reads and retains the last published page; writes require a confirmed terminal result.
    void CancelLoading();

    //! Requests a zero-based page using the current published window size.
    void SetPage(std::uint64_t page);

    //! Returns the opaque stable source key of a currently loaded item.
    std::string GetRowKey(ItemId item) const;

    //! Rejects synchronous source writes; use RequestEdit for confirmed asynchronous commits.
    bool Apply(std::vector<ModelEdit> edits, std::optional<std::uint64_t> expectedRevision = {}) override;

    //! Validates and queues one optimistic transactional edit; confirmed results update every bound view.
    ModelEditSession RequestEdit(std::vector<ModelEdit> edits,
                                 std::optional<std::uint64_t> expectedRevision = {}) override;

    //! Diagnoses replacing source-owned storage through the ordinary mutable-model API.
    bool Replace(ModelSnapshot snapshot, std::optional<std::uint64_t> expectedRevision = {}) override;

    //! Diagnoses row ordering for sources without an explicit structural write contract.
    void Restructure(const std::function<void(ModelBuilder&)>& edit) override;

  private:
    //! Initializes owner-side status and lifetime metadata before starting workers.
    AdapterModel(UiContext& ui, std::shared_ptr<const ModelAdapter> adapter, BackgroundTasks& workers);

    //! Retains bounded slots independently of widget/model destruction.
    struct Data;

    //! Owns owner-side state and weak service subscriptions.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
