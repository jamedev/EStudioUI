#pragma once

#include <EStudioUI/UiDocument.h>
#include "DesignSnap.h"

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
    void SetWidgets(UiBuildResult result, std::vector<std::string> order,
                    std::unordered_map<std::string, WidgetRef> containers = {});

    //! Resolves the deepest visible content slot at a window-coordinate point.
    std::string ContainerAt(Point point) const;
    //! Returns local content coordinates for a selected drop container.
    Point ContainerPoint(std::string_view id, Point point) const;
    //! Shows or clears the current palette drop target.
    void SetDropTarget(std::string id);
    //! Publishes bounded alignment feedback without changing canonical geometry.
    void SetGuides(std::vector<DesignGuide> guides);
    //! Returns visible guides for inspection and automation.
    const std::vector<DesignGuide>& GetGuides() const;
    //! Reports cancelled geometry gestures without committing history.
    Signal<>& GestureCancelled();

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

    //! Observes registered content slots rather than assuming every widget is a container.
    std::unordered_map<std::string, WidgetRef> m_Containers;
    //! Owns the current drop target identity.
    std::string m_DropTarget;
    //! Owns at most two alignment guides.
    std::vector<DesignGuide> m_Guides;
    //! Reports capture cancellation.
    Signal<> m_Cancelled;

    //! Current stable selection.
    std::string m_Selected;

    //! Reports stable selections.
    Signal<const std::string&> m_SelectedSignal;

    //! Reports geometry gestures.
    Signal<const std::string&, Point, bool, bool> m_GeometryEdited;
};
} // namespace EStudioUI
