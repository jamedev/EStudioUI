#include <EStudioUI/DockWidget.h>
#include <EStudioUI/DockManager.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/TextBuffer.h>

#include "WindowCommandButton.h"

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
//! Delivers captured grip coordinates; only the manager changes ownership after release.
class DockGrip final : public Label
{
  public:
    explicit DockGrip(std::string title) : Label(std::move(title), "dock-grip")
    {
        SetLayoutDirection(Axis::Horizontal);
        SetWidth(SizeRule::Fill());
        SetHeight(SizeRule::Fill());
        SetCursor(CursorShape::ResizeAll);
    }

    Signal<Point> m_Begin;
    Signal<Point> m_Move;
    Signal<Point> m_End;
    Signal<> m_Cancel;

  protected:
    void OnPointerDown(const PointerEvent& event) override
    {
        if (event.m_Button != PointerButton::Left ||
            !GetContext()->Platform().GetWindowMetrics().m_HasScreenCoordinates)
            return;
        m_Dragging = true;
        m_Begin.Emit(Screen(event.m_Position));
    }

    void OnPointerMove(const PointerEvent& event) override
    {
        if (m_Dragging)
            m_Move.Emit(Screen(event.m_Position));
    }

    void OnPointerUp(const PointerEvent& event) override
    {
        if (!m_Dragging)
            return;
        m_Dragging = false;
        m_End.Emit(Screen(event.m_Position));
    }

    void OnCaptureLost() override
    {
        if (!m_Dragging)
            return;
        m_Dragging = false;
        m_Cancel.Emit();
    }

  private:
    Point Screen(Point position) const
    {
        return GetContext()->Platform().GetWindowMetrics().ToScreen(GetContext()->ToWindowPoint(*this, position));
    }

    bool m_Dragging = false;
};
} // namespace

DockWidget::DockWidget(std::string id, std::string title)
    : Widget("dockwidget"), m_DockId(std::move(id)), m_Title(std::move(title))
{
    SetHeight(SizeRule::Fill());
    auto& header = Add<Widget>("dock-header");
    header.SetLayoutDirection(Axis::Horizontal);
    header.SetHeight(SizeRule::Fixed(30));
    auto& grip = header.Add<DockGrip>(m_Title);
    m_Caption = &grip;
    m_Float = &header.Add<Detail::WindowCommandButton>(Detail::WindowCommand::Float);
    m_Float->SetWidth(SizeRule::Fixed(28));
    m_Float->SetHeight(SizeRule::Fill());
    m_Float->SetAccessibleName("Float or dock panel");
    auto& close = header.Add<Detail::WindowCommandButton>(Detail::WindowCommand::Close);
    close.SetWidth(SizeRule::Fixed(28));
    close.SetHeight(SizeRule::Fill());
    close.SetAccessibleName("Close panel");
    m_Content = &Add<Widget>("dock-content");
    m_Content->SetHeight(SizeRule::Fill());
    SetAccessibleName(m_Title);
    const WidgetRef self(this);
    m_Float->Clicked().Connect(
        [self]
        {
            auto* live = dynamic_cast<DockWidget*>(self.Get());
            auto* manager = live ? dynamic_cast<DockManager*>(live->m_Manager.Get()) : nullptr;
            if (!manager)
                return;
            if (manager->IsFloating(*live))
                manager->Dock(*live, manager->GetDockArea(*live));
            else
                manager->Float(*live);
        });
    close.Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<DockWidget*>(self.Get()))
                live->Close();
        });
    grip.m_Begin.Connect(
        [self](Point point)
        {
            if (auto* live = dynamic_cast<DockWidget*>(self.Get()))
                if (auto* manager = dynamic_cast<DockManager*>(live->m_Manager.Get()))
                    manager->BeginDrag(*live, point);
        });
    grip.m_Move.Connect(
        [self](Point point)
        {
            if (auto* live = dynamic_cast<DockWidget*>(self.Get()))
                if (auto* manager = dynamic_cast<DockManager*>(live->m_Manager.Get()))
                    manager->UpdateDrag(point);
        });
    grip.m_End.Connect(
        [self](Point point)
        {
            if (auto* live = dynamic_cast<DockWidget*>(self.Get()))
                if (auto* manager = dynamic_cast<DockManager*>(live->m_Manager.Get()))
                    manager->EndDrag(point);
        });
    grip.m_Cancel.Connect(
        [self]
        {
            if (auto* live = dynamic_cast<DockWidget*>(self.Get()))
                if (auto* manager = dynamic_cast<DockManager*>(live->m_Manager.Get()))
                    manager->CancelDrag();
        });
}

const std::string& DockWidget::GetDockId() const
{
    VerifyAccess();
    return m_DockId;
}

const std::string& DockWidget::GetTitle() const
{
    VerifyAccess();
    return m_Title;
}

void DockWidget::SetTitle(std::string title)
{
    VerifyStructuralMutation();
    if (title.size() > 4096 || TextBuffer::Sanitize(title) != title)
        throw std::invalid_argument("A dock caption must be single-line UTF-8, at most 4096 bytes.");
    m_Title = std::move(title);
    m_Caption->SetText(m_Title);
    SetAccessibleName(m_Title);
    if (auto* manager = dynamic_cast<DockManager*>(m_Manager.Get()))
    {
        if (auto* host = manager->GetFloatingWindow(*this))
            host->SetTitle(m_Title);
        else
            manager->UpdateTitle(*this);
    }
}

Widget& DockWidget::Content() const
{
    VerifyAccess();
    return *m_Content;
}

bool DockWidget::IsFloating() const
{
    VerifyAccess();
    auto* manager = dynamic_cast<DockManager*>(m_Manager.Get());
    return manager && manager->IsFloating(*this);
}

bool DockWidget::IsOpen() const
{
    VerifyAccess();
    auto* manager = dynamic_cast<DockManager*>(m_Manager.Get());
    return manager && manager->IsDockVisible(*this);
}

void DockWidget::Close()
{
    VerifyStructuralMutation();
    if (auto* manager = dynamic_cast<DockManager*>(m_Manager.Get()))
        manager->SetDockVisible(*this, false);
}
} // namespace EStudioUI
