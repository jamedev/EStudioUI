#include <EStudioUI/Button.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float a, float b)
{
    return std::abs(a - b) < 0.001f;
}

void Signals()
{
    Signal<int> signal;
    std::vector<int> delivered;
    Connection second;
    bool added = false;
    signal.Connect(
        [&](int value)
        {
            delivered.push_back(value);
            second.Disconnect();
            if (!added)
            {
                added = true;
                signal.Connect([&](int v) { delivered.push_back(v + 20); });
            }
        });
    second = signal.Connect([&](int value) { delivered.push_back(value + 10); });
    signal.Emit(1);
    Check(delivered == std::vector<int>{1}, "disconnect and connect during delivery");
    signal.Emit(2);
    Check(delivered == std::vector<int>({1, 2, 22}), "new connection starts at next delivery");
    Check(!second.IsConnected(), "connection tracks disconnection");
    Signal<> scoped;
    int calls = 0;
    {
        ScopedConnection connection(scoped.Connect([&] { ++calls; }));
        scoped.Emit();
    }
    scoped.Emit();
    Check(calls == 1, "scoped disconnection");
    auto sender = std::make_unique<Signal<>>();
    auto connection = sender->Connect([&] { sender.reset(); });
    sender->Connect([&] { ++calls; });
    sender->Emit();
    Check(!sender && calls == 2 && !connection.IsConnected(), "delivery safely survives sender deletion");
    Signal<int> recursive;
    int total = 0;
    recursive.Connect(
        [&](int depth)
        {
            ++total;
            if (depth < 3)
                recursive.Emit(depth + 1);
        });
    recursive.Emit(0);
    Check(total == 4, "reentrant signals");
    auto throwing = recursive.Connect([](int) { throw std::runtime_error("expected"); });
    try
    {
        recursive.Emit(3);
    }
    catch (const std::runtime_error&)
    {
    }
    throwing.Disconnect();
    recursive.Emit(3);
    Check(total == 6, "delivery recovers after callback exception");
    recursive.DisconnectAll();
    recursive.Emit(0);
    Check(total == 6, "disconnect all");
}

