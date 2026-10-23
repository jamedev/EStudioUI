#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/OpenGLWidget.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>

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
    return std::abs(a - b) < 0.02f;
}

template <typename Callback> void Throws(Callback callback, const std::string& message)
{
    bool caught = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        caught = true;
    }
    Check(caught, message);
}

Point Center(const Widget& widget)
{
    const auto bounds = widget.GetBounds();
    return {bounds.m_X + bounds.m_Width * 0.5f, bounds.m_Y + bounds.m_Height * 0.5f};
}

void Click(UiContext& ui, Point point, int clicks = 1)
{
    ui.PointerDown(point, {}, clicks);
    ui.PointerUp(point);
}

void Allocation()
{
    Throws([] { SizeRule::Fixed(-1); }, "negative fixed size rejected");
    Throws([] { SizeRule::Fill(0); }, "zero Fill weight rejected");
    Throws([] { SizeRule::Content(50, 20); }, "inverted bounds rejected");
    const auto capped = Layout::Distribute(500, {{100, 100, 100, 0}, {0, 0, 50, 1}, {0, 0, 1000, 1}});
    Check(Near(capped.m_Sizes[1], 50) && Near(capped.m_Sizes[2], 350), "space from a capped child is redistributed");
    const auto weighted = Layout::Distribute(500, {{100, 100, 100, 0}, {40, 40, 1000, 1}, {40, 40, 1000, 3}});
    Check(Near(weighted.m_Sizes[1], 120) && Near(weighted.m_Sizes[2], 280),
          "Fill weights divide free space above hard minima");
    const auto small = Layout::Distribute(70, {{100, 100, 100, 0}, {40, 40, 90, 1}});
    Check(Near(small.m_Overflow, 70) && Near(small.m_Sizes[0], 100), "overflow preserves fixed/minimum sizes");
    const auto unused = Layout::Distribute(500, {{10, 0, 20, 1}, {50, 50, 50, 0}});
    Check(Near(unused.m_Unused, 430), "fully capped containers report unused space");
    Check(Layout::Distribute(100, {}).m_Unused == 100, "empty container leaves space unused");
    std::mt19937 random(1234);
    std::uniform_real_distribution<float> size(0, 100);
    for (int sample = 0; sample < 300; ++sample)
    {
        std::vector<LayoutItem> items;
        float bases = 0;
        for (int i = 0; i < 10; ++i)
        {
            const float minimum = size(random);
            items.push_back({minimum, minimum, minimum + size(random), size(random) + 1});
            bases += minimum;
        }
        const float available = size(random) * 10;
        const auto result = Layout::Distribute(available, items);
        const float sum = std::accumulate(result.m_Sizes.begin(), result.m_Sizes.end(), 0.0f);
        Check(Near(sum + result.m_Unused - result.m_Overflow, available), "allocation conserves space");
        for (std::size_t i = 0; i < items.size(); ++i)
            Check(result.m_Sizes[i] >= items[i].m_Minimum - 0.001f && result.m_Sizes[i] <= items[i].m_Maximum + 0.001f,
                  "allocation obeys every bound");
        Check(Near(result.m_Overflow, std::max(0.0f, bases - available)), "allocation overflow matches required bases");
    }
}

