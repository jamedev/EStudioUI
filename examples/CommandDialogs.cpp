#include <EStudioUI/InputDialog.h>
#include <EStudioUI/MenuBar.h>
#include <EStudioUI/MessageBox.h>
#include <EStudioUI/UiContext.h>

void BuildCommands(EStudioUI::UiContext& ui)
{
    using namespace EStudioUI;
    auto& bar = ui.Root().Add<MenuBar>();
    auto& file = bar.AddMenu("File");
    auto save = ui.CreateAction("Save sketch");
    save->SetShortcut({Key::S, ShortcutModifier::Primary});
    file.AddAction(save).SetIcon("S");
    save->Triggered().Connect([] { /* application document save */ });
    auto& prompt = InputDialog::Create(bar, "Rename sketch", "Sketch name", "Sketch 01");
    file.AddAction("Rename").Clicked().Connect(
        [target = WidgetRef(&prompt)]
        {
            if (auto* live = dynamic_cast<InputDialog*>(target.Get()))
                live->Open().OnFinished(
                    [](InputDialogResult result)
                    {
                        if (result.m_Text)
                        {
                            // Consume owned accepted text without retaining the prompt/editor.
                        }
                    });
        });
    auto& message = MessageBox::Create(bar, "Unsaved sketch", "Save changes before closing?");
    message.SetKind(MessageKind::Question);
    message.SetButtons({StandardButton::Save, StandardButton::Discard, StandardButton::Cancel});
    file.AddAction("Close sketch")
        .Clicked()
        .Connect(
            [target = WidgetRef(&message)]
            {
                if (auto* live = dynamic_cast<MessageBox*>(target.Get()))
                    live->Open().OnFinished(
                        [](PopupResult result)
                        {
                            if (result.m_Reason == PopupCloseReason::Accepted)
                            {
                                // Save and Discard have distinct StandardButton response values.
                            }
                        });
            });
}

int main()
{
    return 0;
}
