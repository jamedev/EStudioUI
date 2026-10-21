#include <EStudioUI/ToolBar.h>
#include <EStudioUI/UiContext.h>

#include <iostream>
#include <stdexcept>

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

void Click(UiContext& ui, Widget& widget)
{
    const auto b = widget.GetBounds();
    const Point p{b.m_X + b.m_Width / 2, b.m_Y + b.m_Height / 2};
    ui.PointerDown(p);
    ui.PointerUp(p);
}

void Controls(FontFace& font)
{
    auto chooser = std::make_unique<SplitToolButton>();
    chooser->AddAction("Line");
    chooser->AddAction("Arc");
    chooser->AddAction("Spline").SetEnabled(false);
    auto* split = chooser.get();
    Check(split->GetActionCount() == 3 && split->GetDefaultActionIndex() == 0,
          "detached composition supports action creation and default selection");
    UiContext ui(font);
    auto& toolbar = ui.Root().Add<ToolBar>();
    auto& first = toolbar.AddButton("First");
    toolbar.AddChild(std::move(chooser));
    auto& last = toolbar.AddButton("Last");
    std::size_t selected = 9;
    int invoked = 0;
    split->Triggered().Connect(
        [&](std::size_t index)
        {
            selected = index;
            ++invoked;
        });
    ui.Prepare(700, 200);
    Click(ui, split->GetPrimaryButton());
    Check(selected == 0 && invoked == 1, "primary click activates current default");
    ui.SetFocus(&split->GetPrimaryButton());
    ui.KeyEvent(Key::Down, true, false, {false, false, true});
    ui.Prepare(700, 200);
    Check(split->IsMenuOpen(), "Alt+Down opens a composed tool chooser");
    ui.KeyEvent(Key::Down, true);
    ui.KeyEvent(Key::Down, true);
    ui.KeyEvent(Key::Enter, true);
    Check(!split->IsMenuOpen() && selected == 1 && invoked == 2 && split->GetPrimaryButton().GetText() == "Arc",
          "menu action updates default and activates once after dismissal");
    Click(ui, split->GetPrimaryButton());
    Check(selected == 1 && invoked == 3, "primary repeats remembered action");
    split->SetDefaultAction(2);
    split->TriggerDefault();
    Check(invoked == 3, "disabled default cannot activate");
    split->SetDefaultAction(1);
    ui.SetFocus(&first);
    ui.KeyEvent(Key::Right, true);
    Check(ui.GetFocus() == &split->GetPrimaryButton(), "toolbar arrows enter a split command");
    ui.KeyEvent(Key::Right, true);
    ui.KeyEvent(Key::Right, true);
    Check(ui.GetFocus() == &last, "toolbar arrows leave a split command");
    ui.SetFocus(&split->GetPrimaryButton());
    ui.KeyEvent(Key::Menu, true);
    Check(split->IsMenuOpen(), "context-menu key opens the chooser through normal routing");
    ui.KeyEvent(Key::Escape, true);
    split->OpenMenu();
    const WidgetRef menu(ui.GetPopup());
    split->SetEnabled(false);
    ui.Prepare(700, 200);
    Check(!ui.GetPopup() && menu.Get(), "disabling logical popup owner dismisses menu");
    split->SetEnabled(true);
    split->SetId("chooser");
    split->OpenMenu();
    std::string error;
    Check(ui.SetStyleSheet("#chooser { display: none; }", error), "owner visibility CSS compiles");
    ui.Prepare(700, 200);
    Check(!ui.GetPopup(), "CSS-hidden owner dismisses popup on the same preparation");
    Check(ui.SetStyleSheet("", error), "restore visible owner CSS");
    ui.Prepare(700, 200);
    split->OpenMenu();
    toolbar.RemoveChild(*split);
    ui.Prepare(700, 200);
    Check(!menu.Get() && !ui.GetPopup(), "deleting owner prunes owned root popup and focus");
}