void Sizing(FontFace& font)
{
    UiContext ui(font);
    auto& root = ui.Root();
    root.SetLayoutDirection(Axis::Horizontal);
    root.SetGap(10);
    auto& fixed = root.Add<Widget>();
    auto& capped = root.Add<Widget>();
    auto& fluid = root.Add<Widget>();
    fixed.SetId("fixed");
    fixed.SetWidth(SizeRule::Fixed(100));
    capped.SetWidth(SizeRule::Fill(1, 0, 50));
    fluid.SetWidth(SizeRule::Fill(1, 40));
    std::string error;
    Check(ui.SetStyleSheet("#fixed { width: 999px; }", error), "size test CSS parses");
    ui.Prepare(500, 200);
    Check(Near(fixed.GetBounds().m_Width, 100), "code contracts override theme widths");
    Check(Near(capped.GetBounds().m_Width, 50) && Near(fluid.GetBounds().m_Width, 330),
          "widget allocator redistributes capped growth after gaps");
    capped.SetVisible(false);
    ui.Prepare(500, 200);
    Check(Near(fluid.GetBounds().m_Width, 390), "hidden widgets consume neither space nor gaps");
    ui.Prepare(80, 200);
    Check(Near(fixed.GetBounds().m_Width, 100) && Near(fluid.GetBounds().m_Width, 40),
          "tiny window preserves contracts");
    Check(!ui.GetOverflowingContainers().empty(), "layout overflow is inspectable");
    StyleSheet css;
    Check(
        css.Parse("panel { width: 2fr; height: content; row-height: 24px; } toolbutton:checked { color: red; }", error),
        "new CSS dimensions and states parse");
    const auto style = css.Compute({"panel", "", {}, 0, nullptr});
    Check(style.m_Width.m_Unit == Length::Unit::Fraction && Near(style.m_Width.m_Value, 2),
          "CSS fractional dimensions retain weights");
    Check(!css.Parse("panel { width: 0fr; }", error), "CSS rejects zero fractional weights");
    Check(!css.Parse("panel { row-height: 0px; }", error), "CSS rejects unusable row heights");
    Check(css.LoadFile(std::filesystem::path(ESTUDIOUI_ASSETS_PATH) / "dark.css", error),
          "complete dark stylesheet parses: " + error);
    Check(css.LoadFile(std::filesystem::path(ESTUDIOUI_ASSETS_PATH) / "light.css", error),
          "complete light stylesheet parses: " + error);
}

ToolButton* FindTabCommand(Widget& widget, const std::string& text)
{
    if (auto* button = dynamic_cast<ToolButton*>(&widget); button && button->GetText() == text)
        return button;
    for (const auto& child : widget.GetChildren())
        if (auto* found = FindTabCommand(*child, text))
            return found;
    return nullptr;
}

