#include <EStudioUI/Bindings.h>
#include <EStudioUI/BusyIndicator.h>
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/Frame.h>
#include <EStudioUI/GroupBox.h>
#include <EStudioUI/Models.h>
#include <EStudioUI/RadioButton.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/Separator.h>
#include <EStudioUI/Splitter.h>
#include <EStudioUI/ToolTip.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
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

void Click(UiContext& ui, const Widget& widget)
{
    ui.PointerDown(Center(widget));
    ui.PointerUp(Center(widget));
}

void Choices()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& check = ui.Root().Add<CheckBox>("Enable snapping");
    auto group = std::make_shared<RadioGroup>();
    auto& a = ui.Root().Add<RadioButton>("Length", group);
    auto& b = ui.Root().Add<RadioButton>("Angle", group);
    ui.Prepare(500, 400);
    Click(ui, check);
    Check(check.IsChecked(), "Pointer release checks a checkbox");
    check.SetTristate(true);
    check.Trigger();
    Check(check.GetCheckState() == CheckState::Indeterminate, "Three-state activation includes indeterminate");
    auto& tree = ui.Semantics();
    auto node = tree.GetNode(tree.GetId(check));
    Check(node && node->m_Info.m_Indeterminate && !node->m_Info.m_Checked, "Semantic toggle preserves indeterminate");
    Check(tree.Perform(tree.GetId(check), {SemanticAction::Toggle}) == SemanticStatus::Succeeded &&
              check.GetCheckState() == CheckState::Unchecked,
          "Accessibility shares the checkbox activation path");
    ui.SetFocus(&check);
    ui.KeyEvent(Key::Space, true);
    Check(!check.IsChecked(), "Space waits for matching release");
    ui.KeyEvent(Key::Space, false);
    Check(check.IsChecked(), "Space release checks the focused control");
    check.SetTristate(false);
    State<bool> state(ui.States(), false);
    auto binding = BindChecked(check, state);
    state.Set(true);
    Check(check.IsChecked(), "Boolean state drives the checkbox");
    check.SetChecked(false);
    Check(!state.Get(), "Checkbox writes through the shared binding");
    a.SetChecked(true);
    bool coherent = false;
    const ScopedConnection observed(a.StateChanged().Connect(
        [&](CheckState value)
        {
            if (value == CheckState::Unchecked)
                coherent = !a.IsChecked() && b.IsChecked() && group->GetCheckedButton() == &b;
        }));
    b.SetChecked(true);
    Check(coherent && !a.IsChecked() && b.IsChecked(), "All radio states commit before any member observer");
    b.SetChecked(false);
    Check(b.IsChecked(), "Required radio selection cannot be cleared");
    group->SetSelectionRequired(false);
    b.SetChecked(false);
    Check(!group->GetCheckedButton(), "Optional radio groups support an empty selection");
    a.SetChecked(true);
    ui.SetFocus(&a);
    ui.KeyEvent(Key::Right, true);
    Check(b.IsChecked() && ui.GetFocus() == &b, "Arrow navigation selects and focuses another group member");
    Throws([&] { b.SetCheckState(CheckState::Indeterminate); }, "Radios reject a third state");
    auto action = ui.CreateAction("Shared choice");
    action->SetCheckable(true);
    check.SetAction(action);
    check.Trigger();
    Check(action->GetSnapshot()->m_Checked && check.IsChecked(), "Checkable actions share canonical checkbox state");
    Throws([&] { a.SetAction(action); }, "Independent toggle actions cannot bypass radio exclusivity");
    action->SetCheckable(false);
    Check(!check.IsEnabled(), "A checkbox presentation becomes unavailable when its action stops being checkable");
    action->SetCheckable(true);
    Check(check.IsEnabled(), "Restoring checkability restores action-driven checkbox availability");
    check.SetChecked(true);
    bool wrongThread = false;
    std::jthread worker(
        [&]
        {
            try
            {
                check.SetChecked(false);
            }
            catch (const std::logic_error&)
            {
                wrongThread = true;
            }
        });
    worker.join();
    Check(wrongThread && check.IsChecked(), "Wrong-thread mutation is rejected before state changes");
    auto& transient = ui.Root().Add<CheckBox>("Disposable");
    const WidgetRef expired(&transient);
    transient.StateChanged().Connect([&](CheckState) { ui.Root().RemoveChild(transient); });
    transient.SetChecked(true);
    Check(!expired.Get(), "A state observer can delete its checkbox");
    auto& reentrant = ui.Root().Add<CheckBox>();
    const ScopedConnection loop(
        reentrant.StateChanged().Connect([&](CheckState) { reentrant.SetChecked(!reentrant.IsChecked()); }));
    Throws([&] { reentrant.SetChecked(true); }, "Reentrant checkbox observers are bounded");
}

