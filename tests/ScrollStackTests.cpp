#include <EStudioUI/Bindings.h>
#include <EStudioUI/BusyIndicator.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/ScrollBar.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/StackedWidget.h>
#include <EStudioUI/TextBox.h>
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

bool Near(double a, double b)
{
    return std::abs(a - b) < .02;
}

Point Center(Rect bounds)
{
    return {bounds.m_X + bounds.m_Width * .5f, bounds.m_Y + bounds.m_Height * .5f};
}

void Style(UiContext& ui, std::string extra = {})
{
    std::string error;
    Check(ui.SetStyleSheet("root, scrollbar, stackedwidget, stackpage { padding: 0; border-width: 0; gap: 0; } "
                           "scrollbar::scroll-track { padding: 2px; } " +
                               extra,
                           error),
          "Scrollbar/stack CSS compiles");
}

void GeometryAndRange(FontFace& font)
{
    UiContext ui(font);
    auto& bar = ui.Root().Add<ScrollBar>(Axis::Horizontal);
    bar.SetFloatingBounds({20, 20, 304, 20});
    bar.SetRange(-100, 900);
    bar.SetPageStep(250);
    bar.SetValue(-100);
    Style(ui);
    ui.Prepare(500, 300);
    auto thumb = bar.GetThumbBounds();
    Check(Near(thumb.m_Width, 60) && Near(thumb.m_X, 22), "Page/(span+page) determines the proportional thumb");
    bar.SetValue(400);
    Check(Near(bar.GetThumbBounds().m_X, 142), "Midrange value centers the thumb within its travel");
    bar.SetValue(900);
    Check(Near(bar.GetThumbBounds().m_X + bar.GetThumbBounds().m_Width, 322), "Maximum reaches the track end exactly");
    bar.SetPageStep(0);
    Check(Near(bar.GetThumbBounds().m_Width, 20), "Zero page extent retains a usable minimum thumb");
    bar.SetRange(7, 7);
    Check(bar.GetValue() == 7 && Near(bar.GetThumbBounds().m_Width, 300), "Equal limits clamp and fill the track");
    bar.SetRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    bar.SetPageStep(std::numeric_limits<double>::max());
    bar.SetValue(0);
    Check(Near(bar.GetThumbBounds().m_Width, 100) && Near(bar.GetThumbBounds().m_X, 122),
          "Extreme finite limits and page size cannot overflow thumb geometry");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    Throws([&] { bar.SetRange(2, 1); }, "Reversed limits are rejected");
    Throws([&] { bar.SetRange(0, nan); }, "Nonfinite limits are rejected");
    Throws([&] { bar.SetValue(nan); }, "Nonfinite values are rejected");
    Throws([&] { bar.SetPageStep(-1); }, "Negative page extent is rejected");
    Throws([&] { bar.SetSingleStep(0); }, "Zero single step is rejected");
    Throws([&] { bar.SetOrientation(static_cast<Axis>(99)); }, "Invalid orientation is rejected");
    Check(bar.GetOrientation() == Axis::Horizontal && bar.GetValue() == 0, "Rejected updates preserve canonical state");
    bar.SetOrientation(Axis::Vertical);
    bar.SetFloatingBounds({20, 20, 20, 304});
    ui.Prepare(500, 400);
    Check(Near(bar.GetThumbBounds().m_Height, 100) && Near(bar.GetThumbBounds().m_Y, 122),
          "Vertical values increase downward with the same geometry");
}

void LocalHover(FontFace& font)
{
    UiContext ui(font);
    auto& bar = ui.Root().Add<ScrollBar>(Axis::Horizontal);
    bar.SetFloatingBounds({20, 20, 304, 20});
    bar.SetRange(0, 1000);
    bar.SetPageStep(250);
    bar.SetValue(500);
    Style(ui, "scrollbar::scroll-thumb:hover { width:30px; }");
    ui.Prepare(500, 300);
    ui.PointerMove({24, 30});
    ui.Prepare(500, 300);
    Check(Near(bar.GetThumbBounds().m_Width, 60), "Hovering the track does not select the thumb's hover dimensions");
    const auto neutral = bar.GetThumbBounds();
    ui.PointerMove({neutral.m_X + 2, Center(neutral).m_Y});
    ui.Prepare(500, 300);
    Check(Near(bar.GetThumbBounds().m_Width, 30), "Thumb hover dimensions resolve once from neutral hit geometry");
    ui.PointerMove({24, 30});
    ui.Prepare(500, 300);
    Check(Near(bar.GetThumbBounds().m_Width, 60), "Leaving the thumb restores its proportional length");
}

