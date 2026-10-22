#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/OpenGLWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
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

template <class Callback> bool Rejects(Callback callback)
{
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        return true;
    }
    return false;
}

Point Center(const Widget& widget)
{
    const auto bounds = widget.GetBounds();
    return {bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2};
}

Modifiers Primary()
{
    Modifiers result;
    result.m_Control = true;
#ifdef __APPLE__
    result.m_Meta = true;
#else
    result.m_PhysicalControl = true;
#endif
    return result;
}

void Press(UiContext& ui, Key key, Modifiers modifiers = {})
{
    ui.KeyEvent(key, true, false, modifiers);
}

void Click(UiContext& ui, Widget& widget)
{
    const auto position = Center(widget);
    ui.PointerDown(position);
    ui.PointerUp(position);
}

class InputProbe : public Button
{
  public:
    InputProbe() : Button("Probe")
    {
    }

    std::vector<InputEvent> m_Events;
    std::function<void()> m_Preview;
    std::function<void()> m_Delivery;
    bool m_HandleAll = false;
    bool m_ConsumePreview = false;
    int m_CaptureLosses = 0;

  protected:
    bool OnInputPreview(const InputEvent& event) override
    {
        if (m_ConsumePreview)
            return true;
        if (m_Preview)
        {
            auto callback = m_Preview;
            callback();
        }
        return Button::OnInputPreview(event);
    }

    bool OnInput(const InputEvent& event) override
    {
        m_Events.push_back(event);
        if (m_Delivery)
        {
            auto callback = m_Delivery;
            callback();
        }
        return m_HandleAll || Button::OnInput(event);
    }

    void OnCaptureLost() override
    {
        ++m_CaptureLosses;
        Button::OnCaptureLost();
    }
};

class FocusPopup : public Widget
{
  public:
    std::function<void()> m_OnFocus;

    bool IsFocusable() const override
    {
        return true;
    }

  protected:
    void OnFocus(bool focused) override
    {
        if (focused && m_OnFocus)
        {
            auto callback = m_OnFocus;
            callback();
        }
    }
};

void SharedActions(FontFace& font)
{
    UiContext ui(font);
    auto action = ui.CreateAction("Save");
    auto& first = ui.Root().Add<ToolBar>().AddAction(action);
    auto& second = ui.Root().Add<Button>();
    second.SetAction(action);
    auto& menu = ui.Root().Add<ContextMenu>();
    auto& entry = menu.AddAction(action);
    int executions = 0;
    action->Triggered().Connect([&] { ++executions; });
    action->SetShortcut({Key::S, ShortcutModifier::Primary});
    action->SetCheckable(true);
    ui.Prepare(800, 600);
    Click(ui, first);
    Check(executions == 1 && action->GetSnapshot()->m_Checked && first.IsChecked(),
          "pointer executes/toggles shared action once");
    Check(entry.IsChecked() && second.GetAction() == action, "all presentations share canonical checked state");
    Press(ui, Key::S, Primary());
    Check(executions == 2 && !first.IsChecked() && ui.GetLastShortcutStatus() == ShortcutStatus::Triggered,
          "duplicate presentations execute one shortcut once");
    ui.KeyEvent(Key::S, true, true, Primary());
    Check(executions == 2, "shortcuts do not repeat by default");
    auto altGraph = Primary();
    altGraph.m_Alt = altGraph.m_AltGraph = true;
    Press(ui, Key::S, altGraph);
    Check(executions == 2, "AltGr cannot execute a primary shortcut");
    auto shifted = Primary();
    shifted.m_Shift = true;
    Press(ui, Key::S, shifted);
    Check(executions == 2, "additional modifiers prevent matching");
    int notifications = 0;
    action->Properties().Subscribe([&](const StateChange<ActionState>&) { ++notifications; });
    ui.States().Batch(
        [&]
        {
            action->SetText("Save document");
            action->SetEnabled(false);
            action->SetChecked(true);
        });
    Check(notifications == 1 && first.GetText() == "Save document" && second.GetText() == first.GetText() &&
              !first.IsEnabled() && !second.IsEnabled() && entry.IsChecked(),
          "one coherent action revision updates all presentations");
    Check(!action->Trigger(), "disabled command cannot execute programmatically");
    action->SetEnabled(false);
    Check(notifications == 1, "equality-only action update does not notify");
    Check(Rejects([&] { action->SetShortcuts({Shortcut{}}); }), "invalid shortcut state is rejected atomically");
    Check(action->GetSnapshot()->m_Shortcuts.size() == 1, "failed update preserves previous gesture");
    action->SetEnabled(true);
    action->SetCheckable(false);
    Check(!first.IsChecked() && !entry.IsChecked(), "disabling checkability clears canonical and visual state");
    Check(Rejects([&] { action->SetChecked(true); }), "noncheckable action cannot be checked");
    bool rejected = false;
    std::thread worker([&] { rejected = Rejects([&] { action->SetText("foreign"); }); });
    worker.join();
    Check(rejected && first.GetText() == "Save document", "action owner affinity applies before state changes");
    Check(Rejects([&] { ui.States().Batch([&] { action->Trigger(); }); }),
          "commands do not execute inside staged transactions");
    action->Triggered().Connect([&] { Check(!action->Trigger(), "recursive action execution is suppressed"); });
    action->Trigger();
    action->SetText("Again");
    ui.Root().RemoveChild(second);
    const auto handle = action->GetHandle();
    DispatchReceipt queued;
    const auto previousCount = executions;
    std::thread producer([&] { queued = handle.Post([](Action& live) { live.Trigger(); }); });
    producer.join();
    Check(executions == previousCount && queued.GetStatus() == DispatchStatus::Pending,
          "worker action request remains queued until owner delivery");
    ui.GetDispatcher().Drain();
    Check(executions == previousCount + 1 && queued.GetStatus() == DispatchStatus::Completed,
          "typed action handle executes once on the owner");
    action->SetText("Still live");
    Check(first.GetText() == "Still live", "deleting one presentation retires only its binding");
    auto& chooser = ui.Root().Add<SplitToolButton>();
    chooser.AddAction(action);
    action->SetText("Shared default");
    Check(chooser.GetPrimaryButton().GetText() == "Shared default", "split default follows shared command captions");
    const int previousExecutions = executions;
    chooser.TriggerDefault();
    Check(executions == previousExecutions + 1, "split primary executes its shared default exactly once");
    action->SetEnabled(false);
    Check(!chooser.GetPrimaryButton().IsEnabled(), "split primary follows shared execution eligibility");
    action->SetEnabled(true);
    bool closedBeforeCommand = false;
    action->Triggered().Connect([&] { closedBeforeCommand = !menu.IsOpen(); });
    menu.Open({250, 180});
    Click(ui, entry);
    Check(closedBeforeCommand && !menu.IsOpen(), "menu dismisses before shared command callbacks");
}

