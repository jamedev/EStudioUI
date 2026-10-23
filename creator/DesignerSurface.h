#pragma once

#include <EStudioUI/UiDocument.h>

namespace EStudioUI
{
//! A real framework preview with a captured design overlay, independent of preview widgets.
class DesignerSurface final : public Widget
{
  public:
    //! Creates a preview host and a last-painted selection overlay.
    DesignerSurface();

    //! Returns the ordinary runtime build host.
    Widget& Host() const;

    //! Updates stable widget observations after a successful build.
    void SetWidgets(UiBuildResult result, std::vector<std::string> order);

    //! Selects an observed design identity.
    void SetSelected(std::string id);

    //! Switches between authoring and live widget interaction.
    void SetDesignMode(bool design);

    //! Reports selected design identities.
    Signal<const std::string&>& Selected();

    //! Reports captured move/resize gestures as complete offsets from their initial geometry.
    Signal<const std::string&, Point, bool, bool>& GeometryEdited();

  protected:
    //! Places the input overlay over the ordinary host.
    void OnLayout() override;

  private:
    //! Owned preview host observation.
    Widget* m_Host = nullptr;

    //! Owned captured overlay observation.
    Widget* m_Overlay = nullptr;

    //! Actual runtime widgets with weak lifetimes.
    UiBuildResult m_Built;

    //! Stable painter-order design identities.
    std::vector<std::string> m_Order;

    //! Current stable selection.
    std::string m_Selected;

    //! Reports stable selections.
    Signal<const std::string&> m_SelectedSignal;

    //! Reports geometry gestures.
    Signal<const std::string&, Point, bool, bool> m_GeometryEdited;
};
} // namespace EStudioUI