void Lifetimes(FontFace& font)
{
    UiContext ui(font);
    auto& split = ui.Root().Add<SplitToolButton>();
    split.AddAction("Remove owner");
    const WidgetRef reference(&split);
    split.Triggered().Connect([&](std::size_t) { ui.Root().RemoveChild(split); });
    ui.Prepare(400, 200);
    split.OpenMenu();
    ui.KeyEvent(Key::Enter, true);
    ui.Prepare(400, 200);
    Check(!reference.Get() && ui.Root().GetChildren().empty() && !ui.GetPopup(),
          "action callback can destroy chooser and popup is cleaned safely");
    for (int index = 0; index < 20; ++index)
    {
        auto& next = ui.Root().Add<SplitToolButton>();
        next.AddAction("Transient");
        next.OpenMenu();
        ui.Root().ClearChildren();
        ui.Prepare(400, 200);
        Check(ui.Root().GetChildren().empty(), "bulk root cleanup avoids reentrant sibling destruction");
    }
}

class FocusHook : public Button
{
  public:
    //! Reports focus transitions to a deliberately destructive test callback.
    Signal<bool> m_FocusChanged;

  protected:
    //! Preserves ordinary button state before invoking the test hook.
    void OnFocus(bool focused) override
    {
        Button::OnFocus(focused);
        m_FocusChanged.Emit(focused);
    }
};

void ReentrantCommandDeletion(FontFace& font)
{
    for (bool arrow : {false, true})
    {
        UiContext ui(font);
        auto& split = ui.Root().Add<SplitToolButton>();
        split.AddAction("Command");
        auto* button = arrow ? dynamic_cast<ToolButton*>(split.GetChildren().back().get()) : &split.GetPrimaryButton();
        Check(button != nullptr, "split command exposes ordinary composed tool buttons");
        button->SetCheckable(true);
        const WidgetRef owner(&split);
        const WidgetRef sender(button);
        button->Toggled().Connect([&](bool) { ui.Root().RemoveChild(split); });
        ui.Prepare(400, 200);
        ui.SetFocus(button);
        ui.KeyEvent(Key::Enter, true);
        ui.Prepare(400, 200);
        Check(!owner.Get() && !sender.Get() && !ui.GetFocus() && !ui.GetPopup(),
              "earlier toggle callback can delete split before internal command/open slot runs");
    }

    UiContext ui(font);
    auto& split = ui.Root().Add<SplitToolButton>();
    auto& action = split.AddAction("Destroy tree");
    action.SetCheckable(true);
    const WidgetRef owner(&split);
    const WidgetRef sender(&action);
    action.Toggled().Connect([&](bool) { ui.Root().ClearChildren(); });
    ui.Prepare(400, 200);
    split.OpenMenu();
    const WidgetRef popup(ui.GetPopup());
    ui.KeyEvent(Key::Enter, true);
    ui.Prepare(400, 200);
    Check(!owner.Get() && !sender.Get() && !popup.Get() && !ui.GetPopup() && !ui.GetFocus(),
          "earlier action toggle may destroy menu before its close/default slots run");
}

void ReentrantPopupReplacement(FontFace& font)
{
    UiContext ui(font);
    auto& focus = ui.Root().Add<FocusHook>();
    auto& oldMenu = ui.Root().Add<ContextMenu>();
    oldMenu.AddAction("Previous menu");
    auto& split = ui.Root().Add<SplitToolButton>();
    split.AddAction("Replacement menu");
    const WidgetRef owner(&split);
    bool armed = false;
    focus.m_FocusChanged.Connect(
        [&](bool focused)
        {
            if (focused && armed)
            {
                armed = false;
                ui.Root().RemoveChild(split);
            }
        });
    ui.Prepare(400, 200);
    ui.SetFocus(&focus);
    oldMenu.Open({0, 50});
    armed = true;
    split.OpenMenu();
    ui.Prepare(400, 200);
    Check(!owner.Get() && !ui.GetPopup() && ui.GetFocus() == &focus && ui.Root().GetChildren().size() == 2,
          "restoring old-menu focus may delete replacement owner before ShowPopup preparation completes");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Controls(font);
        Lifetimes(font);
        ReentrantCommandDeletion(font);
        ReentrantPopupReplacement(font);
        std::cout << "Split tool button PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Split tool button FAIL / " << error.what() << '\n';
        return 1;
    }
}
