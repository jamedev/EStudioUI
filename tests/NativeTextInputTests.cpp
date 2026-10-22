#include "NativeTextInput.h"
#include "WindowsSupport.h"

#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/TextInputClient.h>
#include <EStudioUI/UiContext.h>

#include <imm.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

struct NativeOwner
{
    HWND m_Window = nullptr;
    HIMC m_Previous = nullptr;
    HIMC m_Context = nullptr;

    NativeOwner()
    {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"EStudioUI.NativeTextInput.Test";
        Check(RegisterClassW(&type) != 0, "Register native composition probe class");
        m_Window = CreateWindowW(type.lpszClassName, L"EStudioUI native composition test", WS_OVERLAPPEDWINDOW, 40, 40,
                                 640, 400, nullptr, nullptr, type.hInstance, nullptr);
        Check(m_Window != nullptr, "Create real HWND without a GPU requirement");
        m_Context = ImmCreateContext();
        Check(m_Context != nullptr, "Create original native input context");
        m_Previous = ImmAssociateContext(m_Window, m_Context);
    }

    ~NativeOwner()
    {
        if (m_Window)
        {
            ImmAssociateContext(m_Window, m_Previous);
            if (m_Context)
                ImmDestroyContext(m_Context);
            DestroyWindow(m_Window);
        }
    }
};

class ProbeEditor final : public Widget, public TextInputClient
{
  public:
    ProbeEditor() : Widget("probe-editor")
    {
    }

    bool IsFocusable() const override
    {
        return true;
    }

    TextInputState GetTextInputState() const override
    {
        ++m_Snapshots;
        TextInputState state;
        state.m_Text = "A\xF0\x9F\x98\x80Z";
        state.m_Anchor = 1;
        state.m_Caret = 5;
        const auto bounds = GetBounds();
        state.m_CaretBounds = {bounds.m_X + 7, bounds.m_Y + 3, 2, 19};
        state.m_Composing = m_Composing;
        return state;
    }

    bool OnTextComposition(const TextCompositionEvent& event) override
    {
        ++m_Updates;
        m_Composing = event.m_Kind == TextCompositionKind::Preedit;
        if (event.m_Kind == TextCompositionKind::Cancel)
            ++m_Cancellations;
        if (event.m_Kind == TextCompositionKind::Commit)
            m_Committed += event.m_Text;
        if (auto* ui = GetContext())
            ui->NotifyTextInputChanged();
        return true;
    }

    int m_Updates = 0;
    int m_Cancellations = 0;
    bool m_Composing = false;
    std::string m_Committed;
    mutable int m_Snapshots = 0;
};

