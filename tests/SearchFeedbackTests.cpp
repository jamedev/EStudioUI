#include <EStudioUI/SearchBox.h>
#include <EStudioUI/CommandPalette.h>
#include <EStudioUI/NotificationCenter.h>
#include <EStudioUI/LogView.h>
#include <EStudioUI/Bindings.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/ThemeController.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void Throws(F action, const char* message)
{
    bool threw = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    Check(threw, message);
}

void Tick(UiContext& ui, double time)
{
    ui.SetTime(time);
    ui.PumpPlatformServices();
    ui.GetDispatcher().Drain();
    ui.Prepare(800, 600);
}

void Timers(FontFace& font)
{
    UiContext ui(font);
    std::vector<int> order;
    UiTimer nested;
    auto a = ui.After(1,
                      [&]
                      {
                          order.push_back(1);
                          nested = ui.After(0, [&] { order.push_back(3); });
                      });
    auto b = ui.After(1, [&] { order.push_back(2); });
    auto cancelled = ui.After(0, [&] { order.push_back(99); });
    cancelled.Cancel();
    Tick(ui, .9);
    Check(order.empty() && a.IsPending(), "Timers stay dormant before the GUI-clock deadline");
    Tick(ui, 1);
    Check(order == std::vector<int>{1, 2} && !a.IsPending() && nested.IsPending(),
          "Equal deadlines preserve order and nested zero timers await another idle turn");
    Tick(ui, 1);
    Check(order == std::vector<int>{1, 2, 3}, "Deferred deadline executes exactly once");
    auto error = ui.After(0, [] { throw std::runtime_error("contained"); });
    Tick(ui, 1);
    Check(error.GetError() && !error.IsPending(), "Timer errors remain inspectable after terminal removal");
    auto& target = ui.Root().Add<Widget>();
    bool fired = false;
    auto bound = ui.After(target, 0, [&](Widget&) { fired = true; });
    ui.Root().RemoveChild(target);
    Tick(ui, 1);
    Check(!fired && !bound.IsPending(), "Receiver-bound deadline expires without a raw widget capture");
    Throws([&] { ui.After(-1, [] {}); }, "Negative delays are rejected");
    std::vector<UiTimer> capacity;
    for (int i = 0; i < 1024; ++i)
    {
        capacity.push_back(ui.After(5, [] {}));
    }
    Throws([&] { ui.After(5, [] {}); }, "Timer queue is bounded");
    capacity.clear();
    auto closing = std::make_unique<UiContext>(font);
    UiTimer queued;
    auto destroy = closing->After(0, [&] { closing.reset(); });
    queued = closing->After(0, [] { throw std::runtime_error("Must not run after context destruction"); });
    closing->PumpPlatformServices();
    Check(!queued.IsPending() && !queued.GetError(), "Destruction in a deadline retires the rest of its ready batch");
    Check(!ui.HasAnimations(), "Pending static deadlines do not request continuous GPU rendering");
}