void TabOverflow(FontFace& font)
{
    UiContext ui(font);
    auto& tabs = ui.Root().Add<TabWidget>();
    tabs.SetHeight(SizeRule::Fill());
    for (int i = 0; i < 30; ++i)
        tabs.AddTab("Workspace " + std::to_string(i));
    tabs.SetCurrentIndex(29);
    ui.Prepare(360, 260);
    auto* chooser = FindTabCommand(tabs, "...");
    auto* last = FindTabCommand(tabs, "Workspace 29");
    Check(chooser && last && chooser->GetBounds().m_X + chooser->GetBounds().m_Width <= 360.01f,
          "A narrow tab strip reserves an on-screen overflow chooser");
    Check(last->GetBounds().m_X >= last->GetClip().m_X && last->GetBounds().m_X + last->GetBounds().m_Width <=
                                                              last->GetClip().m_X + last->GetClip().m_Width + 0.01f,
          "Programmatic selection reveals the last tab without shrinking its caption");
    Click(ui, Center(*chooser));
    auto* list = dynamic_cast<ListWidget*>(ui.GetPopup());
    Check(list && list->GetItemCount() == 30 && list->GetCurrentIndex() == 29,
          "The bounded virtual overflow list exposes every tab and its current selection");
    ui.Prepare(360, 260);
    ui.KeyEvent(Key::Home, true);
    ui.KeyEvent(Key::Enter, true);
    Check(!ui.GetPopup() && tabs.GetCurrentIndex() == 0,
          "Keyboard activation in the overflow chooser selects and dismisses through shared popup handling");
    ui.Prepare(360, 260);
    auto* first = FindTabCommand(tabs, "Workspace 0");
    Check(first->GetBounds().m_X >= first->GetClip().m_X, "The first tab is revealed after choosing it");
    Click(ui, Center(*chooser));
    list = dynamic_cast<ListWidget*>(ui.GetPopup());
    const WidgetRef selectedPage(&tabs.GetPage(29));
    tabs.RemoveTab(0);
    list->SetCurrentIndex(29);
    ui.KeyEvent(Key::Enter, true);
    Check(tabs.GetCurrentIndex() == 28 && &tabs.GetPage(28) == selectedPage.Get(),
          "An open chooser keeps persistent page identities when preceding tabs are removed");
    ui.Prepare(240, 260);
    Check(last->GetBounds().m_X + last->GetBounds().m_Width <= last->GetClip().m_X + last->GetClip().m_Width + 0.01f,
          "Live narrowing keeps the selected tab inside its viewport");
    ui.Prepare(5000, 260);
    Check(FindTabCommand(tabs, "Workspace 1")->GetBounds().m_X >= 0 && last->GetBounds().m_X >= 0,
          "Widening restores the natural tab row after overflow");
    tabs.SetTabText(28, "Renamed workspace");
    Click(ui, Center(*chooser));
    list = dynamic_cast<ListWidget*>(ui.GetPopup());
    Check(list && list->GetItem(28) == "Renamed workspace", "Reopening refreshes tab chooser captions");
    ui.KeyEvent(Key::Escape, true);
    Check(!ui.GetPopup(), "Escape dismisses the chooser without changing pages");
    Click(ui, Center(*chooser));
    list = dynamic_cast<ListWidget*>(ui.GetPopup());
    tabs.RemoveTab(28);
    list->SetCurrentIndex(28);
    ui.KeyEvent(Key::Enter, true);
    Check(!ui.GetPopup() && tabs.GetCurrentIndex() == 27,
          "Choosing a removed page dismisses without selecting an unrelated replacement");
    Click(ui, Center(*chooser));
    list = dynamic_cast<ListWidget*>(ui.GetPopup());
    ui.KeyEvent(Key::Escape, true);
    const WidgetRef tabsRef(&tabs);
    list->CurrentChanged().Connect([&](int) { ui.Root().RemoveChild(tabs); });
    Click(ui, Center(*chooser));
    ui.Prepare(5000, 260);
    Check(!tabsRef.Get() && !ui.GetPopup(),
          "Refreshing a chooser tolerates owner deletion from a list selection observer");
}

void TabsAndCommands(FontFace& font)
{
    UiContext ui(font);
    auto& tabs = ui.Root().Add<TabWidget>();
    tabs.SetHeight(SizeRule::Fill());
    auto& first = tabs.AddTab("First");
    auto& input = first.Add<TextBox>("persistent");
    auto& second = tabs.AddTab("Second");
    second.Add<Button>("Second action");
    ui.Prepare(500, 300);
    Check(tabs.GetTabCount() == 2 && tabs.GetCurrentIndex() == 0, "first page selected automatically");
    Check(first.IsVisible() && !second.IsVisible(), "inactive pages are hidden");
    ui.SetFocus(&input);
    ui.KeyEvent(Key::Tab, true, false, {false, true, false});
    Check(tabs.GetCurrentIndex() == 1, "Ctrl+Tab selects the next page from an editor");
    ui.KeyEvent(Key::Tab, true, false, {true, true, false});
    Check(tabs.GetCurrentIndex() == 0, "Ctrl+Shift+Tab selects the previous page");
    ui.Prepare(500, 300);
    ui.SetFocus(&input);
    tabs.SetCurrentIndex(1);
    ui.Prepare(500, 300);
    Check(!ui.GetFocus(), "switching pages clears hidden-page focus");
    Check(input.GetText() == "persistent", "switching pages preserves editor state");
    Throws([&] { tabs.SetCurrentIndex(99); }, "invalid page selection is rejected");
    Check(tabs.GetCurrentIndex() == 1, "invalid selection preserves active page");
    tabs.AddTab("Third");
    int changes = 0;
    tabs.CurrentChanged().Connect([&](int) { ++changes; });
    tabs.RemoveTab(2);
    Check(changes == 0 && tabs.GetCurrentIndex() == 1,
          "removing an inactive later tab does not emit a selection change");
    tabs.RemoveTab(0);
    Check(tabs.GetTabCount() == 1 && tabs.GetCurrentIndex() == 0, "removal keeps surviving page selected");
    tabs.RemoveTab(0);
    Check(tabs.GetCurrentIndex() == -1 && tabs.GetTabCount() == 0, "empty tab container has no selection");
    auto& toolbar = ui.Root().Add<ToolBar>();
    auto& toggle = toolbar.AddButton("Toggle");
    auto& disabled = toolbar.AddButton("Disabled");
    auto& last = toolbar.AddButton("Last");
    disabled.SetEnabled(false);
    toggle.SetCheckable(true);
    int toggles = 0;
    toggle.Toggled().Connect([&](bool checked) { toggles += checked ? 1 : 10; });
    ui.Prepare(500, 300);
    Click(ui, Center(toggle));
    Check(toggle.IsChecked() && toggles == 1, "tool button toggles on pointer activation");
    ui.KeyEvent(Key::Space, true);
    ui.KeyEvent(Key::Space, false);
    Check(!toggle.IsChecked() && toggles == 11, "tool button toggles on keyboard activation");
    ui.KeyEvent(Key::Right, true);
    Check(ui.GetFocus() == &last, "toolbar arrow navigation skips disabled commands");
}

