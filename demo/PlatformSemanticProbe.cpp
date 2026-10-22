#include "PlatformGallery.h"

#include <EStudioUI/ListWidget.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/TableWidget.h>

#include <stdexcept>

namespace EStudioUI
{
namespace
{
void RequireSemantic(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(std::string("Virtual semantics GPU: ") + message);
}

void CheckSelectionPixels(Window& window, SemanticId id)
{
    window.RenderFrame();
    const auto node = window.Ui().Semantics().GetNode(id);
    RequireSemantic(node && !node->m_Offscreen && node->m_Info.m_Selected,
                    "revealed selection has visible prepared geometry");
    const auto bounds = node->m_Bounds;
    RequireSemantic(bounds.m_Width > 20 && bounds.m_Height > 8, "selection has useful clipped bounds");
    const Point sample{bounds.m_X + bounds.m_Width - 12, bounds.m_Y + bounds.m_Height / 2};
    RequireSemantic(window.Ui().Semantics().HitTest(sample) == id,
                    "semantic item bounds agree with the actual transformed hit path");
    const auto pixel = window.Graphics().ReadPixel(sample);
    RequireSemantic(pixel.m_G > pixel.m_R + 0.2f && pixel.m_G > pixel.m_B + 0.2f,
                    "semantic selection agrees with the selected row/cell GPU fill");
}
} // namespace

void PlatformGallery::VerifyVirtualItems()
{
    WindowOptions options;
    options.m_Width = 640;
    options.m_Height = 280;
    options.m_Headless = m_Window.IsHeadless();
    Window probe(options, m_Assets / "NotoSans.ttf");
    auto& ui = probe.Ui();
    auto& list = ui.Root().Add<ListWidget>();
    list.SetHeight(SizeRule::Fill());
    list.SetStyleProperty(StyleProperty::SelectionBackground, Color{0.03f, 0.75f, 0.12f, 1});
    std::vector<std::string> captions;
    captions.reserve(100000);
    for (int index = 0; index < 100000; ++index)
        captions.push_back("Entry " + std::to_string(index + 1));
    list.SetItems(std::move(captions));
    probe.RenderFrame();
    auto& tree = ui.Semantics();
    const auto listId = tree.GetId(list);
    RequireSemantic(tree.GetChildCount(listId) == 100000 && list.GetChildren().empty(),
                    "100,000 logical rows do not allocate row widgets");
    const auto last = tree.GetChildAt(listId, 99999);
    RequireSemantic(tree.GetNode(last)->m_Offscreen, "last logical row starts outside the viewport");
    RequireSemantic(tree.Perform(last, {SemanticAction::ScrollIntoView}) == SemanticStatus::Succeeded &&
                        tree.Perform(last, {SemanticAction::Select}) == SemanticStatus::Succeeded,
                    "offscreen rows use canonical reveal and selection actions");
    RequireSemantic(list.GetCurrentIndex() == 99999, "semantic selection updates the real list");
    CheckSelectionPixels(probe, last);

    ui.Root().ClearChildren();
    RequireSemantic(!tree.GetNode(last), "retained virtual identities expire with their actual owner");
    auto& table = ui.Root().Add<TableWidget>();
    table.SetHeight(SizeRule::Fill());
    table.SetStyleProperty(StyleProperty::SelectionBackground, Color{0.03f, 0.75f, 0.12f, 1});
    table.SetColumns({{"Name", SizeRule::Fixed(240)}, {"Value", SizeRule::Fill()}});
    std::vector<std::vector<std::string>> rows;
    rows.reserve(1000);
    for (int index = 0; index < 1000; ++index)
        rows.push_back({"Row " + std::to_string(index + 1), "Editable"});
    table.SetRows(std::move(rows));
    probe.RenderFrame();
    const auto cell = tree.GetGridItem(tree.GetId(table), 999, 1);
    RequireSemantic(cell && tree.GetNode(cell)->m_Offscreen, "grid lookup addresses an offscreen cell directly");
    RequireSemantic(tree.Perform(cell, {SemanticAction::ScrollIntoView}) == SemanticStatus::Succeeded &&
                        tree.Perform(cell, {SemanticAction::Select}) == SemanticStatus::Succeeded,
                    "offscreen table cells use canonical reveal and selection");
    CheckSelectionPixels(probe, cell);
    RequireSemantic(tree.Perform(cell, {SemanticAction::SetText, "GPU edit Ω"}) == SemanticStatus::Succeeded &&
                        table.GetCell(999, 1) == "GPU edit Ω",
                    "cell value edits update canonical table data");
    probe.RenderFrame();
    RequireSemantic(tree.GetNode(cell)->m_Info.m_Value == "GPU edit Ω", "cell semantic value follows its edit");
    probe.Close();
}
} // namespace EStudioUI