void Search(FontFace& font)
{
    UiContext ui(font);
    ThemeController theme(ui);
    auto& box = ui.Root().Add<SearchBox>();
    box.SetWidth(SizeRule::Fixed(320));
    box.SetSuggestions({{"a", "Épure", "Accent"}, {"b", "Alpha", "First"}});
    ui.Prepare(800, 600);
    ui.SetFocus(&box.GetEditor());
    std::uint64_t delivered = 0;
    int requests = 0;
    box.QueryRequested().Connect(
        [&](std::string, std::uint64_t id)
        {
            ++requests;
            delivered = id;
        });
    box.SetText("a");
    box.SetText("é");
    const auto current = box.GetRequestId();
    Tick(ui, .1);
    Check(!requests, "Search coalesces rapid canonical edits before its deadline");
    Tick(ui, .2);
    Check(requests == 1 && delivered == current, "Only latest committed query is delivered");
    auto* popup = dynamic_cast<CompletionPopup*>(ui.GetPopup());
    Check(popup && popup->GetItems().size() == 1 && popup->GetItems()[0].m_Id == "a",
          "Completion uses Unicode case folding and stable application IDs");
    Check(ui.GetFocus() == &box.GetEditor(), "Completion preserves native text input focus");
    Check(!box.SetCompletions(current - 1, {{"late", "obsolete", ""}}),
          "Stale asynchronous results cannot replace newer suggestions");
    ui.KeyEvent(Key::Enter, true);
    Check(box.GetText() == "Épure" && !ui.GetPopup(), "Enter accepts completion before normal submission");
    box.SetText("alpha");
    Tick(ui, .4);
    popup = dynamic_cast<CompletionPopup*>(ui.GetPopup());
    Check(popup != nullptr, "A new committed query opens a fresh completion session");
    const auto row = popup->GetList().GetViewport();
    const Point point{row.m_X + 20, row.m_Y + 12};
    ui.PointerDown(point);
    ui.PointerUp(point);
    Check(box.GetText() == "Alpha" && !ui.GetPopup(),
          "Pointer focus transfer accepts a completion instead of dismissing it first");
    State<std::string> text(ui.States(), "initial");
    auto binding = BindText(box, text);
    text.Set("new");
    Tick(ui, .6);
    Check(box.GetText() == "new" && !binding.GetError(),
          "State notification can schedule debounced search without nested traversal");
    const auto request = box.GetRequestId();
    ui.SetFocus(nullptr);
    Check(!box.SetCompletions(request, {{"x", "stale focus", ""}}),
          "Results arriving after focus loss cannot reopen completion");
    ui.SetFocus(&box.GetEditor());
    box.SetText("alpha");
    Tick(ui, .9);
    const auto dismissed = box.GetRequestId();
    ui.PointerDown({750, 550});
    ui.PointerUp({750, 550});
    Check(!box.SetCompletions(dismissed, {{"late", "Alpha", ""}}),
          "Outside dismissal expires pending results even when the editor regains focus");
    auto& completion = CompletionPopup::Create(box);
    Throws([&] { completion.SetItems({{"same", "one", ""}, {"same", "two", ""}}); },
           "Duplicate completion IDs are rejected before publication");
    completion.SetItems({{"ok", "One", ""}});
    Throws([&] { completion.SetItems(std::vector<CompletionItem>(257)); }, "Suggestion storage is bounded");
    Check(completion.GetItems().size() == 1, "Failed completion replacement preserves prior result data");
    auto& owner = ui.Root().Add<Widget>();
    auto& disposable = CompletionPopup::Create(owner);
    disposable.SetItems({{"a", "A", ""}});
    const WidgetRef popupLife(&disposable), ownerLife(&owner);
    disposable.GetList().CurrentChanged().Connect(
        [&ui, ownerLife](int)
        {
            if (auto* live = ownerLife.Get())
            {
                ui.Root().RemoveChild(*live);
            }
        });
    disposable.SetItems({{"b", "B", ""}});
    ui.Prepare(800, 600);
    Check(!ownerLife.Get() && !popupLife.Get(),
          "Completion publication remains safe when selection observers delete its owner");
    auto& transient = ui.Root().Add<SearchBox>();
    transient.SetText("pending");
    ui.Root().RemoveChild(transient);
    Tick(ui, 1);
}

