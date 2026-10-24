#include <EStudioUI/DocumentWorkspace.h>
#include <EStudioUI/MessageBox.h>
#include <EStudioUI/ThemeController.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

void Turn(UiContext& ui)
{
    ui.PumpPlatformServices();
    ui.GetDispatcher().Drain();
    ui.Prepare(1100, 700);
}

void Choose(UiContext& ui, StandardButton response)
{
    auto* dialog = dynamic_cast<Dialog*>(ui.GetPopup());
    Check(dialog != nullptr, "Close preflight uses a real asynchronous dialog");
    Check(dialog->Buttons().GetButton(response)->Trigger(), "A real standard-response button accepts the decision");
    Turn(ui);
}

void ExistingTabMenus(FontFace& font)
{
    UiContext ui(font);
    ThemeController theme(ui);
    auto& tabs = ui.Root().Add<TabWidget>();
    tabs.AddTab("First");
    int requested = -1;
    tabs.TabContextMenuRequested().Connect([&](int index, Point) { requested = index; });
    tabs.AddTab("Second");
    tabs.MoveTab(0, 1);
    Turn(ui);
    const auto pointFor = [&](int index)
    {
        const auto rect = tabs.GetTabButton(index).GetBounds();
        return Point{rect.m_X + 8, rect.m_Y + rect.m_Height * .5f};
    };
    ui.ContextMenu(pointFor(1));
    Check(requested == 1, "Late menu subscription resolves an existing reordered page identity");
    ui.ContextMenu(pointFor(0));
    Check(requested == 0, "New pages share the same opt-in menu routing");
}

void Workspace(FontFace& font)
{
    UiContext ui(font);
    ThemeController theme(ui);
    auto& workspace = ui.Root().Add<DocumentWorkspace>();
    State<int> a(ui.States(), 0), b(ui.States(), 0);
    auto ah = std::make_shared<UndoHistory>(ui.States());
    auto bh = std::make_shared<UndoHistory>(ui.States());
    DocumentSaveRequest pending;
    auto& content =
        workspace.AddDocument({"a", "Load case A", ah, [&](DocumentSaveRequest request) { pending = request; }});
    content.Add<Label>("A persistent engineering view");
    workspace.AddDocument(
        {"b", "Load case B", bh, [](DocumentSaveRequest request) { request.Complete({DocumentSaveStatus::Saved}); }});
    Turn(ui);
    workspace.SetCurrentDocument("a");
    workspace.MoveDocument("a", 1);
    Check(workspace.GetDocumentIds() == std::vector<std::string>{"b", "a"} && &workspace.Content("a") == &content,
          "Reordering preserves stable identities and actual content");
    ah->Set(a, 1, "Load");
    Check(workspace.GetDocumentInfo("a").m_Modified && workspace.Tabs().IsTabModified(1),
          "Canonical history drives dirty indicators");
    Check(workspace.UndoAction()->Trigger() && a.Get() == 0 && !workspace.GetDocumentInfo("a").m_Modified,
          "Shared actions target the active document history");
    workspace.RedoAction()->Trigger();
    auto cancelled = workspace.RequestClose("a");
    Check(cancelled.IsPending() && workspace.GetDocumentIds().size() == 2,
          "Close begins asynchronously without removing views");
    Turn(ui);
    Choose(ui, StandardButton::Cancel);
    Check(cancelled.GetResult()->m_Status == DocumentCloseStatus::Cancelled && &workspace.Content("a") == &content,
          "Cancel retains every actual view and its unsaved model");
    auto closing = workspace.RequestClose("a");
    Turn(ui);
    Choose(ui, StandardButton::Save);
    Check(pending.IsPending() && closing.IsPending() && workspace.GetDocumentInfo("a").m_Saving,
          "Save keeps closure pending without a nested event loop");
    ah->Set(a, 2, "Newer load");
    bool first = false, second = true;
    std::thread worker(
        [&]
        {
            first = pending.Complete({DocumentSaveStatus::Saved});
            second = pending.Complete({DocumentSaveStatus::Saved});
        });
    worker.join();
    Turn(ui);
    Check(first && !second && workspace.GetDocumentInfo("a").m_Modified && closing.IsPending(),
          "Worker completion is exactly once and an older save cannot discard newer edits");
    Choose(ui, StandardButton::Cancel);
    auto failure = workspace.RequestClose("a");
    Turn(ui);
    Choose(ui, StandardButton::Save);
    pending.Complete({DocumentSaveStatus::Failed, "Disk full"});
    Turn(ui);
    Check(failure.GetResult()->m_Status == DocumentCloseStatus::Failed && a.Get() == 2 &&
              workspace.GetDocumentIds().size() == 2,
          "Save failure keeps document data and all views intact");
    bh->Set(b, 1);
    auto all = workspace.RequestCloseAll();
    Turn(ui);
    Choose(ui, StandardButton::Discard);
    Choose(ui, StandardButton::Cancel);
    Check(all.GetResult()->m_Status == DocumentCloseStatus::Cancelled && workspace.GetDocumentIds().size() == 2,
          "Close-all preflights every document before removing any requested view");
    auto success = workspace.RequestCloseAll();
    Turn(ui);
    Choose(ui, StandardButton::Save);
    Turn(ui);
    // B uses an immediate application completion; A still has an owned pending ticket.
    Choose(ui, StandardButton::Save);
    pending.Complete({DocumentSaveStatus::Saved});
    Turn(ui);
    Check(success.GetResult()->m_Status == DocumentCloseStatus::Closed && workspace.GetDocumentIds().empty(),
          "All successful saves commit one coherent document-close batch");
    Check(!workspace.UndoAction()->GetSnapshot()->m_Enabled, "No active document leaves undo disabled");
}

