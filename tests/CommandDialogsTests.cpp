#include <EStudioUI/Dialog.h>
#include <EStudioUI/InputDialog.h>
#include <EStudioUI/MenuBar.h>
#include <EStudioUI/MessageBox.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
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

template <class F> void Throws(F action, const char* message)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, message);
}

Point Center(const Widget& widget)
{
    const auto b = widget.GetBounds();
    return {b.m_X + b.m_Width * 0.5f, b.m_Y + b.m_Height * 0.5f};
}

void Click(UiContext& ui, Widget& widget)
{
    ui.PointerDown(Center(widget));
    ui.PointerUp(Center(widget));
}

void KeyDown(UiContext& ui, Key key)
{
    ui.KeyEvent(key, true);
    ui.KeyEvent(key, false);
}

void PopupGeometry()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& owner = ui.Root().Add<Button>("Popover");
    owner.SetWidth(SizeRule::Fixed(80));
    owner.SetHeight(SizeRule::Fixed(30));
    owner.SetFloatingBounds({100, 100, 80, 30});
    auto& popup = Popup::Create(owner);
    popup.SetWidth(SizeRule::Fixed(140));
    popup.SetHeight(SizeRule::Fixed(90));
    popup.Add<Button>("Apply");
    ui.Prepare(500, 400);
    ui.SetFocus(&owner);
    auto session = popup.Open(owner);
    Check(session.IsPending() && ui.GetPopupCount() == 1, "Anchored popup opens through the shared stack");
    Check(popup.GetBounds().m_Y == 130 && popup.GetBounds().m_X == 100, "Below placement uses the actual anchor");
    const auto measures = ui.GetStats().m_Measurements;
    Transform shifted;
    shifted.m_Operations.push_back({TransformOperation::Kind::Translate, {35, 12}});
    owner.SetStyleProperty(StyleProperty::Transform, shifted);
    ui.Prepare(500, 400);
    Check(popup.GetBounds().m_X == 135 && popup.GetBounds().m_Y == 142,
          "Paint-only anchor transforms reposition a live popup");
    Check(ui.GetStats().m_Measurements == measures, "Anchor transform retains unrelated intrinsic measurements");
    owner.ClearStyleProperty(StyleProperty::Transform);
    ui.Prepare(500, 400);
    const auto same = popup.Open(Point{1, 1});
    Check(same.IsPending() && session.IsPending() && ui.GetPopupCount() == 1, "Repeated Open preserves one session");
    owner.SetFloatingBounds({380, 340, 80, 30});
    ui.Prepare(500, 400);
    Check(popup.GetBounds().m_Y == 250 && popup.GetBounds().m_X == 360, "Popup flips above and clamps horizontally");
    ui.Prepare(100, 70);
    Check(popup.GetBounds().m_Width == 140 && popup.GetBounds().m_Height == 90,
          "Popup viewport clipping preserves explicit fixed dimensions");
    ui.Prepare(500, 400);
    KeyDown(ui, Key::Escape);
    Check(session.GetResult()->m_Reason == PopupCloseReason::Escape && same.GetResult().has_value(),
          "Escape settles every copy of one session");
    Check(ui.GetFocus() == &owner, "Popup restores its live trigger focus");
    session = popup.Open(owner);
    owner.SetVisible(false);
    ui.Prepare(500, 400);
    Check(!session.IsPending(), "Hidden popup owner ends the presentation");
    owner.SetVisible(true);
    auto& other = ui.Root().Add<Button>("Other anchor");
    ui.Prepare(500, 400);
    session = popup.Open(other);
    ui.Root().RemoveChild(other);
    ui.Prepare(500, 400);
    Check(session.GetResult()->m_Reason == PopupCloseReason::OwnerExpired,
          "Removed directional anchor cannot leave a stale popup");
    PopupOptions invalid;
    invalid.m_Modal = invalid.m_Modeless = true;
    Throws([&] { popup.Open(Point{}, invalid); }, "Conflicting modal/modeless options are rejected");
    invalid = {};
    invalid.m_Placement = PopupPlacement::Below;
    Throws([&] { popup.Open(Point{}, invalid); }, "Directional placement requires an anchor");
    invalid = {};
    invalid.m_AnchorWidget = &popup;
    Throws([&] { popup.Open(Point{}, invalid); }, "Self-anchoring cannot create geometry feedback");
    session = popup.Open(owner);
    const WidgetRef weak(&popup);
    session.OnFinished([&](PopupResult) { ui.Root().RemoveChild(popup); });
    popup.Close();
    Check(!weak.Get() && session.GetResult().has_value(), "Completion may remove its popup without use-after-free");
}