void NativeInput(FontFace& font)
{
    NativeOwner native;
    UiContext ui(font);
    auto& editor = ui.Root().Add<ProbeEditor>();
    editor.SetFloatingBounds({30, 40, 200, 40});
    PlatformHost host;
    Point scale{1.5f, 1.25f};
    host.m_Dispatcher = ui.GetDispatcher();
    host.m_GetMetrics = [&]
    {
        POINT origin{};
        ClientToScreen(native.m_Window, &origin);
        WindowMetrics metrics;
        metrics.m_LogicalSize = {640, 400};
        metrics.m_FramebufferSize = {960, 500};
        metrics.m_ContentScale = scale;
        metrics.m_InputScale = scale;
        metrics.m_PixelScale = scale;
        metrics.m_ScreenOrigin = {static_cast<float>(origin.x), static_cast<float>(origin.y)};
        metrics.m_HasScreenCoordinates = true;
        return metrics;
    };
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(std::move(host)));
    ui.Prepare(640, 400);
    ui.SetFocus(&editor);
    Detail::NativeTextInput adapter(ui, native.m_Window);
    adapter.Flush();
    const auto context = ImmGetContext(native.m_Window);
    Check(context && context != native.m_Context, "Adapter owns a dedicated IMM context");
    ImmReleaseContext(native.m_Window, context);

    COMPOSITIONFORM composition{};
    Check(ImmGetCompositionWindow(context, &composition) != FALSE, "Read actual IMM composition-window geometry");
    const auto state = ui.GetTextInputState();
    POINT expected{static_cast<LONG>(std::lround(state.m_ScreenCaretBounds->m_X)),
                   static_cast<LONG>(std::lround(state.m_ScreenCaretBounds->m_Y))};
    ScreenToClient(native.m_Window, &expected);
    Check(composition.dwStyle == CFS_POINT && composition.ptCurrentPos.x == expected.x &&
              composition.ptCurrentPos.y == expected.y,
          "IMM composition geometry uses scaled physical client coordinates");
    CANDIDATEFORM candidate{};
    Check(ImmGetCandidateWindow(context, 0, &candidate) != FALSE && candidate.dwStyle == CFS_EXCLUDE &&
              candidate.rcArea.left == expected.x && candidate.rcArea.top == expected.y &&
              candidate.rcArea.bottom > candidate.rcArea.top,
          "Actual native candidate exclusion follows the prepared caret rectangle");
    const int snapshots = editor.m_Snapshots;
    for (int i = 0; i < 100; ++i)
        adapter.Flush();
    Check(editor.m_Snapshots == snapshots, "Unchanged native service turns do not copy or query the editor text");

    const auto required = SendMessageW(native.m_Window, WM_IME_REQUEST, IMR_DOCUMENTFEED, 0);
    Check(required > static_cast<LRESULT>(sizeof(RECONVERTSTRING)), "Native IME can query surrounding-text budget");
    std::vector<std::uint32_t> storage((static_cast<std::size_t>(required) + 3) / 4);
    auto* feed = reinterpret_cast<RECONVERTSTRING*>(storage.data());
    feed->dwSize = static_cast<DWORD>(required);
    Check(SendMessageW(native.m_Window, WM_IME_REQUEST, IMR_DOCUMENTFEED, reinterpret_cast<LPARAM>(feed)) == required,
          "Native IME obtains owned surrounding text");
    Check(feed->dwStrLen == 4 && feed->dwCompStrLen == 2 && feed->dwCompStrOffset == sizeof(wchar_t),
          "Surrounding selection converts UTF-8 byte offsets into UTF-16 code-unit lengths and byte offsets");
    const auto* fedText =
        reinterpret_cast<const wchar_t*>(reinterpret_cast<const std::byte*>(feed) + feed->dwStrOffset);
    Check(std::wstring_view(fedText, feed->dwStrLen) == L"A\U0001F600Z",
          "Supplementary character survives native feed");
    feed->dwSize = sizeof(RECONVERTSTRING);
    Check(SendMessageW(native.m_Window, WM_IME_REQUEST, IMR_DOCUMENTFEED, reinterpret_cast<LPARAM>(feed)) == 0,
          "Native document feed rejects an undersized caller buffer");

    const int before = editor.m_Updates;
    {
        Detail::InputDispatchScope nested;
        for (int i = 0; i < 200; ++i)
            SendMessageW(native.m_Window, WM_IME_STARTCOMPOSITION, 0, 0);
        adapter.Flush();
        Check(editor.m_Updates == before, "Native reentrancy never mutates a client during an input callback");
    }
    adapter.Flush();
    Check(editor.m_Updates == before + 1 && editor.m_Composing && adapter.IsComposing(),
          "Reserved native queue coalesces temporary updates and delivers after the unsafe traversal");
    IMECHARPOSITION character{};
    character.dwSize = sizeof(character);
    Check(SendMessageW(native.m_Window, WM_IME_REQUEST, IMR_QUERYCHARPOSITION, reinterpret_cast<LPARAM>(&character)) !=
                  0 &&
              character.pt.x == static_cast<LONG>(std::lround(state.m_ScreenCaretBounds->m_X)) &&
              character.cLineHeight > 0,
          "Native character-position request returns actual desktop caret coordinates");

    SendMessageW(native.m_Window, WM_IME_ENDCOMPOSITION, 0, 0);
    Check(editor.m_Composing, "Native end message remains asynchronous until a safe owner turn");
    adapter.Flush();
    Check(!editor.m_Composing && !adapter.IsComposing(), "Native composition end clears temporary presentation");
    SendMessageW(native.m_Window, WM_IME_CHAR, 0x65E5, 0);
    SendMessageW(native.m_Window, WM_IME_CHAR, 0xD83D, 0);
    SendMessageW(native.m_Window, WM_IME_CHAR, 0xDE00, 0);
    Check(editor.m_Committed.empty(), "Native committed UTF-16 messages wait for an idle owner turn");
    adapter.Flush();
    Check(editor.m_Committed == "\xE6\x97\xA5\xF0\x9F\x98\x80" && !editor.m_Composing,
          "Actual Unicode WM_IME_CHAR commits preserve CJK text and supplementary surrogate pairs once");
    SendMessageW(native.m_Window, WM_IME_STARTCOMPOSITION, 0, 0);
    ui.SetFocus(nullptr);
    adapter.Flush();
    Check(!editor.m_Composing && !adapter.IsComposing(), "Queued preedit cannot enter an expired focus session");
    Check(ImmGetContext(native.m_Window) == nullptr, "No editable focus disables the native composition context");
    ui.SetFocus(&editor);
    adapter.Flush();
    SendMessageW(native.m_Window, WM_IME_STARTCOMPOSITION, 0, 0);
    adapter.Flush();
    Check(editor.m_Composing, "Editor can start a later native session after focus restoration");
    ui.CancelTextComposition();
    adapter.Flush();
    Check(!adapter.IsComposing() && !editor.m_Composing, "Explicit local cancellation stops native composition");
    const int previousSnapshots = editor.m_Snapshots;
    scale = {2, 2};
    adapter.Flush();
    Check(editor.m_Snapshots == previousSnapshots + 1 && ImmGetCompositionWindow(context, &composition) != FALSE &&
              composition.ptCurrentPos.x != expected.x,
          "A native scale change refreshes cached text geometry without requiring a text edit");
    adapter.RethrowError();
    adapter.Close();
    adapter.Close();
    const auto restored = ImmGetContext(native.m_Window);
    Check(restored == native.m_Context, "Closure restores the original native input context exactly once");
    if (restored)
        ImmReleaseContext(native.m_Window, restored);
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        NativeInput(font);
        std::cout << "Native text input: " << s_Checks
                  << " checks passed; real HWND/IMM geometry, document feed and "
                     "owned composition delivery. Installed-language IME conversion remains an interactive check.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Native text input: " << error.what() << '\n';
        return 1;
    }
}
