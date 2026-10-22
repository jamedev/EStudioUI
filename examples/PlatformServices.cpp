#include <EStudioUI/Application.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Window.h>

int main()
{
    using namespace EStudioUI;
    Application app;
    Window window(WindowOptions{}, "assets/NotoSans.ttf");
    auto& ui = window.Ui();
    ui.Root().SetGap(12);
    auto& editor = ui.Root().Add<TextBox>("A single committed edit");
    editor.SetAccessibleName("Project title");
    editor.SetPlaceholder("Try a Windows input method here");
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
    auto& reveal = ui.Root().Add<Button>("Reveal last item");
    auto& items = ui.Root().Add<ListWidget>();
    items.SetAccessibleName("Project entries");
    items.SetHeight(SizeRule::Fill());
    std::vector<std::string> captions;
    for (int index = 0; index < 1000; ++index)
        captions.push_back("Entry " + std::to_string(index + 1));
    items.SetItems(std::move(captions));
    reveal.Clicked().Connect(
        [target = ui.GetHandle(items)]
        {
            target.Post(
                [](ListWidget& list)
                {
                    // Native clients use this same bounded semantic path and canonical selection.
                    auto& tree = list.GetContext()->Semantics();
                    const auto owner = tree.GetId(list);
                    const auto count = tree.GetChildCount(owner);
                    if (count)
                        tree.Perform(tree.GetChildAt(owner, count - 1), {SemanticAction::Select});
                });
        });
    return app.Run();
}
