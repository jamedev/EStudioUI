#include <EStudioUI/Application.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/Window.h>

int main()
{
    using namespace EStudioUI;
    Application app;
    Window window(WindowOptions{}, "assets/NotoSans.ttf");
    auto& ui = window.Ui();
    auto& root = ui.Root();
    root.SetGap(12);
    root.SetStyleProperty(StyleProperty::Padding, Insets{16, 16, 16, 16});

    auto save = ui.CreateAction("Save document");
    save->SetShortcut({Key::S, ShortcutModifier::Primary});
    auto& toolbar = root.Add<ToolBar>();
    toolbar.AddAction(save);
    auto& commands = toolbar.AddButton("Commands");
    auto& editor = root.Add<TextBox>("Edit this document name");
    auto& status = root.Add<Label>("Ready");
    save->Triggered().Connect(
        [target = WidgetRef(&status)]
        {
            if (auto* live = dynamic_cast<Label*>(target.Get()))
                live->SetText("Saved through the shared command.");
        });

    auto menu = std::make_unique<ContextMenu>();
    menu->AddAction(save);
    menu->AddMenu("More commands").AddAction(save);
    const WidgetRef popup(&ui.AddOwnedPopup(std::move(menu), commands));
    commands.Clicked().Connect(
        [popup, source = WidgetRef(&commands)]
        {
            if (auto* button = source.Get())
                if (auto* live = dynamic_cast<ContextMenu*>(popup.Get()))
                {
                    const auto bounds = button->GetBounds();
                    live->Open({bounds.m_X, bounds.m_Y + bounds.m_Height});
                }
        });

    auto& settings = root.Add<Widget>();
    settings.SetVisible(false);
    settings.SetWidth(SizeRule::Fixed(320));
    settings.SetHeight(SizeRule::Fixed(160));
    settings.SetStyleProperty(StyleProperty::Background, Color{0.15f, 0.2f, 0.3f, 1});
    settings.Add<TextBox>("Asynchronous settings");
    auto& accept = settings.Add<Button>("Apply");
    auto& open = toolbar.AddButton("Settings");
    open.Clicked().Connect(
        [target = WidgetRef(&settings), status = WidgetRef(&status)]
        {
            if (auto* panel = target.Get())
            {
                auto result = panel->GetContext()->ShowPopup(*panel, {120, 120}, PopupOptions::Modal());
                result.OnFinished(
                    [status](PopupResult response)
                    {
                        if (auto* label = dynamic_cast<Label*>(status.Get());
                            label && response.m_Reason != PopupCloseReason::ContextClosed)
                            label->SetText(response.m_Reason == PopupCloseReason::Accepted ? "Applied" : "Cancelled");
                    });
            }
        });
    accept.Clicked().Connect(
        [target = WidgetRef(&settings)]
        {
            if (auto* panel = target.Get())
                panel->GetContext()->ClosePopup(*panel, {PopupCloseReason::Accepted, 1});
        });
    ui.SetFocus(&editor);
    return app.Run();
}