void EditorCommitAndHiddenOwner(FontFace& font)
{
    UiContext ui(font);
    ThemeController theme(ui);
    auto& pages = ui.Root().Add<TabWidget>();
    auto& documentPage = pages.AddTab("Documents");
    auto& workspace = documentPage.Add<DocumentWorkspace>();
    pages.AddTab("Other view");
    State<int> value(ui.States(), 0);
    auto history = std::make_shared<UndoHistory>(ui.States());
    DocumentSaveRequest ticket;
    int captured = 0, commitValue = 9;
    auto& content = workspace.AddDocument({"project", "Project", history, [&](DocumentSaveRequest request)
                                           {
                                               captured = value.Get();
                                               ticket = request;
                                           }});
    auto& editor = content.Add<TextBox>("Pending editor draft");
    const auto endpoint = value.GetHandle();
    editor.FocusLost().Connect(
        [&]
        {
            if (endpoint.IsConnected())
            {
                history->Set(value, commitValue, "Commit editor");
            }
        });
    Turn(ui);
    ui.SetFocus(&editor);
    const auto& selector = workspace.Tabs().GetTabButton(0);
    auto* close = selector.GetChildren().front().get();
    const auto before = close->GetBounds();
    const Point point{before.m_X + before.m_Width / 2, before.m_Y + before.m_Height / 2};
    ui.PointerDown(point);
    ui.PointerUp(point);
    Turn(ui);
    Check(value.Get() == 9 && ui.GetPopup() && close->GetBounds() == before,
          "Committing an editor cannot move the close target beneath the same pointer gesture");
    Choose(ui, StandardButton::Cancel);
    commitValue = 10;
    ui.SetFocus(&editor);
    workspace.SaveDocument("project");
    Check(captured == 10 && ticket.GetCheckpoint() == history->CaptureCheckpoint(),
          "Save commits a focused editor before capturing data/checkpoint");
    ticket.Complete({DocumentSaveStatus::Saved});
    Turn(ui);
    Check(!workspace.GetDocumentInfo("project").m_Modified, "A saved current editor value becomes clean");
    history->Set(value, 11);
    pages.SetCurrentIndex(1);
    auto closing = workspace.RequestClose("project");
    Turn(ui);
    Check(pages.GetCurrentIndex() == 0 && ui.GetPopup(),
          "A hidden document workspace is revealed before close approval");
    workspace.CancelClose();
    Check(closing.GetResult()->m_Status == DocumentCloseStatus::Cancelled && !ui.GetPopup(),
          "Explicit cancellation dismisses approval without removing views");
    workspace.Changed().Connect([] { throw std::runtime_error("presentation observer"); });
    closing = workspace.RequestClose("project");
    Turn(ui);
    Choose(ui, StandardButton::Discard);
    Check(closing.GetResult()->m_Status == DocumentCloseStatus::Closed && workspace.GetCallbackError(),
          "A presentation-observer failure is inspectable and cannot strand a terminal close session");
}

