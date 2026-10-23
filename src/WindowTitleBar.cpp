#include <EStudioUI/WindowTitleBar.h>
#include <EStudioUI/ToolButton.h>

#include "OverflowStrip.h"
#include "WindowCommandButton.h"

namespace EStudioUI
{
WindowTitleBar::WindowTitleBar(Window& window) : Widget("window-titlebar")
{
    SetLayoutDirection(Axis::Horizontal);
    SetHeight(SizeRule::Fixed(38));
    SetGap(0);
    SetWindowDragRegion(true);
    auto& menuViewport = Add<Detail::OverflowStrip>("titlebar-menus");
    menuViewport.SetWidth(SizeRule::Fill(1, 0, 540));
    menuViewport.SetHeight(SizeRule::Fill());
    menuViewport.SetWindowDragRegion(true);
    m_Menus = &menuViewport.Add<MenuBar>();
    m_Menus->SetWidth(SizeRule::Content());
    m_Menus->SetHeight(SizeRule::Fill());
    m_Menus->SetWindowDragRegion(true);
    m_Content = &Add<Detail::OverflowStrip>("titlebar-content");
    m_Content->SetLayoutDirection(Axis::Horizontal);
    m_Content->SetWidth(SizeRule::Fill());
    m_Content->SetHeight(SizeRule::Fill());
    m_Content->SetWindowDragRegion(true);
    m_Caption = &m_Content->Add<Label>(window.GetTitle());
    m_Caption->SetWidth(SizeRule::Content());
    m_Caption->SetHeight(SizeRule::Fill());
    m_Caption->SetWindowDragRegion(true);
    m_Minimize = &Add<Detail::WindowCommandButton>(Detail::WindowCommand::Minimize, true);
    m_Maximize = &Add<Detail::WindowCommandButton>(Detail::WindowCommand::Maximize, true);
    m_Close = &Add<Detail::WindowCommandButton>(Detail::WindowCommand::Close, true);
    for (auto* button : {m_Minimize, m_Maximize, m_Close})
    {
        button->AddClass("caption-button");
        button->SetWidth(SizeRule::Fixed(46));
        button->SetHeight(SizeRule::Fill());
        button->SetVisible(window.GetTitleBarSupport() != PlatformSupport::Unsupported);
    }
    m_Close->AddClass("caption-close");
    m_Minimize->SetAccessibleName("Minimize window");
    m_Maximize->SetAccessibleName("Maximize window");
    m_Close->SetAccessibleName("Close window");
    const auto target = window.GetHandle();
    m_Minimize->Clicked().Connect([target] { target.Post([](Window& live) { live.Minimize(); }); });
    m_Maximize->Clicked().Connect(
        [target]
        {
            target.Post(
                [](Window& live)
                {
                    if (live.IsMaximized())
                    {
                        live.Restore();
                    }
                    else
                    {
                        live.Maximize();
                    }
                });
        });
    m_Close->Clicked().Connect([target] { target.Post([](Window& live) { live.Close(); }); });
}

MenuBar& WindowTitleBar::Menus() const
{
    VerifyAccess();
    return *m_Menus;
}

Widget& WindowTitleBar::Content() const
{
    VerifyAccess();
    return *m_Content;
}

void WindowTitleBar::SetCaption(std::string caption)
{
    VerifyStructuralMutation();
    m_Caption->SetText(std::move(caption));
}

void WindowTitleBar::SynchronizeWindowState(bool maximized)
{
    if (m_Maximized == maximized)
    {
        return;
    }
    m_Maximized = maximized;
    for (const auto& child : GetChildren())
    {
        if (child.get() == m_Maximize)
        {
            auto& button = static_cast<Detail::WindowCommandButton&>(*child);
            button.SetCommand(maximized ? Detail::WindowCommand::Restore : Detail::WindowCommand::Maximize);
            button.SetAccessibleName(maximized ? "Restore window" : "Maximize window");
            break;
        }
    }
}

WindowHitRegion WindowTitleBar::HitTest(Point windowPoint) const
{
    VerifyAccess();
    auto* context = GetContext();
    if (!context || !IsVisible() || !GetBounds().Contains(context->ToLayoutPoint(*this, windowPoint)))
    {
        return WindowHitRegion::Client;
    }
    auto* target = context->GetWidgetAt(windowPoint);
    if (target == m_Minimize)
    {
        return WindowHitRegion::Minimize;
    }
    if (target == m_Maximize)
    {
        return WindowHitRegion::Maximize;
    }
    if (target == m_Close)
    {
        return WindowHitRegion::Close;
    }
    for (auto* node = target; node; node = node->GetParent())
    {
        if (node == this)
        {
            return target->IsWindowDragRegion() ? WindowHitRegion::Caption : WindowHitRegion::Client;
        }
    }
    return WindowHitRegion::Client;
}
} // namespace EStudioUI
