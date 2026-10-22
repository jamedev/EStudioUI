#include <EStudioUI/Button.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

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

void CaptionUpdates(FontFace& font)
{
    UiContext ui(font);
    ui.Root().SetLayoutDirection(Axis::Horizontal);
    auto& label = ui.Root().Add<Label>("A");
    auto& button = ui.Root().Add<Button>("B");
    auto& tool = ui.Root().Add<ToolButton>("C");
    label.SetWidth(SizeRule::Content());
    button.SetWidth(SizeRule::Content());
    tool.SetWidth(SizeRule::Content());
    ui.Prepare(800, 200);
    const auto before = ui.GetStats();
    const auto original = label.GetBounds();
    label.SetText("A much wider caption");
    ui.Prepare(800, 200);
    Check(label.GetBounds().m_Width > original.m_Width, "caption content updates intrinsic layout");
    Check(ui.GetStats().m_StyleResolutions == before.m_StyleResolutions,
          "caption content retains resolved widget styles");
    Check(ui.GetStats().m_Measurements == before.m_Measurements + 1,
          "caption content remeasures only its own intrinsic content");
    button.SetText("A longer button");
    tool.SetText("A longer tool caption");
    ui.Prepare(800, 200);
    const auto after = ui.GetStats();
    Check(after.m_StyleResolutions == before.m_StyleResolutions &&
              after.m_Measurements == before.m_Measurements + 3,
          "inherited button captions use the same precise content path");
    label.SetText(label.GetText());
    button.SetText(button.GetText());
    tool.SetText(tool.GetText());
    ui.Prepare(800, 200);
    Check(ui.GetStats().m_LayoutPasses == after.m_LayoutPasses &&
              ui.GetStats().m_Measurements == after.m_Measurements &&
              ui.GetStats().m_StyleResolutions == after.m_StyleResolutions,
          "equal captions schedule no style or geometry work");
}

void EditorUpdates(FontFace& font)
{
    UiContext ui(font);
    auto& editor = ui.Root().Add<TextBox>("before");
    editor.SetPlaceholder("Hint");
    ui.Prepare(500, 120);
    auto before = ui.GetStats();
    editor.SetText("after");
    ui.Prepare(500, 120);
    Check(ui.GetStats().m_StyleResolutions == before.m_StyleResolutions &&
              ui.GetStats().m_Measurements == before.m_Measurements + 1,
          "nonempty editor content updates geometry without selector work");
    before = ui.GetStats();
    editor.SetPlaceholder("Another hint");
    ui.Prepare(500, 120);
    Check(ui.GetStats().m_StyleResolutions == before.m_StyleResolutions,
          "placeholder caption changes retain styles while its pseudo-state is unchanged");
    before = ui.GetStats();
    editor.SetPlaceholder("Another hint");
    editor.SetReadOnly(false);
    editor.SelectAll();
    ui.Prepare(500, 120);
    Check(ui.GetStats().m_StyleResolutions == before.m_StyleResolutions &&
              ui.GetStats().m_Measurements == before.m_Measurements &&
              ui.GetStats().m_LayoutPasses == before.m_LayoutPasses,
          "selection and equal editor properties retain style and measurement caches");
    int changes = 0;
    editor.TextChanged().Connect([&](const std::string&) { ++changes; });
    editor.SetText("after");
    Check(editor.GetSelectedText().empty() && changes == 0,
          "equal editor replacement preserves its explicit selection reset without text notification");
    editor.SetText("");
    ui.Prepare(500, 120);
    Check(ui.GetStats().m_StyleResolutions > before.m_StyleResolutions,
          "empty and placeholder pseudo-state changes still resolve dependent selectors");
}

void ScalarUpdates(FontFace& font)
{
    UiContext ui(font);
    auto& slider = ui.Root().Add<Slider>();
    auto& numeric = ui.Root().Add<NumericInput>(3);
    ui.Prepare(600, 220);
    auto before = ui.GetStats();
    int sliderChanges = 0;
    slider.ValueChanged().Connect([&](double) { ++sliderChanges; });
    slider.SetValue(0.5);
    slider.SetValue(0.5);
    slider.SetRange(slider.GetMinimum(), slider.GetMaximum());
    ui.Prepare(600, 220);
    Check(sliderChanges == 1 && ui.GetStats().m_StyleResolutions == before.m_StyleResolutions &&
              ui.GetStats().m_Measurements == before.m_Measurements &&
              ui.GetStats().m_LayoutPasses == before.m_LayoutPasses,
          "scalar slider changes repaint once without style or layout work");
    int numericChanges = 0;
    numeric.ValueChanged().Connect([&](double) { ++numericChanges; });
    numeric.GetEditor().SetText("-");
    numeric.SetValue(3);
    Check(numeric.GetEditor().GetText() == "3.000000" && numericChanges == 0,
          "equal numeric values still restore an intermediate editing presentation");
    ui.Prepare(600, 220);
    before = ui.GetStats();
    numeric.SetValue(3);
    numeric.SetRange(numeric.GetMinimum(), numeric.GetMaximum());
    ui.Prepare(600, 220);
    Check(ui.GetStats().m_StyleResolutions == before.m_StyleResolutions &&
              ui.GetStats().m_Measurements == before.m_Measurements &&
              ui.GetStats().m_LayoutPasses == before.m_LayoutPasses,
          "equal numeric value and bounds preserve the settled caches");
}

