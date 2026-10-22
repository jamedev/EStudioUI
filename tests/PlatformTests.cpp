#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/UiContext.h>

#include <atomic>
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

template <typename Function> void Throws(Function function, const char* message)
{
    bool threw = false;
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    Check(threw, message);
}

class HeldServices final : public PlatformServices
{
  public:
    explicit HeldServices(UiDispatcher queue) : PlatformServices(PlatformHost{std::move(queue)})
    {
    }

    ~HeldServices() override
    {
        Close();
    }

    std::function<void(FileDialogResult)> m_Complete;
    std::stop_token m_Stop;

  protected:
    PlatformCapabilities DescribeCapabilities() const override
    {
        return {};
    }

    ClipboardResult ReadClipboardCore() override
    {
        throw std::runtime_error("Native read failed");
    }

    PlatformResult WriteClipboardCore(const ClipboardData&) override
    {
        throw std::runtime_error("Native write failed");
    }

    void BeginFileDialog(const FileDialogOptions&, std::stop_token stop,
                         std::function<void(FileDialogResult)> complete) override
    {
        m_Stop = stop;
        m_Complete = std::move(complete);
    }
};

void VerifyData()
{
    ClipboardData data;
    data.m_Text = "line one\r\nline two\nUnicode: \xc3\xb1 \xe6\x97\xa5";
    data.m_Html = "<b>Unicode \xc3\xb1</b>";
    data.m_Files = {std::filesystem::path("sample.txt")};
    data.m_Custom["application/x-estudioui-test"] = {0, 1, 0, 255};
    data.Validate();
    Check(!data.IsEmpty() && ClipboardData{}.IsEmpty(), "clipboard format presence is explicit");
    auto invalid = data;
    invalid.m_Text = std::string("bad\0text", 8);
    Throws([&] { invalid.Validate(); }, "embedded clipboard NUL is rejected");
    invalid = data;
    invalid.m_Text = "\xc0\xaf";
    Throws([&] { invalid.Validate(); }, "overlong UTF-8 is rejected");
    invalid = data;
    invalid.m_Custom["not a MIME name"] = {};
    Throws([&] { invalid.Validate(); }, "invalid MIME names are rejected");
    invalid = {};
    invalid.m_Custom["application/big"] = std::vector<std::uint8_t>(ClipboardData::s_MaxBytes + 1);
    Throws([&] { invalid.Validate(); }, "aggregate native clipboard data is bounded");
    WindowIcon icon{2, 1, {255, 0, 0, 255, 0, 255, 0, 255}};
    icon.Validate();
    icon.m_Width = 0;
    Throws([&] { icon.Validate(); }, "invalid icons are rejected before native publication");
    FileDialogOptions options;
    options.m_Filters = {{"Images", "*.png;*.jpg"}};
    options.Validate();
    options.m_Kind = FileDialogKind::SaveFile;
    options.m_Multiple = true;
    Throws([&] { options.Validate(); }, "save pickers reject multiple selection");
    options.m_Multiple = false;
    options.m_FileName = std::filesystem::path("folder/file.txt");
    Throws([&] { options.Validate(); }, "filename and folder have independent contracts");
    options.m_FileName.clear();
    options.m_DefaultExtension = ".txt";
    Throws([&] { options.Validate(); }, "save extension excludes its leading dot");
    PlatformCapabilities capabilities;
    Check(capabilities.Get(PlatformFeature::TextComposition) == PlatformSupport::Unsupported,
          "unimplemented composition is explicitly unsupported");
    Throws([&] { capabilities.Get(PlatformFeature::Count); }, "capability indexing is validated");
}

void VerifyCoordinates()
{
    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        WindowMetrics metrics;
        metrics.m_LogicalSize = {800, 600};
        metrics.m_FramebufferSize = {800 * scale, 600 * scale};
        metrics.m_ContentScale = metrics.m_InputScale = metrics.m_PixelScale = {scale, scale};
        metrics.m_ScreenOrigin = {-1920, 80};
        metrics.m_HasScreenCoordinates = true;
        const auto native = metrics.FromNative({120 * scale, 90 * scale});
        Check(native == Point{120, 90}, "native input has a consistent logical coordinate space");
        const auto screen = metrics.ToScreen(Point{120, 90});
        Check(metrics.FromScreen(screen) == Point{120, 90}, "DPI and negative-desktop-origin mappings round trip");
        const auto bounds = metrics.ToScreen(Rect{10, 20, 200, 40});
        Check(bounds.m_Width == 200 * scale && bounds.m_Height == 40 * scale,
              "semantic bounds use physical desktop pixels");
    }
    WindowMetrics invalid;
    Throws([&] { invalid.ToScreen(Point{}); }, "unavailable physical desktop coordinates are explicit");
    invalid.m_InputScale.m_X = 0;
    Throws([&] { invalid.FromNative({1, 2}); }, "zero DPI scale is rejected");
    invalid = {};
    Throws([&] { invalid.FromNative({std::numeric_limits<float>::infinity(), 0}); },
           "nonfinite native input is rejected");
}

