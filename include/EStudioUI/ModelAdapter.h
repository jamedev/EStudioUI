#pragma once

#include "ModelQuery.h"
#include <stop_token>

namespace EStudioUI
{
//! Owns a discoverable source dataset without UI/native objects.
struct ModelDataSet
{

    //! Stable source name and presentation caption.
    std::string m_Id, m_Title;

    //! Describes the source kind, such as table or view.
    std::string m_Kind;

    //! Identifies source schema changes independently of row-value changes.
    std::uint64_t m_SchemaRevision = 0;

    //! Owns the schema and typed editor metadata.
    std::vector<ModelColumn> m_Columns;

    //! Allows edits only when a source can address records unambiguously.
    bool m_Editable = false;

    //! Reports whether source keys retain identity across queries; unkeyed views expire IDs on refresh.
    bool m_StableKeys = true;
};

//! Owns one loaded record with its opaque, stable source key.
struct AdapterRow
{

    //! Encodes source identity independently of sorting/filtering and UI ItemId allocation.
    std::string m_Key;

    //! Owns exact typed field values, including null and binary values.
    std::vector<ModelValue> m_Values;

    //! Optional bounded presentation prepared on the worker by the bridge.
    std::vector<std::string> m_Display;
};

//! Owns one bounded page and its coherent query counts.
struct AdapterPage
{

    //! Describes the dataset represented by this result.
    ModelDataSet m_DataSet;

    //! Owns the actual query, including any clamped result offset.
    ModelQuery m_Query;

    //! Counts all source records before filtering.
    std::uint64_t m_TotalRows = 0;

    //! Counts records matching the filter.
    std::uint64_t m_MatchedRows = 0;

    //! Owns at most the requested window of records.
    std::vector<AdapterRow> m_Rows;
};

//! Owns one optimistic field write against a source key and exact old value.
struct AdapterEdit
{

    //! Identifies the record independently of page position.
    std::string m_Key;

    //! Identifies an original schema column.
    std::size_t m_Column = 0;

    //! Owns the exact value seen before the edit and its proposed replacement.
    ModelValue m_Original, m_Value;
};

//! Implements application data access on workers; snapshots/results may contain no GUI resources.
class ModelAdapter
{
  public:
    //! Releases an adapter without joining external work.

    virtual ~ModelAdapter();

    //! Discovers datasets on a worker; implementations must cooperate with cancellation.

    virtual std::vector<ModelDataSet> GetDataSets(std::stop_token stop) const = 0;

    //! Loads one bounded query window on a worker; concurrent calls must be safe.

    virtual AdapterPage Load(const ModelQuery& query, std::stop_token stop) const = 0;

    //! Atomically commits optimistic writes; the default adapter is read-only.

    virtual AdapterPage Commit(const ModelQuery& query, std::span<const AdapterEdit> edits, std::stop_token stop) const;
};
} // namespace EStudioUI
