#include <EStudioUI/DocumentWorkspace.h>
#include <EStudioUI/MainWindow.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

//! Builds one real document; the application supplies owned-snapshot persistence.
void BuildDocumentWorkspace(
    EStudioUI::Window& window, EStudioUI::State<std::string>& value,
    std::function<void(std::shared_ptr<const std::string>, EStudioUI::DocumentSaveRequest)> persist)
{
    using namespace EStudioUI;
    auto& shell = window.Ui().Root().Add<MainWindow>(window);
    auto& workspace = shell.Central().Add<DocumentWorkspace>(window);
    auto history = std::make_shared<UndoHistory>(window.Ui().States());
    const auto endpoint = value.GetHandle();
    auto& content = workspace.AddDocument(
        {"project", "Project properties", history,
         [endpoint, state = &value, persist = std::move(persist)](DocumentSaveRequest request)
         {
             if (!endpoint.IsConnected())
             {
                 request.Complete({DocumentSaveStatus::Failed, "The application model has expired."});
                 return;
             }
             // Capture now; persist may open a picker and write on an application worker.
             persist(state->GetSnapshot(), request);
         }});
    auto& editor = content.Add<TextBox>(value.Get());
    editor.SetWidth(SizeRule::Fill());
    editor.Submitted().Connect(
        [endpoint, state = &value, history](const std::string& text)
        {
            if (endpoint.IsConnected())
            {
                history->Set(*state, text, "Edit project properties");
            }
        });
    const WidgetRef editorRef(&editor);
    value.Subscribe(
        [editorRef](const StateChange<std::string>& change)
        {
            if (auto* field = dynamic_cast<TextBox*>(editorRef.Get()))
            {
                field->SetText(change.GetAfter());
            }
        });
    auto& tools = shell.AddToolBar();
    tools.AddAction(workspace.UndoAction());
    tools.AddAction(workspace.RedoAction());
    tools.AddAction(workspace.SaveAction());
    tools.AddAction(workspace.CloseAction());
}

int main()
{
    return 0;
}
