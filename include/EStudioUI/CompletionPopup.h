#pragma once
#include "ListWidget.h"
#include "Popup.h"

namespace EStudioUI
{
//! One owned completion result; IDs are application identities, independent of row positions.
struct CompletionItem
{
    //! Stable application key.
    std::string m_Id;
    //! Text inserted on acceptance.
    std::string m_Text;
    //! Optional explanation shown beside the caption.
    std::string m_Detail;
};

//! Reusable bounded completion presentation using a virtualized canonical list model.
class CompletionPopup : public Popup
{
  public:
    //! Creates a hidden popup with at most 256 result rows.
    CompletionPopup();
    //! Creates a popup owned logically by an attached control.
    static CompletionPopup& Create(Widget& owner);
    //! Atomically validates and replaces suggestions; IDs must be unique and nonempty.
    void SetItems(std::vector<CompletionItem> items);
    //! Returns the owned current suggestions.
    const std::vector<CompletionItem>& GetItems() const;
    //! Opens below an editor while preserving its focus and input.
    PopupSession Show(Widget& anchor);
    //! Moves the highlighted result, clamping at the result boundaries.
    void Move(int delta);
    //! Accepts the highlighted result and closes before notifying observers.
    bool Accept();
    //! Reports an owned accepted value, safe across closure or deletion callbacks.
    Signal<CompletionItem>& Accepted();
    //! Exposes the virtual list for styling and advanced selection.
    ListWidget& GetList();

  private:
    //! Virtual result list.
    ListWidget* m_List = nullptr;
    //! Current owned result values.
    std::vector<CompletionItem> m_Items;
    //! Stable canonical list model for one attached popup.
    std::shared_ptr<ItemModel> m_Model;
    //! Rejects superseded reentrant presentation work.
    std::uint64_t m_Revision = 0;
    //! Acceptance observers.
    Signal<CompletionItem> m_Accepted;
};
} // namespace EStudioUI
