#include <EStudioUI/SnapshotModelAdapter.h>
#include <algorithm>

namespace EStudioUI
{
SnapshotModelAdapter::SnapshotModelAdapter(std::string name, ModelSnapshot snapshot) : m_Snapshot(std::move(snapshot))
{
    ModelQuery query;
    query.m_DataSet = name;
    query.Validate(m_Snapshot.GetColumns().size());
    m_DataSet.m_Id = m_DataSet.m_Title = std::move(name);
    m_DataSet.m_Kind = "snapshot";
    m_DataSet.m_Columns = m_Snapshot.GetColumns();
    for (auto& column : m_DataSet.m_Columns)
    {
        column.m_Editable = false;
    }
}

SnapshotModelAdapter::~SnapshotModelAdapter() = default;

std::vector<ModelDataSet> SnapshotModelAdapter::GetDataSets(std::stop_token stop) const
{
    if (stop.stop_requested())
    {
        throw std::runtime_error("Snapshot discovery cancelled.");
    }
    return {m_DataSet};
}

AdapterPage SnapshotModelAdapter::Load(const ModelQuery& query, std::stop_token stop) const
{
    query.Validate(m_DataSet.m_Columns.size());
    if (query.m_DataSet != m_DataSet.m_Id)
    {
        throw std::invalid_argument("Unknown snapshot dataset.");
    }
    const auto matches = CompileModelFilter(query.m_Filter, m_DataSet.m_Columns.size());
    std::vector<ItemId> selected;
    for (std::size_t index = 0; index < m_Snapshot.GetItemCount(); ++index)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Snapshot query cancelled.");
        }
        const auto item = m_Snapshot.GetItemByIndex(index);
        if (matches(m_Snapshot, item))
        {
            selected.push_back(item);
        }
    }
    if (!query.m_Sort.empty())
    {
        std::stable_sort(selected.begin(), selected.end(),
                         [&](ItemId a, ItemId b)
                         {
                             if (stop.stop_requested())
                             {
                                 throw std::runtime_error("Snapshot sorting cancelled.");
                             }
                             for (const auto& key : query.m_Sort)
                             {
                                 const auto left = m_Snapshot.GetValue(a, key.m_Column),
                                            right = m_Snapshot.GetValue(b, key.m_Column);
                                 const bool leftNull = !left.index(), rightNull = !right.index();
                                 if (leftNull != rightNull)
                                 {
                                     return key.m_NullsLast ? !leftNull : leftNull;
                                 }
                                 const auto order = CompareModelValues(left, right);
                                 if (order)
                                 {
                                     return key.m_Ascending ? order < 0 : order > 0;
                                 }
                             }
                             return false;
                         });
    }
    AdapterPage page;
    page.m_DataSet = m_DataSet;
    page.m_Query = query;
    page.m_TotalRows = m_Snapshot.GetItemCount();
    page.m_MatchedRows = selected.size();
    if (page.m_Query.m_Offset >= selected.size())
    {
        page.m_Query.m_Offset = selected.empty() ? 0 : ((selected.size() - 1) / query.m_Limit) * query.m_Limit;
    }
    const auto end = std::min(selected.size(), static_cast<std::size_t>(page.m_Query.m_Offset) + query.m_Limit);
    for (auto row = static_cast<std::size_t>(page.m_Query.m_Offset); row < end; ++row)
    {
        AdapterRow result;
        result.m_Key = std::to_string(selected[row]);
        for (std::size_t column = 0; column < m_DataSet.m_Columns.size(); ++column)
        {
            result.m_Values.push_back(m_Snapshot.GetValue(selected[row], column));
        }
        page.m_Rows.push_back(std::move(result));
    }
    return page;
}
} // namespace EStudioUI