void Dialogs()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& launcher = ui.Root().Add<Button>("Open settings");
    launcher.SetFloatingBounds({5, 5, 110, 35});
    int background = 0;
    launcher.Clicked().Connect([&] { ++background; });
    auto& dialog = Dialog::Create(launcher, "Sketch settings");
    auto& editor = dialog.Content().Add<TextBox>("42");
    editor.SetValidator(std::make_shared<IntegerValidator>(1, 100));
    ui.Prepare(800, 600);
    ui.SetFocus(&launcher);
    auto session = dialog.Open();
    Check(ui.GetFocus() == &editor, "Dialog chooses its first content control as initial focus");
    auto bounds = dialog.GetBounds();
    Check(std::abs(bounds.m_X * 2 + bounds.m_Width - 800) < 0.01f &&
              std::abs(bounds.m_Y * 2 + bounds.m_Height - 600) < 0.01f,
          "Dialog is centered on initial layout");
    ui.Prepare(1000, 700);
    bounds = dialog.GetBounds();
    Check(std::abs(bounds.m_X * 2 + bounds.m_Width - 1000) < 0.01f, "Dialog stays centered while resizing");
    Click(ui, launcher);
    Check(background == 0 && session.IsPending(), "Modal outside presses neither click through nor dismiss");
    auto node = ui.Semantics().GetNode(ui.Semantics().GetId(dialog));
    Check(node && node->m_Info.m_Role == SemanticRole::Dialog && node->m_Info.m_Name == "Sketch settings",
          "Dialog semantics expose the canonical title");
    Check(!ui.Semantics().GetNode(ui.Semantics().GetId(launcher))->m_Enabled,
          "Modal semantics disable underlying controls");
    editor.SetText("-");
    KeyDown(ui, Key::Enter);
    Check(session.IsPending(), "Intermediate editor input blocks default acceptance");
    editor.SetText("64");
    KeyDown(ui, Key::Enter);
    Check(session.GetResult()->m_Reason == PopupCloseReason::Accepted &&
              session.GetResult()->m_Value == static_cast<int>(StandardButton::Ok),
          "Enter activates the configured standard default");
    Check(ui.GetFocus() == &launcher, "Dialog acceptance restores prior focus");
    session = dialog.Open();
    ui.SetFocus(dialog.Buttons().GetButton(StandardButton::Cancel));
    KeyDown(ui, Key::Enter);
    Check(session.GetResult()->m_Reason == PopupCloseReason::Rejected,
          "A focused Cancel handles Enter before the default");
    dialog.SetButtons({StandardButton::Apply, StandardButton::Help, StandardButton::Save, StandardButton::Cancel});
    session = dialog.Open();
    Click(ui, *dialog.Buttons().GetButton(StandardButton::Apply));
    Check(session.IsPending(), "Apply keeps the dialog open for application handlers");
    Click(ui, *dialog.Buttons().GetButton(StandardButton::Help));
    Check(session.IsPending(), "Help keeps the dialog open");
    KeyDown(ui, Key::Escape);
    Check(session.GetResult()->m_Reason == PopupCloseReason::Escape, "Escape uses the shared top-layer result");
    session = dialog.Open(false);
    Click(ui, launcher);
    Check(background == 1 && session.IsPending(), "Modeless dialogs keep outside controls interactive without closing");
    KeyDown(ui, Key::Escape);
    Check(session.IsPending(), "Escape outside a modeless dialog stays with the focused workspace");
    auto& workspacePopup = Popup::Create(launcher);
    workspacePopup.Add<Button>("Workspace menu");
    auto workspaceSession = workspacePopup.Open(launcher);
    Check(session.IsPending() && ui.GetPopupCount() == 2,
          "Workspace command surfaces preserve an existing modeless tool");
    workspacePopup.Close();
    Check(session.IsPending() && !workspaceSession.IsPending(), "Closing a workspace popup retains the modeless tool");

    ui.SetFocus(&launcher);
    dialog.Reject();
    Check(ui.GetFocus() == &launcher, "Closing a modeless dialog preserves focus already outside it");
    Check(session.GetResult()->m_Reason == PopupCloseReason::Rejected,
          "Explicit modeless rejection completes normally");
    Throws([&] { dialog.SetDefaultButton(&launcher); }, "Default action must be owned by its dialog");
    Throws([&] { dialog.SetInitialFocus(&launcher); }, "Initial focus must be owned by its dialog");
    const auto configured = dialog.Buttons().GetButtons();
    Throws([&] { dialog.SetButtons({StandardButton::Ok, StandardButton::Ok}); }, "Duplicate responses are rejected");
    Check(dialog.Buttons().GetButtons() == configured, "Invalid button candidates preserve the prior row");
    Throws([&] { dialog.SetButtons({static_cast<StandardButton>(99)}); }, "Unknown standard response is rejected");
    auto& message = MessageBox::Create(launcher, "Unsaved sketch", "Save your changes before closing the sketch?");
    message.SetKind(MessageKind::Question);
    message.SetButtons({StandardButton::Save, StandardButton::Discard, StandardButton::Cancel});
    session = message.Open();
    Click(ui, *message.Buttons().GetButton(StandardButton::Discard));
    Check(session.GetResult()->m_Value == static_cast<int>(StandardButton::Discard) &&
              session.GetResult()->m_Reason == PopupCloseReason::Accepted,
          "Destructive response has a stable distinct accepted value");
    session = dialog.Open();
    auto& popover = Popup::Create(editor);
    popover.Add<Button>("Suggestion");
    auto nested = popover.Open(editor);
    Check(ui.GetPopupCount() == 2 && ui.GetPopupParent(popover) == &dialog,
          "Owned popover infers the enclosing dialog");
    dialog.Accept(97);
    Check(nested.GetResult()->m_Reason == PopupCloseReason::ParentClosed && session.GetResult()->m_Value == 97,
          "Closing a dialog settles descendants separately");
    session = dialog.Open();
    session.OnFinished([](PopupResult) { throw std::runtime_error("observer"); });
    dialog.Reject();
    Check(session.GetCallbackError() != nullptr, "Dialog result contains observer failures");
    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                dialog.Open();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    Check(rejected, "Dialog direct access remains owner-thread only");
}