void Popups(FontFace& font)
{
    UiContext ui(font);
    auto& source = ui.Root().Add<Button>("Source");
    auto& menu = ui.Root().Add<ContextMenu>();
    int first = 0;
    int last = 0;
    menu.AddAction("First").Clicked().Connect([&] { ++first; });
    menu.AddAction("Disabled", false);
    menu.AddSeparator();
    menu.AddAction("Last").Clicked().Connect([&] { ++last; });
    source.ContextMenuRequested().Connect([&](Point point) { menu.Open(point); });
    ui.Prepare(500, 300);
    ui.SetFocus(&source);
    ui.ContextMenu(Center(source));
    Check(menu.IsOpen() && ui.GetFocus() == &menu, "right-click request opens popup with focus");
    ui.KeyEvent(Key::Down, true);
    ui.KeyEvent(Key::Down, true);
    ui.KeyEvent(Key::Enter, true);
    Check(last == 1 && first == 0 && !menu.IsOpen(), "popup navigation skips disabled actions and separators");
    Check(ui.GetFocus() == &source, "popup restores previous focus after activation");
    menu.Open({499, 299});
    const auto bounds = menu.GetBounds();
    Check(bounds.m_X >= 0 && bounds.m_X + bounds.m_Width <= 500.01f && bounds.m_Y + bounds.m_Height <= 300.01f,
          "popup is clamped to window edges");
    ui.KeyEvent(Key::Escape, true);
    Check(!menu.IsOpen(), "Escape closes popup");
    menu.Open({100, 100});
    menu.SetVisible(false);
    ui.Prepare(500, 300);
    Check(!menu.IsOpen(), "hiding a popup clears its routing layer");
    int sourceClicks = 0;
    source.Clicked().Connect([&] { ++sourceClicks; });
    menu.Open({400, 200});
    Click(ui, {1, 1});
    Check(!menu.IsOpen() && sourceClicks == 0, "outside click dismisses without activating the widget behind it");
    ui.SetFocus(&source);
    ui.KeyEvent(Key::Menu, true);
    Check(menu.IsOpen(), "keyboard Menu command uses the same popup request");
    ui.Deactivate();
    Check(!menu.IsOpen() && !ui.GetFocus(), "window deactivation closes popup and focus");
}