void Combos()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& combo = ui.Root().Add<ComboBox>();
    combo.SetHeight(SizeRule::Fixed(42));
    std::vector<std::string> values;
    values.reserve(100000);
    for (int i = 0; i < 100000; ++i)
        values.push_back("Item " + std::to_string(i));
    values[17] = "Straße";
    combo.SetItems(std::move(values));
    ui.Prepare(640, 440);
    Check(combo.GetItemCount() == 100000 && combo.GetCurrentIndex() == 0,
          "Convenience items create a canonical 100,000-record choice model");
    const auto match = combo.FindCompletion("STRASSE");
    Check(match && combo.GetModel()->GetSnapshot().GetPosition(*match) == 17,
          "Prefix search uses Unicode case folding");
    combo.SetCurrentIndex(17);
    combo.ShowPopup();
    auto* popup = dynamic_cast<ListWidget*>(ui.GetPopup());
    Check(popup && popup->GetItemCount() == 100000 && popup->GetChildren().empty(),
          "Large combo popup uses virtual rows without per-item widgets");
    ui.KeyEvent(Key::Down, true);
    Check(combo.GetCurrentIndex() == 17, "Popup navigation previews without committing selection");
    ui.KeyEvent(Key::Escape, true);
    Check(!combo.IsPopupOpen() && combo.GetCurrentIndex() == 17, "Escape dismisses preview without changing selection");
    combo.ShowPopup();
    ui.KeyEvent(Key::Down, true);
    ui.KeyEvent(Key::Enter, true);
    Check(combo.GetCurrentIndex() == 18 && !combo.IsPopupOpen(), "Enter accepts a highlighted popup item");
    auto model = combo.GetModel();
    const auto current = model->GetSnapshot().GetItem(18);
    model->SetValue(current, 0, std::string("Renamed"));
    Check(combo.GetCurrentText() == "Renamed", "Model edits refresh the canonical selected caption");
    Throws([&] { model->SetValue(current, 0, std::string("\xff")); },
           "Invalid indexed text is rejected before model publication");
    Check(combo.GetCurrentText() == "Renamed", "Rejected model candidates preserve the field and index");
    combo.SetEditable(true);
    combo.GetEditor().SetText("My custom value");
    Check(combo.GetCurrentIndex() == -1 && combo.GetCurrentText() == "My custom value",
          "Editable combo retains user text when clearing canonical selection");
    int completions = 0;
    combo.SetCompletionProvider(
        [&](std::string_view)
        {
            ++completions;
            return std::optional(current);
        });
    Check(combo.FindCompletion("anything") == current && completions == 1,
          "Applications can replace completion matching without a custom editor");
    combo.SetCompletionProvider({});
    combo.SetCurrentIndex(3);
    auto selection = combo.GetSelectionModel();
    selection->Select(model->GetSnapshot().GetItem(9));
    Check(combo.GetCurrentIndex() == 9, "Shared selection drives combo presentation");
    ui.Prepare(640, 440);
    const auto before = ui.GetStats().m_Measurements;
    ui.Prepare(640, 440);
    Check(ui.GetStats().m_Measurements == before, "An unchanged large combo retains intrinsic measurements");
    combo.ShowPopup();
    popup = dynamic_cast<ListWidget*>(ui.GetPopup());
    ui.Prepare(640, 440);
    const auto viewport = popup->GetViewport();
    const Point first{viewport.m_X + 12, viewport.m_Y + 12};
    ui.PointerDown(first);
    ui.PointerUp(first);
    Check(!combo.IsPopupOpen(), "One pointer click accepts a popup item");
    const auto id = ui.Semantics().GetId(combo);
    const auto node = ui.Semantics().GetNode(id);
    Check(node && node->m_Info.m_Role == SemanticRole::ComboBox && !node->m_Info.m_ReadOnly,
          "Combo exposes shared value and expansion semantics");
    auto detached = std::make_unique<ComboBox>();
    detached->SetItems({"One", "Two"});
    detached->SetCurrentIndex(1);
    auto& attached = static_cast<ComboBox&>(ui.Root().AddChild(std::move(detached)));
    attached.ShowPopup();
    Check(attached.GetCurrentIndex() == 1 && attached.GetCurrentText() == "Two",
          "Detached convenience state survives first model attachment");
    attached.HidePopup();
    auto& disposable = ui.Root().Add<ComboBox>();
    const WidgetRef disposed(&disposable);
    disposable.CurrentChanged().Connect([&](int) { ui.Root().RemoveChild(disposable); });
    disposable.SetItems({"Close me"});
    Check(!disposed.Get(), "A selection observer can delete a combo during item setup");
}