void ScopedShortcuts(FontFace& font)
{
    UiContext ui(font);
    auto& area = ui.Root().Add<Widget>();
    auto& nested = area.Add<Widget>();
    auto& editor = nested.Add<TextBox>();
    auto global = ui.CreateAction("Global");
    auto local = ui.CreateAction("Local");
    auto direct = ui.CreateAction("Direct");
    for (const auto& action : {global, local, direct})
        action->SetShortcut({Key::K, ShortcutModifier::Primary});
    int globalCount = 0, localCount = 0, directCount = 0;
    global->Triggered().Connect([&] { ++globalCount; });
    local->Triggered().Connect([&] { ++localCount; });
    direct->Triggered().Connect([&] { ++directCount; });
    ui.Root().AddAction(global);
    area.AddAction(local, ShortcutScope::Subtree);
    editor.AddAction(direct, ShortcutScope::Widget);
    ui.Prepare(600, 400);
    ui.SetFocus(&editor);
    Press(ui, Key::K, Primary());
    Check(directCount == 1 && !localCount && !globalCount, "widget shortcut takes precedence over subtree/window");
    editor.RemoveAction(direct);
    Press(ui, Key::K, Primary());
    Check(localCount == 1 && !globalCount, "subtree shortcut takes precedence over window");
    nested.AddAction(direct, ShortcutScope::Subtree);
    Press(ui, Key::K, Primary());
    Check(directCount == 2, "nearest matching subtree wins");
    nested.RemoveAction(direct);
    area.AddAction(direct, ShortcutScope::Subtree);
    Press(ui, Key::K, Primary());
    Check(localCount == 1 && directCount == 2 && ui.GetLastShortcutStatus() == ShortcutStatus::Ambiguous,
          "equal-scope ambiguity consumes input without executing an arbitrary action");
    direct->SetEnabled(false);
    Press(ui, Key::K, Primary());
    Check(localCount == 2, "disabled duplicate does not introduce ambiguity");
    area.SetVisible(false);
    ui.Prepare(600, 400);
    Press(ui, Key::K, Primary());
    Check(globalCount == 1, "hidden subtree bindings no longer participate");
    ui.Root().RemoveChild(area);
    for (int i = 0; i < 10000; ++i)
        ui.Root().Add<Widget>();
    ui.Prepare(600, 400);
    const auto before = ui.GetStats();
    Press(ui, Key::K, Primary());
    const auto after = ui.GetStats();
    Check(after.m_ShortcutChecks - before.m_ShortcutChecks == 1 && globalCount == 2,
          "shortcut work visits one surviving association, not ten thousand widgets");
    Check(after.m_Measurements == before.m_Measurements && after.m_StyleResolutions == before.m_StyleResolutions,
          "shortcut execution preserves unrelated intrinsic/style caches");
}

