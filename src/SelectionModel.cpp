#include <EStudioUI/SelectionModel.h>

#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
SelectionModel::SelectionModel(std::shared_ptr<ItemModel> model)
    : m_Model(std::move(model)),
      m_State(m_Model->GetStore(), SelectionState{},
              [model = m_Model](const SelectionState& value)
              {
                  const auto snapshot = model->GetSnapshot();
                  if ((value.m_Current && !snapshot.Contains(value.m_Current)) ||
                      (value.m_Anchor && !snapshot.Contains(value.m_Anchor)) ||
                      (value.m_Current && value.m_Column >= snapshot.GetColumns().size()))
                      throw std::invalid_argument(
                          "Selection refers to an absent item or column in its canonical model.");
                  if (value.m_Mode == SelectionMode::Single && value.m_Items.size() > 1)
                      throw std::invalid_argument("Single selection cannot contain multiple items.");
                  for (const auto item : value.m_Items)
                      if (!snapshot.Contains(item))
                          throw std::invalid_argument("Selected identities must belong to the canonical model.");
              })
{
}

SelectionModel::~SelectionModel() = default;

std::shared_ptr<SelectionModel> SelectionModel::Create(std::shared_ptr<ItemModel> model)
{
    if (!model)
        throw std::invalid_argument("Selection requires a live shared model.");
    auto root = model->GetRootModel();
    auto result = std::shared_ptr<SelectionModel>(new SelectionModel(root));
    root->m_Selections.push_back(result);
    return result;
}

std::shared_ptr<ItemModel> SelectionModel::GetModel() const
{
    m_Model->VerifyAccess();
    return m_Model;
}

std::shared_ptr<const SelectionState> SelectionModel::GetSnapshot() const
{
    return m_State.GetSnapshot();
}

ItemId SelectionModel::GetCurrent() const
{
    return m_State.Get().m_Current;
}

std::size_t SelectionModel::GetColumn() const
{
    return m_State.Get().m_Column;
}

bool SelectionModel::IsSelected(ItemId item) const
{
    return m_State.Get().m_Items.contains(item);
}

SelectionMode SelectionModel::GetMode() const
{
    return m_State.Get().m_Mode;
}

void SelectionModel::SetMode(SelectionMode mode)
{
    auto value = m_State.Get();
    value.m_Mode = mode;
    if (mode == SelectionMode::Single && value.m_Items.size() > 1)
    {
        const bool retain = value.m_Items.contains(value.m_Current);
        value.m_Items.clear();
        if (retain)
            value.m_Items.insert(value.m_Current);
    }
    m_State.Set(std::move(value));
}

void SelectionModel::Select(ItemId item, std::size_t column, SelectionAction action)
{
    if (!item)
    {
        Clear();
        return;
    }
    auto value = m_State.Get();
    const bool wasSelected = value.m_Items.contains(item);
    if (action == SelectionAction::Replace || value.m_Mode == SelectionMode::Single)
        value.m_Items.clear();
    if (action == SelectionAction::Toggle && wasSelected)
        value.m_Items.erase(item);
    else
        value.m_Items.insert(item);
    value.m_Current = value.m_Anchor = item;
    value.m_Column = column;
    m_State.Set(std::move(value));
}

void SelectionModel::SelectRange(std::span<const ItemId> order, ItemId target, std::size_t column, bool additive)
{
    if (GetMode() == SelectionMode::Single)
    {
        Select(target, column);
        return;
    }
    auto value = m_State.Get();
    const auto end = std::find(order.begin(), order.end(), target);
    if (end == order.end())
        throw std::invalid_argument("Range target must be visible in the supplied view order.");
    auto begin = std::find(order.begin(), order.end(), value.m_Anchor);
    if (begin == order.end())
    {
        begin = end;
        value.m_Anchor = target;
    }
    if (!additive)
        value.m_Items.clear();
    const auto first = std::min(begin, end), last = std::max(begin, end);
    value.m_Items.insert(first, last + 1);
    value.m_Current = target;
    value.m_Column = column;
    m_State.Set(std::move(value));
}

void SelectionModel::Clear()
{
    SelectionState value;
    value.m_Mode = m_State.Get().m_Mode;
    m_State.Set(std::move(value));
}

void SelectionModel::SetItems(std::vector<ItemId> items, ItemId current, std::size_t column)
{
    SelectionState value;
    value.m_Mode = m_State.Get().m_Mode;
    value.m_Items.insert(items.begin(), items.end());
    value.m_Current = value.m_Anchor = current ? current : items.empty() ? 0 : items.front();
    value.m_Column = value.m_Current ? column : 0;
    m_State.Set(std::move(value));
}

void SelectionModel::Prune(const ModelSnapshot& candidate)
{
    auto value = m_State.Get();
    std::erase_if(value.m_Items, [&](ItemId item) { return !candidate.Contains(item); });
    if (value.m_Current && !candidate.Contains(value.m_Current))
        value.m_Current = 0;
    if (value.m_Anchor && !candidate.Contains(value.m_Anchor))
        value.m_Anchor = 0;
    if (!value.m_Current || value.m_Column >= candidate.GetColumns().size())
        value.m_Column = 0;
    m_State.Set(std::move(value));
}

Connection SelectionModel::AddValidator(std::function<void()> validator)
{
    return m_State.AddValidator([validator = std::move(validator)](const SelectionState&) { validator(); });
}
} // namespace EStudioUI