void DisabledChoices()
{
    struct Record
    {
        std::string m_Text;
        bool m_Enabled;
    };

    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto column = Column("Text", &Record::m_Text);
    column.m_Role = [](const Record& record, ModelRole role) -> ModelValue
    { return role == ModelRole::Enabled ? ModelValue(record.m_Enabled) : ModelValue{}; };
    auto model = MakeTableModel(ui.States(),
                                std::vector<Record>{{"First", true}, {"Forbidden", false}, {"Third", true}}, column);
    auto& combo = ui.Root().Add<ComboBox>();
    combo.SetModel(model);
    combo.SetCurrentIndex(0);
    ui.Prepare(500, 300);
    Check(combo.GetEditor().GetContentBounds().m_Height >= 22,
          "Default combo sizing leaves room for its actual text metrics");
    ui.SetFocus(&combo.GetEditor());
    ui.KeyEvent(Key::Down, true);
    Check(combo.GetCurrentIndex() == 2, "Closed combo navigation skips disabled model choices");
    Check(!combo.FindCompletion("For"), "Disabled choices are excluded from indexed completion");
    combo.SetModel(model);
    Check(combo.GetCurrentIndex() == 2, "Reapplying the same model is a no-op");
    ui.PointerDown(Center(combo.GetEditor()));
    ui.PointerUp(Center(combo.GetEditor()));
    Check(combo.IsPopupOpen(), "Read-only combo body opens the virtual popup");
    combo.HidePopup();
    int focusable = 0;
    for (const auto& child : combo.GetChildren())
        if (child->IsFocusable())
            ++focusable;
    Check(focusable == 1, "A composed combo contributes one keyboard Tab stop");
}

