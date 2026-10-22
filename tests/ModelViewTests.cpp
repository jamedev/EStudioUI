#include <EStudioUI/Models.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/UiContext.h>

#include <atomic>
#include <iostream>
#include <stdexcept>

using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
        throw std::runtime_error(message);
}

template <typename F> void Reject(F action, const char* message)
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

struct Part
{
    std::string m_Name;
    double m_Length = 0;
    bool m_Valid = true;
};

std::shared_ptr<ItemModel> Parts(StateStore& store)
{
    auto length = Column("Length", &Part::m_Length);
    length.m_Schema.m_Validate = [](const ModelValue& value)
    {
        if (std::get<double>(value) < 0)
            throw std::invalid_argument("Negative length");
    };
    return MakeTableModel(store, std::vector<Part>{{"First", 20, true}, {"Second", 3, true}, {"Third", 10, false}},
                          Column("Name", &Part::m_Name), std::move(length), Column("Valid", &Part::m_Valid));
}

TextBox& Editor(TableWidget& table)
{
    return static_cast<TextBox&>(*table.GetChildren().front());
}

void Views(FontFace& font)
{
    UiContext ui(font);
    auto model = Parts(ui.States());
    const auto data = model->GetSnapshot();
    const auto a = data.GetItem(0), b = data.GetItem(1), c = data.GetItem(2);
    auto selection = SelectionModel::Create(model);
    auto proxy = SortFilterModel::Create(model);
    auto& table = ui.Root().Add<TableWidget>();
    table.SetFloatingBounds({0, 0, 500, 250});
    table.SetModel(model);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetFloatingBounds({510, 0, 200, 250});
    list.SetModel(proxy);
    auto& tree = ui.Root().Add<TreeWidget>();
    tree.SetFloatingBounds({720, 0, 200, 250});
    tree.SetModel(model);
    table.SetSelectionModel(selection);
    list.SetSelectionModel(selection);
    tree.SetSelectionModel(selection);
    ui.Prepare(940, 400);
    Check(table.GetRowCount() == 3 && list.GetItemCount() == 3 && tree.GetVisibleRowCount() == 3,
          "three views share records");
    table.SetCurrentCell(1, 1);
    Check(list.GetCurrentIndex() == 1 && tree.GetCurrentItem() == b && selection->GetCurrent() == b,
          "shared selection follows IDs");
    proxy->SetSort(1);
    Check(list.GetCurrentIndex() == 0 && table.GetCurrentCell().first == 1,
          "different view orders share stable selection");
    proxy->SetFilter([c](const ModelSnapshot&, ItemId item) { return item != c; });
    selection->Select(c);
    Check(list.GetCurrentIndex() == -1 && list.GetCurrentItemId() == c && table.GetCurrentItemId() == c,
          "filtered view retains canonical selection");
    proxy->SetFilter({});
    Check(list.GetCurrentIndex() == 1 && selection->GetCurrent() == c, "selection reappears after filtering");
    int cellNotifications = 0;
    ScopedConnection cellObserver(table.CellChanged().Connect([&](int, int) { ++cellNotifications; }));
    table.SetCell(0, 1, "25.5");
    Check(cellNotifications == 1, "shared table retains committed CellChanged convenience notifications");
    Check(std::get<double>(model->GetSnapshot().GetValue(a, 1)) == 25.5, "typed programmatic cell edit");
    Reject([&] { table.SetCell(0, 1, "bad"); }, "invalid programmatic typed edit reports error");
    Check(cellNotifications == 1, "rejected typed edits do not emit CellChanged");
    const auto invalidRevision = model->GetRevision();
    table.BeginEdit(0, 1);
    Editor(table).SetText("-2");
    table.CommitEdit();
    Check(!table.GetEditError().empty() && Editor(table).IsVisible() && model->GetRevision() == invalidRevision,
          "invalid user commit keeps canonical value and editor alive");
    Editor(table).SetText("42.5");
    table.CommitEdit();
    Check(table.GetEditError().empty() && !Editor(table).IsVisible() &&
              std::get<double>(model->GetSnapshot().GetValue(a, 1)) == 42.5,
          "successful typed commit closes editor");
    table.BeginEdit(0, 1);
    Editor(table).SetText("50");
    table.SortByColumn(1);
    Check(Editor(table).IsVisible() && table.GetCurrentItemId() == a,
          "sorting retains editor identity and shared selection");
    table.CommitEdit();
    Check(std::get<double>(model->GetSnapshot().GetValue(a, 1)) == 50,
          "sorted editor commits original stable identity");
    const auto at = static_cast<int>(*table.GetModel()->GetSnapshot().GetPosition(a));
    table.BeginEdit(at, 1);
    Editor(table).SetText("60");
    model->SetValue(a, 1, 70.0);
    table.CommitEdit();
    Check(!table.GetEditError().empty() && std::get<double>(model->GetSnapshot().GetValue(a, 1)) == 70,
          "external edit conflict cannot overwrite newer canonical data");
    table.CancelEdit();
    auto other = Parts(ui.States());
    Reject([&] { list.SetSelectionModel(SelectionModel::Create(other)); }, "foreign model selection rejected");
    table.BeginEdit(static_cast<int>(*table.GetModel()->GetSnapshot().GetPosition(a)), 1);
    model->Restructure([&](ModelBuilder& builder) { builder.Remove(a); });
    Check(!Editor(table).IsVisible() && !selection->IsSelected(a),
          "deleting active record cancels editor and prunes selection");
    ui.Prepare(940, 400);
    ui.Prepare(940, 400); // Settle the focus/style work left by hiding the removed editor.
    const auto before = ui.GetStats();
    model->SetValue(b, 0, std::string("Changed"));
    ui.Prepare(940, 400);
    const auto after = ui.GetStats();
    std::cout << "Shared edit work: styles=" << after.m_StyleResolutions - before.m_StyleResolutions
              << "; measurements=" << after.m_Measurements - before.m_Measurements
              << "; arrangements=" << after.m_Arrangements - before.m_Arrangements << "\n";
    Check(after.m_StyleResolutions == before.m_StyleResolutions && after.m_Measurements == before.m_Measurements,
          "fill-column field edits retain styles and intrinsic measurements");
    Check(list.GetItem(static_cast<int>(*proxy->GetSnapshot().GetPosition(b))) == "Changed",
          "list reads shared edited value");
    tree.SetItemText(b, "Tree edit");
    Check(model->GetSnapshot().GetText(b) == "Tree edit", "tree edits canonical text");
    ui.States().Batch(
        [&]
        {
            selection->Select(b, 1);
            Check(table.GetCurrentItemId() == b && table.GetCurrentCell().second == 1,
                  "view getters read staged coherent selection");
        });
    auto& doomed = ui.Root().Add<ListWidget>();
    doomed.SetModel(model);
    doomed.SetSelectionModel(selection);
    doomed.CurrentChanged().Connect([&](int) { ui.Root().RemoveChild(doomed); });
    selection->Clear();
    Check(ui.Root().GetChildren().size() == 3, "selection callback can remove a receiving view");
    Reject([&] { ui.States().Batch([&] { table.BeginEdit(0, 1); }); },
           "editor frame preparation rejected before transaction-side mutation");
    Check(!Editor(table).IsVisible(), "rejected nested editor start leaves no visible editor");
}

