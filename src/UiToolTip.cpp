#include <EStudioUI/UiContext.h>
#include <EStudioUI/ToolTip.h>
#include <algorithm>
#include <cmath>

namespace EStudioUI
{
void UiContext::DismissToolTip()
{
    m_ToolTipDeadline = -1;
    m_ToolTipHideAt = -1;
    if (auto* popup = m_ToolTipPopup.Get(); popup && IsPopupOpen(*popup))
        ClosePopup(*popup);
}

void UiContext::ArmToolTip(Widget* preferred)
{
    if (m_ToolTipOpening || m_Deactivating || m_CompositionClient.Get())
        return;
    Widget* target = preferred;
    if (!target)
        for (const auto& ref : m_HoverPath)
            if (auto* candidate = ref.Get(); candidate && !candidate->m_ToolTip.empty())
            {
                target = candidate;
                break;
            }
    while (target && target->m_ToolTip.empty())
        target = LogicalParent(*target);
    if (target && (!target->IsVisible() || !target->IsEnabled()))
        target = nullptr;
    if (target == m_ToolTipTarget.Get())
        return;
    const WidgetRef reference(target);
    DismissToolTip();
    target = reference.Get();
    if (target && (target->GetContext() != this || !target->IsVisible() || !target->IsEnabled()))
        target = nullptr;
    m_ToolTipTarget = WidgetRef(target);
    if (target && !target->m_ToolTip.empty())
        m_ToolTipDeadline = GetTime() + target->m_ToolTipDelay;
}

void UiContext::UpdateToolTip(double time)
{
    VerifyAccess();
    if (m_CompositionClient.Get())
    {
        DismissToolTip();
        m_ToolTipTarget = {};
        return;
    }
    auto* target = m_ToolTipTarget.Get();
    if (!target || !target->IsVisible() || !target->IsEnabled() || target->m_ToolTip.empty())
    {
        DismissToolTip();
        m_ToolTipTarget = {};
        return;
    }
    if (m_ToolTipHideAt >= 0 && time >= m_ToolTipHideAt)
    {
        DismissToolTip();
        return;
    }
    if (m_ToolTipDeadline < 0 || time < m_ToolTipDeadline)
        return;
    m_ToolTipDeadline = -1;
    if (m_Deactivating || m_Capture.Get())
        return;
    Widget* parent = nullptr;
    if (!m_Popups.empty())
    {
        parent = m_Popups.back().m_Widget.Get();
        if (!parent || !IsWithin(target, parent))
            return;
    }
    auto* popup = dynamic_cast<ToolTip*>(m_ToolTipPopup.Get());
    if (!popup)
    {
        auto owned = std::make_unique<ToolTip>();
        popup = owned.get();
        AddOwnedPopup(std::move(owned), *target);
        m_ToolTipPopup = WidgetRef(popup);
    }
    else
    {
        // Recreate logical ownership when help moves to another control.
        m_Root.RemoveChild(*popup);
        auto owned = std::make_unique<ToolTip>();
        popup = owned.get();
        AddOwnedPopup(std::move(owned), *target);
        m_ToolTipPopup = WidgetRef(popup);
    }
    popup->SetText(target->m_ToolTip);
    const auto b = target->GetBounds();
    auto anchor = ToWindowPoint(*target, {b.m_X, b.m_Y + b.m_Height + 8});
    if (m_PointerPosition && target->IsHovered())
        anchor = {m_PointerPosition->m_X + 12, m_PointerPosition->m_Y + 20};
    PopupOptions options;
    options.m_Parent = parent;
    options.m_TakeFocus = false;
    options.m_RestoreFocus = false;
    options.m_InputTransparent = true;
    m_ToolTipOpening = true;
    try
    {
        ShowPopup(*popup, anchor, options);
        m_ToolTipOpening = false;
        m_ToolTipHideAt = time + 10;
    }
    catch (...)
    {
        m_ToolTipOpening = false;
        throw;
    }
}

double UiContext::NextToolTipDeadline() const
{
    VerifyAccess();
    return m_ToolTipHideAt >= 0 ? m_ToolTipHideAt : m_ToolTipDeadline;
}
} // namespace EStudioUI