void Collections(FontFace& font)
{
    UiContext ui(font);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetHeight(SizeRule::Fixed(150));
    std::vector<std::string> items;
    for (int i = 0; i < 50000; ++i)
        items.push_back("Item " + std::to_string(i));
    list.SetItems(std::move(items));
    ui.Prepare(700, 600);
    Check(list.GetItemCount() == 50000 && list.GetChildren().empty(),
          "large list stores data without creating row widgets");
    ui.SetFocus(&list);
    ui.KeyEvent(Key::End, true);
    Check(list.GetCurrentIndex() == 49999 && list.GetScrollOffset().m_Y > 1000000,
          "End selects and reveals the final virtual row");
    const auto before = list.GetScrollOffset().m_Y;
    ui.Scroll(Center(list), {0, 1});
    Check(list.GetScrollOffset().m_Y < before, "wheel scroll moves list viewport");
    const auto viewport = list.GetViewport();
    ui.PointerDown({viewport.m_X + viewport.m_Width + 6, viewport.m_Y + viewport.m_Height * 0.5f});
    ui.PointerMove({viewport.m_X + viewport.m_Width + 6, viewport.m_Y - 100});
    ui.PointerUp({viewport.m_X + viewport.m_Width + 6, viewport.m_Y - 100});
    Check(Near(list.GetScrollOffset().m_Y, 0), "captured scrollbar drag reaches the first virtual row");
    list.SetItems({"Alpha", "Beta", "Gamma"});
    ui.TextInput("B");
    Check(list.GetCurrentIndex() == 1, "typed prefix selects a matching list item");
    list.RemoveItem(0);
    Check(list.GetCurrentIndex() == 0 && list.GetItem(0) == "Beta", "list removal preserves selected surviving item");
    list.RemoveItem(0);
    Check(list.GetCurrentIndex() == -1, "removing selected list item clears selection");
    auto& tree = ui.Root().Add<TreeWidget>();
    tree.SetHeight(SizeRule::Fixed(150));
    const auto root = tree.AddItem(0, "Root");
    const auto branch = tree.AddItem(root, "Branch");
    const auto leaf = tree.AddItem(branch, "Leaf");
    Check(tree.GetVisibleRowCount() == 1, "collapsed tree exposes only top-level rows");
    tree.SetCurrentItem(leaf);
    Check(tree.GetVisibleRowCount() == 3 && tree.IsExpanded(root) && tree.IsExpanded(branch),
          "selecting a leaf reveals its ancestors");
    ui.Prepare(700, 600);
    ui.SetFocus(&tree);
    ui.KeyEvent(Key::Left, true);
    Check(tree.GetCurrentItem() == branch, "tree Left moves to parent");
    ui.KeyEvent(Key::Left, true);
    Check(!tree.IsExpanded(branch) && tree.GetVisibleRowCount() == 2, "tree Left collapses expanded branch");
    ui.KeyEvent(Key::Right, true);
    Check(tree.IsExpanded(branch), "tree Right expands branch");
    tree.RemoveItem(branch);
    Check(tree.GetCurrentItem() == 0 && tree.GetVisibleRowCount() == 1,
          "subtree removal clears selected descendant safely");
    const auto replacement = tree.AddItem(root, "Replacement");
    Check(replacement > leaf, "tree IDs are not reused after removal");
    auto& table = ui.Root().Add<TableWidget>();
    table.SetHeight(SizeRule::Fill(1, 180));
    table.SetColumns({{"Name", SizeRule::Fill(1, 100)}, {"Value", SizeRule::Fixed(120)}});
    table.SetRows({{"Zulu", "9"}, {"Alpha", "1"}, {"Alpha", "2"}});
    Throws([&] { table.SetRows({{"invalid"}}); }, "nonrectangular table data is rejected");
    Check(table.GetRowCount() == 3 && table.GetCell(0, 0) == "Zulu",
          "invalid replacement preserves table data atomically");
    ui.Prepare(700, 600);
    table.SetCurrentCell(0, 1);
    table.SortByColumn(0);
    Check(table.GetCell(2, 0) == "Zulu" && table.GetCurrentCell().first == 2,
          "sorting preserves selected row identity");
    Check(table.GetCell(0, 1) == "1" && table.GetCell(1, 1) == "2", "sorting is stable for equal keys");
    table.BeginEdit(0, 1);
    Check(dynamic_cast<TextBox*>(ui.GetFocus()) != nullptr, "table opens a focused text editor");
    ui.TextInput("edited");
    ui.KeyEvent(Key::Enter, true);
    Check(table.GetCell(0, 1) == "edited" && ui.GetFocus() == &table, "Enter commits table edit and restores focus");
    table.BeginEdit(0, 1);
    ui.TextInput("cancelled");
    ui.KeyEvent(Key::Escape, true);
    Check(table.GetCell(0, 1) == "edited", "Escape cancels table edit");
    table.BeginEdit(0, 1);
    ui.TextInput("blurred");
    ui.SetFocus(&list);
    Check(table.GetCell(0, 1) == "blurred", "focus loss commits table edit");
    table.SetEditable(false);
    table.BeginEdit(0, 1);
    Check(ui.GetFocus() == &list, "read-only table does not open a cell editor");
    table.SetColumnWidth(0, 150);
    Check(Near(table.GetColumnWidths()[0], 150), "user column widths override Fill allocation");
    ui.SetFocus(&table);
    ui.KeyEvent(Key::Down, true);
    Check(table.GetCurrentCell().first == 1, "table keyboard selection moves between rows");
    ui.Prepare(700, 600);
    const Point edge{table.GetViewport().m_X + 150, table.GetContentBounds().m_Y + 12};
    ui.PointerDown(edge);
    ui.PointerMove({edge.m_X + 35, edge.m_Y});
    ui.PointerUp({edge.m_X + 35, edge.m_Y});
    Check(Near(table.GetColumnWidths()[0], 185), "header-edge pointer drag resizes the column");
    table.SetEditable(true);
    table.BeginEdit(0, 1);
    ui.TextInput("scroll commit");
    ui.Scroll(Center(table), {0, -1});
    Check(table.GetCell(0, 1) == "scroll commit" && ui.GetFocus() == &table,
          "wheel input commits an active cell editor before scrolling");
}