void ConcurrentCompletion(FontFace& font)
{
    UiContext ui(font);
    auto& workspace = ui.Root().Add<DocumentWorkspace>();
    DocumentSaveRequest ticket;
    workspace.AddDocument({"case", "Case", {}, [&](DocumentSaveRequest request) { ticket = request; }});
    workspace.SetDocumentModified("case");
    workspace.SaveDocument("case");
    std::atomic<int> accepted = 0, wakes = 0;
    ui.GetDispatcher().SetWakeup([&] { ++wakes; });
    std::vector<std::thread> workers;
    for (int index = 0; index < 8; ++index)
    {
        workers.emplace_back(
            [&]
            {
                if (ticket.Complete({DocumentSaveStatus::Saved}))
                {
                    ++accepted;
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    Turn(ui);
    Check(accepted == 1 && wakes >= 1 && !workspace.GetDocumentInfo("case").m_Modified,
          "Concurrent completion accepts one result and wakes the owner without UI access");
    bool rejected = false;
    auto connection = ui.IdleTurn().Connect(
        [&]
        {
            try
            {
                ui.PumpPlatformServices();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    ui.PumpPlatformServices();
    connection.Disconnect();
    Check(rejected, "Idle service callbacks cannot recursively pump another owner turn");
    ui.GetDispatcher().SetWakeup({});
}

void LifetimeAndCapacity(FontFace& font)
{
    UiContext ui(font);
    auto* workspace = &ui.Root().Add<DocumentWorkspace>();
    DocumentSaveRequest ticket;
    workspace->AddDocument({"part", "Part", {}, [&](DocumentSaveRequest request) { ticket = request; }});
    workspace->SetDocumentModified("part");
    workspace->SaveDocument("part");
    auto queued = workspace->RequestClose("part");
    Turn(ui);
    std::size_t posted = 0;
    for (; posted < 5000; ++posted)
    {
        if (ui.GetDispatcher().Post([] {}).GetStatus() == DispatchStatus::Rejected)
        {
            break;
        }
    }
    Check(posted > 0 && posted < 5000, "The ordinary producer queue is actually saturated");
    std::thread worker([&] { ticket.Complete({DocumentSaveStatus::Saved}); });
    worker.join();
    ui.PumpPlatformServices();
    Check(queued.GetResult()->m_Status == DocumentCloseStatus::Closed,
          "Reserved save slots settle during idle even when normal posting is rejected");
    ui.GetDispatcher().Drain(5000, std::chrono::seconds(1));
    workspace->AddDocument(
        {"part", "Replacement identity", {}, [&](DocumentSaveRequest request) { ticket = request; }});
    workspace->SetDocumentModified("part");
    const auto replacement = workspace->SaveDocument("part");
    const auto pendingClose = workspace->RequestClose("part");
    ui.Root().RemoveChild(*workspace);
    Check(pendingClose.GetResult()->m_Status == DocumentCloseStatus::OwnerClosed &&
              !replacement.Complete({DocumentSaveStatus::Saved}),
          "Owner removal settles close and expires worker tickets");
    Check(!ticket.IsPending(), "Expired tickets never retain a widget owner");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        ExistingTabMenus(font);
        Workspace(font);
        LifetimeAndCapacity(font);
        EditorCommitAndHiddenOwner(font);
        ConcurrentCompletion(font);
        std::cout << "Document workspace: " << s_Checks << " checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Document workspace: " << error.what() << '\n';
        return 1;
    }
}