void VerifyMemory()
{
    UiDispatcher queue;
    MemoryPlatformServices services(queue);
    ClipboardData data;
    data.m_Text = "first\r\nsecond\rthird";
    data.m_Html = "<p>first</p>";
    data.m_Custom["application/x-example"] = {1, 0, 2};
    Check(static_cast<bool>(services.WriteClipboard(data)), "memory clipboard publication succeeds");
    data.m_Html = "caller changed";
    auto read = services.ReadClipboard();
    Check(read.m_Data.m_Text == "first\nsecond\nthird" && read.m_Data.m_Html == "<p>first</p>",
          "clipboard snapshots own data and preserve canonical document newlines");
    read.m_Data.m_Custom.clear();
    Check(!services.ReadClipboard().m_Data.m_Custom.empty(), "caller snapshots cannot mutate the clipboard");
    services.QueueFileDialogResult({FileDialogStatus::Accepted, {"selected.txt"}});
    auto session = services.ShowFileDialog();
    int delivered = 0;
    const auto owner = std::this_thread::get_id();
    session.OnFinished(
        [&](const FileDialogResult& result)
        {
            Check(std::this_thread::get_id() == owner && result.m_Status == FileDialogStatus::Accepted,
                  "picker completion runs on its owner");
            ++delivered;
        });
    Check(session.IsPending() && !delivered, "even immediate injected results stay asynchronous");
    queue.Drain();
    Check(delivered == 1 && !session.IsPending(), "one owner turn settles the picker once");
    session.OnFinished([&](const FileDialogResult&) { ++delivered; });
    Check(delivered == 2 && !session.Cancel(), "late observers see completion and completed cancellation is a no-op");
    session.OnFinished([](const FileDialogResult&) { throw std::runtime_error("Observer failed"); });
    Check(session.GetCallbackError() != nullptr, "picker observer errors remain inspectable");
    auto cancel = services.ShowFileDialog();
    Check(cancel.Cancel() && cancel.GetResult()->m_Status == FileDialogStatus::Cancelled,
          "pending cancellation has one explicit outcome");
    queue.Drain();
    Check(cancel.GetResult()->m_Status == FileDialogStatus::Cancelled,
          "late native completion cannot replace cancellation");
    auto closing = services.ShowFileDialog();
    services.Close();
    Check(closing.GetResult()->m_Status == FileDialogStatus::OwnerClosed, "owner closure settles pending pickers");
    Check(services.ShowFileDialog().GetResult()->m_Status == FileDialogStatus::OwnerClosed,
          "closed services reject new picker work");
    Check(services.ReadClipboard().m_Result.m_Status == PlatformStatus::Closed,
          "closed clipboard access has an explicit status");
    queue.Drain();
    queue.Close();
}

void VerifyWorkerAndOverload()
{
    UiDispatcher queue(1);
    HeldServices services(queue);
    auto session = services.ShowFileDialog();
    const auto occupied = queue.Post([] {});
    std::thread worker([complete = services.m_Complete] { complete({FileDialogStatus::Accepted, {"worker.txt"}}); });
    worker.join();
    Check(session.IsPending(), "native producer publication never invokes owner observers inline");
    queue.Drain();
    services.Pump();
    Check(session.GetResult()->m_Status == FileDialogStatus::Accepted,
          "a reserved result survives queue saturation and a rejected wake hint");
    Check(occupied.GetStatus() == DispatchStatus::Completed, "native results preserve existing queued work");
    std::atomic<bool> wrongThread = false;
    std::thread invalid(
        [&]
        {
            try
            {
                services.ReadClipboard();
            }
            catch (const std::logic_error&)
            {
                wrongThread = true;
            }
        });
    invalid.join();
    Check(wrongThread, "native service access checks ownership before native calls");
    Check(services.ReadClipboard().m_Result.m_Status == PlatformStatus::Failed,
          "native read failures become explicit outcomes");
    Check(services.SetClipboardText("copy").m_Status == PlatformStatus::Failed,
          "native write failures become explicit outcomes");
    auto pending = services.ShowFileDialog();
    auto stop = services.m_Stop;
    services.Close();
    Check(stop.stop_requested() && pending.GetResult()->m_Status == FileDialogStatus::OwnerClosed,
          "owner closure cancels native computation before resource retirement");
    queue.Close();
}

void VerifyContextIntegration()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto adapter = std::make_unique<HeldServices>(ui.GetDispatcher());
    auto* previous = adapter.get();
    ui.SetPlatformServices(std::move(adapter));
    auto session = ui.Platform().ShowFileDialog();
    bool closedBeforeObserver = false;
    session.OnFinished(
        [&](const FileDialogResult& result)
        {
            closedBeforeObserver = result.m_Status == FileDialogStatus::OwnerClosed && previous->IsClosed();
            ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(ui.GetDispatcher()));
        });
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(ui.GetDispatcher()));
    Check(closedBeforeObserver && !ui.Platform().IsClosed(),
          "reentrant adapter replacement commits before old session observers");
    Throws([&] { ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(UiDispatcher{})); },
           "foreign dispatcher adapter rejected before replacement");
    ui.States().Batch(
        [&] { Throws([&] { ui.Platform().SetClipboardText("staged"); }, "native calls reject uncommitted state"); });
    PlatformHost host;
    host.m_Dispatcher = ui.GetDispatcher();
    host.m_FocusWindow = []() -> PlatformResult { throw std::runtime_error("Native focus error"); };
    host.m_SetCursor = [](CursorShape) -> PlatformResult { throw std::runtime_error("Native cursor error"); };
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(host));
    Check(ui.Platform().FocusWindow().m_Status == PlatformStatus::Failed &&
              ui.Platform().SetCursor(CursorShape::Hand).m_Status == PlatformStatus::Failed,
          "native host failures are explicit results");
    ui.GetDispatcher().Close();
    Check(ui.Platform().FocusWindow().m_Status == PlatformStatus::Closed &&
              ui.Platform().SetCursor(CursorShape::Hand).m_Status == PlatformStatus::Closed,
          "closed queue prevents native window calls");
}

} // namespace

int main()
{
    try
    {
        VerifyData();
        VerifyCoordinates();
        VerifyMemory();
        VerifyWorkerAndOverload();
        VerifyContextIntegration();
        std::cout << "Platform services: " << s_Checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Platform services: " << error.what() << '\n';
        return 1;
    }
}