void RichInput(FontFace& font)
{
    UiContext ui(font);
    auto& probe = ui.Root().Add<InputProbe>();
    probe.m_HandleAll = true;
    ui.Prepare(600, 400);
    ui.SetFocus(&probe);
    KeyboardEvent key{Key::Q, true, false, {}, Key::A, 37, 12.5};
    ui.DispatchInput(key);
    const auto received = std::get<KeyboardEvent>(probe.m_Events.back());
    Check(received.m_Key == Key::Q && received.m_PhysicalKey == Key::A && received.m_ScanCode == 37 &&
              received.m_Timestamp == 12.5,
          "logical/physical/scancode/timestamp information survives routing");
    ui.DispatchInput(TextInputEvent{"owned text", 13});
    Check(std::get<TextInputEvent>(probe.m_Events.back()).m_Text == "owned text",
          "committed text remains separate owning input");
    Shortcut physical{Key::A, ShortcutModifier::None, true};
    Check(physical.Matches(key) && !Shortcut{Key::A}.Matches(key),
          "physical and logical shortcut matching differ deliberately");
    PointerEvent pointer{Center(probe)};
    pointer.m_Type = PointerEventType::Down;
    pointer.m_Button = PointerButton::Middle;
    pointer.m_Buttons = 4;
    pointer.m_PointerId = 7;
    pointer.m_Device = PointerDevice::Pen;
    pointer.m_Pressure = 0.6f;
    pointer.m_Timestamp = 14;
    ui.DispatchInput(pointer);
    const auto delivered = std::get<PointerEvent>(probe.m_Events.back());
    Check(delivered.m_PointerId == 7 && delivered.m_Device == PointerDevice::Pen &&
              delivered.m_Button == PointerButton::Middle && delivered.m_Buttons == 4 && delivered.m_Pressure == 0.6f,
          "pointer device, identity, buttons, pressure survive delivery");
    const auto count = probe.m_Events.size();
    pointer.m_Type = PointerEventType::Up;
    pointer.m_PointerId = 8;
    ui.DispatchInput(pointer);
    Check(probe.m_Events.size() == count, "another pointer cannot release existing capture");
    pointer.m_PointerId = 7;
    pointer.m_Position = {-100, -100};
    ui.DispatchInput(pointer);
    Check(probe.m_Events.size() == count + 1 && !std::get<PointerEvent>(probe.m_Events.back()).m_InsideClip,
          "matching capture release retains outside-clip information");
    probe.m_Delivery = [&]
    {
        Check(Rejects([&] { Press(ui, Key::A); }), "nested input is diagnosed");
        ui.Prepare(600, 400);
        Check(ui.GetPhase() == UiPhase::Idle, "explicit geometry preparation during input remains supported");
        Check(Rejects([&] { ui.SetTime(ui.GetTime()); }), "nested frame advancement during input is diagnosed");
        Check(Rejects([&] { ui.GetDispatcher().Drain(); }), "nested queue drain during input is diagnosed");
    };
    Press(ui, Key::B);
    probe.m_Delivery = [] { throw std::runtime_error("user input callback"); };
    Check(Rejects([&] { Press(ui, Key::C); }), "callback error propagates through direct delivery");
    probe.m_Delivery = {};
    Press(ui, Key::D);
    Check(std::get<KeyboardEvent>(probe.m_Events.back()).m_Key == Key::D, "input guard restores after an exception");
    key.m_Timestamp = std::numeric_limits<double>::infinity();
    Check(Rejects([&] { ui.DispatchInput(key); }), "nonfinite event metadata is rejected before routing");
    pointer.m_Pressure = 2;
    Check(Rejects([&] { ui.DispatchInput(pointer); }), "invalid pressure is rejected before capture mutation");
    auto& next = ui.Root().Add<InputProbe>();
    ui.Prepare(600, 400);
    probe.m_Preview = [&] { ui.SetFocus(&next); };
    Press(ui, Key::E);
    Check(next.m_Events.empty(), "preview focus change does not retarget the same key to a new receiver");
    probe.m_Preview = {};
    probe.m_HandleAll = false;
    ui.PointerDown(Center(probe));
    Check(probe.IsPressed(), "pointer press establishes ordinary button capture");
    probe.m_ConsumePreview = true;
    ui.PointerUp(Center(probe));
    Check(!probe.IsPressed() && probe.m_CaptureLosses > 0, "intercepted pointer release cancels widget capture state");
    probe.m_ConsumePreview = false;
    auto& edit = ui.Root().Add<TextBox>();
    edit.SetText("abc");
    ui.Prepare(600, 400);
    ui.SetFocus(&edit);
    auto altGraph = Primary();
    altGraph.m_Alt = altGraph.m_AltGraph = true;
    Press(ui, Key::A, altGraph);
    ui.TextInput("@");
    Check(edit.GetText() == "abc@", "AltGr does not accidentally select all before committed text");
}

