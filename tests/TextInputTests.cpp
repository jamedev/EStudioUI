#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

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

TextCompositionEvent Event(UiContext& ui, TextCompositionKind kind, std::string text = {}, std::size_t caret = 0)
{
    const auto state = ui.GetTextInputState();
    return {kind, std::move(text), caret, state.m_Client, state.m_Epoch};
}

void Composition(FontFace& font)
{
    UiContext ui(font);
    auto& editor = ui.Root().Add<TextBox>("Before after");
    auto& other = ui.Root().Add<TextBox>("Other");
    ui.Prepare(600, 160);
    ui.SetFocus(&editor);
    editor.SetSelection(0, 6);
    int changes = 0, submissions = 0;
    editor.TextChanged().Connect([&](const auto&) { ++changes; });
    editor.Submitted().Connect([&](const auto&) { ++submissions; });
    const auto original = editor.GetText();
    const auto begin = ui.GetTextInputState();
    Check(begin.m_Active && begin.m_Client && begin.m_Epoch && begin.m_Text == original && begin.m_Anchor == 0 &&
              begin.m_Caret == 6 && !begin.m_Composing,
          "snapshot owns canonical surrounding text and byte selection");
    Check(ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "temporary", 4)),
          "preedit accepted on focused editor");
    Check(editor.GetText() == original && editor.GetSelectedText() == "Before" && changes == 0 &&
              ui.GetTextInputState().m_Composing,
          "preedit does not mutate canonical text, selection, history, or text notifications");
    ui.KeyEvent(Key::Enter, true);
    ui.KeyEvent(Key::Left, true);
    ui.TextInput("duplicate");
    Check(editor.GetText() == original && submissions == 0 && editor.GetSelectionCaret() == 6,
          "ordinary input cannot submit or move canonical text while composing");
    Check(ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "composed", 8)), "preedit updates");
    Check(ui.DispatchComposition(Event(ui, TextCompositionKind::Commit, "Composed")), "composition committed");
    Check(editor.GetText() == "Composed after" && changes == 1 && !ui.GetTextInputState().m_Composing,
          "commit replaces original selection once");
    ui.KeyEvent(Key::Z, true, false, {.m_Control = true});
    Check(editor.GetText() == original && changes == 2 && editor.GetSelectionAnchor() == 0 &&
              editor.GetSelectionCaret() == 6,
          "entire composition is one undoable edit preserving original selection");
    const auto stale = Event(ui, TextCompositionKind::Commit, "Stale");
    ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "cancel", 6));
    ui.KeyEvent(Key::Escape, true);
    Check(editor.GetText() == original && !ui.GetTextInputState().m_Composing && changes == 2,
          "Escape cancels preedit without canonical notifications");
    Check(!ui.DispatchComposition(stale), "explicit cancellation expires queued native generations");
    ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "focus", 5));
    const auto oldFocus = Event(ui, TextCompositionKind::Commit, "Wrong target");
    ui.SetFocus(&other);
    Check(!editor.GetTextInputState().m_Composing && !ui.DispatchComposition(oldFocus) && other.GetText() == "Other",
          "focus change cancels preedit and rejects old native target");
    ui.SetFocus(&editor);
    ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "programmatic", 4));
    const auto replaced = Event(ui, TextCompositionKind::Commit, "Old OS update");
    editor.SetText("Replacement");
    Check(!ui.GetTextInputState().m_Composing && !ui.DispatchComposition(replaced) && editor.GetText() == "Replacement",
          "programmatic replacement cancels composition and expires native updates");
    ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "readonly", 8));
    editor.SetReadOnly(true);
    Check(!ui.GetTextInputState().m_Active && !ui.GetTextInputState().m_Composing &&
              !ui.DispatchComposition(Event(ui, TextCompositionKind::Commit, "Denied")),
          "read-only transition cancels preedit and rejects native commit");
    editor.SetReadOnly(false);
    ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "deactivate", 3));
    ui.Deactivate();
    Check(!editor.GetTextInputState().m_Composing && !ui.GetTextInputState().m_Active,
          "window deactivation releases composition ownership");
    ui.SetFocus(&editor);
    ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "popup", 5));
    auto& popup = ui.Root().Add<ContextMenu>();
    ui.ShowPopup(popup, {50, 50}, PopupOptions::Modal());
    Check(!editor.GetTextInputState().m_Composing && !ui.GetTextInputState().m_Active,
          "modal popup focus boundary cancels the previous editor composition");
    ui.CloseAllPopups();
}