void InputResults()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& launcher = ui.Root().Add<Button>("Rename");
    auto& input = InputDialog::Create(launcher, "Rename sketch", "Sketch name", "Original");
    ui.Prepare(700, 500);
    auto first = input.Open();
    Check(first.IsPending() && ui.GetFocus() == &input.Editor(), "Input prompt immediately focuses its shared editor");
    input.Editor().SetText("Changed");
    std::optional<InputDialogResult> observed;
    first.OnFinished([&](InputDialogResult value) { observed = std::move(value); });
    KeyDown(ui, Key::Enter);
    Check(observed && observed->m_Text == "Changed", "Input completion carries owned accepted bytes");
    auto second = input.Open();
    input.Editor().SetText("Unaccepted");
    KeyDown(ui, Key::Escape);
    Check(!second.GetResult()->m_Text && first.GetResult()->m_Text == "Changed",
          "Cancellation returns no text and preserves earlier results");
    input.Editor().SetText("12");
    input.Editor().SetValidator(std::make_shared<IntegerValidator>(1, 99));
    auto third = input.Open();
    input.Editor().SetText("-");
    Click(ui, *input.Buttons().GetButton(StandardButton::Ok));
    Check(third.IsPending(), "Input OK shares Acceptable validation with Enter");
    input.Editor().SetText("23");
    const WidgetRef weak(&input);
    third.OnFinished(
        [&](InputDialogResult result)
        {
            Check(result.m_Text == "23", "Input snapshots text before callbacks remove its editor");
            ui.Root().RemoveChild(input);
        });
    input.Accept();
    Check(!weak.Get() && third.GetResult()->m_Text == "23", "Accepted input result outlives its dialog");
    auto& expired = InputDialog::Create(launcher, "Pending", "Name");
    auto pending = expired.Open();
    ui.Root().RemoveChild(launcher);
    ui.Prepare(700, 500);
    Check(pending.GetResult()->m_Popup.m_Reason == PopupCloseReason::OwnerExpired && !pending.GetResult()->m_Text,
          "Owner expiry cancels input without a partial value");
}