void Feedback(FontFace& font)
{
    UiContext ui(font);
    ThemeController theme(ui);
    auto& text = ui.Root().Add<TextBox>();
    text.SetValidator(std::make_shared<IntegerValidator>(1, 9));
    auto& message = ui.Root().Add<ValidationMessage>();
    message.Bind(text, "Invalid", "Choose 1–9");
    text.SetText("0");
    Check(message.IsVisible() && message.GetSeverity() == FeedbackSeverity::Warning,
          "Inline feedback follows editable validator prefixes");
    text.SetText("5");
    Check(!message.IsVisible(), "Acceptable input clears inline feedback");
    auto& notifications = NotificationCenter::Create(ui);
    const auto first = notifications.Show("Saved", FeedbackSeverity::Success, 1);
    auto& palette = CommandPalette::Create(text);
    auto action = ui.CreateAction("Export drawing");
    int executions = 0;
    action->Triggered().Connect([&] { ++executions; });
    palette.SetActions({action});
    auto session = palette.Show();
    notifications.Show("Background task finished", FeedbackSeverity::Information, 0);
    Check(session.IsPending() && ui.GetPopup() == &palette,
          "Nonmodal feedback cannot replace an existing modal session");
    ui.Prepare(800, 600);
    auto node = ui.Semantics().GetNode(ui.Semantics().GetId(notifications));
    Check(node && node->m_Visible && node->m_Info.m_Live == SemanticLiveSetting::Polite,
          "Viewport overlays participate in logical accessibility");
    Tick(ui, 1);
    Check(notifications.GetCount() == 1 && !notifications.Dismiss(first),
          "Expiry removes only the matching notification");
    Check(palette.ExecuteCurrent() && executions == 1 && !session.IsPending(),
          "Palette closes before executing the shared canonical Action");
    action->SetEnabled(false);
    palette.Show();
    Check(!palette.ExecuteCurrent() && executions == 1, "Disabled commands cannot execute through the palette");
    palette.Close();
    for (int i = 0; i < 10; ++i)
    {
        notifications.Show(std::to_string(i), FeedbackSeverity::Information, 0);
    }
    Check(notifications.GetCount() == 4, "Notifications evict the oldest visible message at their bound");
    notifications.Clear();
    auto& overlay = ui.AddOverlay(std::make_unique<ToolButton>("Detached overlay"));
    overlay.SetWidth(SizeRule::Fixed(120));
    overlay.SetHeight(SizeRule::Fixed(32));
    ui.Prepare(800, 600);
    const WidgetRef overlayLife(&overlay);
    ui.Root().RemoveChild(overlay);
    ui.Prepare(800, 600);
    Check(!overlayLife.Get(), "Removed overlays expire without leaving an input or semantic target");
    Check(!notifications.IsVisible() && !ui.HasAnimations(),
          "Cleared feedback leaves no continuously rendered surface");
}

void Logging(FontFace& font)
{
    UiContext ui(font);
    auto& log = ui.Root().Add<LogView>();
    log.SetCapacity(3);
    auto sink = log.GetSink();
    std::thread worker(
        [sink]
        {
            for (int i = 0; i < 600; ++i)
            {
                sink.Append({std::to_string(i), "worker", FeedbackSeverity::Information});
            }
        });
    worker.join();
    Check(sink.GetDroppedCount() == 88, "Bounded worker inbox reports overload without an unbounded UI queue");
    Tick(ui, 0);
    Check(log.GetCount() == 3, "One idle batch evicts old retained log records");
    const auto snapshot = log.GetTable().GetModel()->GetSnapshot();
    const auto last = snapshot.GetItem(2);
    log.Append({{"next", "owner", FeedbackSeverity::Warning}});
    Check(log.GetTable().GetModel()->GetSnapshot().Contains(last) && snapshot.GetText(last, 2) == "511",
          "Surviving log records keep identity and old snapshots own their bytes");
    Throws([&] { log.Append({{std::string(17000, 'x'), "", FeedbackSeverity::Error}}); },
           "Oversized log entries reject atomically");
    Check(log.GetCount() == 3, "Rejected log batch leaves retained rows intact");
    log.Clear();
    Check(log.GetCount() == 0, "Clear removes retained messages");
    ui.Root().RemoveChild(log);
    Check(!sink.Append({"after shutdown", "", FeedbackSeverity::Information}),
          "Worker sink remains safe and closed after view destruction");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Timers(font);
        Search(font);
        Feedback(font);
        Logging(font);
        std::cout << "Search/feedback: " << s_Checks << " checks passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
