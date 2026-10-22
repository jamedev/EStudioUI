#include "NativeAccessibility.h"
#include "NativePlatformServices.h"
#include "WindowsSupport.h"

#include <EStudioUI/Button.h>
#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/UiContext.h>

#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
using Microsoft::WRL::ComPtr;
int s_Checks = 0;

void Trace(const char* message)
{
    std::cout << "Native probe: " << message << std::endl;
}

class ProbeDeadline
{
  public:
    ProbeDeadline()
        : m_Thread(
              [this](std::stop_token stop)
              {
                  std::unique_lock lock(m_Mutex);
                  m_Changed.wait_for(lock, stop, std::chrono::seconds(70), [] { return false; });
                  if (!stop.stop_requested())
                  {
                      std::fputs("Native probe exceeded 70 seconds; see the last completed stage.\n", stderr);
                      std::fflush(stderr);
                      TerminateProcess(GetCurrentProcess(), 2);
                  }
              })
    {
    }

  private:
    std::mutex m_Mutex;
    std::condition_variable_any m_Changed;
    std::jthread m_Thread;
};

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
        throw std::runtime_error(message);
}

void Require(HRESULT code, const char* message)
{
    if (FAILED(code))
        throw std::runtime_error(std::string(message) + " HRESULT=" + std::to_string(code));
}

struct NativeOwner
{
    HWND m_Window = nullptr;

    NativeOwner()
    {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"EStudioUI.NativePlatform.Test";
        RegisterClassW(&type);
        m_Window = CreateWindowExW(0, type.lpszClassName, L"EStudioUI native platform test", WS_OVERLAPPEDWINDOW, 40,
                                   40, 820, 640, nullptr, nullptr, type.hInstance, nullptr);
        if (!m_Window)
            throw std::runtime_error("Create native platform test owner");
    }

    ~NativeOwner()
    {
        if (m_Window)
            DestroyWindow(m_Window);
    }
};

void Pump(UiContext& ui)
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    ui.GetDispatcher().Drain();
    ui.PumpPlatformServices();
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
}

template <typename Predicate> bool Until(UiContext& ui, Predicate predicate, int milliseconds = 10000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        Pump(ui);
    return predicate();
}

PlatformHost Host(UiContext& ui, HWND window)
{
    PlatformHost host;
    host.m_Dispatcher = ui.GetDispatcher();
    host.m_NativeWindow = reinterpret_cast<std::uintptr_t>(window);
    host.m_WakeNativeLoop = [window] { PostMessageW(window, WM_NULL, 0, 0); };
    host.m_GetMetrics = [window]
    {
        RECT client{};
        POINT origin{};
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
        WindowMetrics metrics;
        metrics.m_LogicalSize = {800, 600};
        metrics.m_FramebufferSize = {static_cast<float>(client.right), static_cast<float>(client.bottom)};
        metrics.m_ScreenOrigin = {static_cast<float>(origin.x), static_cast<float>(origin.y)};
        metrics.m_HasScreenCoordinates = true;
        return metrics;
    };
    host.m_FocusWindow = [] { return PlatformResult{}; };
    return host;
}

ComPtr<IUIAutomationElement> Find(IUIAutomation& automation, IUIAutomationElement& root, const wchar_t* id)
{
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition;
    const auto code = automation.CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value);
    Require(code, "Create native automation condition");
    ComPtr<IUIAutomationElement> element;
    Require(root.FindFirst(TreeScope_Descendants, condition.Get(), &element), "Find native semantic child");
    if (!element)
        throw std::runtime_error("Native semantic child was not exposed");
    return element;
}