void Input(FontFace& font)
{
    UiContext ui(font);
    auto& bar = ui.Root().Add<ScrollBar>(Axis::Horizontal);
    bar.SetFloatingBounds({20, 20, 304, 20});
    bar.SetRange(0, 1000);
    bar.SetPageStep(250);
    Style(ui);
    ui.Prepare(500, 300);
    const Point start = Center(bar.GetThumbBounds());
    ui.PointerDown(start);
    ui.PointerMove({start.m_X + 120, start.m_Y});
    Check(Near(bar.GetValue(), 500), "Thumb dragging maps pointer travel to the full value span");
    ui.PointerUp({900, start.m_Y});
    const auto released = bar.GetValue();
    ui.PointerMove(start);
    Check(bar.GetValue() == released, "Release outside the widget ends the capture");
    ui.PointerDown({315, 30});
    Check(Near(bar.GetValue(), 750), "Track press advances one page without jumping to the pointer");
    ui.PointerMove({200, 30});
    Check(Near(bar.GetValue(), 750), "A track press does not turn into a thumb drag");
    ui.PointerUp({200, 30});
    ui.SetFocus(&bar);
    bar.SetSingleStep(7);
    ui.KeyEvent(Key::Left, true);
    Check(bar.GetValue() == 743, "Horizontal arrow applies the configured single step");
    ui.KeyEvent(Key::PageUp, true);
    Check(bar.GetValue() == 493, "PageUp applies the visible extent");
    ui.KeyEvent(Key::Home, true);
    Check(bar.GetValue() == 0, "Home reaches the minimum");
    ui.KeyEvent(Key::End, true);
    Check(bar.GetValue() == 1000, "End reaches the maximum");
    ui.Scroll({150, 30}, {0, 1});
    Check(bar.GetValue() == 979, "Wheel scroll applies three single steps");
    bar.SetValue(0);
    PointerEvent pointer{Center(bar.GetThumbBounds())};
    pointer.m_Type = PointerEventType::Down;
    pointer.m_Button = PointerButton::Left;
    pointer.m_PointerId = 7;
    ui.DispatchInput(pointer);
    pointer.m_Type = PointerEventType::Move;
    pointer.m_Position.m_X += 60;
    pointer.m_PointerId = 8;
    ui.DispatchInput(pointer);
    Check(bar.GetValue() == 0, "Another pointer cannot drive a captured thumb");
    pointer.m_PointerId = 7;
    ui.DispatchInput(pointer);
    Check(Near(bar.GetValue(), 250), "The captured pointer retains its drag identity");
    ui.Deactivate();
    const auto cancelled = bar.GetValue();
    pointer.m_Position.m_X += 60;
    ui.DispatchInput(pointer);
    Check(bar.GetValue() == cancelled, "Deactivation cancels a thumb gesture");
    bar.SetEnabled(false);
    ui.Prepare(500, 300);
    ui.PointerDown({315, 30});
    ui.PointerUp({315, 30});
    Check(bar.GetValue() == cancelled, "Disabled scrollbars reject pointer input");
    bar.SetEnabled(true);
    Style(ui, "scrollbar { transform: translate(40px, 50px) scale(1.25); }");
    ui.Prepare(500, 300);
    bar.SetValue(0);
    const auto from = ui.ToWindowPoint(bar, Center(bar.GetThumbBounds()));
    const auto to = ui.ToWindowPoint(bar, {Center(bar.GetThumbBounds()).m_X + 120, Center(bar.GetThumbBounds()).m_Y});
    ui.PointerDown(from);
    ui.PointerMove(to);
    ui.PointerUp(to);
    Check(Near(bar.GetValue(), 500), "Transformed widget input matches the painted scrollbar");
}

