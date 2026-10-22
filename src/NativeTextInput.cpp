#include "NativeTextInput.h"
#include "WindowsSupport.h"

#include <EStudioUI/TextInputClient.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <commctrl.h>
#include <imm.h>
#endif

namespace EStudioUI::Detail
{
struct NativeTextInput::Impl : std::enable_shared_from_this<NativeTextInput::Impl>
{
    UiContext* m_Ui = nullptr;
    std::weak_ptr<void> m_Owner;
    std::exception_ptr m_Error;
    bool m_Closed = false;
    bool m_Flushing = false;
    bool m_Composing = false;

#ifdef _WIN32
    HWND m_Window = nullptr;
    HIMC m_Context = nullptr;
    HIMC m_PreviousContext = nullptr;
    bool m_Associated = false;
    bool m_HasCaret = false;
    int m_CaretHeight = 0;
    TextInputState m_State;
    std::uint64_t m_TextRevision = 0;
    std::optional<WindowMetrics> m_Metrics;
    RECT m_PositionedCaret{};
    bool m_Positioned = false;
    std::uint64_t m_Client = 0;
    std::uint64_t m_Epoch = 0;
    std::wstring m_Preedit;
    std::size_t m_PreeditCursor = 0;
    std::wstring m_ResultCharacters;
    std::size_t m_ResultCharacterOffset = 0;
    wchar_t m_LeadSurrogate = 0;
    std::deque<TextCompositionEvent> m_Pending;
    std::size_t m_PendingBytes = 0;

    static constexpr std::size_t s_MaxEvents = 128;
    static constexpr std::size_t s_MaxCompositionBytes = 65536;
    static constexpr std::size_t s_MaxPendingBytes = 8 * 1024 * 1024;

    bool CanAccessUi() const
    {
        if (!m_Ui || m_Closed || m_Ui->GetPhase() != UiPhase::Idle || IsDeliveringInput())
            return false;
        try
        {
            m_Ui->VerifyTraversalEntry();
            return true;
        }
        catch (const std::logic_error&)
        {
            return false;
        }
    }

    bool RefreshSnapshot()
    {
        const auto revision = m_Ui->GetTextInputRevision();
        const auto metrics = m_Ui->Platform().GetWindowMetrics();
        const auto sameMetrics = [&](const WindowMetrics& previous)
        {
            return previous.m_LogicalSize == metrics.m_LogicalSize &&
                   previous.m_FramebufferSize == metrics.m_FramebufferSize &&
                   previous.m_ContentScale == metrics.m_ContentScale && previous.m_InputScale == metrics.m_InputScale &&
                   previous.m_PixelScale == metrics.m_PixelScale && previous.m_ScreenOrigin == metrics.m_ScreenOrigin &&
                   previous.m_HasScreenCoordinates == metrics.m_HasScreenCoordinates;
        };
        if (revision == m_TextRevision && m_Metrics && sameMetrics(*m_Metrics))
            return false;
        m_State = m_Ui->GetTextInputState();
        m_TextRevision = revision;
        m_Metrics = metrics;
        return true;
    }

    void Queue(TextCompositionKind kind, std::string text = {}, std::size_t caret = 0)
    {
        if (m_Closed || !m_Client)
            return;
        if (text.size() > s_MaxCompositionBytes)
            throw std::length_error("The IME update exceeds the shared composition payload budget.");
        TextCompositionEvent event;
        event.m_Kind = kind;
        event.m_Text = std::move(text);
        event.m_Caret = caret;
        event.m_Client = m_Client;
        event.m_Epoch = m_Epoch;
        if (!m_Pending.empty() && kind == TextCompositionKind::Preedit &&
            m_Pending.back().m_Kind == TextCompositionKind::Preedit && m_Pending.back().m_Client == event.m_Client &&
            m_Pending.back().m_Epoch == event.m_Epoch)
        {
            if (event.m_Text.size() > s_MaxPendingBytes - (m_PendingBytes - m_Pending.back().m_Text.size()))
                throw std::length_error("The reserved native composition queue exceeded its bounded budget.");
            m_PendingBytes -= m_Pending.back().m_Text.size();
            m_Pending.back() = std::move(event);
            m_PendingBytes += m_Pending.back().m_Text.size();
        }
        else
        {
            if (m_Pending.size() == s_MaxEvents || event.m_Text.size() > s_MaxPendingBytes - m_PendingBytes)
                throw std::length_error("The reserved native composition queue exceeded its bounded budget.");
            m_PendingBytes += event.m_Text.size();
            m_Pending.push_back(std::move(event));
        }
        // The reserved queue owns delivery even when the ordinary dispatcher is full.
        // Posting WM_NULL wakes GLFW's wait without running an input or framework loop.
        PostMessageW(m_Window, WM_NULL, 0, 0);
    }

