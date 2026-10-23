#include "DesignerSurface.h"

#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>

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

    std::function<void(Point)> m_Select;
    std::function<Rect()> m_Bounds;
    std::function<void(Point, bool, bool)> m_Edit;

  protected:
    void Paint(Renderer& renderer, FontFace&, double, float opacity) const override
    {
        const auto bounds = m_Bounds ? m_Bounds() : Rect{};
        if (bounds.m_Width <= 0 || bounds.m_Height <= 0)
        {
            return;
        }
        const Color color{0.35f, 0.8f, 1, opacity};
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
        if (!m_Resize && m_Select)
        {
            m_Select(event.m_Position);
        }
        m_Start = event.m_Position;
        m_Dragging = true;
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
        if (m_Dragging && m_Edit)
        {
            m_Edit({event.m_Position.m_X - m_Start.m_X, event.m_Position.m_Y - m_Start.m_Y}, m_Resize, true);
        }
        m_Dragging = false;
    }

    void OnCaptureLost() override
    {
        m_Dragging = false;
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

void DesignerSurface::SetWidgets(UiBuildResult result, std::vector<std::string> order)
{
    VerifyStructuralMutation();
    m_Built = std::move(result);
    m_Order = std::move(order);
    RequestRepaint();
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
