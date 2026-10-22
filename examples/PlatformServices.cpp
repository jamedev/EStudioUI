#include <EStudioUI/Application.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/Window.h>

int main()
{
    using namespace EStudioUI;
    Application app;
    Window window(WindowOptions{}, "assets/NotoSans.ttf");
    auto& ui = window.Ui();
    auto& open = ui.Root().Add<Button>("Choose a file");
    auto& status = ui.Root().Add<Label>("Ready");
    status.SetAccessibleName("Chosen file");
    open.Clicked().Connect(
        [target = WidgetRef(&status)]
        {
            auto* label = dynamic_cast<Label*>(target.Get());
            if (!label)
                return;
            auto session = label->GetContext()->Platform().ShowFileDialog({});
            session.OnFinished(
                [target](const FileDialogResult& result)
                {
                    auto* live = dynamic_cast<Label*>(target.Get());
                    if (!live || result.m_Status == FileDialogStatus::OwnerClosed)
                        return;
                    live->SetText(result.m_Status == FileDialogStatus::Accepted ? "Selected a file" : "Picker closed");
                });
        });
    return app.Run();
}
