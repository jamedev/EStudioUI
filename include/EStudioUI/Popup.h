#pragma once

#include "PopupSession.h"
#include "Widget.h"

namespace EStudioUI
{
//! An owned, asynchronous interactive surface for menus, popovers and custom content.
class Popup : public Widget
{
  public:
    //! Creates a hidden surface; attach it to the UI root or use Create.
    explicit Popup(std::string type = "popup");

    //! Creates a root-owned surface whose lifetime follows its logical owner.
    static Popup& Create(Widget& owner);

    //! Opens at a window origin; an already open presentation returns its current session.
    PopupSession Open(Point origin, PopupOptions options = {});

    //! Tracks an anchor through layout/resize, flipping at viewport edges when possible.
    PopupSession Open(Widget& anchor, PopupPlacement placement = PopupPlacement::Below, PopupOptions options = {});

    //! Completes this presentation and its descendants without blocking or pumping events.
    void Close(PopupResult result = {});

    //! Returns whether this surface currently belongs to the popup stack.
    bool IsOpen() const;
};
} // namespace EStudioUI