void Accessibility(UiContext& ui, HWND window)
{
    auto& button = ui.Root().Add<Button>("Native invoke");
    button.SetId("invoke");
    auto& text = ui.Root().Add<TextBox>("Before");
    text.SetId("value");
    text.SetAccessibleName("Project title");
    auto& slider = ui.Root().Add<Slider>();
    slider.SetId("range");
    slider.SetRange(-5, 50);
    auto& toggle = ui.Root().Add<ToolButton>("Grid");
    toggle.SetId("toggle");
    toggle.SetCheckable(true);
    ui.Prepare(800, 600);
    int invocations = 0;
    button.Clicked().Connect([&] { ++invocations; });
    Trace("attach UI Automation provider");
    Detail::NativeAccessibility bridge(ui, window);
    std::atomic<bool> done = false;
    std::atomic<bool> readyToClose = false;
    std::atomic<bool> closed = false;
    std::exception_ptr error;
    // UI Automation clients run on an MTA thread; the owner continues its normal queue/message processing.
    std::jthread client(
        [&]
        {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            try
            {
                Require(initialized, "Initialize native automation client MTA");
                ComPtr<IUIAutomation> automation;
                Require(
                    CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)),
                    "Create real Windows automation client");
                ComPtr<IUIAutomationElement> root;
                Require(automation->ElementFromHandle(window, &root), "Obtain native HWND automation root");
                Trace("UI Automation root connected");
                auto invokeElement = Find(*automation.Get(), *root.Get(), L"invoke");
                BSTR name = nullptr;
                Require(invokeElement->get_CurrentName(&name), "Read native accessible name");
                const bool named = name && std::wstring_view(name) == L"Native invoke";
                SysFreeString(name);
                if (!named)
                    throw std::runtime_error("Native button caption was not exposed");
                ComPtr<IUIAutomationInvokePattern> invoke;
                Require(invokeElement->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)),
                        "Obtain invoke pattern");
                Require(invoke->Invoke(), "Native invoke action");
                Trace("UI Automation invoke requested");
                auto valueElement = Find(*automation.Get(), *root.Get(), L"value");
                ComPtr<IUIAutomationValuePattern> value;
                Require(valueElement->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value)),
                        "Obtain value pattern");
                BSTR replacement = SysAllocString(L"After Ω");
                if (!replacement)
                    throw std::bad_alloc();
                const auto editedValue = value->SetValue(replacement);
                SysFreeString(replacement);
                Require(editedValue, "Native Unicode value edit");
                BSTR contents = nullptr;
                Require(value->get_CurrentValue(&contents), "Read native edited value");
                const bool edited = contents && std::wstring_view(contents) == L"After Ω";
                SysFreeString(contents);
                if (!edited)
                    throw std::runtime_error("Native value edit did not commit");
                Trace("UI Automation value committed");
                auto rangeElement = Find(*automation.Get(), *root.Get(), L"range");
                ComPtr<IUIAutomationRangeValuePattern> range;
                Require(rangeElement->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range)),
                        "Obtain range pattern");
                Require(range->SetValue(12.5), "Native range edit");
                if (SUCCEEDED(range->SetValue(100)))
                    throw std::runtime_error("Out of range native value was accepted");
                Trace("UI Automation range committed");
                auto toggleElement = Find(*automation.Get(), *root.Get(), L"toggle");
                ComPtr<IUIAutomationTogglePattern> toggler;
                Require(toggleElement->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggler)),
                        "Obtain toggle pattern");
                Require(toggler->Toggle(), "Native toggle action");
                ToggleState checked{};
                Require(toggler->get_CurrentToggleState(&checked), "Read native toggle state");
                if (checked != ToggleState_On)
                    throw std::runtime_error("Native toggle did not commit");
                Trace("UI Automation patterns committed");
                readyToClose = true;
                while (!closed)
                    SwitchToThread();
                BSTR denied = SysAllocString(L"Denied after closure");
                if (!denied)
                    throw std::bad_alloc();
                const auto expired = value->SetValue(denied);
                SysFreeString(denied);
                if (SUCCEEDED(expired))
                    throw std::runtime_error("Retained native provider accepted an edit after bridge closure");
                Trace("UI Automation retained edit rejected");
            }
            catch (...)
            {
                error = std::current_exception();
            }
            Trace("UI Automation client releases COM");
            if (SUCCEEDED(initialized))
                CoUninitialize();
            done = true;
            Trace("UI Automation client stopped");
        });
    const bool connected = Until(ui, [&] { return done.load() || readyToClose.load(); }, 20000);
    Trace("flush UI Automation events");
    bridge.Flush();
    Trace("disconnect UI Automation provider");
    bridge.Close();
    Trace("provider disconnected");
    closed = true;
    // COM teardown may still call the owner's STA. Keep messages and the queue
    // moving until the client has actually stopped; the process watchdog bounds failure.
    const bool promptlyStopped = Until(ui, [&] { return done.load(); }, 10000);
    while (!done)
        Pump(ui);
    client.join();
    Check(connected, "Native automation client timed out");
    Check(promptlyStopped, "Native automation client cleanup exceeded ten seconds");
    if (error)
        std::rethrow_exception(error);
    Check(invocations == 1 && text.GetText() == "After Ω" && slider.GetValue() == 12.5 && toggle.IsChecked(),
          "Real UI Automation client uses canonical control actions");
    Check(true, "Retained native value provider expires before HWND teardown");
}

