#include "DesignerSurface.h"

#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <utility>

namespace EStudioUI
{
namespace
{
class DesignOverlay final : public Widget
{
  public:
    DesignOverlay() : Widget("design-overlay")
    {
    }

    void Refresh()
    {
        RequestRepaint();
    }

    std::function<void(Point)> m_Select;
    std::function<Rect()> m_Bounds;
    std::function<void(Point, bool, bool)> m_Edit;
    std::function<Rect()> m_Drop;
    std::function<std::vector<DesignGuide>()> m_Guides;
    std::function<void()> m_Cancel;

    bool IsFocusable() const override
    {
        return true;
    }

  protected:
    void Paint(Renderer& renderer, FontFace&, double, float opacity) const override
    {
        auto color = GetStyle().m_Color;
        color.m_A *= opacity;
        const auto area = GetBounds();
        if (m_Guides)
        {
            for (const auto& guide : m_Guides())
            {
                renderer.DrawLine(guide.m_X ? Point{guide.m_Position, area.m_Y} : Point{area.m_X, guide.m_Position},
                                  guide.m_X ? Point{guide.m_Position, area.m_Y + area.m_Height}
                                            : Point{area.m_X + area.m_Width, guide.m_Position},
                                  color, 1, GetClip());
            }
        }
        if (m_Drop)
        {
            const auto drop = m_Drop();
            if (drop.m_Width > 0 && drop.m_Height > 0)
            {
                Style highlight;
                highlight.m_Background = color;
                highlight.m_Background.m_A = .08f;
                highlight.m_BorderColor = color;
                highlight.m_BorderWidth = 2;
                renderer.DrawBox(drop, highlight, GetClip(), opacity);
            }
        }
        const auto bounds = m_Bounds ? m_Bounds() : Rect{};
        if (bounds.m_Width <= 0 || bounds.m_Height <= 0)
        {
            return;
        }
        renderer.DrawLine({bounds.m_X, bounds.m_Y}, {bounds.m_X + bounds.m_Width, bounds.m_Y}, color, 2, GetClip());
        renderer.DrawLine({bounds.m_X, bounds.m_Y}, {bounds.m_X, bounds.m_Y + bounds.m_Height}, color, 2, GetClip());
        renderer.DrawLine({bounds.m_X + bounds.m_Width, bounds.m_Y},
                          {bounds.m_X + bounds.m_Width, bounds.m_Y + bounds.m_Height}, color, 2, GetClip());
        renderer.DrawLine({bounds.m_X, bounds.m_Y + bounds.m_Height},
                          {bounds.m_X + bounds.m_Width, bounds.m_Y + bounds.m_Height}, color, 2, GetClip());
        Style handle;
        handle.m_Background = color;
        renderer.DrawBox({bounds.m_X + bounds.m_Width - 7, bounds.m_Y + bounds.m_Height - 7, 10, 10}, handle, GetClip(),
                         opacity);
    }

    void OnPointerDown(const PointerEvent& event) override
    {
        const auto old = m_Bounds ? m_Bounds() : Rect{};
        m_Resize = Rect{old.m_X + old.m_Width - 10, old.m_Y + old.m_Height - 10, 16, 16}.Contains(event.m_Position);
        m_Start = event.m_Position;
        m_Dragging = true;
        if (!m_Resize && m_Select)
        {
            m_Select(event.m_Position);
        }
    }

    void OnPointerMove(const PointerEvent& event) override
    {
        if (m_Dragging && m_Edit)
        {
            m_Edit({event.m_Position.m_X - m_Start.m_X, event.m_Position.m_Y - m_Start.m_Y}, m_Resize, false);
        }
    }

    void OnPointerUp(const PointerEvent& event) override
    {
        const bool dragging = std::exchange(m_Dragging, false);
        if (dragging && m_Edit)
        {
            m_Edit({event.m_Position.m_X - m_Start.m_X, event.m_Position.m_Y - m_Start.m_Y}, m_Resize, true);
        }
    }

    void OnCaptureLost() override
    {
        const bool active = std::exchange(m_Dragging, false);
        if (active && m_Cancel)
        {
            m_Cancel();
        }
    }

    void OnKey(Key key, bool down, bool, Modifiers) override
    {
        if (key == Key::Escape && down)
        {
            OnCaptureLost();
        }
    }