void Menus()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& bar = ui.Root().Add<MenuBar>();
    auto& file = bar.AddMenu("File");
    auto& edit = bar.AddMenu("Edit");
    auto& view = bar.AddMenu("View");
    auto action = ui.CreateAction("Save");
    action->SetShortcut({Key::S, ShortcutModifier::Primary});
    int executed = 0;
    action->Triggered().Connect([&] { ++executed; });
    auto& save = file.AddAction(action);
    save.SetIcon("S");
    auto& recent = file.AddMenu("Recent");
    recent.AddAction("Sketch 01");
    file.AddSeparator();
    file.AddAction("Unavailable", false);
    edit.AddAction("Undo");
    auto& check = view.AddAction("Grid");
    check.SetCheckable(true);
    check.SetChecked(true);
    auto& editor = ui.Root().Add<TextBox>();
    ui.Prepare(800, 600);
    ui.SetFocus(&editor);
    Modifiers primary;
    primary.m_Control = true;
#ifdef __APPLE__
    primary.m_Meta = true;
#else
    primary.m_PhysicalControl = true;
#endif
    ui.KeyEvent(Key::S, true, false, primary);
    Check(executed == 1, "Menu action shortcut is available before first opening");
    Check(!save.GetShortcutHint().empty(), "Menu action formats its canonical shortcut hint");
    auto session = bar.OpenMenu(0);
    Check(bar.GetCurrentIndex() == 0 && file.IsOpen(), "Menu bar opens its first command surface");
    Click(ui, bar.GetButton(1));
    Check(bar.GetCurrentIndex() == 1 && !session.IsPending(),
          "Direct trigger press switches menus without click-through");
    ui.PointerMove(Center(bar.GetButton(2)));
    Check(bar.GetCurrentIndex() == 2, "Hover switches top-level menus while a chain is open");
    KeyDown(ui, Key::Left);
    Check(bar.GetCurrentIndex() == 1, "Left navigates to the preceding top-level menu");
    bar.GetButton(0).SetEnabled(false);
    KeyDown(ui, Key::Left);
    Check(bar.GetCurrentIndex() == 2, "Keyboard menu navigation skips disabled triggers");
    bar.GetButton(0).SetEnabled(true);
    bar.OpenMenu(0);
    KeyDown(ui, Key::Down);
    KeyDown(ui, Key::Down);
    KeyDown(ui, Key::Right);
    Check(recent.IsOpen() && ui.GetPopupCount() == 2, "Right opens the focused nested menu");
    KeyDown(ui, Key::Left);
    Check(!recent.IsOpen() && file.IsOpen(), "Left closes only the nested menu");
    KeyDown(ui, Key::Home);
    KeyDown(ui, Key::Enter);
    Check(executed == 2 && !file.IsOpen() && bar.GetCurrentIndex() == -1,
          "Menu activation closes before the shared command executes");
    Check(ui.GetFocus() == &editor, "Switching a menu chain preserves the original focus return");
    auto& alternateSave = edit.AddAction(action);
    save.SetAction(nullptr);
    ui.KeyEvent(Key::S, true, false, primary);
    Check(executed == 3, "Unbinding one menu presentation preserves another shared shortcut");
    alternateSave.SetAction(nullptr);
    ui.KeyEvent(Key::S, true, false, primary);
    Check(executed == 3 && ui.GetLastShortcutStatus() == ShortcutStatus::Unmatched,
          "Unbinding the final menu presentation retires its persistent shortcut");
    ui.SetFocus(&bar);
    KeyDown(ui, Key::Down);
    Check(bar.GetCurrentIndex() >= 0, "Tab-stop menu bar opens with Down");
    KeyDown(ui, Key::Escape);
    auto node = ui.Semantics().GetNode(ui.Semantics().GetId(bar));
    Check(node && node->m_Info.m_Role == SemanticRole::MenuBar, "Menu bar exposes a distinct semantic role");
    bar.OpenMenu(2);
    Check(ui.Semantics().GetNode(ui.Semantics().GetId(check))->m_Info.m_Checked,
          "Checked command menu item retains canonical semantics");
    const WidgetRef menuWeak(&view);
    ui.Root().RemoveChild(bar);
    ui.Prepare(800, 600);
    Check(!menuWeak.Get() && ui.GetPopupCount() == 0, "Menu-bar removal retires all root-owned menu surfaces");
}
} // namespace

int main()
{
    try
    {
        PopupGeometry();
        Dialogs();
        InputResults();
        Menus();
        std::cout << "Command/dialog checks: " << s_Checks << " passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Command/dialog check " << s_Checks << ": " << error.what() << '\n';
        return 1;
    }
}