void EditorRepresentability(FontFace& font)
{
    UiContext ui(font);
    auto& editor = ui.Root().Add<TextBox>("12");
    editor.SetMaxBytes(2);
    editor.SetValidator(std::make_shared<IntegerValidator>(-99, 99));
    editor.SelectAll();
    ui.Prepare(400, 120);
    const auto before = ui.GetStats();
    int changes = 0;
    editor.TextChanged().Connect([&](const std::string&) { ++changes; });
    Check(editor.CanSetText("5"), "representability accepts complete valid input");
    Check(editor.CanSetText("-"), "representability accepts an editable intermediate input");
    Check(!editor.CanSetText("x"), "representability rejects invalid policy input");
    Check(!editor.CanSetText("123"), "representability rejects byte-limit truncation");
    Check(!editor.CanSetText("1\n") && !editor.CanSetText(std::string(1, static_cast<char>(0xff))),
          "representability rejects newline normalization and invalid UTF-8 replacement");
    ui.Prepare(400, 120);
    Check(editor.GetText() == "12" && editor.GetSelectedText() == "12" && changes == 0 &&
              ui.GetStats().m_StyleResolutions == before.m_StyleResolutions &&
              ui.GetStats().m_LayoutPasses == before.m_LayoutPasses,
          "representability does not change live text, selection, notifications, or invalidation");
}

void CollectionUpdates(FontFace& font)
{
    UiContext ui(font);
    auto& list = ui.Root().Add<ListWidget>();
    auto& table = ui.Root().Add<TableWidget>();
    auto& tree = ui.Root().Add<TreeWidget>();
    list.SetFloatingBounds({0, 0, 220, 160});
    table.SetFloatingBounds({230, 0, 320, 160});
    tree.SetFloatingBounds({560, 0, 220, 160});
    list.SetItems({"One", "Two"});
    table.SetColumns({{"Title", SizeRule::Content()}});
    table.SetRows({{"Short"}, {"Second"}});
    const auto parent = tree.AddItem(0, "Parent");
    tree.AddItem(parent, "Child");
    ui.Prepare(800, 220);
    const auto before = ui.GetStats();
    const auto width = table.GetColumnWidths().front();
    list.AddItem("Three");
    table.SetCell(0, 0, "A much wider content cell");
    tree.SetExpanded(parent, true);
    ui.Prepare(800, 220);
    Check(ui.GetStats().m_StyleResolutions == before.m_StyleResolutions,
          "virtual collection model edits retain shared style palettes");
    Check(table.GetColumnWidths().front() > width && tree.GetVisibleRowCount() == 2,
          "model edits refresh content columns and visible virtual rows");
    list.SetCurrentIndex(1);
    list.SetItems({"One", "Two", "Three"});
    Check(list.GetCurrentIndex() == -1 && list.GetScrollOffset().m_Y == 0,
          "model replacement preserves its documented selection and scroll reset");
    Check(list.GetChildren().empty() && tree.GetChildren().empty() && table.GetChildren().size() == 1,
          "collection updates preserve virtual rows and the single reusable table editor");
}

void TableEditorScrollAndLifetime(FontFace& font)
{
    UiContext ui(font);
    auto& table = ui.Root().Add<TableWidget>();
    table.SetFloatingBounds({0, 0, 300, 160});
    table.SetColumns({{"Title", SizeRule::Fill()}});
    std::vector<std::vector<std::string>> rows(30, {"Value"});
    table.SetRows(rows);
    ui.Prepare(400, 220);
    table.BeginEdit(1, 0);
    auto& editor = static_cast<TextBox&>(*table.GetChildren().front());
    ui.Prepare(400, 220);
    const auto before = ui.GetStats();
    const float top = editor.GetBounds().m_Y;
    table.SetScrollOffset({0, 8});
    ui.Prepare(400, 220);
    Check(std::abs(editor.GetBounds().m_Y - (top - 8)) < 0.01f,
          "scroll arrangement repositions the reusable cell editor");
    Check(ui.GetStats().m_StyleResolutions == before.m_StyleResolutions &&
              ui.GetStats().m_Measurements == before.m_Measurements,
          "scrolling preserves intrinsic measurement and style caches");
    const WidgetRef lifetime(&table);
    editor.FocusLost().Connect([&] { ui.Root().RemoveChild(table); });
    table.CancelEdit();
    Check(!lifetime.Get(), "edit cancellation remains safe when editor focus loss deletes its table");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        CaptionUpdates(font);
        EditorUpdates(font);
        ScalarUpdates(font);
        EditorRepresentability(font);
        CollectionUpdates(font);
        TableEditorScrollAndLifetime(font);
        std::cout << "Control updates PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Control updates FAIL / " << error.what() << '\n';
        return 1;
    }
}