void BindingSemanticsAndLifetime(FontFace& font)
{
    UiContext ui(font);
    auto& bar = ui.Root().Add<ScrollBar>();
    bar.SetFloatingBounds({20, 20, 20, 300});
    State<double> offset(ui.States(), 20,
                         [](double value)
                         {
                             if (value > 80)
                                 throw std::invalid_argument("Application viewport limit");
                         });
    auto binding = BindValue(bar, offset);
    Check(bar.GetValue() == 20, "Binding immediately presents canonical state");
    bar.SetValue(35);
    Check(offset.Get() == 35, "Scrollbar gestures and application updates share one state");
    offset.Set(45);
    Check(bar.GetValue() == 45, "State changes update the scrollbar");
    bar.SetValue(90);
    Check(bar.GetValue() == 45 && offset.Get() == 45 && binding.GetError(),
          "Validation rejection restores canonical state");
    Style(ui);
    ui.Prepare(300, 400);
    auto& tree = ui.Semantics();
    const auto id = tree.GetId(bar);
    auto node = tree.GetNode(id);
    Check(node && node->m_Info.m_Role == SemanticRole::ScrollBar && node->m_Info.m_Number == 45 &&
              node->m_Info.m_Step == 1,
          "Scrollbar semantics expose the live canonical range");
    Check(tree.Perform(id, {SemanticAction::SetNumber, {}, 60}) == SemanticStatus::Succeeded && offset.Get() == 60,
          "Accessibility uses the same validated value path");
    Check(tree.Perform(id, {SemanticAction::SetNumber, {}, 101}) == SemanticStatus::InvalidValue,
          "Accessibility rejects values outside the range");
    const auto before = ui.GetStats();
    bar.SetValue(65);
    bar.SetPageStep(30);
    ui.Prepare(300, 400);
    Check(ui.GetStats().m_Measurements == before.m_Measurements &&
              ui.GetStats().m_Arrangements == before.m_Arrangements &&
              ui.GetStats().m_StyleResolutions == before.m_StyleResolutions,
          "Value/page changes preserve style, intrinsic and layout caches");
    Check(!ui.HasAnimations(), "Static scrolling introduces no continuous frame requests");
    bool rejected = false;
    std::jthread worker(
        [&]
        {
            try
            {
                bar.SetValue(70);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    Check(rejected && bar.GetValue() == 65, "Wrong-thread direct mutation is rejected");
    auto& disposable = ui.Root().Add<ScrollBar>();
    const WidgetRef weak(&disposable);
    disposable.ValueChanged().Connect([&](double) { ui.Root().RemoveChild(disposable); });
    disposable.SetValue(1);
    Check(!weak.Get(), "A value observer may delete its scrollbar");
    auto& looping = ui.Root().Add<ScrollBar>();
    const ScopedConnection loop(
        looping.ValueChanged().Connect([&](double value) { looping.SetValue(value == 1 ? 2 : 1); }));
    Throws([&] { looping.SetValue(1); }, "Reentrant value oscillation is diagnosed without recursive stack growth");
}

class MeasuredPage final : public Widget
{
  public:
    explicit MeasuredPage(int& count) : Widget("measured-page"), m_Count(count)
    {
    }

  protected:
    Point Measure(FontFace&) const override
    {
        ++m_Count;
        return {200, 100};
    }

  private:
    int& m_Count;
};

void Pages(FontFace& font)
{
    UiContext ui(font);
    auto& stack = ui.Root().Add<StackedWidget>();
    stack.SetHeight(SizeRule::Fill());
    Check(stack.GetCurrentIndex() == -1 && !stack.GetCurrentPage(), "An empty stack has no current page");
    int firstMeasures = 0, secondMeasures = 0;
    auto& first = stack.AddPage<MeasuredPage>(firstMeasures);
    first.SetHeight(SizeRule::Fill());
    auto& editor = first.Add<TextBox>("Persistent draft");
    auto& second = stack.AddPage<MeasuredPage>(secondMeasures);
    second.SetHeight(SizeRule::Fill());
    second.SetWidth(SizeRule::Fixed(900));
    auto& busy = second.Add<BusyIndicator>();
    busy.SetHeight(SizeRule::Fixed(24));
    Style(ui);
    ui.Prepare(500, 400);
    Check(stack.GetPageCount() == 2 && stack.GetCurrentPage() == &first && first.IsVisible() && !second.IsVisible(),
          "The first page is selected and inactive pages stay hidden");
    Check(firstMeasures > 0 && secondMeasures == 0 && first.GetBounds().m_Width < 510,
          "A hidden oversized page neither measures nor contributes layout demand");
    Check(!ui.HasAnimations(), "Animations in inactive pages do not request frames");
    ui.SetFocus(&editor);
    TextCompositionEvent preedit;
    preedit.m_Kind = TextCompositionKind::Preedit;
    preedit.m_Text = "temporary";
    preedit.m_Caret = 9;
    Check(ui.DispatchComposition(preedit), "The active editor accepts composition");
    stack.SetCurrentPage(second);
    ui.Prepare(500, 400);
    Check(ui.GetFocus() != &editor && editor.GetText() == "Persistent draft",
          "Page switch cancels old focus/composition without committing preedit");
    Check(secondMeasures > 0 && ui.HasAnimations(), "Selecting a page activates its layout and animations");
    const auto inactiveMeasures = firstMeasures;
    ui.Prepare(450, 350);
    Check(firstMeasures == inactiveMeasures, "Resizing never measures an inactive page");
    stack.SetCurrentIndex(0);
    ui.Prepare(500, 400);
    Check(editor.GetText() == "Persistent draft" && !ui.HasAnimations(),
          "Returning preserves page state and retires hidden animation work");
    Widget unrelated;
    Throws([&] { stack.SetCurrentPage(unrelated); }, "Selecting an unrelated page is rejected");
    Throws([&] { stack.SetCurrentIndex(5); }, "Invalid page indices are rejected");
    Throws([&] { stack.AddPage(std::unique_ptr<Widget>{}); }, "Null page adoption is rejected");
    int changes = 0;
    stack.CurrentChanged().Connect([&](int) { ++changes; });
    stack.SetCurrentIndex(1);
    changes = 0;
    const WidgetRef selected(&second);
    stack.RemovePage(0);
    Check(changes == 1 && stack.GetCurrentIndex() == 0 && stack.GetCurrentPage() == selected.Get(),
          "Removing an earlier page preserves selected identity and publishes its new index");
    auto& third = stack.AddPage();
    changes = 0;
    stack.RemovePage(0);
    Check(changes == 1 && stack.GetCurrentIndex() == 0 && stack.GetCurrentPage() == &third && third.IsVisible(),
          "Removing the active page publishes a replacement even at the same index");
    stack.AddPage();
    changes = 0;
    stack.RemovePage(1);
    Check(changes == 0, "Removing a later inactive page does not publish an unchanged selection");
    stack.ClearPages();
    Check(stack.GetCurrentIndex() == -1 && stack.GetPageCount() == 0 && changes == 1,
          "Clearing publishes one coherent empty state");
    stack.AddPage();
    Check(stack.GetCurrentIndex() == 0, "A cleared stack selects the next first page");
    auto node = ui.Semantics().GetNode(ui.Semantics().GetId(stack));
    Check(node && node->m_Info.m_Role == SemanticRole::Group,
          "A stack is a semantic group without fictitious tab-strip actions");
}

void PageCallbacks(FontFace& font)
{
    UiContext ui(font);
    auto& stack = ui.Root().Add<StackedWidget>();
    stack.AddPage();
    stack.AddPage();
    std::vector<int> changes;
    const Connection selection = stack.CurrentChanged().Connect(
        [&](int index)
        {
            Check(stack.GetCurrentIndex() == index && stack.GetPage(index).IsVisible(),
                  "Page state is coherent before observer delivery");
            changes.push_back(index);
            if (index == 1)
                stack.SetCurrentIndex(0);
        });
    stack.SetCurrentIndex(1);
    Check(changes == std::vector<int>{1, 0}, "Reentrant selection settles in bounded delivery order");
    selection.Disconnect();
    auto& disposable = ui.Root().Add<StackedWidget>();
    disposable.AddPage();
    disposable.AddPage();
    const WidgetRef weak(&disposable);
    disposable.CurrentChanged().Connect([&](int) { ui.Root().RemoveChild(disposable); });
    disposable.SetCurrentIndex(1);
    Check(!weak.Get(), "A selection observer can delete the stack");
    auto& newborn = ui.Root().Add<StackedWidget>();
    newborn.CurrentChanged().Connect([&](int) { ui.Root().RemoveChild(newborn); });
    Throws([&] { newborn.AddPage(); },
           "Deleting a stack during first-page notification cannot return a dangling page reference");
    const ScopedConnection loop(
        stack.CurrentChanged().Connect([&](int index) { stack.SetCurrentIndex(index == 0 ? 1 : 0); }));
    Throws([&] { stack.SetCurrentIndex(1); }, "Reentrant selection oscillation is bounded");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        GeometryAndRange(font);
        LocalHover(font);
        Input(font);
        BindingSemanticsAndLifetime(font);
        Pages(font);
        PageCallbacks(font);
        std::cout << s_Checks << " scroll/stack checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Scroll/stack: " << error.what() << '\n';
        return 1;
    }
}
