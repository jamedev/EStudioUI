#pragma once

#include "ItemModel.h"

#include <set>

namespace EStudioUI
{
//! Selects the user interaction policy without changing item identity semantics.
enum class SelectionMode
{
    Single,
    Multiple
};

//! Selects how one user action changes the current selected set.
enum class SelectionAction
{
    Replace,
    Add,
    Toggle
};

//! Owns one coherent selection value, independently of view scrolling or expansion.
struct SelectionState
{
    //! Selected canonical item identities, including temporarily filtered items.
    std::set<ItemId> m_Items;

    //! Current keyboard/editing identity, which may be unselected after a toggle.
    ItemId m_Current = 0;

    //! Current schema column, independent of item ordering.
    std::size_t m_Column = 0;

    //! Anchor identity for Shift-range selection.
    ItemId m_Anchor = 0;

    //! User single/multiple-selection policy.
    SelectionMode m_Mode = SelectionMode::Single;

    //! Suppresses equal selection publications.
    bool operator==(const SelectionState&) const = default;
};

//! Shares canonical selected identities across compatible views and sort/filter adapters.
class SelectionModel : public std::enable_shared_from_this<SelectionModel>
{
  public:
    //! Creates selection in the canonical model's store and registers transactional pruning.
    static std::shared_ptr<SelectionModel> Create(std::shared_ptr<ItemModel> model);

    //! Releases receiver-bound subscriptions on its owner.
    ~SelectionModel();

    //! Returns the canonical owning model domain.
    std::shared_ptr<ItemModel> GetModel() const;

    //! Captures an owned immutable selection snapshot on the owner.
    std::shared_ptr<const SelectionState> GetSnapshot() const;

    //! Returns the current item identity.
    ItemId GetCurrent() const;

    //! Returns the current editing column.
    std::size_t GetColumn() const;

    //! Tests selection without copying the selected set.
    bool IsSelected(ItemId item) const;

    //! Returns the current interaction policy.
    SelectionMode GetMode() const;

    //! Changes the policy; switching to single retains only the current selected item.
    void SetMode(SelectionMode mode);

    //! Applies a canonical item action, updating current and anchor coherently.
    void Select(ItemId item, std::size_t column = 0, SelectionAction action = SelectionAction::Replace);

    //! Selects an inclusive range in one view's visible order, retaining hidden selections when additive.
    void SelectRange(std::span<const ItemId> order, ItemId target, std::size_t column = 0, bool additive = false);

    //! Replaces a complete selected set once; zero current chooses its first supplied identity.
    void SetItems(std::vector<ItemId> items, ItemId current = 0, std::size_t column = 0);

    //! Clears selected, current, and anchor identities in one publication.
    void Clear();

    //! Adds a lifetime-bound view phase precondition to selection commits.
    Connection AddValidator(std::function<void()> validator);

    //! Observes coherent selection commits; returned connections are persistent until disconnected.
    template <typename Callback> Connection Subscribe(Callback callback)
    {
        return m_State.Subscribe(std::move(callback));
    }

  private:
    friend class ItemModel;

    //! Initializes canonical selection; use Create to register its weak model endpoint.
    explicit SelectionModel(std::shared_ptr<ItemModel> model);

    //! Removes missing identities in the model's active transaction before any observer runs.
    void Prune(const ModelSnapshot& candidate);

    //! Owns the canonical model independently of any individual view.
    std::shared_ptr<ItemModel> m_Model;

    //! Publishes selected/current/anchor changes in the same store as model data.
    State<SelectionState> m_State;
};
} // namespace EStudioUI
