#pragma once

#include "ModelSnapshot.h"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace EStudioUI
{
//! Exposes one field of an ordinary C++ record through an immutable model adapter.
template <typename T> struct RecordColumn
{

    //! Stores public formatting, editing, and sizing metadata.
    ModelColumn m_Schema;

    //! Reads an owned raw value from a record.
    std::function<ModelValue(const T&)> m_Get;

    //! Assigns a validated raw value to an isolated copied record.
    std::function<void(T&, const ModelValue&)> m_Set;

    //! Optionally supplies semantic roles such as foreground, enabled, or tooltip.
    std::function<ModelValue(const T&, ModelRole)> m_Role;
};

namespace Detail
{
//! Stores immutable identities and hierarchy separately from paged record values.
struct RecordIndex
{

    //! Lists IDs in storage order.
    std::vector<ItemId> m_Items;

    //! Maps IDs to storage offsets.
    std::unordered_map<ItemId, std::size_t> m_Offsets;

    //! Stores one parent per record.
    std::vector<ItemId> m_Parents;

    //! Stores sibling positions in storage order.
    std::vector<std::size_t> m_Positions;

    //! Stores child identities by parent, including the invisible root.
    std::unordered_map<ItemId, std::vector<ItemId>> m_Children;
};

//! Validates unique IDs, existing parents, and acyclic hierarchy in linear expected work.
std::shared_ptr<const RecordIndex> MakeRecordIndex(std::vector<ItemId> items, std::vector<ItemId> parents);

//! Owns records in copy-on-write pages and shares immutable schema and identity indexes.
template <typename T> class RecordData final : public ModelData
{
  public:
    //! Moves owned records into bounded pages, validating the complete initial dataset.
    RecordData(std::vector<T> records, std::vector<RecordColumn<T>> columns, std::vector<ItemId> items,
               std::vector<ItemId> parents)
        : m_Index(MakeRecordIndex(std::move(items), std::move(parents))),
          m_Columns(std::make_shared<const std::vector<RecordColumn<T>>>(std::move(columns)))
    {
        if (records.size() != m_Index->m_Items.size())
            throw std::invalid_argument("Record and identity counts must match.");
        for (const auto& column : *m_Columns)
        {
            if (!column.m_Get)
                throw std::invalid_argument("Every record column requires a getter.");
            m_Schema.push_back(column.m_Schema);
        }
        for (std::size_t first = 0; first < records.size(); first += s_PageSize)
        {
            auto page = std::make_shared<std::vector<T>>();
            const auto end = std::min(records.size(), first + s_PageSize);
            page->reserve(end - first);
            for (auto index = first; index < end; ++index)
            {
                for (const auto& column : *m_Columns)
                    ValidateModelValue(column.m_Schema, column.m_Get(records[index]));
                page->push_back(std::move(records[index]));
            }
            m_Pages.push_back(std::move(page));
        }
    }

    //! Returns immutable column metadata.
    const std::vector<ModelColumn>& GetColumns() const override
    {
        return m_Schema;
    }

    //! Returns the complete record count without reading values.
    std::size_t GetItemCount() const override
    {
        return m_Index->m_Items.size();
    }

    //! Reports the original C++ record type without reading values.
    const std::type_info& GetRecordType() const noexcept override
    {
        return typeid(T);
    }

    //! Resolves one immutable typed record retained by its owning page.
    const void* GetRecordAddress(ItemId item) const override
    {
        const auto offset = m_Index->m_Offsets.at(item);
        return &m_Pages[offset / s_PageSize]->at(offset % s_PageSize);
    }

    //! Returns one storage-order identity.
    ItemId GetItemByIndex(std::size_t index) const override
    {
        return m_Index->m_Items.at(index);
    }

    //! Looks up identity without scanning records.
    bool Contains(ItemId item) const override
    {
        return m_Index->m_Offsets.contains(item);
    }

    //! Returns a parent's immediate child count.
    std::size_t GetRowCount(ItemId parent) const override
    {
        const auto found = m_Index->m_Children.find(parent);
        return found == m_Index->m_Children.end() ? 0 : found->second.size();
    }

    //! Returns one child in stable sibling order.
    ItemId GetItem(std::size_t row, ItemId parent) const override
    {
        return m_Index->m_Children.at(parent).at(row);
    }

    //! Returns an existing record's parent.
    ItemId GetParent(ItemId item) const override
    {
        return m_Index->m_Parents.at(m_Index->m_Offsets.at(item));
    }

    //! Returns an identity's sibling position.
    std::optional<std::size_t> GetPosition(ItemId item) const override
    {
        const auto found = m_Index->m_Offsets.find(item);
        return found == m_Index->m_Offsets.end() ? std::nullopt
                                                 : std::optional<std::size_t>(m_Index->m_Positions[found->second]);
    }

    //! Reads one raw field or optional presentation role.
    ModelValue GetValue(ItemId item, std::size_t column, ModelRole role) const override
    {
        const auto offset = m_Index->m_Offsets.at(item);
        const auto& record = m_Pages[offset / s_PageSize]->at(offset % s_PageSize);
        const auto& field = m_Columns->at(column);
        if (role == ModelRole::Edit)
            return field.m_Get(record);
        if (field.m_Role)
        {
            auto value = field.m_Role(record, role);
            if (value.index())
                return value;
        }
        if (role == ModelRole::Display)
        {
            const auto raw = field.m_Get(record);
            return field.m_Schema.m_Format ? field.m_Schema.m_Format(raw) : FormatModelValue(raw);
        }
        if (role == ModelRole::Enabled)
            return true;
        if (role == ModelRole::Checked)
        {
            const auto value = field.m_Get(record);
            if (std::holds_alternative<bool>(value))
                return value;
        }
        return {};
    }

    //! Copies each touched page once while retaining the unchanged record pages and identity index.
    std::shared_ptr<const ModelData> WithEdits(std::span<const ModelEdit> edits) const override
    {
        auto result = std::make_shared<RecordData>(*this);
        std::map<std::pair<ItemId, std::size_t>, ModelValue> final;
        for (const auto& edit : edits)
        {
            if (edit.m_Role != ModelRole::Edit)
                throw std::invalid_argument("This record adapter supports raw Edit-role writes only.");
            final.insert_or_assign({edit.m_Item, edit.m_Column}, edit.m_Value);
        }
        std::unordered_map<std::size_t, std::shared_ptr<std::vector<T>>> changed;
        for (const auto& [key, value] : final)
        {
            const auto& column = m_Columns->at(key.second);
            if (!column.m_Schema.m_Editable || !column.m_Set)
                throw std::invalid_argument("This model column is read-only.");
            const auto offset = m_Index->m_Offsets.at(key.first);
            const auto pageIndex = offset / s_PageSize;
            auto& page = changed[pageIndex];
            if (!page)
            {
                page = std::make_shared<std::vector<T>>(*m_Pages[pageIndex]);
                result->m_Pages[pageIndex] = page;
            }
            column.m_Set(page->at(offset % s_PageSize), value);
        }
        return result;
    }

    //! Rebuilds hierarchy/order while retaining typed records and custom semantic-role accessors.
    std::shared_ptr<const ModelData> WithStructure(std::span<const ModelRow> rows) const override
    {
        std::vector<T> records;
        std::vector<ItemId> items, parents;
        records.reserve(rows.size());
        for (const auto& row : rows)
        {
            if (row.m_Values.size() != m_Columns->size())
                throw std::invalid_argument("Structural row values must match the original typed schema.");
            const auto found = m_Index->m_Offsets.find(row.m_Item);
            const bool existing = found != m_Index->m_Offsets.end();
            if (existing)
                records.push_back(m_Pages[found->second / s_PageSize]->at(found->second % s_PageSize));
            else if constexpr (std::is_same_v<T, std::vector<ModelValue>>)
                records.emplace_back(m_Columns->size());
            else if constexpr (std::is_default_constructible_v<T>)
                records.emplace_back();
            else
                throw std::logic_error(
                    "New records of this C++ type require an application-built typed replacement snapshot.");
            auto& record = records.back();
            for (std::size_t column = 0; column < m_Columns->size(); ++column)
            {
                const auto& field = (*m_Columns)[column];
                if (existing && field.m_Get(record) == row.m_Values[column])
                    continue;
                if (!field.m_Set)
                    throw std::logic_error("The typed record field has no structural assignment adapter.");
                field.m_Set(record, row.m_Values[column]);
            }
            items.push_back(row.m_Item);
            parents.push_back(row.m_Parent);
        }
        return std::make_shared<const RecordData>(std::move(records), *m_Columns, std::move(items), std::move(parents));
    }

  private:
    //! Bounds record copying for a field edit independently of total row count.
    static constexpr std::size_t s_PageSize = 256;

    //! Shares identity and hierarchy indexes across field-only edits.
    std::shared_ptr<const RecordIndex> m_Index;

    //! Shares pure record accessors across immutable versions.
    std::shared_ptr<const std::vector<RecordColumn<T>>> m_Columns;

    //! Exposes schema through the non-template storage interface.
    std::vector<ModelColumn> m_Schema;

    //! Owns immutable record pages, sharing untouched pages between snapshots.
    std::vector<std::shared_ptr<const std::vector<T>>> m_Pages;
};
} // namespace Detail

//! Creates a typed column from a C++ data member; policies remain customizable on the returned column.
template <typename T, typename V> RecordColumn<T> Column(std::string title, V T::* member)
{
    RecordColumn<T> result;
    result.m_Schema.m_Title = std::move(title);
    if constexpr (std::is_same_v<V, bool>)
    {
        result.m_Schema.m_ValueType = ModelValueType::Boolean;
    }
    else if constexpr (std::is_integral_v<V>)
    {
        result.m_Schema.m_ValueType = ModelValueType::Integer;
    }
    else if constexpr (std::is_floating_point_v<V>)
    {
        result.m_Schema.m_ValueType = ModelValueType::Real;
    }
    else if constexpr (std::is_same_v<V, std::string>)
    {
        result.m_Schema.m_ValueType = ModelValueType::Text;
    }
    else if constexpr (std::is_same_v<V, ModelBlob>)
    {
        result.m_Schema.m_ValueType = ModelValueType::Blob;
        result.m_Schema.m_EditorType = ModelValueType::Blob;
    }
    else
    {
        result.m_Schema.m_ValueType = ModelValueType::Color;
        result.m_Schema.m_Editable = false;
    }
    result.m_Get = [member](const T& record) -> ModelValue
    {
        if constexpr (std::is_integral_v<V> && !std::is_same_v<V, bool>)
        {
            if constexpr (std::is_unsigned_v<V>)
                if (record.*member > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                    throw std::out_of_range("Unsigned model value exceeds the signed 64-bit value domain.");
            return static_cast<std::int64_t>(record.*member);
        }
        else if constexpr (std::is_floating_point_v<V>)
        {
            return static_cast<double>(record.*member);
        }
        else
        {
            return record.*member;
        }
    };
    result.m_Set = [member](T& record, const ModelValue& value)
    {
        if constexpr (std::is_integral_v<V> && !std::is_same_v<V, bool>)
        {
            const auto number = std::get<std::int64_t>(value);
            if (static_cast<long double>(number) < static_cast<long double>(std::numeric_limits<V>::lowest()) ||
                static_cast<long double>(number) > static_cast<long double>(std::numeric_limits<V>::max()))
                throw std::out_of_range("Model edit exceeds the C++ field's integer range.");
            record.*member = static_cast<V>(number);
        }
        else if constexpr (std::is_floating_point_v<V>)
        {
            const double number = std::get<double>(value);
            if (number < static_cast<double>(std::numeric_limits<V>::lowest()) ||
                number > static_cast<double>(std::numeric_limits<V>::max()))
                throw std::out_of_range("Model edit exceeds the C++ field's floating-point range.");
            record.*member = static_cast<V>(number);
        }
        else
        {
            record.*member = std::get<V>(value);
        }
    };
    return result;
}

//! Moves typed records into immutable paged storage; callers may supply stable domain IDs and parent IDs.
template <typename T>
ModelSnapshot MakeTableSnapshot(std::vector<T> records, std::vector<RecordColumn<T>> columns,
                                std::vector<ItemId> items = {}, std::vector<ItemId> parents = {})
{
    if (items.empty())
        for (std::size_t i = 0; i < records.size(); ++i)
            items.push_back(AllocateItemId());
    if (parents.empty())
        parents.resize(records.size());
    return ModelSnapshot(std::make_shared<const Detail::RecordData<T>>(std::move(records), std::move(columns),
                                                                       std::move(items), std::move(parents)));
}
} // namespace EStudioUI
