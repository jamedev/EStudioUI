#include "Verification.h"
#include "DatabaseBrowserWindow.h"
#include "../demo/GpuProbeWindow.h"
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace EStudioUI
{
namespace
{
template <class T> T* Find(Widget& root)
{
    if (auto* value = dynamic_cast<T*>(&root))
    {
        return value;
    }
    for (const auto& child : root.GetChildren())
    {
        if (auto* found = Find<T>(*child))
        {
            return found;
        }
    }
    return nullptr;
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
} // namespace

void VerifyDatabaseBrowser(DatabaseBrowserWindow& browser, Window& window, const std::filesystem::path& output)
{
    std::filesystem::create_directories(output);
    AllowOversizedGpuProbe(window);
    int checks = 0;
    const auto check = [&](bool result, const char* description)
    {
        ++checks;
        if (!result)
        {
            throw std::runtime_error(std::string("Database browser: ") + description);
        }
    };
    const auto frame = [&] { window.RenderFrame(window.Ui().GetTime() + .01); };
    const auto wait = [&](const std::function<bool()>& ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do
        {
            frame();
            if (ready())
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        const auto model = browser.GetModel();
        throw std::runtime_error("Browser request timed out: " +
                                 (model ? model->GetStatus().m_Error : std::string("example creation")));
    };
    wait(
        [&]
        {
            const auto model = browser.GetModel();
            return model && model->GetStatus().m_Operation == AdapterOperation::Idle && !model->GetCatalog()->empty();
        });
    auto model = browser.GetModel();
    check(model->GetCatalog()->size() == 4, "Real SQLite catalog exposes three tables and a read-only view");
    ModelQuery query;
    query.m_DataSet = "load_cases";
    model->SetQuery(query);
    wait(
        [&]
        {
            return model->GetStatus().m_Operation == AdapterOperation::Idle &&
                   model->GetStatus().m_Query.m_DataSet == "load_cases";
        });
    check(model->GetStatus().m_TotalRows == 100000 && model->GetSnapshot().GetRowCount() == 256,
          "A hundred-thousand-row source publishes one bounded result window");
    frame();
    window.Graphics().SavePpm(output / "database-browser.ppm");
    query = model->GetStatus().m_Query;
    ModelFilter part;
    part.m_Column = 1;
    part.m_Operator = ModelFilterOperator::StartsWith;
    part.m_Value = std::string("Part-1");
    ModelFilter load;
    load.m_Column = 3;
    load.m_Operator = ModelFilterOperator::Greater;
    load.m_Value = 500.0;
    ModelFilter active;
    active.m_Column = 5;
    active.m_Value = std::int64_t{1};
    ModelFilter missing;
    missing.m_Column = 6;
    missing.m_Operator = ModelFilterOperator::IsNull;
    ModelFilter either;
    either.m_Any = true;
    either.m_Children = {active, missing};
    query.m_Filter.m_Children = {part, load, either};
    query.m_Sort = {{1, true, false}, {3, false, false}};
    model->SetQuery(query);
    wait([&] { return model->GetStatus().m_Operation == AdapterOperation::Idle; });
    const auto filtered = model->GetSnapshot();
    check(model->GetStatus().m_MatchedRows > 256 && model->GetStatus().m_MatchedRows < 100000,
          "AND/OR typed predicates execute over the complete database");
    for (std::size_t row = 0; row < filtered.GetRowCount(); ++row)
    {
        const auto item = filtered.GetItem(row);
        check(std::get<std::string>(filtered.GetValue(item, 1)).starts_with("Part-1") &&
                  std::get<double>(filtered.GetValue(item, 3)) > 500,
              "Each actual loaded record satisfies its server predicate");
    }
    model->SetPage(1);
    wait([&] { return model->GetStatus().m_Operation == AdapterOperation::Idle; });
    check(model->GetStatus().m_Query.m_Offset == 256, "Server paging advances without copying the full source");
    model->SetPage(0);
    wait([&] { return model->GetStatus().m_Operation == AdapterOperation::Idle; });
    frame();
    window.Graphics().SavePpm(output / "database-filtered.ppm");
    auto& table = browser.DataView();
    const auto record = model->GetSnapshot().GetItem(0);
    const auto key = model->GetRowKey(record);
    const auto old = std::get<double>(model->GetSnapshot().GetValue(record, 3));
    table.BeginEdit(0, 3);
    auto* numeric = Find<NumericInput>(*table.GetCellEditor());
    check(numeric != nullptr, "A real typed numeric widget edits an SQLite cell");
    frame();
    window.Graphics().SavePpm(output / "database-numeric-editor.ppm");
    numeric->SetValue(old + 2.5);
    table.CommitEdit();
    wait([&] { return model->GetStatus().m_Operation == AdapterOperation::Idle && !table.GetCellEditor(); });
    check(model->GetSnapshot().Contains(record) &&
              std::get<double>(model->GetSnapshot().GetValue(record, 3)) == old + 2.5 &&
              model->GetRowKey(record) == key,
          "Confirmed worker edits preserve stable identity and exact values");
    const auto rejected = model->RequestEdit({{record, 0, std::int64_t{999}}});
    check(rejected.GetResult()->m_Status == ModelEditStatus::Failed, "Primary key editing is rejected before a write");
    auto obsolete = model->GetStatus().m_Query;
    obsolete.m_Filter = {};
    ModelFilter none;
    none.m_Column = 0;
    none.m_Value = std::int64_t{-1};
    obsolete.m_Filter.m_Children = {none};
    model->SetQuery(obsolete);
    auto latest = obsolete;
    latest.m_Filter.m_Children.front().m_Value = std::int64_t{1};
    model->SetQuery(latest);
    wait([&] { return model->GetStatus().m_Operation == AdapterOperation::Idle; });
    check(model->GetSnapshot().GetRowCount() == 1 &&
              std::get<std::int64_t>(model->GetSnapshot().GetValue(model->GetSnapshot().GetItem(0), 0)) == 1,
          "Superseded reads cannot replace the latest query");
    browser.Tabs().SetCurrentIndex(1);
    frame();
    auto& tree = browser.Playground();
    const auto source = tree.GetModel()->GetRootModel();
    const auto initial = source->GetSnapshot();
    const auto wing = initial.GetItem(0), fuselage = initial.GetItem(1), spar = initial.GetItem(0, wing);
    const auto point = [&](std::size_t row, float fraction = .5f)
    {
        const auto view = tree.GetViewport();
        return Point{view.m_X + 70, view.m_Y + (static_cast<float>(row) + fraction) * tree.GetStyle().m_RowHeight -
                                        tree.GetScrollOffset().m_Y};
    };
    window.Graphics().SavePpm(output / "tree-table-before-drag.ppm");
    window.Ui().PointerDown(point(1));
    window.Ui().PointerMove(point(3));
    frame();
    window.Graphics().SavePpm(output / "tree-table-drag.ppm");
    window.Graphics().SetPartialRedrawEnabled(false);
    frame();
    window.Graphics().SavePpm(output / "tree-table-drag-full.ppm");
    check(Read(output / "tree-table-drag.ppm") == Read(output / "tree-table-drag-full.ppm"),
          "Drag previews match full-redraw pixels without paint identity collisions");
    window.Graphics().SetPartialRedrawEnabled(true);
    window.Ui().PointerUp(point(3));
    frame();
    check(source->GetSnapshot().GetParent(spar) == fuselage,
          "Captured drag moves the actual canonical subtree into another parent");
    auto* list = dynamic_cast<ListWidget*>(window.Ui().Root().FindById("browser-playground-list"));
    check(list != nullptr, "The second view is the ordinary shared-model list");
    const auto listArea = list->GetViewport();
    const Point toList{listArea.m_X + 40, listArea.m_Y + 3.5f * list->GetStyle().m_RowHeight};
    window.Ui().PointerDown(point(4));
    window.Ui().PointerMove(toList);
    window.Ui().PointerUp(toList);
    frame();
    check(source->GetSnapshot().GetParent(spar) == 0 && list->GetItemCount() == 3,
          "Cross-view drops preserve identity and shared selection");
    const auto first = tree.GetCellBounds(0, 0), second = tree.GetCellBounds(0, 1);
    const float header = tree.GetContentBounds().m_Y + tree.GetStyle().m_RowHeight * .5f;
    const Point fromHeader{first.m_X + first.m_Width * .5f, header},
        toHeader{second.m_X + second.m_Width * .5f, header};
    window.Ui().PointerDown(fromHeader);
    window.Ui().PointerMove(toHeader);
    window.Ui().PointerUp(toHeader);
    frame();
    check(tree.GetColumnOrder().front() == 1,
          "Captured header dragging reorders presentation without changing schema coordinates");
    tree.SetColumnVisible(3, false);
    frame();
    check(tree.GetColumnWidths()[3] == 0, "Hidden columns consume no allocation or hit-test width");
    tree.SetColumnVisible(3, true);
    tree.BeginEdit(0, 3);
    auto* boolean = Find<CheckBox>(*tree.GetCellEditor());
    check(boolean != nullptr, "Boolean cells use a real checkbox editor");
    const auto root = source->GetSnapshot().GetItem(0);
    const bool before = std::get<bool>(source->GetSnapshot().GetValue(root, 3));
    boolean->Trigger();
    frame();
    check(std::get<bool>(source->GetSnapshot().GetValue(root, 3)) != before,
          "Checkbox acceptance follows the canonical edit result path");
    tree.BeginEdit(0, 4);
    auto* choice = Find<ComboBox>(*tree.GetCellEditor());
    check(choice != nullptr, "Choice cells use a composed combo editor");
    choice->SetCurrentIndex(2);
    tree.CommitEdit();
    frame();
    check(std::get<std::string>(source->GetSnapshot().GetValue(root, 4)) == "Composite",
          "Choice edits preserve typed canonical values");
    tree.BeginEdit(0, 6);
    auto* binary = Find<TextBox>(*tree.GetCellEditor());
    check(binary != nullptr, "Binary cells use an exact bounded hexadecimal editor");
    binary->SetText("0x00FF42");
    tree.CommitEdit();
    frame();
    check(std::get<ModelBlob>(source->GetSnapshot().GetValue(root, 6)) == ModelBlob({0, 255, 66}),
          "Binary edits preserve zero bytes and exact payloads");
    tree.BeginEdit(0, 5);
    auto* color = Find<ColorEditor>(*tree.GetCellEditor());
    check(color != nullptr, "Color cells reuse the framework color widget");
    frame();
    window.Graphics().SavePpm(output / "tree-table-color-editor.ppm");
    color->SetColor({.2f, .7f, .4f, 1});
    tree.CommitEdit();
    frame();
    check(std::get<Color>(source->GetSnapshot().GetValue(root, 5)).m_G > .6f,
          "Color editor commits through ordinary model validation");
    window.Graphics().SavePpm(output / "tree-table.ppm");
    window.Graphics().SavePpm(output / "data-views-retained.ppm");
    window.Graphics().SetPartialRedrawEnabled(false);
    frame();
    window.Graphics().SavePpm(output / "data-views-full.ppm");
    check(Read(output / "data-views-retained.ppm") == Read(output / "data-views-full.ppm"),
          "Retained and forced-full data-view framebuffers are identical");
    checks += VerifyGpuProbeChrome(window, output, output);
    std::ofstream report(output / "database-browser-report.txt");
    report << "Database browser: " << checks
           << " checks passed.\nActual C++ views, SQLite worker transactions, captured drag/drop and framebuffer "
              "equality.\nSoftware GL validates correctness, not hardware throughput.\n";
    std::cout << "Database browser GPU: " << checks << " checks passed\n";
}
} // namespace EStudioUI