void FocusAndPopupStack(FontFace& font)
{
    UiContext ui(font);
    auto& source = ui.Root().Add<Button>("Open");
    auto& scope = ui.Root().Add<Widget>();
    scope.SetFocusScope(true);
    auto& first = scope.Add<TextBox>();
    auto& last = scope.Add<TextBox>();
    ui.Prepare(800, 600);
    ui.SetFocus(&last);
    Press(ui, Key::Tab);
    Check(ui.GetFocus() == &first, "Tab cycles inside an explicit focus scope");
    Press(ui, Key::Tab, Modifiers{true});
    Check(ui.GetFocus() == &last, "reverse Tab cycles inside the same scope");
    auto& dialog = ui.Root().Add<Widget>();
    dialog.SetWidth(SizeRule::Fixed(300));
    dialog.SetHeight(SizeRule::Fixed(200));
    dialog.SetVisible(false);
    auto& accept = dialog.Add<Button>("Accept");
    auto& cancel = dialog.Add<Button>("Cancel");
    auto menu = std::make_unique<ContextMenu>();
    menu->AddAction("Choice");
    auto* menuPointer = menu.get();
    ui.AddOwnedPopup(std::move(menu), accept);
    ui.SetFocus(&source);
    auto modal = ui.ShowPopup(dialog, {100, 120}, PopupOptions::Modal());
    Check(modal.IsPending() && ui.GetFocus() == &accept, "nonfocusable modal surface focuses its first eligible child");
    ui.SetFocus(&source);
    Check(ui.GetFocus() == &accept, "modal scope rejects programmatic background focus");
    auto global = ui.CreateAction("Background");
    global->SetShortcut({Key::G, ShortcutModifier::Primary});
    int background = 0;
    global->Triggered().Connect([&] { ++background; });
    ui.Root().AddAction(global);
    Press(ui, Key::G, Primary());
    Check(background == 0, "modal barrier blocks background window shortcuts");
    const auto anchor = Center(accept);
    auto child = menuPointer->Open({anchor.m_X + 60, anchor.m_Y + 40});
    Check(ui.GetPopupCount() == 2 && ui.GetPopupParent(*menuPointer) == &dialog && modal.IsPending(),
          "owned popup infers its modal parent without closing it");
    Press(ui, Key::Escape);
    Check(child.GetResult()->m_Reason == PopupCloseReason::Escape && modal.IsPending() && ui.GetFocus() == &accept,
          "Escape closes only the top popup and restores modal focus");
    child = menuPointer->Open({anchor.m_X + 60, anchor.m_Y + 40});
    Click(ui, source);
    Check(!child.IsPending() && modal.IsPending() && ui.GetPopupCount() == 1,
          "outside modal click dismisses a child while preserving the modal barrier");
    child = menuPointer->Open({anchor.m_X + 60, anchor.m_Y + 40});
    ui.ClosePopup(dialog, {PopupCloseReason::Accepted, 42});
    Check(modal.GetResult()->m_Reason == PopupCloseReason::Accepted && modal.GetResult()->m_Value == 42 &&
              child.GetResult()->m_Reason == PopupCloseReason::ParentClosed,
          "closing a parent settles every session exactly once");
    Check(ui.GetPopupCount() == 0 && ui.GetFocus() == &source, "closing the modal restores pre-modal focus");
    int completions = 0;
    modal.OnFinished(
        [&](PopupResult result)
        {
            ++completions;
            Check(result.m_Value == 42, "late observer sees owned result");
        });
    modal.OnFinished([](PopupResult) { throw std::runtime_error("completion"); });
    modal.OnFinished([&](PopupResult) { ++completions; });
    Check(completions == 2 && modal.GetCallbackError(),
          "observer errors are contained and do not suppress later observers");
    auto deactivated = ui.ShowPopup(dialog, {100, 120}, PopupOptions::Modal());
    deactivated.OnFinished([&](PopupResult) { ui.ShowPopup(dialog, {0, 0}); });
    ui.Deactivate();
    Check(deactivated.GetResult()->m_Reason == PopupCloseReason::Deactivated && deactivated.GetCallbackError() &&
              !ui.GetPopup() && !ui.GetFocus(),
          "deactivation cannot be undone by a completion observer reopening a popup");
    (void)cancel;
}

