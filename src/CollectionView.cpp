#include <EStudioUI/CollectionView.h>
#include <EStudioUI/Bindings.h>

namespace EStudioUI
{
CollectionView::CollectionView(Widget& owner, std::shared_ptr<ItemModel> model,
                               std::function<void(Widget&, const ModelChange&)> changed,
                               std::function<void(Widget&)> selectionChanged, std::shared_ptr<SelectionModel> selection)
    : m_Owner(&owner), m_Model(std::move(model)), m_SelectionChanged(std::move(selectionChanged))
{
    Detail::BindingAccess::VerifyTargetMutation(owner);
    if (!m_Model)
        throw std::invalid_argument("A collection view requires a live shared model.");
    m_Model->VerifyAccess();
    auto guard = [target = m_Owner](const ModelSnapshot&)
    {
        if (auto* live = target.Get())
            Detail::BindingAccess::VerifyTargetMutation(*live);
    };
    m_Connections.emplace_back(m_Model->AddValidator(guard));
    if (const auto root = m_Model->GetRootModel(); root != m_Model)
        m_Connections.emplace_back(root->AddValidator(guard));
    m_Connections.emplace_back(m_Model->Subscribe(
        [target = m_Owner, changed = std::move(changed)](const ModelChange& change)
        {
            if (auto* live = target.Get())
            {
                const auto lease = Detail::BindingAccess::RetainOwner(*live);
                Detail::BindingAccess::VerifyTargetMutation(*live);
                changed(*live, change);
            }
        }));
    SetSelectionModel(selection ? std::move(selection) : SelectionModel::Create(m_Model));
}

CollectionView::~CollectionView() = default;

std::shared_ptr<ItemModel> CollectionView::GetModel() const
{
    m_Model->VerifyAccess();
    return m_Model;
}

std::shared_ptr<SelectionModel> CollectionView::GetSelectionModel() const
{
    m_Model->VerifyAccess();
    return m_Selection;
}

void CollectionView::SetSelectionModel(std::shared_ptr<SelectionModel> selection)
{
    auto* owner = m_Owner.Get();
    if (!owner)
        throw std::logic_error("Cannot attach selection to an expired collection view.");
    Detail::BindingAccess::VerifyTargetMutation(*owner);
    if (!selection || selection->GetModel() != m_Model->GetRootModel())
        throw std::invalid_argument("Shared selection must belong to this view's canonical model domain.");
    std::vector<ScopedConnection> connections;
    connections.emplace_back(selection->AddValidator(
        [target = m_Owner]
        {
            if (auto* live = target.Get())
                Detail::BindingAccess::VerifyTargetMutation(*live);
        }));
    connections.emplace_back(selection->Subscribe(
        [target = m_Owner, changed = m_SelectionChanged](const StateChange<SelectionState>&)
        {
            if (auto* live = target.Get())
            {
                const auto lease = Detail::BindingAccess::RetainOwner(*live);
                Detail::BindingAccess::VerifyTargetMutation(*live);
                changed(*live);
            }
        }));
    m_SelectionConnections = std::move(connections);
    m_Selection = std::move(selection);
}

void CollectionView::Select(ItemId item, std::size_t column, Modifiers modifiers, std::span<const ItemId> order)
{
    // Hold endpoints locally: selection listeners may delete the containing view.
    const auto selection = m_Selection;
    if (modifiers.m_Shift && !order.empty())
        selection->SelectRange(order, item, column, modifiers.m_Control);
    else
        selection->Select(item, column, modifiers.m_Control ? SelectionAction::Toggle : SelectionAction::Replace);
}
} // namespace EStudioUI
