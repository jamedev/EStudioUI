#include <EStudioUI/ModelSnapshot.h>
#include <EStudioUI/RecordModel.h>
#include <EStudioUI/UiDispatcher.h>

#include <map>
#include <stdexcept>

namespace EStudioUI
{
namespace Detail
{
std::shared_ptr<const RecordIndex> MakeRecordIndex(std::vector<ItemId> items, std::vector<ItemId> parents)
{
    if (parents.size() != items.size())
        throw std::invalid_argument("Model parent and identity counts must match.");
    auto result = std::make_shared<RecordIndex>();
    result->m_Items = std::move(items);
    result->m_Parents = std::move(parents);
    result->m_Positions.reserve(result->m_Items.size());
    result->m_Offsets.reserve(result->m_Items.size());
    for (std::size_t index = 0; index < result->m_Items.size(); ++index)
    {
        const auto item = result->m_Items[index];
        if (!item || !result->m_Offsets.emplace(item, index).second)
            throw std::invalid_argument("Model item IDs must be unique and nonzero.");
        auto& siblings = result->m_Children[result->m_Parents[index]];
        result->m_Positions.push_back(siblings.size());
        siblings.push_back(item);
    }
    std::vector<unsigned char> marks(result->m_Items.size());
    for (std::size_t index = 0; index < result->m_Items.size(); ++index)
    {
        std::vector<std::size_t> path;
        auto current = result->m_Items[index];
        while (current)
        {
            const auto found = result->m_Offsets.find(current);
            if (found == result->m_Offsets.end())
                throw std::invalid_argument("Every model parent must exist in the same snapshot.");
            const auto offset = found->second;
            if (marks[offset] == 2)
                break;
            if (marks[offset] == 1)
                throw std::invalid_argument("Model hierarchy cannot contain a cycle.");
            marks[offset] = 1;
            path.push_back(offset);
            current = result->m_Parents[offset];
        }
        for (const auto offset : path)
            marks[offset] = 2;
    }
    return result;
}
} // namespace Detail

std::shared_ptr<const ModelData> ModelData::WithEdits(std::span<const ModelEdit>) const
{
    throw std::logic_error("This external model snapshot is read-only; provide an immutable WithEdits adapter.");
}

std::shared_ptr<const ModelData> ModelData::WithStructure(std::span<const ModelRow>) const
{
    throw std::logic_error(
        "This external snapshot is structurally read-only; publish an application-built replacement.");
}

const std::type_info& ModelData::GetRecordType() const noexcept
{
    return typeid(void);
}

const void* ModelData::GetRecordAddress(ItemId) const
{
    throw std::bad_cast();
}

ModelSnapshot::ModelSnapshot()
{
    static const auto empty = std::make_shared<const Detail::RecordData<std::vector<ModelValue>>>(
        std::vector<std::vector<ModelValue>>{}, std::vector<RecordColumn<std::vector<ModelValue>>>{},
        std::vector<ItemId>{}, std::vector<ItemId>{});
    m_Data = empty;
}

ModelSnapshot::ModelSnapshot(std::shared_ptr<const ModelData> data) : m_Data(std::move(data))
{
    if (!m_Data)
        throw std::invalid_argument("A model snapshot requires owned immutable storage.");
}

const std::shared_ptr<const ModelData>& ModelSnapshot::GetData() const noexcept
{
    return m_Data;
}

const std::vector<ModelColumn>& ModelSnapshot::GetColumns() const
{
    return m_Data->GetColumns();
}

std::size_t ModelSnapshot::GetItemCount() const
{
    return m_Data->GetItemCount();
}

ItemId ModelSnapshot::GetItemByIndex(std::size_t index) const
{
    return m_Data->GetItemByIndex(index);
}

bool ModelSnapshot::Contains(ItemId item) const
{
    return item && m_Data->Contains(item);
}

std::size_t ModelSnapshot::GetRowCount(ItemId parent) const
{
    return m_Data->GetRowCount(parent);
}

ItemId ModelSnapshot::GetItem(std::size_t row, ItemId parent) const
{
    return m_Data->GetItem(row, parent);
}

ItemId ModelSnapshot::GetParent(ItemId item) const
{
    return m_Data->GetParent(item);
}

std::optional<std::size_t> ModelSnapshot::GetPosition(ItemId item) const
{
    return m_Data->GetPosition(item);
}

ModelValue ModelSnapshot::GetValue(ItemId item, std::size_t column, ModelRole role) const
{
    return m_Data->GetValue(item, column, role);
}

std::string ModelSnapshot::GetText(ItemId item, std::size_t column) const
{
    const auto displayed = GetValue(item, column, ModelRole::Display);
    if (displayed.index())
        return FormatModelValue(displayed);
    const auto raw = GetValue(item, column);
    const auto& schema = GetColumns().at(column);
    return schema.m_Format ? schema.m_Format(raw) : FormatModelValue(raw);
}

ModelSnapshot ModelSnapshot::WithEdits(std::span<const ModelEdit> edits) const
{
    auto result = StageEdits(edits);
    for (const auto& edit : edits)
        for (std::size_t column = 0; column < GetColumns().size(); ++column)
            ValidateModelValue(GetColumns()[column], result.GetValue(edit.m_Item, column));
    return result;
}

ModelSnapshot ModelSnapshot::StageEdits(std::span<const ModelEdit> edits) const
{
    std::map<std::tuple<ItemId, std::size_t, ModelRole>, ModelEdit> final;
    for (const auto& edit : edits)
        final.insert_or_assign({edit.m_Item, edit.m_Column, edit.m_Role}, edit);
    std::vector<ModelEdit> changed;
    for (auto& [key, edit] : final)
    {
        (void)key;
        if (!Contains(edit.m_Item))
            throw std::out_of_range("Cannot edit a missing model item.");
        const auto& column = GetColumns().at(edit.m_Column);
        if (!column.m_Editable)
            throw std::invalid_argument("This model column is read-only.");
        if (GetValue(edit.m_Item, edit.m_Column, edit.m_Role) != edit.m_Value)
            changed.push_back(std::move(edit));
    }
    return changed.empty() ? *this : ModelSnapshot(m_Data->WithEdits(changed));
}

bool ModelSnapshot::operator==(const ModelSnapshot& other) const noexcept
{
    return m_Data == other.m_Data;
}

ItemId AllocateItemId()
{
    return UiDispatcher::AllocateTargetKey();
}
} // namespace EStudioUI