void ValidationAndUnicode(FontFace& font)
{
    UiContext ui(font);
    auto& editor = ui.Root().Add<TextBox>("12");
    editor.SetValidator(std::make_shared<IntegerValidator>(0, 100));
    ui.Prepare(300, 100);
    ui.SetFocus(&editor);
    editor.SelectAll();
    int changes = 0;
    editor.TextChanged().Connect([&](const auto&) { ++changes; });
    Check(ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "letters", 3)),
          "validator does not reject temporary uncommitted spelling");
    Check(editor.GetValidationState() == ValidationState::Acceptable && changes == 0,
          "preedit leaves canonical validation unchanged");
    Check(!ui.DispatchComposition(Event(ui, TextCompositionKind::Commit, "letters")) && editor.GetText() == "12" &&
              !ui.GetTextInputState().m_Composing && changes == 0,
          "invalid final composition is rejected without text/history mutation");
    Check(ui.DispatchComposition(Event(ui, TextCompositionKind::Commit, "34")) && editor.GetText() == "34",
          "composition commit uses the canonical validator");
    ui.KeyEvent(Key::Z, true, false, {.m_Control = true});
    Check(editor.GetText() == "12", "rejected commit introduced no undo checkpoint");
    editor.SetValidator({});
    editor.SetText("A");
    editor.SetSelection(1, 1);
    Check(ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "e\xcc\x81", 1)),
          "preedit permits a code-point cursor within a combining grapheme");
    const auto beginning = ui.GetTextInputState().m_CaretBounds;
    ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "e\xcc\x81", 3));
    const auto ending = ui.GetTextInputState().m_CaretBounds;
    Check(ending.m_X > beginning.m_X, "display caret snaps to complete grapheme boundaries");
    Throws([&] { ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "\xf0\x9f\x98\x80", 2)); },
           "preedit cursor cannot split a UTF-8 code point");
    Throws([&] { ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "\xc0\xaf", 0)); },
           "malformed UTF-8 is rejected before client delivery");
    Throws([&] { ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, std::string(65537, 'x'), 0)); },
           "preedit allocation is bounded");
    Check(!ui.DispatchComposition(Event(ui, TextCompositionKind::Preedit, "line\nbreak", 4)),
          "single-line preedit rejects line breaks without corrupting byte offsets");
    ui.CancelTextComposition();
    editor.SetMaxBytes(5);
    ui.DispatchComposition(Event(ui, TextCompositionKind::Commit, "\xf0\x9f\x98\x80"));
    Check(editor.GetText() == "A\xf0\x9f\x98\x80", "commit preserves supplementary-plane UTF-8 under byte budget");
}

class CancellationEditor final : public TextBox
{
  public:
    explicit CancellationEditor(std::string text = "12345") : TextBox(std::move(text))
    {
    }

    bool OnTextComposition(const TextCompositionEvent& event) override
    {
        if (event.m_Kind == TextCompositionKind::Cancel && m_OnCancel)
        {
            const auto callback = m_OnCancel;
            callback();
            return true;
        }
        return TextBox::OnTextComposition(event);
    }

    std::function<void()> m_OnCancel;
};

void CancellationCallbacks(FontFace& font)
{
    const auto deletedBy = [&font](auto operation, const char* message)
    {
        UiContext ui(font);
        auto& editor = ui.Root().Add<CancellationEditor>();
        editor.SetFloatingBounds({20, 20, 180, 40});
        ui.Prepare(400, 100);
        ui.SetFocus(&editor);
        const WidgetRef lifetime(&editor);
        editor.m_OnCancel = [&] { ui.Root().RemoveChild(editor); };
        operation(editor, ui);
        Check(!lifetime.Get() && !ui.GetFocus(), message);
    };
    deletedBy([](CancellationEditor& editor, UiContext&) { editor.SetText(editor.GetText()); },
              "text replacement stops after a virtual cancellation deletes its editor");
    deletedBy([](CancellationEditor& editor, UiContext&) { editor.SetValidator(std::make_shared<IntegerValidator>()); },
              "validator replacement stops after cancellation deletes its editor");
    deletedBy([](CancellationEditor& editor, UiContext&) { editor.SetSelection(1, 2); },
              "selection replacement stops after cancellation deletes its editor");
    deletedBy([](CancellationEditor& editor, UiContext&) { editor.SetReadOnly(true); },
              "read-only change stops after cancellation deletes its editor");
    deletedBy([](CancellationEditor& editor, UiContext&) { editor.SetMaxBytes(2); },
              "byte-limit change stops after cancellation deletes its editor");
    deletedBy([](CancellationEditor& editor, UiContext&) { editor.SelectAll(); },
              "selection-all stops after cancellation deletes its editor");
    deletedBy(
        [](CancellationEditor& editor, UiContext& ui)
        {
            const auto bounds = editor.GetBounds();
            ui.PointerDown({bounds.m_X + bounds.m_Width * 0.5f, bounds.m_Y + bounds.m_Height * 0.5f});
        },
        "pointer positioning stops after cancellation deletes its editor");

    UiContext ui(font);
    auto& editor = ui.Root().Add<CancellationEditor>(std::string(2048, 'a'));
    auto& redirected = ui.Root().Add<TextBox>("Redirected");
    auto& requested = ui.Root().Add<TextBox>("Requested");
    ui.Prepare(600, 160);
    ui.SetFocus(&editor);
    int cancellations = 0;
    editor.m_OnCancel = [&]
    {
        ++cancellations;
        editor.TextBox::SetText(std::string(4096, 'b'));
    };
    const std::string_view borrowed(editor.GetText().data() + 100, 1000);
    editor.SetText(borrowed);
    Check(cancellations == 1 && editor.GetText() == std::string(1000, 'a'),
          "borrowed replacement bytes survive a cancellation hook that replaces the source string");
    cancellations = 0;
    editor.m_OnCancel = [&]
    {
        ++cancellations;
        ui.CancelTextComposition();
        ui.SetFocus(&redirected);
    };
    ui.SetFocus(&requested);
    Check(cancellations == 1 && ui.GetFocus() == &redirected,
          "a cancellation hook may reenter cancellation and redirect focus without recursion or overwrite");
    editor.m_OnCancel = {};
    ui.SetFocus(&editor);
    editor.m_OnCancel = [] { throw std::runtime_error("Cancellation extension failure"); };
    Throws([&] { ui.CancelTextComposition(); }, "ordinary cancellation errors remain observable");
    cancellations = 0;
    editor.m_OnCancel = [&] { ++cancellations; };
    ui.CancelTextComposition();
    Check(cancellations == 1, "cancellation recursion guard restores after extension exceptions");
    editor.m_OnCancel = {};
}