  private:
    Point m_Start;
    bool m_Dragging = false, m_Resize = false;
};
} // namespace

DesignerSurface::DesignerSurface() : Widget("designer-surface")
{
    SetWidth(SizeRule::Fill());
    SetHeight(SizeRule::Fill());
    SetStyleProperty(StyleProperty::ClipContent, true);
    m_Host = &Add<Widget>("design-host");
    m_Host->SetWidth(SizeRule::Fill());
    m_Host->SetHeight(SizeRule::Fill());
    auto& overlay = Add<DesignOverlay>();
    m_Overlay = &overlay;
    overlay.SetFloatingBounds({});
    const WidgetRef self(this);
    overlay.m_Cancel = [self]
    {
        if (auto* live = dynamic_cast<DesignerSurface*>(self.Get()))
        {
            live->m_Cancelled.Emit();
        }
    };
    overlay.m_Drop = [self]
    {
        const auto* live = dynamic_cast<DesignerSurface*>(self.Get());
        if (!live)
        {
            return Rect{};
        }
        const auto found = live->m_Containers.find(live->m_DropTarget);
        return found == live->m_Containers.end() || !found->second.Get() ? Rect{}
                                                                         : found->second.Get()->GetContentBounds();
    };
    overlay.m_Guides = [self]
    {
        const auto* live = dynamic_cast<DesignerSurface*>(self.Get());
        return live ? live->m_Guides : std::vector<DesignGuide>{};
    };
    overlay.m_Bounds = [self]
    {
        const auto* live = dynamic_cast<DesignerSurface*>(self.Get());
        if (!live)
        {
            return Rect{};
        }
        const auto found = live->m_Built.m_Widgets.find(live->m_Selected);
        return found == live->m_Built.m_Widgets.end() || !found->second.Get() ? Rect{}
                                                                              : found->second.Get()->GetBounds();
    };
    overlay.m_Select = [self](Point point)
    {
        auto* live = dynamic_cast<DesignerSurface*>(self.Get());
        if (!live)
        {
            return;
        }
        for (auto item = live->m_Order.rbegin(); item != live->m_Order.rend(); ++item)
        {
            const auto found = live->m_Built.m_Widgets.find(*item);
            auto* widget = found == live->m_Built.m_Widgets.end() ? nullptr : found->second.Get();
            if (widget && widget->IsVisible() &&
                widget->GetBounds().Contains(live->GetContext()->ToLayoutPoint(*widget, point)) &&
                widget->GetClip().Contains(point))
            {
                live->SetSelected(*item);
                const auto id = *item;
                live->m_SelectedSignal.Emit(id);
                return;
            }
        }
    };
    overlay.m_Edit = [self](Point delta, bool resize, bool finished)
    {
        if (auto* live = dynamic_cast<DesignerSurface*>(self.Get()))
        {
            const auto id = live->m_Selected;
            live->m_GeometryEdited.Emit(id, delta, resize, finished);
        }
    };
}

Widget& DesignerSurface::Host() const
{
    VerifyAccess();
    return *m_Host;
}

void DesignerSurface::SetWidgets(UiBuildResult result, std::vector<std::string> order,
                                 std::unordered_map<std::string, WidgetRef> containers)
{
    VerifyStructuralMutation();
    m_Built = std::move(result);
    m_Order = std::move(order);
    m_Containers = std::move(containers);
    RequestRepaint();
}

std::string DesignerSurface::ContainerAt(Point point) const
{
    VerifyAccess();
    if (!m_Overlay->IsVisible() || !GetClip().Contains(point))
    {
        return {};
    }
    for (auto id = m_Order.rbegin(); id != m_Order.rend(); ++id)
    {
        const auto found = m_Containers.find(*id);
        auto* content = found == m_Containers.end() ? nullptr : found->second.Get();
        bool visible = content != nullptr;
        for (auto* ancestor = content; ancestor; ancestor = ancestor->GetParent())
        {
            visible = visible && ancestor->IsVisible();
        }
        if (visible && content->GetClip().Contains(point) &&
            content->GetContentBounds().Contains(GetContext()->ToLayoutPoint(*content, point)))
        {
            return *id;
        }
    }
    return {};
}

Point DesignerSurface::ContainerPoint(std::string_view id, Point point) const
{
    VerifyAccess();
    const auto found = m_Containers.find(std::string(id));
    auto* content = found == m_Containers.end() ? nullptr : found->second.Get();
    if (!content)
    {
        throw std::invalid_argument("The drop container expired.");
    }
    const auto local = GetContext()->ToLayoutPoint(*content, point);
    const auto bounds = content->GetContentBounds();
    return {local.m_X - bounds.m_X, local.m_Y - bounds.m_Y};
}

void DesignerSurface::SetDropTarget(std::string id)
{
    VerifyStructuralMutation();
    if (m_DropTarget != id)
    {
        m_DropTarget = std::move(id);
        static_cast<DesignOverlay*>(m_Overlay)->Refresh();
    }
}

void DesignerSurface::SetGuides(std::vector<DesignGuide> guides)
{
    VerifyStructuralMutation();
    if (guides.size() > 2)
    {
        throw std::invalid_argument("At most two design guides are displayed.");
    }
    m_Guides = std::move(guides);
    static_cast<DesignOverlay*>(m_Overlay)->Refresh();
}

const std::vector<DesignGuide>& DesignerSurface::GetGuides() const
{
    VerifyAccess();
    return m_Guides;
}

Signal<>& DesignerSurface::GestureCancelled()
{
    return m_Cancelled;
}

void DesignerSurface::SetSelected(std::string id)
{
    VerifyStructuralMutation();
    m_Selected = std::move(id);
    RequestRepaint();
}

void DesignerSurface::SetDesignMode(bool design)
{
    VerifyStructuralMutation();
    m_Overlay->SetVisible(design);
}

Signal<const std::string&>& DesignerSurface::Selected()
{
    return m_SelectedSignal;
}

Signal<const std::string&, Point, bool, bool>& DesignerSurface::GeometryEdited()
{
    return m_GeometryEdited;
}

void DesignerSurface::OnLayout()
{
    m_Overlay->SetFloatingBounds(GetContentBounds());
}
} // namespace EStudioUI
