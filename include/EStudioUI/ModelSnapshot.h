#pragma once

#include "ModelValue.h"

#include <cstdint>
#include <memory>
#include <span>
#include <typeinfo>
#include <vector>

namespace EStudioUI
{
//! Stable identity in one canonical model domain; zero identifies its invisible root.
using ItemId = std::uint64_t;

//! One raw field edit applied atomically with the other edits in its batch.
struct ModelEdit
{
    //! Identifies the existing record independently of its row position.
    ItemId m_Item = 0;

    //! Identifies its zero-based schema column.
    std::size_t m_Column = 0;

    //! Owns the proposed typed value.
    ModelValue m_Value;

    //! Selects the semantic field; built-in record adapters edit ModelRole::Edit.
    ModelRole m_Role = ModelRole::Edit;
};

//! Owns a complete row for an explicit structural rebuild.
struct ModelRow
{
    //! Stable row identity.
    ItemId m_Item = 0;

    //! Parent identity, or zero for the invisible root.
    ItemId m_Parent = 0;

    //! Raw values in schema column order.
    std::vector<ModelValue> m_Values;
};

//! Immutable application-owned model storage; const queries must be pure and safe for concurrent readers.
class ModelData
{
  public:
    //! Releases data on the last reader's thread; UI/native resources must stay outside snapshots.
    virtual ~ModelData() = default;

    //! Returns the immutable schema.
    virtual const std::vector<ModelColumn>& GetColumns() const = 0;

    //! Returns the number of all records, including collapsed descendants.
    virtual std::size_t GetItemCount() const = 0;

    //! Returns one record in stable storage traversal order.
    virtual ItemId GetItemByIndex(std::size_t index) const = 0;

    //! Returns whether this snapshot contains an identity.
    virtual bool Contains(ItemId item) const = 0;

    //! Returns the child count; zero addresses top-level records.
    virtual std::size_t GetRowCount(ItemId parent = 0) const = 0;

    //! Returns a child identity, throwing for invalid positions.
    virtual ItemId GetItem(std::size_t row, ItemId parent = 0) const = 0;

    //! Returns the record's parent, or zero for a top-level record.
    virtual ItemId GetParent(ItemId item) const = 0;

    //! Returns its sibling position, or an empty value if absent.
    virtual std::optional<std::size_t> GetPosition(ItemId item) const = 0;

    //! Returns an owned typed field or an empty value for an unsupported role.
    virtual ModelValue GetValue(ItemId item, std::size_t column, ModelRole role = ModelRole::Edit) const = 0;

    //! Produces edited immutable storage; the default adapter is read-only.
    virtual std::shared_ptr<const ModelData> WithEdits(std::span<const ModelEdit> edits) const;

    //! Rebuilds records while retaining application-specific accessors; the default storage is structurally read-only.
    virtual std::shared_ptr<const ModelData> WithStructure(std::span<const ModelRow> rows) const;

    //! Reports the optional concrete C++ record type; the default adapter exposes field values only.
    virtual const std::type_info& GetRecordType() const noexcept;

    //! Resolves an immutable record address retained by this storage; advanced wrappers may forward it.
    virtual const void* GetRecordAddress(ItemId item) const;
};

//! An owned immutable collection snapshot, usable on workers without resolving a UI model.
class ModelSnapshot
{
  public:
    //! Creates an empty, zero-column snapshot.
    ModelSnapshot();

    //! Takes ownership of immutable application-provided storage.
    explicit ModelSnapshot(std::shared_ptr<const ModelData> data);

    //! Returns immutable storage for advanced adapters and cost-aware instrumentation.
    const std::shared_ptr<const ModelData>& GetData() const noexcept;

    //! Returns the immutable field schema.
    const std::vector<ModelColumn>& GetColumns() const;

    //! Returns the number of all records.
    std::size_t GetItemCount() const;

    //! Returns a record in storage traversal order.
    ItemId GetItemByIndex(std::size_t index) const;

    //! Tests record presence without view-specific filtering or selection.
    bool Contains(ItemId item) const;

    //! Returns the number of children of one parent.
    std::size_t GetRowCount(ItemId parent = 0) const;

    //! Returns one child identity.
    ItemId GetItem(std::size_t row, ItemId parent = 0) const;

    //! Returns one record's parent.
    ItemId GetParent(ItemId item) const;

    //! Returns a record's sibling position if it exists.
    std::optional<std::size_t> GetPosition(ItemId item) const;

    //! Reads an owned typed value.
    ModelValue GetValue(ItemId item, std::size_t column, ModelRole role = ModelRole::Edit) const;

    //! Formats one field through its schema, preserving typed raw data.
    std::string GetText(ItemId item, std::size_t column = 0) const;

    //! Returns an owned C++ record copy from a compatible typed adapter.
    template <typename T> T GetRecord(ItemId item) const
    {
        if (m_Data->GetRecordType() != typeid(T))
            throw std::bad_cast();
        const auto* record = static_cast<const T*>(m_Data->GetRecordAddress(item));
        if (!record)
            throw std::bad_cast();
        return *record;
    }

    //! Produces a snapshot with all final field edits validated before storage changes.
    ModelSnapshot WithEdits(std::span<const ModelEdit> edits) const;

    //! Compares immutable storage identity without scanning model records.
    bool operator==(const ModelSnapshot& other) const noexcept;

  private:
    friend class ItemModel;

    //! Builds structurally representable candidates for final validation by the enclosing StateStore transaction.
    ModelSnapshot StageEdits(std::span<const ModelEdit> edits) const;

    //! Owns immutable storage without any widget or event-loop references.
    std::shared_ptr<const ModelData> m_Data;
};

//! Allocates a nonzero process-unique item identity, including from worker builders.
ItemId AllocateItemId();
} // namespace EStudioUI
