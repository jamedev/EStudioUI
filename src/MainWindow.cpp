#include <EStudioUI/MainWindow.h>

namespace EStudioUI
{
MainWindow::MainWindow(Window& window) : Widget("mainwindow")
{
    window.VerifyAccess();
    window.Ui().Root().SetStyleProperty(StyleProperty::Padding, Insets{});
    window.Ui().Root().SetGap(0);
    SetHeight(SizeRule::Fill());
    SetGap(0);
    m_TitleBar = &Add<WindowTitleBar>(window);
    m_ToolBars = &Add<Widget>("mainwindow-toolbars");
    m_ToolBars->SetHeight(SizeRule::Content());
    m_ToolBars->SetGap(0);
    m_Docks = &Add<DockManager>(window);
    m_Status = &Add<StatusBar>();
    window.SetTitleBar(m_TitleBar);
}

WindowTitleBar& MainWindow::TitleBar() const
{
    VerifyAccess();
    return *m_TitleBar;
}

ToolBar& MainWindow::AddToolBar()
{
    VerifyStructuralMutation();
    return m_ToolBars->Add<ToolBar>();
}

Widget& MainWindow::Central() const
{
    VerifyAccess();
    return m_Docks->Central();
}

DockManager& MainWindow::Docks() const
{
    VerifyAccess();
    return *m_Docks;
}

StatusBar& MainWindow::Status() const
{
    VerifyAccess();
    return *m_Status;
}
} // namespace EStudioUI
