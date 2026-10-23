#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/OpenGLWidget.h>
#include <EStudioUI/PaintWidget.h>
#include <EStudioUI/SketchWidget.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/SplitToolButton.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/UiContext.h>

#include <iostream>
#include <stdexcept>
#include <string>
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

template <typename Callback> void RejectOffThread(Callback callback, const char* message)
{
    bool rejected = false;
    std::exception_ptr unexpected;
    std::thread worker(
        [&]
        {
            try
            {
                callback();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            catch (...)
            {
                unexpected = std::current_exception();
            }
        });
    worker.join();
    if (unexpected)
        std::rethrow_exception(unexpected);
    Check(rejected, message);
}

//! Exposes the documented custom-paint helpers without running a renderer.
class CustomControl final : public PaintWidget
{
  public:
    //! Returns custom paint bounds through the base extension API.
    Rect Bounds() const
    {
        return GetPaintBounds();
    }

    //! Requests custom painting through the base extension API.
    void Refresh()
    {
        InvalidatePainting();
    }
};

void ValuesAndEditors()
{
    Label label("before");
    RejectOffThread([&] { label.SetText("after"); }, "label edits reject the wrong thread before mutation");
    RejectOffThread([&] { (void)label.GetText(); }, "label references stay owner-only");
    Check(label.GetText() == "before", "rejected label edit preserves text");

    TextBox text("before");
    RejectOffThread([&] { text.SetText("after"); }, "editor text rejects the wrong thread");
    RejectOffThread([&] { text.SetReadOnly(true); }, "editor policy rejects the wrong thread before mutation");
    RejectOffThread([&] { text.SelectAll(); }, "editor selection rejects the wrong thread before mutation");
    RejectOffThread([&] { text.SetMaxBytes(1); }, "editor byte limit rejects the wrong thread before mutation");
    RejectOffThread([&] { (void)text.ValidationChanged(); }, "editor signal access stays owner-only");
    Check(text.GetText() == "before" && !text.IsReadOnly() && text.GetSelectedText().empty(),
          "rejected editor operations preserve content, policy, and selection");

    NumericInput number(12);
    RejectOffThread([&] { number.SetRange(20, 30); }, "numeric bounds reject the wrong thread before clamping");
    RejectOffThread([&] { number.SetValue(99); }, "numeric value rejects the wrong thread before mutation");
    RejectOffThread([&] { number.SetDecimals(2); }, "numeric precision rejects the wrong thread before mutation");
    RejectOffThread([&] { (void)number.GetEditor(); }, "composed editor access stays owner-only");
    Check(number.GetValue() == 12 && number.GetDecimals() == 6, "rejected numeric edits preserve value and format");

    Slider slider;
    slider.SetValue(25);
    RejectOffThread([&] { slider.SetRange(40, 80); }, "slider bounds reject the wrong thread before mutation");
    RejectOffThread([&] { slider.SetValue(80); }, "slider values reject the wrong thread before mutation");
    RejectOffThread([&] { slider.SetOrientation(Axis::Vertical); }, "slider orientation stays owner-only");
    RejectOffThread([&] { (void)slider.GetValue(); }, "scalar getters stay owner-only");
    Check(slider.GetMinimum() == 0 && slider.GetMaximum() == 100 && slider.GetValue() == 25 &&
              slider.GetOrientation() == Axis::Horizontal,
          "rejected slider edits preserve range, value, and orientation");
}

void CommandsAndContainers()
{
    ToolButton command("Command");
    RejectOffThread([&] { command.SetCheckable(true); }, "toggle policy rejects the wrong thread before mutation");
    RejectOffThread([&] { command.SetChecked(true); }, "toggle state rejects the wrong thread before mutation");
    RejectOffThread([&] { (void)command.Clicked(); }, "activation signal accessor stays owner-only");
    auto& activation = command.Clicked();
    RejectOffThread([&] { activation.Emit(); }, "direct worker emission cannot bypass internal command ownership");
    Check(!command.IsCheckable() && !command.IsChecked(), "rejected command edits preserve state");

    TabWidget tabs;
    tabs.AddTab("One");
    tabs.AddTab("Two");
    RejectOffThread([&] { tabs.AddTab("Three"); }, "tab creation rejects the wrong thread");
    RejectOffThread([&] { tabs.SetCurrentIndex(1); }, "tab selection rejects the wrong thread before mutation");
    RejectOffThread([&] { tabs.RemoveTab(0); }, "tab deletion rejects the wrong thread before mutation");
    Check(tabs.GetTabCount() == 2 && tabs.GetCurrentIndex() == 0, "rejected tab edits preserve pages and selection");

    ToolBar toolbar;
    toolbar.AddButton("Command");
    RejectOffThread([&] { toolbar.AddSeparator(); }, "toolbar composition rejects the wrong thread");
    Check(toolbar.Content().GetChildren().size() == 1, "rejected toolbar edit preserves children");

    ContextMenu menu;
    menu.AddAction("Command");
    RejectOffThread([&] { menu.AddAction("Other"); }, "menu composition rejects the wrong thread");
    RejectOffThread([&] { menu.Open({10, 10}); }, "popup opening rejects the wrong thread");
    RejectOffThread([&] { (void)menu.IsOpen(); }, "popup state access stays owner-only");
    Check(menu.GetChildren().size() == 1 && !menu.IsOpen(), "rejected menu operations preserve pending commands");

    SplitToolButton split;
    split.AddAction("One");
    RejectOffThread([&] { split.AddAction("Two"); }, "split commands reject the wrong thread before mutation");
    RejectOffThread([&] { split.OpenMenu(); }, "split popup transfer rejects the wrong thread before mutation");
    Check(split.GetActionCount() == 1 && split.GetDefaultActionIndex() == 0,
          "rejected split edits preserve command ownership");
}

void Collections()
{
    ListWidget list;
    list.SetItems({"One", "Two"});
    list.SetCurrentIndex(0);
    RejectOffThread([&] { list.SetItems({"Other"}); }, "list replacement rejects the wrong thread before mutation");
    RejectOffThread([&] { list.AddItem("Three"); }, "list insertion rejects the wrong thread before mutation");
    RejectOffThread([&] { list.SetCurrentIndex(1); }, "list selection rejects the wrong thread before mutation");
    RejectOffThread([&] { list.SetScrollOffset({10, 10}); }, "scrolling rejects the wrong thread before mutation");
    RejectOffThread([&] { (void)list.GetScrollThumbBounds(false); }, "scroll geometry stays owner-only");
    Check(list.GetItemCount() == 2 && list.GetItem(0) == "One" && list.GetCurrentIndex() == 0,
          "rejected list operations preserve data and selection");

    TreeWidget tree;
    const auto branch = tree.AddItem(0, "Branch");
    tree.AddItem(branch, "Leaf");
    RejectOffThread([&] { tree.RemoveItem(branch); }, "tree deletion rejects the wrong thread before mutation");
    RejectOffThread([&] { tree.SetItemText(branch, "Other"); }, "tree text rejects the wrong thread before mutation");
    RejectOffThread([&] { tree.SetExpanded(branch, true); }, "tree expansion rejects the wrong thread before mutation");
    Check(tree.GetItemText(branch) == "Branch" && !tree.IsExpanded(branch),
          "rejected tree operations preserve nodes and expansion");

    TableWidget table;
    table.SetColumns({{"Name", SizeRule::Fixed(100)}});
    table.SetRows({{"b"}, {"a"}});
    auto& editorFocusLost = dynamic_cast<TextBox&>(*table.GetChildren()[0]).FocusLost();
    RejectOffThread([&] { editorFocusLost.Emit(); }, "direct worker emission cannot bypass composed editor ownership");
    RejectOffThread([&] { table.SetRows({{"Other"}}); }, "table replacement rejects the wrong thread before mutation");
    RejectOffThread([&] { table.SetCell(0, 0, "Other"); }, "table edits reject the wrong thread before mutation");
    RejectOffThread([&] { table.SortByColumn(0); }, "table sorting rejects the wrong thread before mutation");
    RejectOffThread([&] { table.SetColumnWidth(0, 200); }, "column contracts reject the wrong thread before mutation");
    Check(table.GetRowCount() == 2 && table.GetCell(0, 0) == "b" && table.GetColumnWidths()[0] == 100,
          "rejected table operations preserve data, order, and columns");
}

void ScenesAndCustomPainting()
{
    CanvasWidget canvas;
    RejectOffThread([&] { canvas.SetCamera({100, 200}, 4); }, "canvas camera rejects the wrong thread before mutation");
    RejectOffThread([&] { canvas.SetScene(std::make_shared<CanvasScene>()); }, "scene publication stays owner-only");
    RejectOffThread([&] { (void)canvas.GetScene(); }, "live scene access stays owner-only");
    RejectOffThread([&] { (void)canvas.GetCacheStats(); }, "paint cache diagnostics stay owner-only");
    Check(canvas.GetCenter() == CanvasPoint{} && canvas.GetZoom() == 1,
          "rejected canvas operations preserve camera state");

    SketchWidget sketch;
    RejectOffThread([&] { sketch.SetTool(SketchTool::Circle); },
                    "sketch tool rejects the wrong thread before mutation");
    RejectOffThread([&] { sketch.SetAnnotationText("Other"); }, "sketch text rejects the wrong thread before mutation");
    RejectOffThread([&] { (void)sketch.GetDocument(); }, "live document access stays owner-only");
    Check(sketch.GetTool() == SketchTool::Select && sketch.GetAnnotationText() != "Other",
          "rejected sketch operations preserve tool state");

    CustomControl custom;
    RejectOffThread([&] { (void)custom.Bounds(); }, "custom paint bounds stay owner-only");
    RejectOffThread([&] { custom.Refresh(); }, "custom paint invalidation stays owner-only");

    OpenGLWidget surface;
    RejectOffThread([&] { surface.SetContinuousRendering(true); }, "scene scheduling stays owner-only");
    RejectOffThread([&] { surface.SetCallbacks([] {}, [](const OpenGLFrame&) {}); },
                    "scene callback replacement stays owner-only");
    RejectOffThread([&] { surface.RequestFrame(); }, "scene invalidation stays owner-only");
    RejectOffThread([&] { (void)surface.GetContextIdentity(); }, "GL context diagnostics stay owner-only");
    Check(surface.GetContextIdentity() == 0 && surface.GetFramebufferSize() == Point{},
          "rejected scene operations do not initialize or touch GL resources");
}

void ReentrantChildDestruction()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    UiContext ui(font);
    auto& parent = ui.Root().Add<Widget>();
    auto& first = parent.Add<Widget>();
    auto& second = parent.Add<Widget>();
    const WidgetRef parentRef(&parent), firstRef(&first), secondRef(&second);
    const auto firstHandle = ui.GetHandle(first);
    std::stop_callback removeParent(firstHandle.GetTargetStopToken(), [&] { ui.Root().RemoveChild(parent); });
    Check(parent.RemoveChild(first), "removal completes before its child's stop callback removes the parent");
    Check(!parentRef.Get() && !firstRef.Get() && !secondRef.Get(),
          "reentrant parent deletion leaves no stale child ownership");

    auto& clearing = ui.Root().Add<Widget>();
    auto& clearingFirst = clearing.Add<Widget>();
    auto& clearingSecond = clearing.Add<Widget>();
    const WidgetRef clearingRef(&clearing), clearingFirstRef(&clearingFirst), clearingSecondRef(&clearingSecond);
    const auto clearingHandle = ui.GetHandle(clearingFirst);
    std::stop_callback clearParent(clearingHandle.GetTargetStopToken(), [&] { ui.Root().RemoveChild(clearing); });
    clearing.ClearChildren();
    Check(!clearingRef.Get() && !clearingFirstRef.Get() && !clearingSecondRef.Get(),
          "clear destroys a detached child snapshot safely when a stop callback removes its parent");
}
} // namespace

int main()
{
    try
    {
        ValuesAndEditors();
        CommandsAndContainers();
        Collections();
        ScenesAndCustomPainting();
        ReentrantChildDestruction();
        std::cout << "Control affinity PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Control affinity FAIL / " << error.what() << '\n';
        return 1;
    }
}
