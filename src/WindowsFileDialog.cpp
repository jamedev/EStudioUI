#include "WindowsFileDialog.h"

#ifdef _WIN32
#include <shobjidl.h>
#include <shlobj.h>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace EStudioUI::Detail
{
namespace
{
constexpr UINT s_Cancel = WM_APP + 0x31b;
constexpr UINT_PTR s_CancelTimer = 0x31b;

template <typename T> class ComPointer
{
  public:
    ~ComPointer()
    {
        if (m_Value)
            m_Value->Release();
    }

    T** Put()
    {
        return &m_Value;
    }

    T* Get() const
    {
        return m_Value;
    }

    T* operator->() const
    {
        return m_Value;
    }

  private:
    T* m_Value = nullptr;
};

void Require(HRESULT result, std::string_view operation)
{
    if (FAILED(result))
        throw std::runtime_error(WindowsFailure(result, operation).m_Message);
}

struct WorkerState
{
    mutable std::mutex m_Mutex;
    HWND m_Control = nullptr;
    IFileDialog* m_Dialog = nullptr;
    bool m_Closing = false;
    bool m_Cancelled = false;
    bool m_Finished = false;

    void Cancel()
    {
        const std::lock_guard lock(m_Mutex);
        m_Cancelled = true;
        if (m_Control)
            PostMessageW(m_Control, s_Cancel, 0, 0);
    }

    bool Cancelled() const
    {
        const std::lock_guard lock(m_Mutex);
        return m_Cancelled;
    }

    void RequestNativeClose(IFileDialog* dialog)
    {
        // Only the picker STA calls this method. Shell callbacks can reenter it.
        if (!dialog || m_Closing)
            return;
        m_Closing = true;
        dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        m_Closing = false;
    }
};

LRESULT CALLBACK ControlProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<WorkerState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if ((message == s_Cancel || (message == WM_TIMER && wParam == s_CancelTimer)) && state)
    {
        // A visible picker HWND does not prove Show has finished initialization.
        // Keep cancellation armed until Show returns, including an early Close
        // that fails or is consumed by the shell's initialization message loop.
        if (message == s_Cancel)
            SetTimer(window, s_CancelTimer, 16, nullptr);
        state->RequestNativeClose(state->m_Dialog);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateControl(WorkerState& state)
{
    static std::once_flag registered;
    std::call_once(registered,
                   []
                   {
                       WNDCLASSW type{};
                       type.lpfnWndProc = ControlProcedure;
                       type.hInstance = GetModuleHandleW(nullptr);
                       type.lpszClassName = L"EStudioUI.FilePicker.Control";
                       if (!RegisterClassW(&type))
                           throw std::runtime_error("Could not register the native picker control window.");
                   });
    HWND window = CreateWindowExW(0, L"EStudioUI.FilePicker.Control", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  GetModuleHandleW(nullptr), &state);
    if (!window)
        throw std::runtime_error("Could not create the native picker control window.");
    return window;
}

class CancellationEvents final : public IFileDialogEvents
{
  public:
    explicit CancellationEvents(WorkerState& state) : m_State(state)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IFileDialogEvents))
            return E_NOINTERFACE;
        *result = static_cast<IFileDialogEvents*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_References;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto count = --m_References;
        if (!count)
            delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog* dialog) override
    {
        return CheckCancellation(dialog);
    }

    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog* dialog, IShellItem*) override
    {
        return CheckCancellation(dialog);
    }

    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog* dialog) override
    {
        return CheckCancellation(dialog);
    }

    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog* dialog) override
    {
        return CheckCancellation(dialog);
    }

    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog* dialog, IShellItem*,
                                               FDE_SHAREVIOLATION_RESPONSE* response) override
    {
        if (response)
            *response = FDESVR_DEFAULT;
        return CheckCancellation(dialog);
    }

    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog* dialog) override
    {
        return CheckCancellation(dialog);
    }

    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog* dialog, IShellItem*, FDE_OVERWRITE_RESPONSE* response) override
    {
        if (response)
            *response = FDEOR_DEFAULT;
        return CheckCancellation(dialog);
    }

  private:
    HRESULT CheckCancellation(IFileDialog* dialog)
    {
        // OnFolderChange/OnTypeChange also run when the shell opens the picker.
        // This covers cancellation delivered before its modal message loop.
        if (m_State.Cancelled())
            m_State.RequestNativeClose(dialog);
        return S_OK;
    }

    std::atomic<ULONG> m_References = 1;
    WorkerState& m_State;
};