void Containers()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& area = ui.Root().Add<ScrollArea>();
    area.SetHeight(SizeRule::Fixed(150));
    area.Content().SetGap(6);
    TextBox* last = nullptr;
    for (int i = 0; i < 20; ++i)
    {
        last = &area.Content().Add<TextBox>("Field " + std::to_string(i));
        last->SetHeight(SizeRule::Fixed(38));
    }
    auto& splitter = ui.Root().Add<Splitter>();
    splitter.SetHeight(SizeRule::Fixed(140));
    auto& left = splitter.Add<Frame>();
    auto& right = splitter.Add<Frame>();
    left.SetStyleProperty(StyleProperty::MinWidth, 80.0f);
    right.SetStyleProperty(StyleProperty::MinWidth, 60.0f);
    splitter.SetSizes({1, 2});
    auto& group = ui.Root().Add<GroupBox>("Options");
    group.SetHeight(SizeRule::Fixed(100));
    auto& child = group.Content().Add<Button>("Run");
    group.SetCheckable(true);
    ui.Prepare(600, 600);
    ui.SetFocus(last);
    ui.Prepare(600, 600);
    Check(area.GetScrollOffset().m_Y > 600, "Keyboard focus reveals controls deep in an arbitrary scrolling form");
    const auto viewport = area.GetViewport();
    Check(last->GetBounds().m_Y + last->GetBounds().m_Height <= viewport.m_Y + viewport.m_Height + 0.02f,
          "Revealed field fits the actual clipped viewport");
    const auto measured = ui.GetStats().m_Measurements;
    area.SetScrollOffset({0, 0});
    ui.Prepare(600, 600);
    Check(ui.GetStats().m_Measurements == measured, "Scrolling retains unrelated intrinsic measurements");
    const auto thumb = area.GetScrollThumbBounds(false);
    const Point thumbStart{thumb.m_X + thumb.m_Width * 0.5f, thumb.m_Y + thumb.m_Height * 0.5f};
    ui.PointerDown(thumbStart);
    ui.PointerMove({thumbStart.m_X, thumbStart.m_Y + 50});
    ui.Prepare(600, 600);
    Check(area.GetScrollOffset().m_Y > 100 && area.Content().GetBounds().m_Y < viewport.m_Y - 100,
          "Production ScrollArea thumb dragging moves both the scrollbar and the clipped form");
    const auto duringDrag = area.GetScrollOffset();
    ui.PointerUp({thumbStart.m_X + 80, thumbStart.m_Y + 50});
    ui.PointerMove(thumbStart);
    Check(area.GetScrollOffset() == duringDrag, "Releasing outside a ScrollArea thumb ends its capture");
    ui.PointerDown(thumbStart);
    ui.PointerMove({thumbStart.m_X, thumbStart.m_Y + 200});
    ui.Deactivate();
    const auto cancelled = area.GetScrollOffset();
    ui.PointerMove(thumbStart);
    Check(area.GetScrollOffset() == cancelled, "Window deactivation cancels the production scrollbar drag");
    area.SetScrollOffset({0, 0});
    ui.Prepare(600, 600);
    auto sizes = splitter.GetSizes();
    Check(sizes.size() == 2 && sizes[0] >= 80 && sizes[1] > sizes[0],
          "Splitter weights share remaining space above hard minima");
    const auto handle = splitter.GetHandleBounds(0);
    const Point start{handle.m_X + handle.m_Width * 0.5f, handle.m_Y + 20};
    ui.PointerDown(start);
    ui.PointerMove({start.m_X + 90, start.m_Y});
    ui.PointerUp({start.m_X + 90, start.m_Y});
    ui.Prepare(600, 600);
    Check(splitter.GetSizes()[0] > sizes[0] + 80, "Captured splitter drag changes adjacent pane sizes");
    ui.SetFocus(&splitter);
    sizes = splitter.GetSizes();
    ui.KeyEvent(Key::Right, true);
    ui.Prepare(600, 600);
    Check(splitter.GetSizes()[0] > sizes[0], "Splitter handles support keyboard movement");
    splitter.SetSizes({1, 10000});
    ui.Prepare(600, 600);
    Check(splitter.GetSizes()[0] >= 79.99f, "Extreme saved ratios preserve hard minima");
    Throws([&] { splitter.SetSizes({1}); }, "Splitter restore validates pane count");
    Throws([&] { splitter.SetSizes({0, 1}); }, "Splitter restore rejects invalid ratios");
    splitter.SetVisible(false);
    splitter.SetSizes({2, 1});
    splitter.SetVisible(true);
    ui.Prepare(600, 600);
    Check(splitter.GetSizes()[0] > splitter.GetSizes()[1], "Pane ratios can be configured under a hidden ancestor");
    left.SetVisible(false);
    ui.Prepare(600, 600);
    splitter.SetSizes({3, 1});
    left.SetVisible(true);
    ui.Prepare(600, 600);
    Check(splitter.GetSizes()[0] > splitter.GetSizes()[1], "Hidden panes retain identity-based stretch weights");
    group.SetChecked(false);
    Check(!child.IsEnabled() && group.IsEnabled(), "Unchecking a group disables only its content");
    group.SetChecked(true);
    Check(child.IsEnabled(), "Group header restores content interaction");
    auto& line = ui.Root().Add<Separator>(Axis::Vertical);
    Check(line.GetOrientation() == Axis::Vertical, "Separators expose orientation without fixed code sizing");
}