void CallbackLifetimes(FontFace& font)
{
    UiContext ui(font);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetItems({"Remove sender"});
    const WidgetRef reference(&list);
    list.CurrentChanged().Connect([&](int) { ui.Root().RemoveChild(list); });
    ui.Prepare(400, 300);
    Click(ui, {20, 20}, 2);
    Check(!reference.Get() && !ui.GetFocus(), "list selection callback may delete its sender during double click");
    auto& table = ui.Root().Add<TableWidget>();
    table.SetColumns({{"Cell", SizeRule::Fill()}});
    table.SetRows({{"before"}});
    auto& target = ui.Root().Add<Button>("Next");
    ui.Prepare(400, 300);
    table.BeginEdit(0, 0);
    ui.TextInput("after");
    const WidgetRef tableRef(&table);
    table.CellChanged().Connect([&](int, int) { ui.Root().RemoveChild(table); });
    ui.SetFocus(&target);
    Check(!tableRef.Get() && ui.GetFocus() == &target,
          "cell commit callback may delete table while focus moves elsewhere");
    auto& surface = ui.Root().Add<OpenGLWidget>();
    Check(surface.GetContextIdentity() == 0 && surface.GetFramebufferSize().m_X == 0,
          "OpenGL surfaces initialize lazily without a context in core tests");
    ui.Prepare(400, 300);
    int keys = 0;
    surface.KeyInput().Connect(
        [&](KeyboardEvent event)
        {
            if (event.m_Key == Key::W)
                ++keys;
        });
    ui.SetFocus(&surface);
    ui.KeyEvent(Key::W, true);
    ui.KeyEvent(Key::W, false);
    Check(keys == 2, "focused embedded scenes receive key presses and releases");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Allocation();
        Sizing(font);
        TabsAndCommands(font);
        TabOverflow(font);
        Popups(font);
        Collections(font);
        CallbackLifetimes(font);
        std::cout << "Widgets and layout PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Widgets and layout FAIL / " << error.what() << '\n';
        return 1;
    }
}
