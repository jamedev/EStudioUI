#pragma once

#include "ItemModel.h"
#include "RecordModel.h"
#include "SelectionModel.h"
#include "SortFilterModel.h"

namespace EStudioUI
{
//! Moves ordinary C++ records into a shared typed model with member-based column adapters.
template <typename T, typename... Columns>
std::shared_ptr<ItemModel> MakeTableModel(StateStore& store, std::vector<T> records, Columns... columns)
{
    return std::make_shared<ItemModel>(
        store, MakeTableSnapshot(std::move(records), std::vector<RecordColumn<T>>{std::move(columns)...}));
}
} // namespace EStudioUI