struct Counts
{
    std::atomic_size_t m_Values = 0, m_Items = 0;
};

class CountData final : public ModelData
{
  public:
    ModelSnapshot m_Data;
    std::shared_ptr<Counts> m_Counts;

    CountData(ModelSnapshot data, std::shared_ptr<Counts> counts) : m_Data(std::move(data)), m_Counts(std::move(counts))
    {
    }

    const std::vector<ModelColumn>& GetColumns() const override
    {
        return m_Data.GetColumns();
    }

    std::size_t GetItemCount() const override
    {
        return m_Data.GetItemCount();
    }

    ItemId GetItemByIndex(std::size_t i) const override
    {
        ++m_Counts->m_Items;
        return m_Data.GetItemByIndex(i);
    }

    bool Contains(ItemId id) const override
    {
        return m_Data.Contains(id);
    }

    std::size_t GetRowCount(ItemId id) const override
    {
        return m_Data.GetRowCount(id);
    }

    ItemId GetItem(std::size_t row, ItemId parent) const override
    {
        ++m_Counts->m_Items;
        return m_Data.GetItem(row, parent);
    }

    ItemId GetParent(ItemId id) const override
    {
        return m_Data.GetParent(id);
    }

    std::optional<std::size_t> GetPosition(ItemId id) const override
    {
        return m_Data.GetPosition(id);
    }

    ModelValue GetValue(ItemId id, std::size_t col, ModelRole role) const override
    {
        ++m_Counts->m_Values;
        return m_Data.GetValue(id, col, role);
    }
};