class EventLease
{
  public:
    EventLease(IFileDialog& dialog, WorkerState& state) : m_Dialog(dialog)
    {
        *m_Events.Put() = new CancellationEvents(state);
        Require(m_Dialog.Advise(m_Events.Get(), &m_Cookie), "Observe native picker initialization");
    }

    ~EventLease()
    {
        m_Dialog.Unadvise(m_Cookie);
    }

  private:
    IFileDialog& m_Dialog;
    ComPointer<IFileDialogEvents> m_Events;
    DWORD m_Cookie = 0;
};

std::filesystem::path ItemPath(IShellItem& item)
{
    wchar_t* name = nullptr;
    Require(item.GetDisplayName(SIGDN_FILESYSPATH, &name), "Read native picker path");
    try
    {
        std::filesystem::path path(name);
        CoTaskMemFree(name);
        return path;
    }
    catch (...)
    {
        CoTaskMemFree(name);
        throw;
    }
}

FileDialogResult Pick(HWND owner, const FileDialogOptions& options, WorkerState& state)
{
    ComPointer<IFileDialog> dialog;
    Require(CoCreateInstance(options.m_Kind == FileDialogKind::SaveFile ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
                             nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.Put())),
            "Create native file picker");
    DWORD flags = 0;
    Require(dialog->GetOptions(&flags), "Read native picker options");
    flags |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
    if (options.m_Kind == FileDialogKind::SelectFolder)
        flags |= FOS_PICKFOLDERS;
    else if (options.m_Kind == FileDialogKind::SaveFile)
        flags |= FOS_OVERWRITEPROMPT;
    else
        flags |= FOS_FILEMUSTEXIST;
    if (options.m_Multiple)
        flags |= FOS_ALLOWMULTISELECT;
    Require(dialog->SetOptions(flags), "Configure native picker options");
    if (!options.m_Title.empty())
        Require(dialog->SetTitle(ToWindowsText(options.m_Title).c_str()), "Set native picker caption");
    if (!options.m_InitialFolder.empty())
    {
        ComPointer<IShellItem> folder;
        Require(SHCreateItemFromParsingName(options.m_InitialFolder.c_str(), nullptr, IID_PPV_ARGS(folder.Put())),
                "Resolve initial picker folder");
        Require(dialog->SetFolder(folder.Get()), "Set initial picker folder");
    }
    if (!options.m_FileName.empty())
        Require(dialog->SetFileName(options.m_FileName.c_str()), "Set initial picker filename");
    if (!options.m_DefaultExtension.empty())
        Require(dialog->SetDefaultExtension(ToWindowsText(options.m_DefaultExtension).c_str()), "Set picker extension");
    std::vector<std::wstring> names, patterns;
    std::vector<COMDLG_FILTERSPEC> filters;
    names.reserve(options.m_Filters.size());
    patterns.reserve(options.m_Filters.size());
    for (const auto& filter : options.m_Filters)
    {
        names.push_back(ToWindowsText(filter.m_Name));
        patterns.push_back(ToWindowsText(filter.m_Pattern));
    }
    for (std::size_t i = 0; i < names.size(); ++i)
        filters.push_back({names[i].c_str(), patterns[i].c_str()});
    if (!filters.empty())
        Require(dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data()), "Set native picker filters");
    if (state.Cancelled())
        return {FileDialogStatus::Cancelled};
    const EventLease events(*dialog.Get(), state);
    state.m_Dialog = dialog.Get();
    const HRESULT shown = dialog->Show(owner);
    state.m_Dialog = nullptr;
    KillTimer(state.m_Control, s_CancelTimer);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED) || state.Cancelled())
        return {FileDialogStatus::Cancelled};
    if (FAILED(shown))
    {
        const auto error = WindowsFailure(shown, "Show native file picker");
        return {FileDialogStatus::Failed, {}, error.m_Message, error.m_NativeCode};
    }
    FileDialogResult result;
    result.m_Status = FileDialogStatus::Accepted;
    if (options.m_Multiple)
    {
        ComPointer<IFileOpenDialog> open;
        Require(dialog->QueryInterface(IID_PPV_ARGS(open.Put())), "Query multiple-selection picker");
        ComPointer<IShellItemArray> items;
        Require(open->GetResults(items.Put()), "Read native picker selections");
        DWORD count = 0;
        Require(items->GetCount(&count), "Count native picker selections");
        if (count > 512)
            throw std::length_error("The native picker returned more than 512 paths.");
        for (DWORD i = 0; i < count; ++i)
        {
            ComPointer<IShellItem> item;
            Require(items->GetItemAt(i, item.Put()), "Read native picker selection");
            result.m_Paths.push_back(ItemPath(*item.Get()));
        }
    }
    else
    {
        ComPointer<IShellItem> item;
        Require(dialog->GetResult(item.Put()), "Read native picker selection");
        result.m_Paths.push_back(ItemPath(*item.Get()));
    }
    return result;
}
} // namespace

