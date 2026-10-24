#pragma once

#include "ItemModel.h"
#include "ModelQuery.h"

namespace EStudioUI
{
//! Records the explicit whole-model work of the most recent projection preparation.
struct ProjectionStats
{

    //! Source identities visited while constructing this mapping.
    std::size_t m_Scanned = 0;

    //! Raw typed comparisons performed while sorting sibling ranges.
    std::size_t m_Comparisons = 0;

    //! Indicates that a field-only update retained the previous identity mapping.
    bool m_ReusedMapping = false;
};

//! Shares source records through a sorted/filtered identity mapping, preserving its selection domain.
class SortFilterModel final : public ItemModel
{
  public:
    //! Creates an unsorted/unfiltered projection and a weak source subscription.
    static std::shared_ptr<SortFilterModel> Create(std::shared_ptr<ItemModel> source);

    //! Disconnects source delivery before releasing projection resources.
    ~SortFilterModel() override;

    //! Returns the canonical root revision for stale-work checks.
    std::uint64_t GetRevision() const override;

    //! Returns the original canonical domain shared by all compatible views.
    std::shared_ptr<ItemModel> GetRootModel() override;

    //! Returns the immediate shared source of this projection.
    std::shared_ptr<ItemModel> GetSourceModel() const override;

    //! Applies typed edits to canonical source records, including temporarily filtered identities.
    bool Apply(std::vector<ModelEdit> edits, std::optional<std::uint64_t> expectedRevision = {}) override;

    //! Forwards synchronous/asynchronous edit acknowledgement to the canonical source.
    ModelEditSession RequestEdit(std::vector<ModelEdit> edits,
                                 std::optional<std::uint64_t> expectedRevision = {}) override;

    //! Replaces canonical source data with the supplied revision policy.
    bool Replace(ModelSnapshot snapshot, std::optional<std::uint64_t> expectedRevision = {}) override;

    //! Performs a canonical structural batch without dropping filtered records.
    void Restructure(const std::function<void(ModelBuilder&)>& edit) override;

    //! Sorts each sibling range by a typed raw column; an empty column restores source order.
    void SetSort(std::optional<std::size_t> column, bool ascending = true);

    //! Applies ordered primary/secondary keys while preserving canonical identities.
    void SetSortKeys(std::vector<ModelSort> keys);

    //! Returns an owned description of the current sort keys.
    std::vector<ModelSort> GetSortKeys() const;

    //! Filters records with a pure predicate, retaining matching descendants' ancestors.
    void SetFilter(std::function<bool(const ModelSnapshot&, ItemId)> predicate,
                   std::vector<std::size_t> dependencies = {});

    //! Returns actual scan/comparison work of this projection's latest prepared snapshot.
    ProjectionStats GetPreparationStats() const;

    //! Validates and caches the complete derived mapping before canonical data commits.
    ModelChange PreviewChange(const ModelChange& canonical) const override;

  private:
    //! Captures the source and constructs the initial constant-time identity projection.
    explicit SortFilterModel(std::shared_ptr<ItemModel> source);

    //! Builds or reuses a mapping after a coherent source revision.
    void Refresh(const ModelChange& change);

    //! Identifies upstream rule changes independently of canonical data revisions.
    std::vector<std::uint64_t> SourceRules() const;

    //! Owns the immediate source while preserving canonical record ownership there.
    std::shared_ptr<ItemModel> m_Source;

    //! Retires delivery when this projection is destroyed.
    std::optional<ScopedConnection> m_SourceChanged;

    //! Rejects throwing predicates/comparators before the canonical model publishes.
    std::vector<ScopedConnection> m_SourceValidated;

    //! Identifies one pure prepared projection for the current rule snapshot.
    struct Prepared
    {
        //! Canonical candidate whose immutable storage was examined.
        ModelSnapshot m_Canonical;

        //! Rule-bearing projection snapshot used during preparation.
        ModelSnapshot m_Rules;

        //! Identifies upstream sort/filter policies, which can change without a source data revision.
        std::vector<std::uint64_t> m_SourceRules;

        //! Prepared derived change, ready for notification-phase publication.
        ModelChange m_Change;
    };

    //! Retains one immutable prepared candidate, replacing it on the next validation.
    mutable std::optional<Prepared> m_Prepared;
};
} // namespace EStudioUI