void Bounded(FontFace& font)
{
    std::vector<Part> records;
    for (int i = 0; i < 100000; ++i)
        records.push_back({"Entity " + std::to_string(i), i + 1.0, true});
    auto raw =
        MakeTableSnapshot(std::move(records), std::vector<RecordColumn<Part>>{Column("Name", &Part::m_Name),
                                                                              Column("Length", &Part::m_Length)});
    auto counts = std::make_shared<Counts>();
    UiContext ui(font);
    auto model =
        std::make_shared<ItemModel>(ui.States(), ModelSnapshot(std::make_shared<const CountData>(raw, counts)));
    auto& table = ui.Root().Add<TableWidget>();
    table.SetFloatingBounds({0, 0, 450, 200});
    table.SetModel(model);
    auto& tree = ui.Root().Add<TreeWidget>();
    tree.SetFloatingBounds({460, 0, 200, 200});
    tree.SetModel(model);
    table.SetColumnSize(0, SizeRule::Content());
    table.SetContentMeasurePolicy(ContentMeasurePolicy::Sample, 32);
    counts->m_Values = 0;
    counts->m_Items = 0;
    ui.Prepare(700, 300);
    (void)table.GetColumnWidths();
    Check(counts->m_Values <= 40 && counts->m_Items <= 40,
          "100000 rows have bounded sample preparation; tree does not flatten root range");
    Check(tree.GetVisibleRowCount() == 100000, "count-based large tree extent");
    const auto firstItems = counts->m_Items.load();
    TreeRowMap map;
    map.Reset(model->GetSnapshot(), {});
    Check(map.GetCount() == 100000 && map.GetRow(99999).m_Item == raw.GetItem(99999), "tree random row mapping");
    Check(counts->m_Items - firstItems <= 1, "tree row lookup touches one source identity");
    std::cout << "100000-record view preparation: values=" << counts->m_Values << "; item reads=" << counts->m_Items
              << ".\n";
}

void VisibleWidthsAndDisabledRows(FontFace& font)
{
    UiContext ui(font);
    ModelBuilder builder({ModelColumn{"Name"}});
    for (int i = 0; i < 20; ++i)
        builder.Add({i == 19 ? std::string(60, 'W') : std::string("A")});
    auto model = std::make_shared<ItemModel>(ui.States(), std::move(builder).Build());
    auto& table = ui.Root().Add<TableWidget>();
    table.SetFloatingBounds({0, 0, 260, 130});
    table.SetModel(model);
    table.SetColumnSize(0, SizeRule::Content());
    table.SetContentMeasurePolicy(ContentMeasurePolicy::Visible);
    ui.Prepare(900, 300);
    const auto narrow = table.GetColumnWidths()[0];
    table.SetScrollOffset({0, 100000});
    const auto wide = table.GetColumnWidths()[0];
    Check(wide > narrow + 100, "visible width policy updates after scrolling without recursive extent queries");
    auto name = Column("Name", &Part::m_Name);
    name.m_Role = [](const Part& part, ModelRole role) -> ModelValue
    { return role == ModelRole::Enabled ? ModelValue(part.m_Valid) : ModelValue{}; };
    auto disabled =
        MakeTableModel(ui.States(), std::vector<Part>{{"Blocked", 1, false}, {"Enabled", 2, true}}, std::move(name));
    table.SetModel(disabled);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetFloatingBounds({280, 0, 220, 160});
    list.SetModel(disabled);
    auto& tree = ui.Root().Add<TreeWidget>();
    tree.SetFloatingBounds({520, 0, 220, 160});
    tree.SetModel(disabled);
    bool activated = false;
    list.Activated().Connect([&](int) { activated = true; });
    tree.Activated().Connect([&](TreeWidget::NodeId) { activated = true; });
    ui.Prepare(900, 300);
    for (auto* view : std::vector<ScrollWidget*>{&list, &tree})
    {
        const auto viewport = view->GetViewport();
        const Point point{viewport.m_X + 10, viewport.m_Y + view->GetStyle().m_RowHeight * 0.5f};
        ui.PointerDown(point, {}, 2);
        ui.PointerUp(point);
    }
    Check(!activated && list.GetCurrentIndex() == -1 && tree.GetCurrentItem() == 0,
          "disabled semantic rows reject user selection and activation");
    table.BeginEdit(0, 0);
    Check(!Editor(table).IsVisible(), "disabled semantic cell cannot open a text editor");
    list.SetCurrentIndex(0);
    ui.SetFocus(&list);
    ui.KeyEvent(Key::Enter, true);
    Check(!activated, "disabled current row cannot activate by keyboard");
}

class PhaseProbe final : public Widget
{
  public:
    std::function<void()> m_Probe;

  protected:
    Point Measure(FontFace&) const override
    {
        m_Probe();
        return {10, 10};
    }
};

void Phases(FontFace& font)
{
    UiDispatcher queue;
    StateStore standalone(queue);
    auto model = Parts(standalone);
    UiContext ui(font);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetModel(model);
    auto& probe = ui.Root().Add<PhaseProbe>();
    const auto revision = model->GetRevision();
    const auto item = model->GetSnapshot().GetItem(0);
    probe.m_Probe = [&]
    {
        Reject([&] { model->SetValue(item, 1, 50.0); },
               "standalone model cannot mutate an attached view during measurement");
    };
    ui.Prepare(400, 300);
    Check(model->GetRevision() == revision, "phase rejection precedes canonical publication");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Views(font);
        Bounded(font);
        Phases(font);
        VisibleWidthsAndDisabledRows(font);
        std::cout << "Model views PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Model views FAIL: " << error.what() << '\n';
        return 1;
    }
}