struct WindowsFileDialog::Impl
{
    HWND m_Owner;
    std::function<void()> m_Wake;
    std::shared_ptr<WorkerState> m_State;
    std::jthread m_Thread;

    Impl(HWND owner, std::function<void()> wake) : m_Owner(owner), m_Wake(std::move(wake))
    {
    }
};

WindowsFileDialog::WindowsFileDialog(HWND owner, std::function<void()> wake)
    : m_Impl(std::make_unique<Impl>(owner, std::move(wake)))
{
}

WindowsFileDialog::~WindowsFileDialog()
{
    Cancel();
}

bool WindowsFileDialog::IsRunning() const
{
    if (!m_Impl->m_State)
        return false;
    const std::lock_guard lock(m_Impl->m_State->m_Mutex);
    return !m_Impl->m_State->m_Finished;
}

void WindowsFileDialog::Cancel()
{
    if (m_Impl->m_State)
        m_Impl->m_State->Cancel();
}

void WindowsFileDialog::Begin(FileDialogOptions options, std::stop_token stop,
                              std::function<void(FileDialogResult)> complete)
{
    if (IsRunning())
    {
        complete({FileDialogStatus::Busy, {}, "This window already has a native picker open."});
        return;
    }
    if (m_Impl->m_Thread.joinable())
        m_Impl->m_Thread.join();
    if (!options.m_InitialFolder.empty())
        options.m_InitialFolder = std::filesystem::absolute(options.m_InitialFolder);
    const auto state = std::make_shared<WorkerState>();
    m_Impl->m_State = state;
    try
    {
        m_Impl->m_Thread = std::jthread(
            [state, options = std::move(options), stop, complete = std::move(complete), owner = m_Impl->m_Owner,
             wake = m_Impl->m_Wake]
            {
                const std::stop_callback cancelled(stop, [state] { state->Cancel(); });
                const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                FileDialogResult result;
                HWND control = nullptr;
                try
                {
                    Require(initialized, "Initialize native picker STA");
                    control = CreateControl(*state);
                    {
                        const std::lock_guard lock(state->m_Mutex);
                        state->m_Control = control;
                    }
                    result = Pick(owner, options, *state);
                }
                catch (const std::exception& error)
                {
                    result = {FileDialogStatus::Failed, {}, error.what()};
                }
                catch (...)
                {
                    result = {FileDialogStatus::Failed, {}, "The native picker worker failed."};
                }
                {
                    const std::lock_guard lock(state->m_Mutex);
                    state->m_Control = nullptr;
                }
                if (control)
                    DestroyWindow(control);
                if (SUCCEEDED(initialized))
                    CoUninitialize();
                complete(std::move(result));
                // Hold the same lock used by IsRunning until the last GLFW wake
                // returns. Retirement cannot terminate GLFW before that wake.
                const std::lock_guard lock(state->m_Mutex);
                state->m_Finished = true;
                if (wake)
                    try
                    {
                        wake();
                    }
                    catch (...)
                    {
                    }
            });
    }
    catch (...)
    {
        const std::lock_guard lock(state->m_Mutex);
        state->m_Finished = true;
        throw;
    }
}
} // namespace EStudioUI::Detail
#endif