void ProgressAndHelp()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& progress = ui.Root().Add<ProgressBar>();
    auto& busy = ui.Root().Add<BusyIndicator>();
    auto& source = ui.Root().Add<Button>("Help target");
    source.SetToolTip("Use a short, focused explanation.", 0.5);
    progress.SetRange(-10, 10);
    progress.SetValue(99);
    Check(progress.GetValue() == 10, "Progress values clamp to a finite range");
    Throws([&] { progress.SetRange(4, 4); }, "Progress rejects a zero span");
    ui.Prepare(500, 300);
    Check(ui.HasAnimations(), "Visible busy indicator requests frames");
    busy.SetVisible(false);
    ui.Prepare(500, 300);
    Check(!ui.HasAnimations(), "Hidden busy indicators stop requesting frames");
    busy.SetVisible(true);
    busy.SetIndeterminate(false);
    ui.Prepare(500, 300);
    Check(!ui.HasAnimations(), "Stopped busy indicators remain idle");
    {
        const ScopedConnection loop(
            progress.ValueChanged().Connect([&](double value) { progress.SetValue(value > 0 ? -1 : 1); }));
        Throws([&] { progress.SetValue(1); }, "Progress observers cannot create an unbounded recursive stack");
    }
    progress.SetValue(10);
    const auto node = ui.Semantics().GetNode(ui.Semantics().GetId(progress));
    Check(node && node->m_Info.m_Role == SemanticRole::ProgressBar && node->m_Info.m_ReadOnly &&
              node->m_Info.m_Number == 10,
          "Progress exposes read-only range metadata");
    ui.SetTime(0);
    ui.SetFocus(&source);
    ui.PointerMove(Center(source));
    ui.SetTime(0.2);
    Check(!ui.GetPopup(), "Tooltip delay does not open early");
    ui.SetTime(0.6);
    Check(dynamic_cast<ToolTip*>(ui.GetPopup()) && ui.GetFocus() == &source,
          "Pointer/focus help opens without stealing keyboard focus");
    Check(!ui.HasAnimations(), "A visible tooltip does not request continuous frames");
    PopupOptions helpOptions;
    helpOptions.m_TakeFocus = false;
    helpOptions.m_RestoreFocus = false;
    helpOptions.m_InputTransparent = true;
    auto helpSession = ui.ShowPopup(*ui.GetPopup(), {20, 100}, helpOptions);
    bool nestedRejected = false;
    helpSession.OnFinished(
        [&](PopupResult)
        {
            try
            {
                ui.TextInput("nested");
            }
            catch (const std::logic_error&)
            {
                nestedRejected = true;
            }
        });
    ui.KeyEvent(Key::Escape, true);
    Check(!ui.GetPopup() && ui.GetFocus() == &source, "Any key dismisses help without moving focus");
    Check(nestedRejected, "Tooltip dismissal callbacks remain inside the shared input reentrancy guard");
    ui.PointerMove({499, 299});
    ui.PointerMove(Center(source));
    ui.SetTime(1.2);
    Check(ui.GetPopup() != nullptr, "Help can rearm after leaving and returning");
    ui.Root().RemoveChild(source);
    ui.Prepare(500, 300);
    ui.SetTime(2);
    Check(!ui.GetPopup(), "Deleting a tooltip owner retires the presentation and deadline");
    auto& dialogOwner = ui.Root().Add<Button>("Dialog owner");
    auto owned = std::make_unique<Frame>();
    auto& help = owned->Add<Button>("Help inside modal");
    help.SetToolTip("A nonactivating child overlay", 0.2);
    auto& dialog = ui.AddOwnedPopup(std::move(owned), dialogOwner);
    dialog.SetWidth(SizeRule::Fixed(260));
    dialog.SetHeight(SizeRule::Fixed(100));
    ui.ShowPopup(dialog, {30, 30}, PopupOptions::Modal());
    ui.SetFocus(&help);
    ui.SetTime(2.3);
    Check(ui.GetPopupCount() == 2 && dynamic_cast<ToolTip*>(ui.GetPopup()) && ui.GetFocus() == &help,
          "Focus help can be a nonactivating child of a modal popup");
    ui.KeyEvent(Key::Escape, true);
    Check(ui.GetPopupCount() == 1 && ui.GetPopup() == &dialog, "Dismissing modal help preserves its parent session");
    ui.ClosePopup();
    auto& editor = ui.Root().Add<TextBox>("Committed");
    editor.SetToolTip("Editor help", 0.1);
    ui.Prepare(500, 500);
    ui.SetFocus(&editor);
    TextCompositionEvent composing;
    composing.m_Kind = TextCompositionKind::Preedit;
    composing.m_Text = "draft";
    composing.m_Caret = 5;
    Check(ui.DispatchComposition(composing), "A composed control accepts the shared preedit protocol");
    ui.SetTime(3);
    Check(!ui.GetPopup(), "Native composition cancels delayed help instead of opening an overlay while typing");
    ui.CancelTextComposition();
}

