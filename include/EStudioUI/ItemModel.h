#pragma once

#include "ModelBuilder.h"
#include "State.h"
#include "ThreadAffinity.h"

namespace EStudioUI
{
class SelectionModel;

//! Identifies one changed semantic field without storing its formatted presentation.
struct ModelField
{
    //! Stable item identity.
    ItemId m_Item = 0;

    //! Schema column index.
    std::size_t m_Column = 0;

    //! Changed semantic role.
    ModelRole m_Role = ModelRole::Edit;

    //! Compares exact field identity.
    bool operator==(const ModelField&) const = default;
};

//! Owns one coherent model publication, including immutable before/after data.
struct ModelChange
{
    //! Snapshot before this publication.
    ModelSnapshot m_Before;

    //! Snapshot after this publication.
    ModelSnapshot m_After;

    //! Canonical model revision associated with this data.
    std::uint64_t m_Revision = 0;

    //! Fields touched in this batch; reset publications may omit the complete field list.
    std::vector<ModelField> m_Fields;

    //! Indicates replacement of the schema or complete dataset.
    bool m_Reset = false;

    //! Indicates changes to identities, hierarchy, or row ordering.
    bool m_Structure = false;

    //! Identifies the originating model endpoint independently of data and adapter revisions.
    std::uint64_t m_ModelKey = 0;
};

namespace Detail
{
//! Keeps model data and its accumulated change description in one transactional state value.
struct ModelState
{
    //! Current candidate immutable storage.
    ModelSnapshot m_Snapshot;

    //! Storage at the beginning of this model's current transaction.
    ModelSnapshot m_Base;

    //! Complete affected fields accumulated in the current transaction.
    std::vector<ModelField> m_Fields;

    //! Revision from which the candidate batch began.
    std::uint64_t m_BaseRevision = ~std::uint64_t{0};

    //! Canonical revision carried by derived views, or empty for source models.
    std::optional<std::uint64_t> m_SourceRevision;

    //! Marks a whole-model replacement.
    bool m_Reset = false;

    //! Marks an identity or hierarchy change.
    bool m_Structure = false;
};
} // namespace Detail

//! Publishes immutable collections through a StateStore and serves multiple lifetime-bound views.
class ItemModel : public std::enable_shared_from_this<ItemModel>
{
  public:
    //! Creates a canonical model from owned immutable data on the store's owner.
    ItemModel(StateStore& store, ModelSnapshot initial = {});

    //! Expires queued model handles on the owner without invalidating captured data snapshots.
    virtual ~ItemModel();

    //! Model endpoints retain stable identity.
    ItemModel(const ItemModel&) = delete;

    //! Model endpoints retain stable identity.
    ItemModel& operator=(const ItemModel&) = delete;

    //! Checks model/store lifetime and owner-thread access.
    void VerifyAccess() const;

    //! Returns an owned immutable candidate or committed snapshot on the owner.
    ModelSnapshot GetSnapshot() const;

    //! Returns the canonical source revision used by revision-checked publications.
    virtual std::uint64_t GetRevision() const;

    //! Returns the canonical source shared by sort/filter adapters and selection models.
    virtual std::shared_ptr<ItemModel> GetRootModel();

    //! Returns the immediate source of an adapter, or empty for canonical data.
    virtual std::shared_ptr<ItemModel> GetSourceModel() const;

    //! Returns the process-unique model endpoint identity on its owner.
    std::uint64_t GetIdentity() const;

    //! Returns the model's live owner store.
    StateStore& GetStore() const;

    //! Applies one field edit and returns whether the staged model changed.
    bool SetValue(ItemId item, std::size_t column, ModelValue value);

    //! Parses a typed text edit and publishes it with the column's validation policy.
    bool SetText(ItemId item, std::size_t column, std::string_view text);

    //! Applies final field candidates once; an expected canonical revision rejects obsolete work.
    virtual bool Apply(std::vector<ModelEdit> edits, std::optional<std::uint64_t> expectedRevision = {});

    //! Replaces a complete snapshot atomically, pruning selection in the same store transaction.
    virtual bool Replace(ModelSnapshot snapshot, std::optional<std::uint64_t> expectedRevision = {});

    //! Performs an explicit linear structural rebuild, retaining stable IDs and coherent selection.
    virtual void Restructure(const std::function<void(ModelBuilder&)>& edit);

    //! Captures a typed worker-safe publication endpoint without retaining the model object.
    UiHandle<ItemModel> GetHandle() const;

    //! Registers an owner callback; all model and selection state is published before notification.
    template <typename Callback> Connection Subscribe(Callback callback)
    {
        return m_State.Subscribe(
            [key = GetIdentity(), callback = std::move(callback)](const StateChange<Detail::ModelState>& change) mutable
            {
                const auto& after = change.GetAfter();
                auto fields = after.m_Fields;
                if (after.m_SourceRevision)
                    std::erase_if(fields,
                                  [&](const ModelField& field)
                                  {
                                      return !after.m_Snapshot.Contains(field.m_Item) &&
                                             !change.GetBefore().m_Snapshot.Contains(field.m_Item);
                                  });
                std::invoke(callback, ModelChange{change.GetBefore().m_Snapshot, after.m_Snapshot,
                                                  after.m_SourceRevision.value_or(change.GetRevision()),
                                                  std::move(fields), after.m_Reset, after.m_Structure, key});
            });
    }

    //! Adds a precommit owner/phase or domain rule; retain its connection for scoped removal.
    Connection AddValidator(std::function<void(const ModelSnapshot&)> validator);

    //! Validates derived work against final candidates before canonical publication.
    Connection AddChangeValidator(std::function<void(const ModelChange&)> validator);

    //! Prepares the effect of a canonical candidate through an adapter chain without publishing UI state.
    virtual ModelChange PreviewChange(const ModelChange& canonical) const;

  protected:
    //! Publishes a derived mapping while preserving its source domain and field impacts.
    bool PublishProjection(ModelSnapshot snapshot, const ModelChange& sourceChange, bool reordered);

  private:
    friend class SelectionModel;

    //! Distinguishes candidate state equality from change metadata.
    static bool Equivalent(const Detail::ModelState& left, const Detail::ModelState& right);

    //! Publishes a prepared candidate and reconciles registered selection states before commit.
    bool Publish(ModelSnapshot snapshot, std::vector<ModelField> fields, bool reset, bool structure,
                 std::optional<std::uint64_t> expectedRevision, std::optional<std::uint64_t> sourceRevision = {});

    //! Keeps only weak selection endpoints to avoid model/selection ownership cycles.
    std::vector<std::weak_ptr<SelectionModel>> m_Selections;

    //! Observes a store whose live identity is checked through state before use.
    StateStore& m_Store;

    //! Owns data and change metadata in one coherent transactional value.
    State<Detail::ModelState> m_State;

    //! Guards owner-side endpoint resolution after destruction.
    std::shared_ptr<bool> m_Alive = std::make_shared<bool>(true);

    //! Rejects foreign-thread destruction before changing endpoint lifetime data.
    ThreadAffinity m_Affinity;
};
} // namespace EStudioUI
