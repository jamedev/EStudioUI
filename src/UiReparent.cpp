#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
void UiContext::PrepareReparent(Widget& subtree)
{
    VerifyStructuralMutation();
    const WidgetRef root(&subtree);
    const auto owns = [&](Widget* candidate)
    {
        if (!candidate)
            return false;
        for (auto* node = candidate; node; node = LogicalParent(*node))
            if (node == root.Get())
                return true;
        return false;
    };
    for (unsigned turn = 0; turn < 64; ++turn)
    {
        if (!root.Get())
            return;
        bool closed = false;
        for (std::size_t index = 0; index < m_Popups.size(); ++index)
            if (owns(m_Popups[index].m_Widget.Get()) || owns(m_Popups[index].m_AnchorWidget.Get()))
            {
                ClosePopupBranch(index, {PopupCloseReason::Hidden, 0});
                closed = true;
                break;
            }
        if (closed)
            continue;
        if (owns(m_Focus.Get()))
        {
            SetFocus(nullptr);
            continue;
        }
        if (owns(m_Capture.Get()))
        {
            const auto captured = m_Capture;
            m_Capture = {};
            if (auto* live = captured.Get())
                live->OnCaptureLost();
            continue;
        }
        return;
    }
    throw std::logic_error("Input retirement exceeded 64 reentrant focus, popup or capture changes.");
}

void UiContext::TransferOwnedPopups(Widget& subtree, UiContext& destination)
{
    VerifyStructuralMutation();
    destination.VerifyStructuralMutation();
    std::vector<OwnedPopup> moving;
    bool found = true;
    while (found)
    {
        found = false;
        for (auto entry = m_OwnedPopups.begin(); entry != m_OwnedPopups.end(); ++entry)
        {
            if (std::any_of(moving.begin(), moving.end(),
                            [&](const OwnedPopup& known) { return known.m_Popup.Get() == entry->m_Popup.Get(); }))
                continue;
            auto* owner = entry->m_Owner.Get();
            bool belongs = false;
            for (auto* node = owner; node; node = node->GetParent())
                if (node == &subtree || std::any_of(moving.begin(), moving.end(), [node](const OwnedPopup& record)
                                                    { return record.m_Popup.Get() == node; }))
                {
                    belongs = true;
                    break;
                }
            if (belongs)
            {
                moving.push_back(*entry);
                found = true;
            }
        }
    }
    destination.m_OwnedPopups.reserve(destination.m_OwnedPopups.size() + moving.size());
    destination.m_Root.m_Children.reserve(destination.m_Root.m_Children.size() + moving.size());
    for (const auto& entry : moving)
    {
        auto* popup = entry.m_Popup.Get();
        auto* owner = entry.m_Owner.Get();
        if (!popup || !owner)
            continue;
        const auto item = std::find_if(m_Root.m_Children.begin(), m_Root.m_Children.end(),
                                       [popup](const auto& child) { return child.get() == popup; });
        if (item == m_Root.m_Children.end())
            throw std::logic_error("An owned popup lost its root ownership during transfer.");
        auto owned = std::move(*item);
        m_Root.m_Children.erase(item);
        popup->Attach(&destination, &destination.m_Root);
        popup->m_Node.m_Parent = &owner->m_Node;
        destination.m_Root.m_Children.push_back(std::move(owned));
        destination.m_OwnedPopups.push_back(entry);
        std::erase_if(m_OwnedPopups, [&](const OwnedPopup& old) { return old.m_Popup.Get() == popup; });
    }
    m_Root.Invalidate(Invalidation::Structure);
    destination.m_Root.Invalidate(Invalidation::Structure);
}
} // namespace EStudioUI
