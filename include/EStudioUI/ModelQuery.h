#pragma once

#include "ModelSnapshot.h"

namespace EStudioUI
{
//! Defines portable typed comparisons; null tests never depend on a magic string.
enum class ModelFilterOperator
{
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Contains,
    StartsWith,
    IsNull,
    IsNotNull
};

//! Owns one bounded predicate or an AND/OR group of child predicates.
struct ModelFilter
{

    //! Selects a schema column; an empty value denotes a group.
    std::optional<std::size_t> m_Column;

    //! Chooses the comparison of a leaf.
    ModelFilterOperator m_Operator = ModelFilterOperator::Equal;

    //! Owns a bound comparison value.
    ModelValue m_Value;

    //! Uses OR for groups; false uses AND.
    bool m_Any = false;

    //! Uses exact text case; false folds ASCII consistently with SQLite's built-ins.
    bool m_CaseSensitive = true;

    //! Owns child expressions; leaves cannot also contain children.
    std::vector<ModelFilter> m_Children;

    //! Compares owned filter expressions.
    bool operator==(const ModelFilter&) const = default;
};

//! Describes one ordered sort key independently of a particular data source.
struct ModelSort
{

    //! Refers to the original schema even after visual column reordering.
    std::size_t m_Column = 0;

    //! Selects ascending or descending raw ordering.
    bool m_Ascending = true;

    //! Places nulls after non-null values in either direction.
    bool m_NullsLast = false;

    //! Compares immutable query values.
    bool operator==(const ModelSort&) const = default;
};

//! Owns a server-side query and a bounded result window.
struct ModelQuery
{

    //! Stable source dataset identifier; never executable SQL.
    std::string m_DataSet;

    //! Owns the validated predicate tree.
    ModelFilter m_Filter;

    //! Owns primary through secondary sort keys.
    std::vector<ModelSort> m_Sort;

    //! Guards source schema changes; zero denotes an unversioned provider.
    std::uint64_t m_SchemaRevision = 0;

    //! Requests a result-window offset, not a widget row identity.
    std::uint64_t m_Offset = 0;

    //! Bounds loaded rows; supported range is 1..2048.
    std::size_t m_Limit = 256;

    //! Rejects excessive depth/size, invalid values and out-of-schema indices.
    void Validate(std::size_t columns = 2048) const;

    //! Compares request values without accessing a source.
    bool operator==(const ModelQuery&) const = default;
};

//! Validates once and owns a reusable worker-safe predicate for a compatible schema.
std::function<bool(const ModelSnapshot&, ItemId)> CompileModelFilter(ModelFilter filter, std::size_t columns);

//! Evaluates the same portable expression against an immutable in-memory snapshot.
bool MatchesModelFilter(const ModelSnapshot& snapshot, ItemId item, const ModelFilter& filter);
} // namespace EStudioUI