void MenuAndLifetime(FontFace& font)
{
    PopupSession surviving;
    {
        UiContext ui(font);
        auto& source = ui.Root().Add<Button>("Source");
        auto& menu = ui.Root().Add<ContextMenu>();
        auto& nested = menu.AddMenu("Tools");
        auto action = ui.CreateAction("Execute");
        int executions = 0;
        action->Triggered().Connect(
            [&]
            {
                ++executions;
                Check(!menu.IsOpen() && !nested.IsOpen(), "nested command closes entire menu chain first");
            });
        nested.AddAction(action);
        ui.Prepare(800, 600);
        ui.SetFocus(&source);
        auto parent = menu.Open({200, 180});
        Press(ui, Key::Down);
        Press(ui, Key::Right);
        Check(menu.IsOpen() && nested.IsOpen() && ui.GetPopupCount() == 2,
              "Right opens a nested menu while retaining its parent");
        Press(ui, Key::Left);
        Check(menu.IsOpen() && !nested.IsOpen(), "Left returns to the parent menu");
        Press(ui, Key::Right);
        Press(ui, Key::Enter);
        Check(executions == 1 && !parent.IsPending(), "nested menu executes through the common action path");
        auto owned = std::make_unique<ContextMenu>();
        auto* ownedPointer = owned.get();
        ui.AddOwnedPopup(std::move(owned), source);
        auto ownerSession = ownedPointer->Open({10, 10});
        const WidgetRef removed(ownedPointer);
        ui.Root().RemoveChild(source);
        ui.Prepare(800, 600);
        Check(ownerSession.GetResult()->m_Reason == PopupCloseReason::OwnerExpired && !removed.Get(),
              "owner destruction closes and retires its context-owned popup");
        surviving = menu.Open({20, 20});
    }
    Check(surviving.GetResult()->m_Reason == PopupCloseReason::ContextClosed,
          "session outlives context and receives shutdown outcome");
}