void Css()
{
    StyleSheet css;
    std::string error;
    Check(css.Parse(R"CSS(
        /* Compound, descendant, child, states, and important. */
        window { color: #abcdef; font-size: 18px; }
        button { background: #111; padding: 1px 2px 3px 4px; }
        .card button.primary { background: #224466; }
        #send { background: #123456; }
        panel > button:focus { border: 2px solid rgba(10, 20, 30, 0.5); }
        button.primary { color: white !important; padding-left: 9px; }
        #send { color: red; width: 50%; }
        button:disabled, textbox:read-only { opacity: 0.4; }
    )CSS",
                    error),
          "valid CSS: " + error);
    StyleNode root{"window", "", {}, 0, nullptr};
    StyleNode card{"panel", "", {"card"}, 0, &root};
    StyleNode button{"button", "send", {"primary"}, Focus, &card};
    const auto parent = css.Compute(root);
    const auto style = css.Compute(button, &parent);
    Check(Near(style.m_Background.m_R, 18.0f / 255), "ID specificity beats class specificity");
    Check(Near(style.m_Color.m_R, 1) && Near(style.m_Color.m_G, 1), "important beats normal ID declaration");
    Check(Near(style.m_FontSize, 18), "font-size inheritance");
    Check(Near(style.m_Padding.m_Left, 9) && Near(style.m_Padding.m_Bottom, 3), "longhand overrides shorthand");
    Check(Near(style.m_BorderWidth, 2) && Near(style.m_BorderColor.m_A, 0.5f), "child state selector and rgba");
    Check(Near(style.m_Width.Resolve(300, 0), 150), "percentage dimensions");
    button.m_State = Disabled;
    Check(Near(css.Compute(button).m_Opacity, 0.4f), "disabled pseudo-class and selector list");
    const auto revision = css.GetRevision();
    Check(!css.Parse("button { background: #xyz; }", error), "malformed color rejected");
    Check(!error.empty() && css.GetRevision() == revision, "failed reload keeps stylesheet revision");
    Check(Near(css.Compute(button).m_Background.m_R, 18.0f / 255), "failed reload retains old rules");
    Check(!css.Parse("button { display: grid; }", error), "unsupported declaration value diagnosed");
    Check(!css.Parse("button { font-stretch: imaginary; }", error), "unsupported property diagnosed");
    Check(!css.Parse("button { padding: -1px; }", error), "negative padding rejected");
    Check(!css.Parse("button { color: red;", error), "unclosed block rejected");
    Check(css.Parse(".card button { color: #1234; }", error), "four-digit alpha color");
    Check(Near(css.Compute(button).m_Color.m_A, 68.0f / 255), "hex alpha decoding");
    StyleNode intermediate{"panel", "", {}, 0, &card};
    button.m_Parent = &intermediate;
    Check(Near(css.Compute(button).m_Color.m_R, 17.0f / 255), "descendant matching crosses intermediate parent");
    Check(css.Parse(".card > button { color: red; }", error), "child selector parse");
    Check(!Near(css.Compute(button).m_Color.m_R, 1), "child selector does not cross intermediate parent");
    Check(css.Parse("button { padding-left: 9px; padding: 2px; }", error), "declaration source order parse");
    Check(Near(css.Compute(button).m_Padding.m_Left, 2), "later shorthand wins over longhand");
    Check(css.Parse("\xEF\xBB\xBF"
                    "button { color: red; }",
                    error),
          "UTF-8 BOM accepted for Windows CSS editors");
    Check(css.Parse("button { font-size: 0; }", error) && Near(css.Compute(button).m_FontSize, 0),
          "zero CSS font size hides text");
}

void Editing()
{
    TextBuffer buffer;
    buffer.SetText("Ae\xCC\x81");
    Check(buffer.GetBoundaries() == std::vector<std::size_t>({0, 1, 4}), "combining mark forms one grapheme");
    buffer.Backspace();
    Check(buffer.GetText() == "A", "Backspace deletes combined grapheme");
    buffer.Undo();
    Check(buffer.GetText() == "Ae\xCC\x81" && buffer.GetCaret() == 4 && !buffer.HasSelection(),
          "undo restores original caret before deletion");
    buffer.Redo();
    Check(buffer.GetText() == "A", "redo deletion");
    buffer.SetText("A👩‍💻B🇪🇸");
    Check(buffer.GetBoundaries().size() == 5, "emoji ZWJ and flag are complete graphemes");
    buffer.MoveToEdge(false, false);
    buffer.Move(1, false, false);
    buffer.DeleteForward();
    Check(buffer.GetText() == "AB🇪🇸", "forward deletion removes ZWJ sequence");
    buffer.Undo();
    Check(buffer.GetCaret() == 1 && !buffer.HasSelection(), "forward deletion undo restores original selection");
    buffer.SetText("one two, three");
    buffer.Move(-1, false, true);
    Check(buffer.GetCaret() == 9, "control-left word navigation");
    buffer.SelectWord(5);
    Check(buffer.GetSelectedText() == "two", "double-click word selection");
    buffer.ReplaceSelection("three");
    Check(buffer.GetText() == "one three, three", "replace selected word");
    buffer.Undo();
    Check(buffer.GetSelectedText() == "two", "undo restores selected replacement range");
    buffer.SetMaxBytes(4);
    buffer.SetText("Ae\xCC\x81Z");
    Check(buffer.GetText() == "Ae\xCC\x81", "byte limit preserves grapheme boundary");
    buffer.SetMaxBytes(2);
    Check(buffer.GetText() == "A", "reducing limit never splits grapheme");
    buffer.SetMaxBytes(0);
    Check(buffer.GetText().empty() && !buffer.ReplaceSelection("x"), "zero input limit");
    buffer.SetMaxBytes(100);
    buffer.SetText(std::string("a\r\nb\tc\n") + char(0xFF));
    Check(buffer.GetText() == "a b c \xEF\xBF\xBD", "single-line sanitization and invalid UTF-8 replacement");
    buffer.SelectAll();
    buffer.ReplaceSelection("new");
    buffer.Undo();
    buffer.ReplaceSelection("different");
    Check(!buffer.Redo(), "new edit discards redo branch");
    buffer.SetText("é");
    buffer.SetSelection(1, 1);
    Check(buffer.GetCaret() == 0, "arbitrary byte offsets snap to a grapheme boundary");
}

void FontAndWidgets(FontFace& font)
{
    Check(Near(font.Shape("hidden", 0).m_Width, 0), "zero font size is a valid hidden run");
    const auto layout = font.Shape("Café e\xCC\x81", 20, true);
    Check(!layout.m_Glyphs.empty() && layout.m_Width > 30, "HarfBuzz produces positioned glyphs");
    Check(layout.m_Offsets.size() == 7 && layout.m_Carets.size() == 7,
          "editable layout maps graphemes to caret positions");
    Check(layout.HitTest(-50) == 0 && layout.HitTest(10000) == std::string("Café e\xCC\x81").size(),
          "caret hit test clamps to text");
    for (std::size_t i = 0; i < layout.m_Carets.size(); ++i)
        Check(layout.HitTest(layout.m_Carets[i]) == layout.m_Offsets[i], "caret geometry round trip");
    const auto glyphId = layout.m_Glyphs.front().m_Id;
    const auto& geometry = font.GetGlyph(glyphId);
    Check(!geometry.m_Curves.empty() && geometry.m_MaxY > geometry.m_MinY, "FreeType loads unhinted vector outline");
    Check(&font.GetGlyph(glyphId) == &geometry, "outline cache reuses geometry");

    UiContext ui(font);
    std::string error;
    Check(ui.SetStyleSheet("window { padding: 10px; gap: 10px; } button { width: 140px; height: 40px; } textbox { "
                           "width: 200px; height: 44px; } .hidden { display: none; }",
                           error),
          "interaction stylesheet");
    auto& first = ui.Root().Add<Button>("First");
    auto& disabled = ui.Root().Add<Button>("Disabled");
    disabled.SetEnabled(false);
    auto& text = ui.Root().Add<TextBox>();
    int clicks = 0;
    first.Clicked().Connect([&] { ++clicks; });
    ui.Prepare(500, 400);
    const auto b = first.GetBounds();
    const Point point{b.m_X + 5, b.m_Y + 5};
    ui.PointerDown(point);
    Check(first.IsFocused() && first.IsPressed(), "button acquires focus and pressed state");
    ui.PointerMove({499, 399});
    ui.PointerUp({499, 399});
    Check(clicks == 0 && !first.IsPressed(), "capture cancels click outside");
    ui.PointerDown(point);
    ui.PointerUp(point);
    Check(clicks == 1, "mouse release activates button");
    ui.KeyEvent(Key::Space, true);
    ui.KeyEvent(Key::Space, false, false, {false, true, false});
    Check(clicks == 1 && !first.IsPressed(), "modifier change cancels Space activation without leaving pressed state");
    ui.PointerDown(point);
    first.AddClass("hidden");
    ui.PointerUp(point);
    Check(clicks == 1 && !first.IsPressed(), "hiding a control cancels capture before pointer release");
    first.RemoveClass("hidden");
    ui.Prepare(500, 400);
    ui.SetFocus(&first);
    ui.KeyEvent(Key::Space, true);
    ui.SetFocus(&text);
    ui.KeyEvent(Key::Space, false);
    Check(clicks == 1 && !first.IsPressed(), "focus loss cancels keyboard press");
    ui.SetFocus(&first);
    ui.KeyEvent(Key::Tab, true);
    Check(ui.GetFocus() == &text, "Tab skips disabled widgets");
    ui.KeyEvent(Key::Tab, true, false, {true, false, false});
    Check(ui.GetFocus() == &first, "reverse Tab navigation");
    ui.SetFocus(&text);
    text.AddClass("hidden");
    ui.Prepare(500, 400);
    Check(ui.GetFocus() == nullptr, "hidden control loses focus");
    ui.TextInput("invisible");
    Check(text.GetText().empty(), "hidden control receives no committed text");
    text.RemoveClass("hidden");
    ui.Prepare(500, 400);
    ui.SetFocus(&text);
    ui.TextInput("hello");
    const auto watched = ui.Observe(&text);
    ui.Root().RemoveChild(text);
    Check(watched.Get() == nullptr && ui.GetFocus() == nullptr, "destroyed control invalidates all safe observers");
    auto& ephemeral = ui.Root().Add<Button>("Delete myself");
    const auto ephemeralRef = ui.Observe(&ephemeral);
    int afterDeletion = 0;
    ephemeral.Clicked().Connect([&] { ui.Root().RemoveChild(ephemeral); });
    ephemeral.Clicked().Connect([&] { ++afterDeletion; });
    ui.Prepare(500, 400);
    ui.SetFocus(&ephemeral);
    ui.KeyEvent(Key::Enter, true);
    Check(!ephemeralRef.Get() && afterDeletion == 1 && !ui.GetFocus(),
          "widget safely deletes itself inside signal delivery");
    ui.Root().ClearChildren();
    auto& panel = ui.Root().Add<Widget>();
    panel.SetId("clip");
    auto& overflow = panel.Add<Button>("Clipped");
    int clippedClicks = 0;
    overflow.Clicked().Connect([&] { ++clippedClicks; });
    Check(ui.SetStyleSheet(
              "window { padding: 10px; } #clip { height: 30px; width: 100px; } button { width: 200px; height: 50px; }",
              error),
          "clip stylesheet");
    ui.Prepare(500, 400);
    ui.PointerDown({150, 20});
    ui.PointerUp({150, 20});
    Check(clippedClicks == 0, "ancestor clipping also limits hit testing");
    ui.PointerDown({20, 20});
    ui.PointerUp({20, 20});
    Check(clippedClicks == 1, "visible portion of clipped widget remains interactive");
}
} // namespace

int main()
{
    try
    {
        Signals();
        Css();
        Editing();
        FontFace font(ESTUDIOUI_FONT_PATH);
        FontAndWidgets(font);
        std::cout << "Core PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Core FAIL after " << s_Checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