    void CancelNative() noexcept
    {
        const bool wasComposing = std::exchange(m_Composing, false);
        m_Preedit.clear();
        m_PreeditCursor = 0;
        m_LeadSurrogate = 0;
        if (wasComposing && m_Context)
            ImmNotifyIME(m_Context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
    }

    void SetActive(bool active)
    {
        if (m_Associated == active || !m_Window)
            return;
        if (!active)
            CancelNative();
        m_Associated = active;
        m_Positioned = false;
        ImmAssociateContext(m_Window, active ? m_Context : nullptr);
        if (!active && m_HasCaret)
        {
            DestroyCaret();
            m_HasCaret = false;
        }
    }

    static LONG Pixel(float value)
    {
        if (!std::isfinite(value))
            throw std::invalid_argument("Native text caret coordinates must be finite.");
        return static_cast<LONG>(std::lround(std::clamp(static_cast<double>(value), -100000000.0, 100000000.0)));
    }

    RECT CaretPixels() const
    {
        const auto bounds = m_State.m_ScreenCaretBounds;
        if (bounds)
        {
            POINT first{Pixel(bounds->m_X), Pixel(bounds->m_Y)};
            POINT last{Pixel(bounds->m_X + bounds->m_Width), Pixel(bounds->m_Y + bounds->m_Height)};
            ScreenToClient(m_Window, &first);
            ScreenToClient(m_Window, &last);
            return {first.x, first.y, std::max(first.x + 1, last.x), std::max(first.y + 1, last.y)};
        }
        const auto& boundsInWindow = m_State.m_CaretBounds;
        const auto scale = m_Metrics ? m_Metrics->m_InputScale : Point{1, 1};
        const auto left = Pixel(boundsInWindow.m_X * scale.m_X);
        const auto top = Pixel(boundsInWindow.m_Y * scale.m_Y);
        return {left, top, std::max(left + 1, Pixel((boundsInWindow.m_X + boundsInWindow.m_Width) * scale.m_X)),
                std::max(top + 1, Pixel((boundsInWindow.m_Y + boundsInWindow.m_Height) * scale.m_Y))};
    }

    void Position()
    {
        if (!m_Associated || !m_State.m_Active || !m_Context)
            return;
        const auto caret = CaretPixels();
        if (m_Positioned && caret.left == m_PositionedCaret.left && caret.top == m_PositionedCaret.top &&
            caret.right == m_PositionedCaret.right && caret.bottom == m_PositionedCaret.bottom)
            return;
        m_PositionedCaret = caret;
        m_Positioned = true;
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = {caret.left, caret.top};
        ImmSetCompositionWindow(m_Context, &composition);
        CANDIDATEFORM candidate{};
        candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = {caret.left, caret.bottom};
        candidate.rcArea = caret;
        for (DWORD index = 0; index < 4; ++index)
        {
            candidate.dwIndex = index;
            ImmSetCandidateWindow(m_Context, &candidate);
        }
        if (GetFocus() == m_Window)
        {
            const int height = static_cast<int>(std::clamp<LONG>(caret.bottom - caret.top, 1, 100000));
            if (!m_HasCaret || m_CaretHeight != height)
            {
                if (m_HasCaret)
                    DestroyCaret();
                m_HasCaret = CreateCaret(m_Window, nullptr, 1, height) != FALSE;
                m_CaretHeight = height;
            }
            if (m_HasCaret)
                SetCaretPos(caret.left, caret.top);
        }
    }

    void Begin()
    {
        if (CanAccessUi())
            RefreshSnapshot();
        if (!m_State.m_Active || m_State.m_ReadOnly)
            return;
        m_Client = m_State.m_Client;
        m_Epoch = m_State.m_Epoch;
        m_Composing = true;
        m_Positioned = false;
        m_Preedit.clear();
        m_PreeditCursor = 0;
        m_ResultCharacters.clear();
        m_ResultCharacterOffset = 0;
        m_LeadSurrogate = 0;
        Queue(TextCompositionKind::Preedit);
    }

    std::wstring ReadString(DWORD field) const
    {
        const LONG bytes = ImmGetCompositionStringW(m_Context, field, nullptr, 0);
        if (bytes < 0 || bytes % static_cast<LONG>(sizeof(wchar_t)) != 0 ||
            static_cast<std::size_t>(bytes) > 2 * s_MaxCompositionBytes)
            throw std::invalid_argument("The IME returned an invalid or oversized UTF-16 composition string.");
        std::wstring text(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
        if (bytes && ImmGetCompositionStringW(m_Context, field, text.data(), static_cast<DWORD>(bytes)) != bytes)
            throw std::runtime_error("The IME composition changed while its owned snapshot was being read.");
        return text;
    }

    void Compose(LPARAM flags)
    {
        if (!m_Composing)
            Begin();
        if (!m_Composing)
            return;
        if (!flags)
        {
            Queue(TextCompositionKind::Cancel);
            m_Composing = false;
            m_Preedit.clear();
            return;
        }
        if (flags & GCS_RESULTSTR)
        {
            m_ResultCharacters = ReadString(GCS_RESULTSTR);
            m_ResultCharacterOffset = 0;
            const auto text = FromWindowsText(m_ResultCharacters);
            Queue(TextCompositionKind::Commit, text, text.size());
            m_Preedit.clear();
            m_PreeditCursor = 0;
            m_Composing = false;
        }
        if (flags & (GCS_COMPSTR | GCS_CURSORPOS))
        {
            m_Composing = true;
            m_Preedit = ReadString(GCS_COMPSTR);
            const LONG cursor = ImmGetCompositionStringW(m_Context, GCS_CURSORPOS, nullptr, 0);
            auto offset = cursor < 0 ? m_Preedit.size() : std::min(static_cast<std::size_t>(cursor), m_Preedit.size());
            // IMM cursor offsets are UTF-16 code units, whereas the shared protocol
            // uses UTF-8 bytes. Never slice between a high and low surrogate.
            if (offset && offset < m_Preedit.size() && m_Preedit[offset] >= 0xDC00 && m_Preedit[offset] <= 0xDFFF &&
                m_Preedit[offset - 1] >= 0xD800 && m_Preedit[offset - 1] <= 0xDBFF)
                --offset;
            m_PreeditCursor = offset;
            Queue(TextCompositionKind::Preedit, FromWindowsText(m_Preedit),
                  FromWindowsText(std::wstring_view(m_Preedit).substr(0, offset)).size());
        }
    }

    void LegacyCharacter(WPARAM value)
    {
        if (value > 0xFFFF)
            throw std::invalid_argument("A Unicode WM_IME_CHAR must contain one UTF-16 code unit.");
        const auto character = static_cast<wchar_t>(value);
        if (m_ResultCharacterOffset < m_ResultCharacters.size())
        {
            if (m_ResultCharacters[m_ResultCharacterOffset] == character)
            {
                ++m_ResultCharacterOffset;
                return;
            }
            m_ResultCharacters.clear();
            m_ResultCharacterOffset = 0;
        }
        if (!m_Composing)
            Begin();
        if (!m_Composing)
            return;
        if (character >= 0xD800 && character <= 0xDBFF)
        {
            m_LeadSurrogate = character;
            return;
        }
        std::wstring text;
        if (m_LeadSurrogate)
        {
            text.push_back(std::exchange(m_LeadSurrogate, 0));
            if (character < 0xDC00 || character > 0xDFFF)
                throw std::invalid_argument("The IME supplied an unpaired UTF-16 high surrogate.");
        }
        text.push_back(character);
        const auto utf8 = FromWindowsText(text);
        Queue(TextCompositionKind::Commit, utf8, utf8.size());
        m_Composing = false;
        m_Preedit.clear();
        m_PreeditCursor = 0;
    }

    LRESULT DocumentFeed(LPARAM data) const
    {
        if (!m_State.m_Active)
            return 0;
        const auto text = ToWindowsText(m_State.m_Text);
        const auto anchor = ToWindowsText(std::string_view(m_State.m_Text).substr(0, m_State.m_Anchor)).size();
        const auto caret = ToWindowsText(std::string_view(m_State.m_Text).substr(0, m_State.m_Caret)).size();
        const auto bytes = sizeof(RECONVERTSTRING) + (text.size() + 1) * sizeof(wchar_t);
        if (bytes > std::numeric_limits<DWORD>::max())
            return 0;
        if (!data)
            return static_cast<LRESULT>(bytes);
        auto* target = reinterpret_cast<RECONVERTSTRING*>(data);
        if (target->dwSize < bytes)
            return 0;
        *target = {};
        target->dwSize = static_cast<DWORD>(bytes);
        target->dwStrLen = static_cast<DWORD>(text.size());
        target->dwStrOffset = sizeof(RECONVERTSTRING);
        target->dwCompStrLen = static_cast<DWORD>(std::max(anchor, caret) - std::min(anchor, caret));
        target->dwCompStrOffset = static_cast<DWORD>(std::min(anchor, caret) * sizeof(wchar_t));
        target->dwTargetStrLen = target->dwCompStrLen;
        target->dwTargetStrOffset = target->dwCompStrOffset;
        std::memcpy(reinterpret_cast<std::byte*>(target) + sizeof(RECONVERTSTRING), text.c_str(),
                    (text.size() + 1) * sizeof(wchar_t));
        return static_cast<LRESULT>(bytes);
    }

    LRESULT CharacterPosition(LPARAM data) const
    {
        if (!data || !m_State.m_Active || !m_State.m_ScreenCaretBounds)
            return 0;
        auto* target = reinterpret_cast<IMECHARPOSITION*>(data);
        // The shared boundary supplies the actual shaped caret rectangle. It does
        // not infer widths for other characters of a composition or direction run.
        if (target->dwSize < sizeof(IMECHARPOSITION) || target->dwCharPos != m_PreeditCursor)
            return 0;
        const auto bounds = *m_State.m_ScreenCaretBounds;
        target->pt = {Pixel(bounds.m_X), Pixel(bounds.m_Y)};
        target->cLineHeight = static_cast<UINT>(std::max<LONG>(1, Pixel(bounds.m_Height)));
        GetClientRect(m_Window, &target->rcDocument);
        MapWindowPoints(m_Window, nullptr, reinterpret_cast<POINT*>(&target->rcDocument), 2);
        return TRUE;
    }

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        const auto self = reinterpret_cast<Impl*>(data)->shared_from_this();
        const auto owner = self->m_Owner.lock();
        if (message == WM_NCDESTROY)
        {
            self->Close();
            return DefSubclassProc(window, message, wParam, lParam);
        }
        if (self->m_Closed)
            return DefSubclassProc(window, message, wParam, lParam);
        try
        {
            switch (message)
            {
            case WM_IME_SETCONTEXT:
                if (self->m_Associated)
                    lParam &= ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
                break;
            case WM_IME_STARTCOMPOSITION:
                if (self->m_Associated)
                {
                    self->Begin();
                    return 0;
                }
                break;
            case WM_IME_COMPOSITION:
                if (self->m_Associated)
                {
                    self->Compose(lParam);
                    // DefWindowProc would translate GCS_RESULTSTR to WM_IME_CHAR
                    // and WM_CHAR. Consuming it gives exactly one shared commit.
                    return 0;
                }
                break;
            case WM_IME_ENDCOMPOSITION:
                if (self->m_Associated)
                {
                    self->Queue(TextCompositionKind::Cancel);
                    self->m_Composing = false;
                    self->m_Preedit.clear();
                    self->m_PreeditCursor = 0;
                    return 0;
                }
                break;
            case WM_IME_CHAR:
                if (self->m_Associated)
                {
                    // A legacy Unicode IME may supply WM_IME_CHAR directly.
                    // Handle it here so DefWindowProc cannot create a second WM_CHAR.
                    self->LegacyCharacter(wParam);
                    return 0;
                }
                break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                self->m_ResultCharacters.clear();
                self->m_ResultCharacterOffset = 0;
                break;
            case WM_IME_REQUEST:
                if (wParam == IMR_DOCUMENTFEED)
                    return self->DocumentFeed(lParam);
                if (wParam == IMR_QUERYCHARPOSITION)
                    return self->CharacterPosition(lParam);
                break;
            case WM_KILLFOCUS:
                if (self->m_Composing)
                    self->Queue(TextCompositionKind::Cancel);
                self->CancelNative();
                if (self->m_HasCaret)
                {
                    DestroyCaret();
                    self->m_HasCaret = false;
                }
                break;
            case WM_SETFOCUS:
                self->m_Positioned = false;
                if (self->CanAccessUi())
                    self->Position();
                break;
            case WM_INPUTLANGCHANGE:
            case WM_DPICHANGED:
                self->m_Positioned = false;
                break;
            case WM_IME_NOTIFY:
                if (wParam == IMN_OPENCANDIDATE || wParam == IMN_SETOPENSTATUS)
                {
                    self->m_Positioned = false;
                    PostMessageW(window, WM_NULL, 0, 0);
                }
                break;
            default:
                break;
            }
        }
        catch (...)
        {
            self->m_Error = std::current_exception();
            self->CancelNative();
            PostMessageW(window, WM_NULL, 0, 0);
            if (message == WM_IME_COMPOSITION || message == WM_IME_STARTCOMPOSITION ||
                message == WM_IME_ENDCOMPOSITION || message == WM_IME_CHAR || message == WM_IME_REQUEST)
                return 0;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    void Flush()
    {
        if (!CanAccessUi() || m_Flushing)
            return;
        const auto owner = m_Owner.lock();

        struct FlushScope
        {
            bool& m_Flag;

            explicit FlushScope(bool& flag) : m_Flag(flag)
            {
                m_Flag = true;
            }

            ~FlushScope()
            {
                m_Flag = false;
            }
        } scope(m_Flushing);

        const bool previouslyComposing = m_State.m_Composing;
        RefreshSnapshot();
        if (m_Composing && (!m_State.m_Active || m_State.m_Client != m_Client || m_State.m_Epoch != m_Epoch ||
                            (previouslyComposing && !m_State.m_Composing)))
        {
            Queue(TextCompositionKind::Cancel);
            CancelNative();
        }
        const bool hadPending = !m_Pending.empty();
        while (!m_Pending.empty() && !m_Closed)
        {
            auto event = std::move(m_Pending.front());
            m_Pending.pop_front();
            m_PendingBytes -= event.m_Text.size();
            m_Ui->DispatchComposition(event);
        }
        if (m_Closed)
            return;
        const bool composedBeforeDelivery = m_State.m_Composing;
        if (hadPending)
            RefreshSnapshot();
        if (m_Composing && composedBeforeDelivery && !m_State.m_Composing)
            CancelNative();
        SetActive(m_State.m_Active && !m_State.m_ReadOnly);
        Position();
    }

    void Close() noexcept
    {
        if (m_Closed)
            return;
        m_Closed = true;
        CancelNative();
        m_Pending.clear();
        m_PendingBytes = 0;
        if (m_HasCaret)
        {
            DestroyCaret();
            m_HasCaret = false;
        }
        if (m_Window)
        {
            RemoveWindowSubclass(m_Window, Procedure, reinterpret_cast<UINT_PTR>(this));
            ImmAssociateContext(m_Window, m_PreviousContext);
            m_Window = nullptr;
        }
        if (m_Context)
        {
            ImmDestroyContext(m_Context);
            m_Context = nullptr;
        }
        m_Ui = nullptr;
    }
#else
    void Flush()
    {
    }

    void Close() noexcept
    {
        m_Closed = true;
        m_Ui = nullptr;
    }
#endif
};

NativeTextInput::NativeTextInput(UiContext& ui, void* nativeWindow, std::weak_ptr<void> owner)
    : m_Impl(std::make_shared<Impl>())
{
    ui.VerifyAccess();
    m_Impl->m_Ui = &ui;
    m_Impl->m_Owner = std::move(owner);
#ifdef _WIN32
    const auto window = static_cast<HWND>(nativeWindow);
    if (!window || !IsWindow(window))
        throw std::invalid_argument("Native text input requires a live owner HWND.");
    m_Impl->m_Context = ImmCreateContext();
    if (!m_Impl->m_Context)
        throw std::runtime_error("Could not create the native text input context.");
    m_Impl->m_Window = window;
    m_Impl->m_PreviousContext = ImmAssociateContext(window, nullptr);
    if (!SetWindowSubclass(window, Impl::Procedure, reinterpret_cast<UINT_PTR>(m_Impl.get()),
                           reinterpret_cast<DWORD_PTR>(m_Impl.get())))
    {
        m_Impl->Close();
        throw std::runtime_error("Could not attach the native text input boundary.");
    }
    try
    {
        m_Impl->Flush();
    }
    catch (...)
    {
        m_Impl->Close();
        throw;
    }
#else
    (void)nativeWindow;
#endif
}

NativeTextInput::~NativeTextInput()
{
    Close();
}

void NativeTextInput::Flush()
{
    const auto state = m_Impl;
    state->Flush();
}

void NativeTextInput::RethrowError() const
{
    if (m_Impl->m_Error)
        std::rethrow_exception(m_Impl->m_Error);
}

void NativeTextInput::Close() noexcept
{
    const auto state = m_Impl;
    state->Close();
}

bool NativeTextInput::IsComposing() const noexcept
{
    return m_Impl->m_Composing && !m_Impl->m_Closed;
}
} // namespace EStudioUI::Detail