class CustomEditor : public Widget, public TextInputClient
{
  public:
    mutable bool m_TryMutation = false;
    bool m_Received = false;

    bool IsFocusable() const override
    {
        return true;
    }

    TextInputState GetTextInputState() const override
    {
        if (m_TryMutation)
            const_cast<CustomEditor*>(this)->Add<Widget>();
        TextInputState state;
        state.m_Text = "custom";
        state.m_Anchor = state.m_Caret = 6;
        state.m_CaretBounds = {40, 30, 2, 20};
        return state;
    }

    bool OnTextComposition(const TextCompositionEvent& event) override
    {
        m_Received = event.m_Kind == TextCompositionKind::Commit && event.m_Text == "owned";
        return true;
    }
};

void GeometryAndExtensions(FontFace& font)
{
    UiContext ui(font);
    auto& editor = ui.Root().Add<CustomEditor>();
    std::string error;
    Check(ui.SetStyleSheet("window {background:#111;} widget {transform:translate(12px, 8px);}", error),
          "composition fixture stylesheet accepted");
    PlatformHost host;
    host.m_Dispatcher = ui.GetDispatcher();
    host.m_GetMetrics = []
    {
        WindowMetrics metrics;
        metrics.m_LogicalSize = {400, 100};
        metrics.m_FramebufferSize = {600, 150};
        metrics.m_PixelScale = {1.5f, 1.5f};
        metrics.m_ScreenOrigin = {-1000, 80};
        metrics.m_HasScreenCoordinates = true;
        return metrics;
    };
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(host));
    ui.Prepare(400, 100);
    ui.SetFocus(&editor);
    const auto state = ui.GetTextInputState();
    const auto expected = ui.ToWindowPoint(editor, {40, 30});
    Check(state.m_CaretBounds.m_X == expected.m_X && state.m_CaretBounds.m_Y == expected.m_Y,
          "native caret follows the shared visual transform mapping");
    Check(state.m_ScreenCaretBounds && state.m_ScreenCaretBounds->m_X == -1000 + state.m_CaretBounds.m_X * 1.5f &&
              state.m_ScreenCaretBounds->m_Y == 80 + state.m_CaretBounds.m_Y * 1.5f,
          "candidate geometry uses physical DPI and negative desktop origins");
    const auto before = ui.GetStats();
    for (int i = 0; i < 50; ++i)
        ui.GetTextInputState();
    Check(ui.GetStats().m_StyleResolutions == before.m_StyleResolutions &&
              ui.GetStats().m_LayoutPasses == before.m_LayoutPasses,
          "native caret queries never prepare UI frames");
    Check(ui.DispatchComposition(Event(ui, TextCompositionKind::Commit, "owned")) && editor.m_Received,
          "custom composed editor uses the public protocol");
    editor.m_TryMutation = true;
    Throws([&] { ui.GetTextInputState(); }, "composition snapshot hooks cannot mutate widget structure");
    editor.m_TryMutation = false;
    Check(ui.GetPhase() == UiPhase::Idle, "composition inspection guard restores phase after extension errors");
    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                ui.GetTextInputState();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    Check(rejected, "direct native text queries reject non-owner threads");
    ui.Root().RemoveChild(editor);
    Check(!ui.DispatchComposition({TextCompositionKind::Commit, "late", 0, state.m_Client, state.m_Epoch}),
          "deleted custom editor expires its native identity");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Composition(font);
        ValidationAndUnicode(font);
        CancellationCallbacks(font);
        GeometryAndExtensions(font);
        std::cout << "Text input: " << s_Checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Text input: " << error.what() << '\n';
        return 1;
    }
}