void ToolTipTargetIsolation()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& source = ui.Root().Add<Button>("Focused source");
    source.SetFloatingBounds({20, 120, 160, 40});
    source.SetToolTip("Help belongs to the focused source.", 0.1);
    auto& unrelated = ui.Root().Add<Button>("Unrelated tab");
    unrelated.SetFloatingBounds({300, 30, 160, 40});
    ui.Prepare(500, 300);
    ui.SetTime(0);
    Click(ui, source);
    ui.PointerMove(Center(unrelated));
    ui.SetTime(0.2);
    Check(!ui.GetPopup() && ui.GetFocus() == &source,
          "Hovering an unrelated tab never borrows help from the previously activated control");

    ui.PointerMove(Center(source));
    ui.SetTime(0.4);
    const auto* hoverHelp = dynamic_cast<ToolTip*>(ui.GetPopup());
    Check(hoverHelp && hoverHelp->GetText() == source.GetToolTip() && ui.GetFocus() == &source,
          "Returning to the actual help source opens its nonactivating pointer tooltip");
    ui.PointerMove(Center(unrelated));
    ui.SetTime(0.6);
    Check(!ui.GetPopup(), "Leaving a focused source dismisses its help even while it retains keyboard focus");

    ui.SetFocus(nullptr);
    ui.SetFocus(&source);
    ui.SetTime(0.8);
    const auto* focusHelp = dynamic_cast<ToolTip*>(ui.GetPopup());
    Check(focusHelp && focusHelp->GetBounds().m_Y >= source.GetBounds().m_Y + source.GetBounds().m_Height &&
              ui.GetFocus() == &source,
          "Explicit keyboard focus still shows help beside its source rather than at the unrelated pointer");
    auto sameTab = Center(unrelated);
    sameTab.m_X += 1;
    ui.PointerMove(sameTab);
    ui.SetTime(1);
    Check(!ui.GetPopup(), "A mouse move cancels unrelated focus help even when its hover path has not changed");

    unrelated.SetToolTip("Help belongs to the other tab.", 0.1);
    ui.PointerMove(sameTab);
    ui.SetTime(1.2);
    const auto* otherHelp = dynamic_cast<ToolTip*>(ui.GetPopup());
    Check(otherHelp && otherHelp->GetText() == unrelated.GetToolTip() && ui.GetFocus() == &source,
          "A hovered tab selects its own tooltip independently of the keyboard focus target");

    const WidgetRef removed(&source);
    auto session = ui.GetPopupSession(*ui.GetPopup());
    session.OnFinished([&](PopupResult) { ui.Root().RemoveChild(source); });
    ui.PointerMove(Center(source));
    ui.SetTime(1.4);
    Check(!removed.Get() && !ui.GetPopup(),
          "Tooltip dismissal callbacks may delete the next hover target without leaving a stale help deadline");
}
} // namespace

int main()
{
    try
    {
        Choices();
        Combos();
        DisabledChoices();
        Containers();
        ProgressAndHelp();
        ToolTipTargetIsolation();
        std::cout << s_Checks << " essential widget checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Essential widgets: " << error.what() << '\n';
        return 1;
    }
}
