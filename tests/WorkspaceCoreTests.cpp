#include <EStudioUI/Bindings.h>
#include <EStudioUI/FileDialog.h>
#include <EStudioUI/FilePathEditor.h>
#include <EStudioUI/FolderPicker.h>
#include <EStudioUI/MenuBar.h>
#include <EStudioUI/StatusBar.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/ToolBar.h>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
        throw std::runtime_error(message);
}

template <typename F> void Throws(F action, const char* message)
{
    bool threw = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    Check(threw, message);
}

Point Center(Rect bounds)
{
    return {bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2};
}

std::filesystem::path Path(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

class HeldServices final : public PlatformServices
{
  public:
    explicit HeldServices(UiDispatcher dispatcher) : PlatformServices(PlatformHost{std::move(dispatcher)})
    {
    }

    ~HeldServices() override
    {
        Close();
    }

    FileDialogOptions m_Options;
    std::stop_token m_Stop;
    std::function<void(FileDialogResult)> m_Complete;
    int m_Requests = 0;

  protected:
    PlatformCapabilities DescribeCapabilities() const override
    {
        return {};
    }

    ClipboardResult ReadClipboardCore() override
    {
        return {};
    }

    PlatformResult WriteClipboardCore(const ClipboardData&) override
    {
        return {};
    }

    void BeginFileDialog(const FileDialogOptions& options, std::stop_token stop,
                         std::function<void(FileDialogResult)> complete) override
    {
        m_Options = options;
        m_Stop = stop;
        m_Complete = std::move(complete);
        ++m_Requests;
    }
};

void Paths(FontFace& font)
{
    UiContext ui(font);
    auto& field = ui.Root().Add<FilePathEditor>();
    const auto unicode = Path("models/ni\xc3\xb1o.txt");
    field.SetPath(unicode);
    Check(field.GetPath() == unicode && field.Editor().GetText() == "models/ni\xc3\xb1o.txt",
          "Paths preserve exact UTF-8 bytes");
    Check(!field.CanSetPath(Path(std::string("a\0b", 3))), "Embedded NUL paths are rejected before mutation");
    Throws([&] { field.SetPath(Path(std::string("a\0b", 3))); }, "Invalid paths cannot enter canonical state");
    Check(field.GetPath() == unicode, "Rejected path preserves the previous value");
    Check(!field.CanSetPath(Path("a\nb")), "Multiline paths are explicitly unrepresentable in a single-line field");
    Check(!field.CanSetPath(Path(std::string(65537, 'a'))), "Path byte budget is checked without truncation");
    field.Editor().SetText("edited.txt");
    Check(field.GetPath() == Path("edited.txt"), "Ordinary editor edits update canonical paths");
    State<std::filesystem::path> state(ui.States(), Path("initial.txt"));
    const auto policy = state.AddValidator(
        [](const std::filesystem::path& candidate)
        {
            if (candidate.extension() != ".txt")
                throw std::invalid_argument("Only text documents");
        });
    const auto binding = BindPath(field, state);
    field.SetPath(Path("accepted.txt"));
    Check(state.Get() == Path("accepted.txt"), "Path bindings commit editor values through State");
    field.SetPath(Path("rejected.bin"));
    Check(state.Get() == Path("accepted.txt") && field.GetPath() == state.Get() && binding.GetError(),
          "Rejected path bindings restore canonical state and retain diagnostics");
    Throws([&] { state.Set(Path("bad\n.txt")); }, "State representability is validated before committing");
    Check(state.Get() == Path("accepted.txt"), "Unrepresentable state cannot partially update the field");
    field.SetRecentPaths({Path("one.txt"), Path("two.txt"), Path("one.txt"), {}});
    Check(field.GetRecentPaths().size() == 2, "Recent paths are distinct and omit empty entries");
    Throws([&] { field.SetRecentPaths(std::vector<std::filesystem::path>(17, Path("x.txt"))); },
           "Recent-path payload is bounded");
    FileDialogOptions invalid;
    invalid.m_Multiple = true;
    Throws([&] { field.SetDialogOptions(invalid); }, "One path field cannot enable multiple selection");
    auto& folder = ui.Root().Add<FolderPicker>();
    Check(folder.GetDialogOptions().m_Kind == FileDialogKind::SelectFolder, "FolderPicker selects folders by default");
    folder.SetPath(Path("assets"));
    Check(folder.Editor().GetText() == "assets", "FolderPicker uses the same editable path model");
    auto& status = ui.Root().Add<StatusBar>();
    status.Permanent().Add<Label>("UTF-8");
    status.SetMessage("Saved");
    Check(status.GetMessage() == "Saved", "Status messages retain owned text");
    status.ClearMessage();
    Check(status.GetMessage().empty() && status.Permanent().GetChildren().size() == 1,
          "Clearing status retains permanent widgets");
    binding.Disconnect();
    policy.Disconnect();
}

void PickerLifetime(FontFace& font)
{
    UiContext ui(font);
    auto service = std::make_unique<HeldServices>(ui.GetDispatcher());
    auto* native = service.get();
    ui.SetPlatformServices(std::move(service));
    auto& field = ui.Root().Add<FilePathEditor>();
    auto session = field.Browse();
    field.Browse();
    Check(session.IsPending() && native->m_Requests == 1, "Repeated browsing coalesces one pending picker");
    native->m_Complete({FileDialogStatus::Accepted, {Path("chosen.txt")}});
    ui.Platform().Pump();
    Check(field.GetPath() == Path("chosen.txt") && !field.IsBrowsing(),
          "Asynchronous picker commits at the owner boundary");
    Check(field.GetRecentPaths() == std::vector<std::filesystem::path>{Path("chosen.txt")},
          "Accepted picks update bounded recent paths");
    auto stale = field.Browse();
    field.SetPath(Path("newer.txt"));
    native->m_Complete({FileDialogStatus::Accepted, {Path("stale.txt")}});
    ui.Platform().Pump();
    Check(field.GetPath() == Path("newer.txt") && !stale.IsPending(), "A delayed picker cannot overwrite a newer edit");
    auto abandoned = field.Browse();
    const WidgetRef removed(&field);
    ui.Root().RemoveChild(field);
    Check(!removed.Get() && !abandoned.IsPending() && native->m_Stop.stop_requested(),
          "Destroying a path owner cancels native work and callbacks");
    auto& owner = ui.Root().Add<Widget>();
    auto multiple = FileDialogOptions{};
    multiple.m_Multiple = true;
    auto open = FileDialog::Open(owner, multiple);
    Check(native->m_Options.m_Multiple && native->m_Options.m_Kind == FileDialogKind::OpenFile,
          "Open facade retains multiple selection for document commands");
    open.Cancel();
    auto save = FileDialog::Save(owner);
    Check(native->m_Options.m_Kind == FileDialogKind::SaveFile && !native->m_Options.m_Multiple,
          "Save facade selects a single destination");
    save.Cancel();
    auto folder = FileDialog::SelectFolder(owner);
    Check(native->m_Options.m_Kind == FileDialogKind::SelectFolder && native->m_Options.m_Filters.empty(),
          "Folder facade normalizes native options");
    ui.Root().RemoveChild(owner);
    Check(folder.GetResult()->m_Status == FileDialogStatus::Cancelled,
          "Logical owner lifetime cancels a facade session");
    FilePathEditor detached;
    Throws([&] { detached.Browse(); }, "Detached path browsing diagnoses missing services");
}

void Notifications(FontFace& font)
{
    UiContext ui(font);
    auto& field = ui.Root().Add<FilePathEditor>();
    std::vector<std::filesystem::path> observed;
    field.PathChanged().Connect(
        [&](const auto& value)
        {
            if (value == Path("first"))
                field.SetPath(Path("second"));
        });
    field.PathChanged().Connect([&](const auto& value) { observed.push_back(value); });
    field.SetPath(Path("first"));
    Check(observed == std::vector<std::filesystem::path>{Path("first"), Path("second")},
          "Reentrant path observers receive owned coherent snapshots");
    auto& deleted = ui.Root().Add<FilePathEditor>();
    const WidgetRef weak(&deleted);
    deleted.PathChanged().Connect([&](const auto&) { ui.Root().RemoveChild(deleted); });
    deleted.SetPath(Path("delete"));
    Check(!weak.Get(), "Path callbacks may delete their receiver safely");
}

void Transfer(FontFace& font, FontFace& otherFont)
{
    UiContext source(font), destination(otherFont);
    auto& container = source.Root().Add<Widget>();
    auto& editor = container.Add<TextBox>("document");
    auto& menus = container.Add<MenuBar>();
    auto& file = menus.AddMenu("File");
    auto& nested = file.AddMenu("Export");
    int triggered = 0;
    nested.AddAction("Text").Clicked().Connect([&] { ++triggered; });
    const WidgetRef fileIdentity(&file), nestedIdentity(&nested);
    State<std::string> value(source.States(), "shared");
    const auto binding = BindText(editor, value);
    const auto oldHandle = source.GetHandle(editor);
    source.Prepare(640, 360);
    source.SetFocus(&editor);
    menus.OpenMenu(0);
    static_cast<MenuItem&>(*file.GetChildren().front()).Trigger();
    Check(source.GetPopupCount() == 2, "A source menu and submenu are open before transfer");
    container.ReparentTo(destination.Root());
    Check(editor.GetContext() == &destination && source.GetFocus() != &editor,
          "Transfer retires old focus and switches every descendant context");
    Check(source.GetPopupCount() == 0 && fileIdentity.Get() == &file && nestedIdentity.Get() == &nested,
          "Owned menu identities survive transfer and active sessions close");
    Check(file.GetContext() == &destination && nested.GetContext() == &destination,
          "Nested root-owned menus migrate with their logical owner");
    value.Set("after transfer");
    Check(editor.GetText() == "after transfer" && binding.IsConnected(),
          "Existing shared bindings follow the actual moved widget");
    editor.SetText("from floating");
    Check(value.Get() == "from floating", "Moved controls still write through their canonical State");
    auto expired = oldHandle.Post([](TextBox& live) { live.SetText("wrong dispatcher"); });
    source.GetDispatcher().Drain();
    Check(expired.GetStatus() == DispatchStatus::TargetExpired,
          "Context-bound handles reject moved targets on the old dispatcher");
    destination.Prepare(640, 360);
    menus.OpenMenu(0);
    Check(destination.GetPopupCount() == 1, "Transferred menus reopen in the destination stack");
    destination.CloseAllPopups();
    container.ReparentTo(source.Root());
    source.Prepare(640, 360);
    auto staleAfterReturn = oldHandle.Post([](TextBox& live) { live.SetText("stale after return"); });
    source.GetDispatcher().Drain();
    Check(staleAfterReturn.GetStatus() == DispatchStatus::TargetExpired && editor.GetText() == value.Get(),
          "A pre-transfer handle cannot become valid again after redocking");
    auto fresh = source.GetHandle(editor).Post([](TextBox& live) { live.SetText("fresh receiver"); });
    source.GetDispatcher().Drain();
    Check(fresh.GetStatus() == DispatchStatus::Completed && value.Get() == "fresh receiver",
          "A destination handle resolves the returned widget normally");
    Check(editor.GetContext() == &source && file.GetContext() == &source && binding.IsConnected(),
          "Redocking preserves identity, menus and bindings");
    Throws([&] { container.ReparentTo(editor); }, "Reparenting rejects ownership cycles");
    Throws([&] { source.Root().ReparentTo(destination.Root()); }, "A context root cannot transfer its ownership");
    Widget detached;
    Throws([&] { editor.ReparentTo(detached); }, "An attached target cannot escape into a detached domain");
    std::exception_ptr workerError;
    std::jthread worker(
        [&]
        {
            try
            {
                editor.ReparentTo(destination.Root());
            }
            catch (...)
            {
                workerError = std::current_exception();
            }
        });
    worker.join();
    Check(static_cast<bool>(workerError) && editor.GetContext() == &source,
          "Cross-thread transfer fails before touching ownership");
    Check(triggered == 0, "Transferring menus does not activate application commands");
}

class CaptureProbe final : public Widget
{
  public:
    std::function<void()> m_Lost;

  protected:
    void OnCaptureLost() override
    {
        const auto callback = m_Lost;
        if (callback)
            callback();
    }
};

void RetirementCallbacks(FontFace& font)
{
    UiContext ui(font);
    auto& moving = ui.Root().Add<CaptureProbe>();
    moving.SetFloatingBounds({10, 10, 60, 30});
    auto& destination = ui.Root().Add<Widget>();
    destination.SetFloatingBounds({100, 10, 100, 100});
    const WidgetRef destinationRef(&destination);
    moving.m_Lost = [&] { ui.Root().RemoveChild(destination); };
    ui.Prepare(400, 200);
    ui.PointerDown(Center(moving.GetBounds()));
    Throws([&] { moving.ReparentTo(destination); },
           "Capture cancellation may delete the destination without leaving stale ownership");
    Check(!destinationRef.Get() && moving.GetParent() == &ui.Root(),
          "A deleted reparenting endpoint leaves the source intact");
    auto& replacement = ui.Root().Add<Widget>();
    moving.m_Lost = {};
    moving.ReparentTo(replacement);
    Check(moving.GetParent() == &replacement, "A rejected transfer releases its reentrancy guard");
    moving.ReparentTo(ui.Root());
    bool nestedRejected = false;
    moving.m_Lost = [&]
    {
        try
        {
            moving.ReparentTo(replacement);
        }
        catch (const std::logic_error&)
        {
            nestedRejected = true;
        }
    };
    ui.Prepare(400, 200);
    ui.PointerDown(Center(moving.GetBounds()));
    moving.ReparentTo(replacement);
    Check(nestedRejected && moving.GetParent() == &replacement,
          "Recursive transfers are diagnosed while the outer move completes safely");
}

void ToolbarAndTabs(FontFace& font)
{
    UiContext ui(font);
    auto& toolbar = ui.Root().Add<ToolBar>();
    toolbar.SetFloatingBounds({0, 0, 220, 38});
    std::vector<ToolButton*> buttons;
    for (int index = 0; index < 12; ++index)
    {
        auto& button = toolbar.AddButton("Command " + std::to_string(index));
        button.SetWidth(SizeRule::Fixed(100));
        buttons.push_back(&button);
    }
    auto& custom = toolbar.Content().Add<TextBox>("search");
    custom.SetWidth(SizeRule::Fixed(140));
    ui.Prepare(800, 400);
    const float initial = buttons[0]->GetBounds().m_X;
    toolbar.ScrollBy(300);
    ui.Prepare(800, 400);
    Check(buttons[0]->GetBounds().m_X < initial - 200, "Toolbar commands scroll inside a bounded viewport");
    ui.SetFocus(buttons.back());
    ui.Prepare(800, 400);
    const auto viewport = toolbar.Content().GetContentBounds();
    Check(buttons.back()->GetBounds().m_X >= viewport.m_X - .1f &&
              buttons.back()->GetBounds().m_X + 100 <= viewport.m_X + viewport.m_Width + .1f,
          "Keyboard focus reveals the selected toolbar command");
    Throws([&] { toolbar.ScrollBy(std::numeric_limits<float>::infinity()); },
           "Toolbar scrolling rejects nonfinite offsets");
    ui.SetFocus(&custom);
    ui.Prepare(800, 400);
    Check(custom.GetBounds().m_X + custom.GetBounds().m_Width <= viewport.m_X + viewport.m_Width + .1f,
          "Custom toolbar widgets participate in focus reveal");
    auto& tabs = ui.Root().Add<TabWidget>();
    auto& first = tabs.AddTab("First");
    auto& second = tabs.AddTab("Second");
    auto& third = tabs.AddTab("Third");
    tabs.SetCurrentIndex(1);
    tabs.MoveTab(0, 2);
    Check(&tabs.GetPage(0) == &second && &tabs.GetPage(1) == &third && &tabs.GetPage(2) == &first &&
              tabs.GetCurrentIndex() == 0,
          "Tab reordering preserves pages and selected identity in the forward direction");
    tabs.MoveTab(2, 0);
    Check(&tabs.GetPage(0) == &first && &tabs.GetPage(1) == &second && tabs.GetCurrentIndex() == 1,
          "Backward tab reordering keeps selected identity");
    Throws([&] { tabs.MoveTab(0, 3); }, "Invalid reordering cannot partially mutate tabs");
    tabs.SetTabBarVisible(false);
    ui.Prepare(800, 400);
    auto& semantics = ui.Semantics();
    const auto id = semantics.GetId(tabs);
    Check(!tabs.IsTabBarVisible() && tabs.GetCurrentIndex() == 1 &&
              semantics.GetNode(id)->m_Info.m_Role == SemanticRole::Group && semantics.GetSelectionCount(id) == 0,
          "Hidden tab selectors keep the selected page and expose ordinary group semantics");
    tabs.SetTabBarVisible(true);
    ui.Prepare(800, 400);
    Check(tabs.IsTabBarVisible() && semantics.GetSelectionCount(id) == 1,
          "Showing tab selectors restores ordinary tab semantics");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH), otherFont(ESTUDIOUI_FONT_PATH);
        Paths(font);
        PickerLifetime(font);
        Notifications(font);
        Transfer(font, otherFont);
        ToolbarAndTabs(font);
        RetirementCallbacks(font);
        std::cout << "Workspace core: " << s_Checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Workspace core: " << error.what() << '\n';
        return 1;
    }
}