void PublishNativeBytes(UINT format, const void* bytes, std::size_t count)
{
    const auto block = GlobalAlloc(GMEM_MOVEABLE, count);
    if (!block)
        throw std::bad_alloc();
    auto* destination = GlobalLock(block);
    if (!destination)
    {
        GlobalFree(block);
        throw std::runtime_error("Lock native clipboard test block");
    }
    std::memcpy(destination, bytes, count);
    GlobalUnlock(block);
    if (!SetClipboardData(format, block))
    {
        GlobalFree(block);
        throw std::runtime_error("Publish native clipboard test block");
    }
}

void Clipboard(UiContext& ui, HWND window)
{
    Trace("native clipboard formats");
    auto& services = ui.Platform();
    ClipboardData data;
    data.m_Text = "Windows Ω\nsecond line";
    data.m_Html = "<b>Windows Ω</b>";
    data.m_Files = {std::filesystem::temp_directory_path() / L"EStudioUI Ω.txt"};
    data.m_Custom["application/x-estudioui-test"] = {0, 1, 0, 255};
    data.m_Custom["application/x-estudioui-empty"] = {};
    Check(RegisterClipboardFormatW(L"estudioui.mime:APPLICATION/X-ESTUDIOUI-TEST") != 0,
          "External native application can register equivalent format spelling first");
    Check(static_cast<bool>(services.WriteClipboard(data)), "Publish real Windows clipboard formats");
    const auto read = services.ReadClipboard();
    Check(read.m_Result && read.m_Data.m_Text == data.m_Text && read.m_Data.m_Html == data.m_Html &&
              read.m_Data.m_Files == data.m_Files && read.m_Data.m_Custom == data.m_Custom,
          "Native Unicode, HTML, CF_HDROP and binary custom formats round trip");
    Check(OpenClipboard(nullptr) != FALSE, "Inspect real native clipboard");
    const auto block = GetClipboardData(CF_UNICODETEXT);
    const auto* wide = static_cast<const wchar_t*>(GlobalLock(block));
    const bool crlf = wide && std::wstring_view(wide).find(L"\r\n") != std::wstring_view::npos;
    if (wide)
        GlobalUnlock(block);
    CloseClipboard();
    Check(crlf, "Native Unicode clipboard uses Windows CRLF without changing logical LF values");
    std::atomic<bool> held = false, release = false;
    std::jthread lock(
        [&]
        {
            held = OpenClipboard(nullptr) != FALSE;
            while (!release)
                SwitchToThread();
            if (held)
                CloseClipboard();
        });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!held && std::chrono::steady_clock::now() < deadline)
        SwitchToThread();
    const bool locked = held;
    const auto busy = locked ? services.ReadClipboard().m_Result.m_Status : PlatformStatus::Failed;
    release = true;
    lock.join();
    Check(locked && busy == PlatformStatus::Busy,
          "Busy native clipboard is explicit and does not wait on the UI owner");
    Trace("malformed external clipboard format");
    Check(OpenClipboard(window) != FALSE, "Open native clipboard for malformed format probe");
    EmptyClipboard();
    const auto malformed = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 32);
    SetClipboardData(RegisterClipboardFormatW(L"HTML Format"), malformed);
    CloseClipboard();
    Check(services.ReadClipboard().m_Result.m_Status == PlatformStatus::Failed,
          "Malformed external HTML is bounded and diagnosed");
    Trace("aggregate external clipboard budget");
    Check(OpenClipboard(window) != FALSE, "Open native clipboard for aggregate budget probe");
    EmptyClipboard();
    {
        const std::wstring text(5 * 1024 * 1024, L'x');
        const std::string html = "<!--StartFragment-->" + std::string(5 * 1024 * 1024, 'h') + "<!--EndFragment-->";
        PublishNativeBytes(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        PublishNativeBytes(RegisterClipboardFormatW(L"HTML Format"), html.c_str(), html.size() + 1);
    }
    CloseClipboard();
    Check(services.ReadClipboard().m_Result.m_Status == PlatformStatus::Failed,
          "Combined external formats obey the aggregate native payload budget");
    services.SetClipboardText("");
    Trace("native clipboard checks complete");
}

BOOL CALLBACK FindPicker(HWND window, LPARAM data)
{
    auto* owner = reinterpret_cast<HWND*>(data);
    if (GetWindow(window, GW_OWNER) == owner[0] && IsWindowVisible(window))
    {
        owner[1] = window;
        return FALSE;
    }
    return TRUE;
}

