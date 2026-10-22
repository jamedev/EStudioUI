#include <EStudioUI/ListWidget.h>
#include <EStudioUI/TableWidget.h>
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

bool Near(float a, float b)
{
    return std::abs(a - b) < 0.02f;
}

void SetCss(UiContext& ui, std::string_view css)
{
    std::string error;
    if (!ui.SetStyleSheet(css, error))
        throw std::runtime_error(error);
}

void Click(UiContext& ui, Point point)
{
    ui.PointerDown(point);
    ui.PointerUp(point);
}

void ListParts(FontFace& font)
{
    UiContext ui(font);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetFloatingBounds({0, 0, 300, 160});
    list.SetItems({"First", "Second", "Third", "Fourth"});
    SetCss(ui, R"(
        listwidget { row-height: 32px; font-size: 17px; color: #123456;
            alternate-background: #234567; hover-background: #345678;
            selection-background-color: #456789; selection-color: #fefdfc; }
    )");
    ui.Prepare(600, 400);
    Check(list.GetPartStyle("item").m_Background.m_A == 0, "normal rows retain transparent fallback fills");
    Check(list.GetPartStyle("item", Alternate).m_Background == list.GetStyle().m_AlternateBackground,
          "alternate rows retain the owner palette fallback");
    Check(list.GetPartStyle("item", Hover | Alternate).m_Background == list.GetStyle().m_HoverBackground,
          "hover fallback has precedence over alternating rows");
    Check(list.GetPartStyle("item", Selected | Hover).m_Background == list.GetStyle().m_SelectionBackground,
          "selected fallback has precedence over hover");
    Check(list.GetPartStyle("item", Selected).m_Color == list.GetStyle().m_SelectionColor,
          "selected text retains the owner palette fallback");
    Check(Near(list.GetPartStyle("item").m_FontSize, 17) && Near(list.GetPartStyle("item").m_Padding.m_Left, 8),
          "row defaults inherit the owner font and existing text inset");
    SetCss(ui, R"(
        listwidget { row-height: 32px; }
        listwidget::item { background: linear-gradient(90deg, #102030, #405060);
            border: 2px solid #abcdef; border-radius: 6px; padding: 3px 11px;
            font-size: 19px; text-align: right; height: 99px; opacity: 0.75; }
        listwidget::item:alternate { color: #ff0000; }
        listwidget::item:hover { background: #00ff00; }
        listwidget::item:selected { box-shadow: 0 2px 4px #000000; color: #0000ff; }
        listwidget::item:active { border-width: 4px; }
        listwidget::item:disabled { color: #808080; }
    )");
    ui.Prepare(600, 400);
    const auto& normal = list.GetPartStyle("item");
    Check(normal.m_BackgroundImage.m_Kind != Gradient::Kind::None && Near(normal.m_Radius, 6),
          "item gradients and rounded borders are compiled into shared styles");
    Check(Near(normal.m_BorderWidth, 2) && Near(normal.m_Padding.m_Left, 11) && normal.m_TextAlign == "right" &&
              Near(normal.m_Opacity, 0.75f),
          "item border, padding, alignment, and opacity remain configurable");
    Check(list.GetPartStyle("item", Selected).m_Shadow.m_Color.m_A > 0, "selected row styles retain rich shadows");
    Check(Near(list.GetPartStyle("item", Pressed).m_BorderWidth, 4), "pressed rows have cached active variants");
    const auto viewport = list.GetViewport();
    Click(ui, {viewport.m_X + 40, viewport.m_Y + 48});
    Check(list.GetCurrentIndex() == 1 && Near(list.GetStyle().m_RowHeight, 32),
          "part dimensions preserve row-height selection geometry");
    ui.PointerMove({viewport.m_X + 40, viewport.m_Y + 16});
    ui.Prepare(600, 400);
    const auto stats = ui.GetStats();
    ui.PointerMove({viewport.m_X + 40, viewport.m_Y + 80});
    ui.Prepare(600, 400);
    Check(ui.GetStats().m_StyleResolutions == stats.m_StyleResolutions &&
              ui.GetStats().m_LayoutPasses == stats.m_LayoutPasses,
          "moving between rows selects cached variants without selector or layout work");
    list.SetEnabled(false);
    ui.Prepare(600, 400);
    Check(Near(list.GetPartStyle("item").m_Color.m_R, 128.0f / 255),
          "owner disabled state participates in item selectors");
    Check(list.GetChildren().empty(), "rich list styling creates no per-row widgets");
}

void TreeParts(FontFace& font)
{
    UiContext ui(font);
    auto& tree = ui.Root().Add<TreeWidget>();
    tree.SetFloatingBounds({0, 0, 260, 160});
    const auto parent = tree.AddItem(0, "Parent");
    const auto child = tree.AddItem(parent, "Child");
    SetCss(ui, R"(
        treewidget { row-height: 32px; indent-size: 20px; }
        treewidget::item { padding: 2px 10px; font-size: 18px; }
        treewidget::branch { width: 44px; background: #123456; border-radius: 6px;
            transform: translateX(5px); }
        treewidget::branch:expanded { background: #abcdef; }
        treewidget::branch:active { color: #ff0000; }
        treewidget::item:expanded { color: #00ff00; }
    )");
    ui.Prepare(600, 400);
    const auto viewport = tree.GetViewport();
    Click(ui, {viewport.m_X + 30, viewport.m_Y + 16});
    Check(tree.IsExpanded(parent) && tree.GetVisibleRowCount() == 2,
          "the CSS branch width is also the expansion hit width");
    Check(tree.GetPartStyle("branch", Expanded).m_Background != tree.GetPartStyle("branch", 0).m_Background,
          "expanded branch styles are precomputed separately");
    Check(tree.GetPartStyle("item", Expanded).m_Color.m_G == 1, "expanded state also styles the tree row");
    Click(ui, {viewport.m_X + 70, viewport.m_Y + 16});
    Check(tree.IsExpanded(parent), "row text outside the branch does not toggle expansion");
    Click(ui, {viewport.m_X + 70, viewport.m_Y + 48});
    Check(tree.GetCurrentItem() == child, "indented rows retain correct selection geometry");
    ui.Prepare(600, 400);
    const auto layouts = ui.GetStats().m_LayoutPasses;
    tree.SetCurrentItem(parent);
    ui.Prepare(600, 400);
    Check(ui.GetStats().m_LayoutPasses == layouts, "selection among visible tree rows requires only repaint");
    Check(tree.GetChildren().empty(), "tree branch and item styling creates no model widgets");
}

void TableParts(FontFace& font)
{
    UiContext ui(font);
    auto& table = ui.Root().Add<TableWidget>();
    table.SetFloatingBounds({0, 0, 640, 190});
    table.SetColumns(
        {{"Fixed", SizeRule::Fixed(100)}, {"Content", SizeRule::Content(0, 1000)}, {"Fill", SizeRule::Fill(1, 40)}});
    table.SetRows({{"b", "measured content", "One"}, {"a", "content", "Two"}, {"c", "content", "Three"}});
    SetCss(ui, R"(
        tablewidget { row-height: 32px; }
        tablewidget::cell { font-size: 14px; padding: 2px 8px; border: 1px solid #123456; }
        tablewidget::header { font-size: 18px; padding: 2px 12px; background: #224466; }
        tablewidget::cell:selected { background: linear-gradient(90deg, #335577, #7799bb); }
        tablewidget::cell:hover { color: #00ff00; }
        tablewidget::header:sorted { background: #ffaa00; }
        tablewidget::cell:read-only { color: #808080; }
    )");
    ui.Prepare(700, 400);
    const auto firstWidths = table.GetColumnWidths();
    Check(Near(firstWidths[0], 100) && firstWidths[2] >= 40, "rich cell styles retain fixed/fill column contracts");
    Check(firstWidths[1] >= font.Shape("measured content", 14).m_Width + 18 - 0.02f,
          "content column measurements include the cell font, padding, and border");
    const auto cell = table.GetCellBounds(1, 1);
    Click(ui, {cell.m_X + 10, cell.m_Y + 10});
    Check(table.GetCurrentCell() == std::pair{1, 1}, "styled cell geometry is shared with selection");
    Check(table.GetPartStyle("cell", Selected).m_BackgroundImage.m_Kind != Gradient::Kind::None,
          "selected cells support rich gradient fills");
    const auto firstCell = table.GetCellBounds(0, 0);
    ui.PointerMove({firstCell.m_X + 20, firstCell.m_Y + 16});
    ui.Prepare(700, 400);
    const auto stats = ui.GetStats();
    ui.PointerMove({cell.m_X + 20, cell.m_Y + 16});
    ui.Prepare(700, 400);
    Check(ui.GetStats().m_StyleResolutions == stats.m_StyleResolutions &&
              ui.GetStats().m_LayoutPasses == stats.m_LayoutPasses,
          "cell hover changes require neither CSS resolution nor layout");
    const auto viewport = table.GetViewport();
    Click(ui, {viewport.m_X + 30, viewport.m_Y - 16});
    Check(table.GetCell(0, 0) == "a", "styled headers preserve sorting input");
    Check(table.GetPartStyle("header", Sorted).m_Background.m_R == 1, "sorted headers expose a distinct CSS variant");
    table.SetEditable(false);
    ui.Prepare(700, 400);
    Check(Near(table.GetPartStyle("cell").m_Color.m_R, 128.0f / 255),
          "table editing mode propagates the read-only pseudo-class to cells");
    SetCss(ui, R"(
        tablewidget { row-height: 32px; }
        tablewidget::cell { font-size: 28px; padding: 4px 20px; }
        tablewidget::header { font-size: 22px; padding: 4px 20px; }
    )");
    ui.Prepare(700, 400);
    const auto nextWidths = table.GetColumnWidths();
    Check(Near(nextWidths[0], 100) && nextWidths[1] > firstWidths[1],
          "theme reload invalidates content measurements while preserving fixed columns");
    Check(table.GetChildren().size() == 1, "table retains one reusable editor instead of per-cell widgets");
}

void LargeModels(FontFace& font)
{
    UiContext ui(font);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetFloatingBounds({0, 0, 240, 128});
    list.SetItems({"Small"});
    ui.Prepare(400, 300);
    list.SetItems({"Still small"});
    const auto beforeSmall = ui.GetStats().m_StyleResolutions;
    ui.Prepare(400, 300);
    const auto smallResolutions = ui.GetStats().m_StyleResolutions - beforeSmall;
    std::vector<std::string> items(100000, "Large model row");
    list.SetItems(std::move(items));
    const auto beforeLarge = ui.GetStats().m_StyleResolutions;
    ui.Prepare(400, 300);
    Check(ui.GetStats().m_StyleResolutions - beforeLarge == smallResolutions,
          "collection style resolution cost is independent of the number of model rows");
    Check(list.GetItemCount() == 100000 && list.GetChildren().empty(),
          "large rich-styled models remain compact row data");
    list.SetScrollOffset({0, 32.0f * 90000});
    ui.Prepare(400, 300);
    const auto viewport = list.GetViewport();
    Click(ui, {viewport.m_X + 30, viewport.m_Y + 48});
    Check(list.GetCurrentIndex() == 90001, "large scrolled models retain logical row hit testing");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        ListParts(font);
        TreeParts(font);
        TableParts(font);
        LargeModels(font);
        std::cout << "Styled collections PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Styled collections FAIL / " << error.what() << '\n';
        return 1;
    }
}
