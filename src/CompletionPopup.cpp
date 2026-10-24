#include <EStudioUI/CompletionPopup.h>
#include <EStudioUI/Models.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace EStudioUI
{
CompletionPopup::CompletionPopup() : Popup("completionpopup")
{
    SetWidth(SizeRule::Fixed(360));
    SetHeight(SizeRule::Fixed(224));
    m_List = &Add<ListWidget>();
    m_List->SetWidth(SizeRule::Fill());
    m_List->SetHeight(SizeRule::Fill());
    const WidgetRef self(this);
    auto accept = [self](int)
    {
        if (auto* live = dynamic_cast<CompletionPopup*>(self.Get()))
        {
            live->Accept();
        }
    };
    m_List->Activated().Connect(accept);
    m_List->ItemClicked().Connect(accept);
}

CompletionPopup& CompletionPopup::Create(Widget& owner)
{
    if (!owner.GetContext())
    {
        throw std::logic_error("Attach the completion owner before creating its popup.");
    }
    return static_cast<CompletionPopup&>(owner.GetContext()->AddOwnedPopup(std::make_unique<CompletionPopup>(), owner));
}

void CompletionPopup::SetItems(std::vector<CompletionItem> items)
{
    VerifyStructuralMutation();
    if (!GetContext())
    {
        throw std::logic_error("Attach a completion popup before supplying results.");
    }
    if (items.size() > 256)
    {
        throw std::length_error("Completion results are limited to 256 entries.");
    }
    std::unordered_set<std::string> ids;
    std::size_t bytes = 0;
    for (const auto& item : items)
    {
        bytes += item.m_Id.size() + item.m_Text.size() + item.m_Detail.size();
        if (item.m_Id.empty() || !ids.insert(item.m_Id).second)
        {
            throw std::invalid_argument("Completion IDs must be unique and nonempty.");
        }
    }
    if (bytes > 262144)
    {
        throw std::length_error("Completion results are limited to 256 KiB.");
    }
    const auto snapshot = MakeTableSnapshot(
        items, std::vector<RecordColumn<CompletionItem>>{Column("Suggestion", &CompletionItem::m_Text),
                                                         Column("Detail", &CompletionItem::m_Detail)});
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    const auto revision = ++m_Revision;
    auto previous = std::move(m_Items);
    m_Items = std::move(items);
    try
    {
        if (!m_Model)
        {
            m_Model = std::make_shared<ItemModel>(GetContext()->States(), snapshot);
            m_List->SetModel(m_Model);
        }
        else
        {
            m_Model->Replace(snapshot);
        }
    }
    catch (...)
    {
        if (auto* live = dynamic_cast<CompletionPopup*>(self.Get()); live && live->m_Revision == revision)
        {
            live->m_Items = std::move(previous);
        }
        throw;
    }
    if (auto* live = dynamic_cast<CompletionPopup*>(self.Get()); live && live->m_Revision == revision)
    {
        live->m_List->SetCurrentIndex(live->m_Items.empty() ? -1 : 0);
    }
}

const std::vector<CompletionItem>& CompletionPopup::GetItems() const
{
    VerifyAccess();
    return m_Items;
}

PopupSession CompletionPopup::Show(Widget& anchor)
{
    VerifyStructuralMutation();
    if (m_Items.empty())
    {
        return {};
    }
    PopupOptions options;
    options.m_TakeFocus = false;
    options.m_AllowOwnerInput = true;
    SetWidth(SizeRule::Fixed(std::max(240.0f, anchor.GetBounds().m_Width)));
    return Open(anchor, PopupPlacement::Below, options);
}

void CompletionPopup::Move(int delta)
{
    VerifyStructuralMutation();
    if (!m_Items.empty())
    {
        m_List->SetCurrentIndex(std::clamp(m_List->GetCurrentIndex() + delta, 0, static_cast<int>(m_Items.size()) - 1));
    }
}

bool CompletionPopup::Accept()
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const auto index = m_List->GetCurrentIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= m_Items.size())
    {
        return false;
    }
    const auto item = m_Items[static_cast<std::size_t>(index)];
    const WidgetRef self(this);
    Close({PopupCloseReason::Accepted, index});
    if (auto* live = dynamic_cast<CompletionPopup*>(self.Get()))
    {
        live->m_Accepted.Emit(item);
    }
    return true;
}

Signal<CompletionItem>& CompletionPopup::Accepted()
{
    VerifyAccess();
    return m_Accepted;
}

ListWidget& CompletionPopup::GetList()
{
    VerifyAccess();
    return *m_List;
}
} // namespace EStudioUI