void Pickers(UiContext& ui, HWND window)
{
    for (const auto kind : {FileDialogKind::OpenFile, FileDialogKind::SaveFile, FileDialogKind::SelectFolder})
    {
        Trace(kind == FileDialogKind::OpenFile   ? "open native file picker"
              : kind == FileDialogKind::SaveFile ? "open native save picker"
                                                 : "open native folder picker");
        FileDialogOptions options;
        options.m_Kind = kind;
        options.m_Title = "EStudioUI automatic cancellation probe";
        if (kind == FileDialogKind::SaveFile)
        {
            options.m_FileName = "probe.txt";
            options.m_DefaultExtension = "txt";
        }
        if (kind != FileDialogKind::SelectFolder)
            options.m_Filters = {{"Text", "*.txt"}};
        const auto begin = std::chrono::steady_clock::now();
        auto session = ui.Platform().ShowFileDialog(options);
        int completions = 0;
        session.OnFinished([&](const FileDialogResult&) { ++completions; });
        Check(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(500),
              "Native picker opens asynchronously");
        HWND windows[2] = {window, nullptr};
        const bool shown = Until(ui,
                                 [&]
                                 {
                                     EnumWindows(FindPicker, reinterpret_cast<LPARAM>(windows));
                                     return windows[1] || !session.IsPending();
                                 });
        const bool actual = shown && windows[1];
        Trace("cancel native picker");
        Check(session.Cancel() && !session.Cancel(), "Native picker cancellation settles only once");
        Check(Until(ui, [&] { return !ui.HasPendingPlatformWork(); }),
              "Cancelled native STA unwinds while owner messages continue");
        Check(actual, "Actual native IFileDialog was shown before cancellation");
        Check(session.GetResult()->m_Status == FileDialogStatus::Cancelled && completions == 1,
              "Native cancellation delivers exactly one result after the STA unwinds");
    }
    Trace("retire native picker adapter");
    auto session = ui.Platform().ShowFileDialog({});
    int completions = 0;
    session.OnFinished([&](const FileDialogResult&) { ++completions; });
    HWND windows[2] = {window, nullptr};
    const bool shown = Until(ui,
                             [&]
                             {
                                 EnumWindows(FindPicker, reinterpret_cast<LPARAM>(windows));
                                 return windows[1] || !session.IsPending();
                             });
    Check(shown && windows[1], "Native picker is visible before its adapter is retired");
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(ui.GetDispatcher()));
    Check(session.GetResult()->m_Status == FileDialogStatus::OwnerClosed && completions == 1,
          "Replacing native adapter settles its pending picker");
    Check(Until(ui, [&] { return !ui.HasPendingPlatformWork(); }),
          "Replaced native adapter is retired without blocking UI teardown");
    Check(completions == 1 && IsWindow(window),
          "Late native completion cannot redeliver a result or destroy its retained owner");
    Trace("cancel native picker during startup");
    ui.SetPlatformServices(Detail::CreateNativePlatformServices(Host(ui, window)));
    session = ui.Platform().ShowFileDialog({});
    Check(session.Cancel(), "Native picker can be cancelled before showing its window");
    Check(Until(ui, [&] { return !ui.HasPendingPlatformWork(); }),
          "Startup cancellation unwinds the native STA without a visible-dialog requirement");
    Check(session.GetResult()->m_Status == FileDialogStatus::Cancelled,
          "Late native startup cannot replace the cancelled session result");
}
} // namespace

int main()
{
    ProbeDeadline deadline;
    std::cout << std::unitbuf;
    Trace("initialize owner STA");
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try
    {
        Require(initialized, "Initialize native platform test STA");
        NativeOwner native;
        FontFace font(ESTUDIOUI_FONT_PATH);
        UiContext ui(font);
        ui.GetDispatcher().SetWakeup([window = native.m_Window] { PostMessageW(window, WM_NULL, 0, 0); });
        ui.SetPlatformServices(Detail::CreateNativePlatformServices(Host(ui, native.m_Window)));
        Check(ui.Platform().GetCapabilities().Get(PlatformFeature::ClipboardHtml) == PlatformSupport::Native,
              "Windows adapter reports actual rich clipboard capability");
        Accessibility(ui, native.m_Window);
        wchar_t nativeFlag[2]{};
        if (GetEnvironmentVariableW(L"ESTUDIOUI_NATIVE_PLATFORM_TESTS", nativeFlag, 2) == 1 && nativeFlag[0] == L'1')
        {
            Clipboard(ui, native.m_Window);
            Pickers(ui, native.m_Window);
        }
        else
            std::cout << "Clipboard mutation and visible picker probes require ESTUDIOUI_NATIVE_PLATFORM_TESTS=1.\n";
        std::cout << "Native platform: " << s_Checks << " checks passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    Trace("release owner COM");
    if (SUCCEEDED(initialized))
        CoUninitialize();
    Trace("native probe finished");
    return result;
}
