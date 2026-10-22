#include <EStudioUI/ModelBuilder.h>
#include <EStudioUI/RecordModel.h>

#include <unordered_set>

namespace EStudioUI
{
ModelBuilder::ModelBuilder(std::vector<ModelColumn> columns) : m_Columns(std::move(columns))
{
}

ModelBuilder::ModelBuilder(const ModelSnapshot& snapshot) : m_Columns(snapshot.GetColumns())
{
    m_Origin = snapshot;
    m_Rows.reserve(snapshot.GetItemCount());
    for (std::size_t index = 0; index < snapshot.GetItemCount(); ++index)
    {
        const auto item = snapshot.GetItemByIndex(index);
        std::vector<ModelValue> row;
        for (std::size_t column = 0; column < m_Columns.size(); ++column)
            row.push_back(snapshot.GetValue(item, column));
        Add(std::move(row), snapshot.GetParent(item), item);
    }
    m_Changed = false;
}

ItemId ModelBuilder::Add(std::vector<ModelValue> values, ItemId parent, ItemId item)
{
    if (values.size() != m_Columns.size())
        throw std::invalid_argument("Model rows must match the complete column schema.");
    if (!item)
        item = AllocateItemId();
    m_Changed = true;
    m_Rows.push_back(std::move(values));
    m_Items.push_back(item);
    m_Parents.push_back(parent);
    return item;
}

void ModelBuilder::Remove(ItemId item)
{
    const auto index = Detail::MakeRecordIndex(m_Items, m_Parents);
    if (!index->m_Offsets.contains(item))
        throw std::out_of_range("Cannot remove a missing model item.");
    m_Changed = true;
    std::unordered_set<ItemId> removed;
    std::vector<ItemId> pending{item};
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        removed.insert(current);
        if (const auto found = index->m_Children.find(current); found != index->m_Children.end())
            pending.insert(pending.end(), found->second.begin(), found->second.end());
    }
    std::size_t next = 0;
    for (std::size_t i = 0; i < m_Items.size(); ++i)
    {
        if (removed.contains(m_Items[i]))
            continue;
        if (next != i)
        {
            m_Rows[next] = std::move(m_Rows[i]);
            m_Items[next] = m_Items[i];
            m_Parents[next] = m_Parents[i];
        }
        ++next;
    }
    m_Rows.resize(next);
    m_Items.resize(next);
    m_Parents.resize(next);
}

void ModelBuilder::Move(ItemId item, ItemId parent, ItemId before)
{
    const auto index = Detail::MakeRecordIndex(m_Items, m_Parents);
    if (!index->m_Offsets.contains(item) || (parent && !index->m_Offsets.contains(parent)))
        throw std::out_of_range("Model move requires existing item and parent IDs.");
    if (before == item)
        return;
    if (before && (!index->m_Offsets.contains(before) || m_Parents[index->m_Offsets.at(before)] != parent))
        throw std::invalid_argument("Model move anchor must be a child of the destination parent.");
    for (auto ancestor = parent; ancestor; ancestor = m_Parents[index->m_Offsets.at(ancestor)])
        if (ancestor == item)
            throw std::invalid_argument("A model move cannot create a hierarchy cycle.");
    const auto from = index->m_Offsets.at(item);
    if (m_Parents[from] == parent)
    {
        const auto position = index->m_Positions[from];
        const auto& siblings = index->m_Children.at(parent);
        if ((!before && position + 1 == siblings.size()) ||
            (before && position + 1 < siblings.size() && siblings[position + 1] == before))
            return;
    }
    m_Changed = true;
    auto to = before ? index->m_Offsets.at(before) : m_Items.size();
    auto row = std::move(m_Rows[from]);
    m_Rows.erase(m_Rows.begin() + static_cast<std::ptrdiff_t>(from));
    m_Items.erase(m_Items.begin() + static_cast<std::ptrdiff_t>(from));
    m_Parents.erase(m_Parents.begin() + static_cast<std::ptrdiff_t>(from));
    if (to > from)
        --to;
    m_Rows.insert(m_Rows.begin() + static_cast<std::ptrdiff_t>(to), std::move(row));
    m_Items.insert(m_Items.begin() + static_cast<std::ptrdiff_t>(to), item);
    m_Parents.insert(m_Parents.begin() + static_cast<std::ptrdiff_t>(to), parent);
}

ModelSnapshot ModelBuilder::Build() &&
{
    if (m_Origin && !m_Changed)
        return *m_Origin;
    if (m_Origin)
    {
        std::vector<ModelRow> rows;
        rows.reserve(m_Items.size());
        for (std::size_t i = 0; i < m_Items.size(); ++i)
            rows.push_back({m_Items[i], m_Parents[i], std::move(m_Rows[i])});
        return ModelSnapshot(m_Origin->GetData()->WithStructure(rows));
    }
    using Row = std::vector<ModelValue>;
    std::vector<RecordColumn<Row>> columns;
    for (std::size_t index = 0; index < m_Columns.size(); ++index)
    {
        RecordColumn<Row> column;
        column.m_Schema = std::move(m_Columns[index]);
        column.m_Get = [index](const Row& row) { return row.at(index); };
        column.m_Set = [index](Row& row, const ModelValue& value) { row.at(index) = value; };
        columns.push_back(std::move(column));
    }
    return MakeTableSnapshot(std::move(m_Rows), std::move(columns), std::move(m_Items), std::move(m_Parents));
}
} // namespace EStudioUI