void CallbackAndDomainLifetimes(FontFace& font)
{
    UiContext ui(font);
    auto& button = ui.Root().Add<ToolButton>();
    auto& split = ui.Root().Add<SplitToolButton>();
    std::shared_ptr<Action> command;
    {
        StateStore domain{UiDispatcher{}};
        command = std::make_shared<Action>(domain, "Document command");
        command->SetShortcut({Key::D, ShortcutModifier::Primary});
        button.SetAction(command);
        split.AddAction(command);
        ui.Prepare(800, 600);
        Check(command->Trigger(), "generic command state domain needs no window initialization");
    }
    Check(command->GetStopToken().stop_requested() && !command->Trigger(),
          "state-domain expiry prevents execution without dereferencing expired state");
    Check(!button.IsEnabled() && !split.GetPrimaryButton().IsEnabled(),
          "external domain expiry disables button and split-default presentations");
    Press(ui, Key::D, Primary());
    Check(ui.GetLastShortcutStatus() == ShortcutStatus::Unmatched, "expired command associations are skipped safely");

    auto toggle = ui.CreateAction("Delete presentation");
    toggle->SetCheckable(true);
    auto& doomed = ui.Root().Add<ToolButton>();
    doomed.SetAction(toggle);
    const WidgetRef receiver(&doomed);
    doomed.Toggled().Connect([&](bool) { ui.Root().RemoveChild(doomed); });
    Check(toggle->Trigger() && !receiver.Get(), "a checked-state observer may destroy its own presentation");

    auto self = std::make_unique<Action>(ui.States(), "Delete action");
    auto* original = self.get();
    auto expiredHandle = original->GetHandle();
    const auto pending = expiredHandle.Post([](Action&) { throw std::runtime_error("expired action executed"); });
    self->Triggered().Connect([&] { self.reset(); });
    Check(original->Trigger() && !self, "accepted action execution survives public wrapper destruction");
    ui.GetDispatcher().Drain();
    Check(!expiredHandle.IsConnected() && pending.GetStatus() == DispatchStatus::TargetExpired,
          "destroyed action handles cancel queued execution without retaining or retargeting the wrapper");

    auto& first = ui.Root().Add<ContextMenu>();
    auto& newer = ui.Root().Add<ContextMenu>();
    first.AddAction("First");
    newer.AddAction("Newer");
    auto before = first.Open({10, 10});
    before.OnFinished([&](PopupResult) { newer.Open({50, 50}); });
    auto superseded = first.Open({20, 20});
    Check(newer.IsOpen() && !first.IsOpen() && !superseded.IsPending() &&
              superseded.GetResult()->m_Reason == PopupCloseReason::Replaced,
          "completion-driven newer popup wins over an interrupted replacement operation");
    ui.CloseAllPopups();

    auto& destroyedOnFocus = ui.Root().Add<FocusPopup>();
    destroyedOnFocus.SetVisible(false);
    destroyedOnFocus.m_OnFocus = [&] { ui.Root().RemoveChild(destroyedOnFocus); };
    auto deleted = ui.ShowPopup(destroyedOnFocus, {20, 20});
    Check(!deleted.IsPending() && deleted.GetResult()->m_Reason == PopupCloseReason::OwnerExpired && !ui.GetPopup(),
          "popup destroyed by its focus callback settles before ShowPopup returns");

    auto& modal = ui.Root().Add<Widget>();
    modal.SetVisible(false);
    auto& returnTarget = modal.Add<Button>("Return target");
    auto& fallback = modal.Add<Button>("Fallback");
    auto parent = ui.ShowPopup(modal, {100, 100}, PopupOptions::Modal());
    auto nested = newer.Open({140, 140}, &modal);
    modal.RemoveChild(returnTarget);
    ui.KeyEvent(Key::Escape, true);
    Check(!nested.IsPending() && parent.IsPending() && ui.GetFocus() == &fallback,
          "expired return focus falls back inside the surviving modal scope");
    ui.CloseAllPopups();
}

void TransformedKeyboardAnchor(FontFace& font)
{
    UiContext ui(font);
    auto& source = ui.Root().Add<Button>("Transformed");
    source.SetId("anchor");
    source.SetWidth(SizeRule::Fixed(120));
    source.SetHeight(SizeRule::Fixed(40));
    std::string error;
    Check(ui.SetStyleSheet("#anchor { transform: translate(80px, 30px); }", error),
          "transformed input fixture stylesheet parses");
    ui.Prepare(600, 400);
    const auto point = Center(source);
    const auto world = ui.ToWindowPoint(source, point);
    Check(std::abs(world.m_X - point.m_X - 80) < 0.02f && std::abs(world.m_Y - point.m_Y - 30) < 0.02f,
          "forward coordinate conversion includes visual transforms");
    Check(std::abs(ui.ToLayoutPoint(source, world).m_X - point.m_X) < 0.02f,
          "forward and inverse input conversion agree");
    Point requested{-1, -1};
    source.ContextMenuRequested().Connect([&](Point value) { requested = value; });
    ui.SetFocus(&source);
    Press(ui, Key::Menu);
    Check(requested == world, "keyboard context-menu request anchors to the actual visual widget");
    auto& scene = ui.Root().Add<OpenGLWidget>();
    ui.Prepare(600, 400);
    ui.SetFocus(&scene);
    KeyboardEvent observed;
    scene.KeyInput().Connect([&](KeyboardEvent event) { observed = std::move(event); });
    ui.DispatchInput(KeyboardEvent{Key::Q, true, false, {}, Key::A, 321, 4.5});
    Check(observed.m_Key == Key::Q && observed.m_PhysicalKey == Key::A && observed.m_ScanCode == 321 &&
              observed.m_Timestamp == 4.5,
          "embedded scenes retain complete keyboard metadata");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        SharedActions(font);
        ScopedShortcuts(font);
        RichInput(font);
        FocusAndPopupStack(font);
        MenuAndLifetime(font);
        CallbackAndDomainLifetimes(font);
        TransformedKeyboardAnchor(font);
        std::cout << s_Checks << " input/action/focus/popup checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Input foundation failed after " << s_Checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
