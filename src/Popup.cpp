#include <EStudioUI/Popup.h>
#include <EStudioUI/UiContext.h>

namespace EStudioUI
{
Popup::Popup(std::string type) : Widget(std::move(type))
{
    SetVisible(false);
}

Popup& Popup::Create(Widget& owner)
{
    owner.VerifyAccess();
    if (!owner.GetContext())
        throw std::logic_error("Attach the popup owner before creating its popup.");
    return static_cast<Popup&>(owner.GetContext()->AddOwnedPopup(std::make_unique<Popup>(), owner));
}

PopupSession Popup::Open(Point origin, PopupOptions options)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!GetContext())
        throw std::logic_error("Attach the popup to a UI root before opening it.");
    if (IsOpen())
        return GetContext()->GetPopupSession(*this);
    return GetContext()->ShowPopup(*this, origin, options);
}

PopupSession Popup::Open(Widget& anchor, PopupPlacement placement, PopupOptions options)
{
    VerifyStructuralMutation();
    anchor.VerifyAccess();
    options.m_AnchorWidget = &anchor;
    options.m_Placement = placement;
    return Open(Point{}, options);
}

void Popup::Close(PopupResult result)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (IsOpen())
        GetContext()->ClosePopup(*this, result);
}

bool Popup::IsOpen() const
{
    VerifyAccess();
    return GetContext() && GetContext()->IsPopupOpen(*this);
}
} // namespace EStudioUI
