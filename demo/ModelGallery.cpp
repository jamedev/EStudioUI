#include "ModelGallery.h"

#include <EStudioUI/ItemDelegate.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/TreeWidget.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

namespace EStudioUI
{
struct ModelGallery::Reads
{
    std::atomic_size_t m_Values = 0;
    std::atomic_size_t m_Items = 0;
};

namespace
{
void Check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(std::string("Model gallery: ") + message);
}

template <typename T> T& Live(const WidgetRef& reference)
{
    auto* widget = dynamic_cast<T*>(reference.Get());
    if (!widget)
        throw std::logic_error("Model gallery receiver expired.");
    return *widget;
}

struct Entity
{
    std::string m_Name;
    std::string m_Kind;
    double m_Length = 0;
    bool m_Valid = true;
    std::uint64_t m_Serial = 0;
};

ModelSnapshot Entities(const ModelSnapshot* previous = nullptr, TaskContext* task = nullptr)
{
    std::vector<Entity> records;
    std::vector<ItemId> ids, parents;
    records.reserve(100000);
    ids.reserve(100000);
    parents.resize(100000);
    std::unordered_map<std::uint64_t, ItemId> existing;
    if (previous)
        for (std::size_t index = 0; index < previous->GetItemCount(); ++index)
        {
            if (task && index % 1024 == 0)
                task->ThrowIfCancellationRequested();
            const auto id = previous->GetItemByIndex(index);
            if (!existing.emplace(previous->GetRecord<Entity>(id).m_Serial, id).second)
                throw std::invalid_argument("Reload requires unique entity business keys.");
        }
    for (std::size_t index = 0; index < 100000; ++index)
    {
        if (task && index % 1024 == 0)
            task->ThrowIfCancellationRequested();
        auto number = std::to_string(index);
        number.insert(number.begin(), 6 - number.size(), '0');
        records.push_back({"Entity " + number,
                           index % 3 == 0   ? "Line"
                           : index % 3 == 1 ? "Arc"
                                            : "Spline",
                           5.0 + static_cast<double>(index % 950) * 0.5, index % 7 != 0,
                           static_cast<std::uint64_t>(index)});
        const auto found = existing.find(static_cast<std::uint64_t>(index));
        ids.push_back(found == existing.end() ? AllocateItemId() : found->second);
    }
    records[0].m_Name = "Group / construction";
    records[0].m_Kind = "Group";
    for (std::size_t index = records.size() - 12; index < records.size(); ++index)
        parents[index] = ids[0];
    auto name = Column("Entity", &Entity::m_Name);
    name.m_Role = [](const Entity& entity, ModelRole role) -> ModelValue
    {
        if (role == ModelRole::Foreground && !entity.m_Valid)
            return Color{0.96f, 0.48f, 0.43f, 1};
        if (role == ModelRole::ToolTip)
            return entity.m_Valid ? std::string("Valid geometry") : std::string("Needs attention");
        return {};
    };
    auto length = Column("Length", &Entity::m_Length);
    length.m_Schema.m_Format = [](const ModelValue& value) { return FormatModelValue(value) + " mm"; };
    length.m_Schema.m_Validate = [](const ModelValue& value)
    {
        if (std::get<double>(value) < 0)
            throw std::invalid_argument("Length must be nonnegative.");
    };
    return MakeTableSnapshot(std::move(records),
                             std::vector<RecordColumn<Entity>>{std::move(name), Column("Kind", &Entity::m_Kind),
                                                               std::move(length), Column("Valid", &Entity::m_Valid)},
                             std::move(ids), std::move(parents));
}

template <typename Counts> class CountData final : public ModelData
{
  public:
    ModelSnapshot m_Data;
    std::shared_ptr<Counts> m_Reads;

    CountData(ModelSnapshot data, std::shared_ptr<Counts> reads) : m_Data(std::move(data)), m_Reads(std::move(reads))
    {
    }

    const std::type_info& GetRecordType() const noexcept override
    {
        return m_Data.GetData()->GetRecordType();
    }

    const void* GetRecordAddress(ItemId item) const override
    {
        return m_Data.GetData()->GetRecordAddress(item);
    }

    const std::vector<ModelColumn>& GetColumns() const override
    {
        return m_Data.GetColumns();
    }

    std::size_t GetItemCount() const override
    {
        return m_Data.GetItemCount();
    }

    ItemId GetItemByIndex(std::size_t index) const override
    {
        ++m_Reads->m_Items;
        return m_Data.GetItemByIndex(index);
    }

    bool Contains(ItemId id) const override
    {
        return m_Data.Contains(id);
    }

    std::size_t GetRowCount(ItemId parent) const override
    {
        return m_Data.GetRowCount(parent);
    }

    ItemId GetItem(std::size_t row, ItemId parent) const override
    {
        ++m_Reads->m_Items;
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

    ModelValue GetValue(ItemId id, std::size_t column, ModelRole role) const override
    {
        ++m_Reads->m_Values;
        return m_Data.GetValue(id, column, role);
    }

    std::shared_ptr<const ModelData> WithEdits(std::span<const ModelEdit> edits) const override
    {
        return std::make_shared<const CountData>(ModelSnapshot(m_Data.GetData()->WithEdits(edits)), m_Reads);
    }

    std::shared_ptr<const ModelData> WithStructure(std::span<const ModelRow> rows) const override
    {
        return std::make_shared<const CountData>(ModelSnapshot(m_Data.GetData()->WithStructure(rows)), m_Reads);
    }
};

class LengthDelegate final : public ItemDelegate
{
  public:
    bool Paint(Renderer& renderer, FontFace&, const ModelSnapshot& snapshot, ItemId item, std::size_t column,
               Rect bounds, const Style&, Rect clip, float opacity) const override
    {
        auto bar = bounds;
        bar.m_Y += bar.m_Height - 4;
        bar.m_Height = 3;
        bar.m_Width *=
            static_cast<float>(std::clamp(std::get<double>(snapshot.GetValue(item, column)) / 500.0, 0.0, 1.0));
        renderer.DrawSolid(bar, {0.25f, 0.8f, 0.65f, 0.6f * opacity}, clip);
        return false;
    }
};

Label& Caption(Widget& parent, std::string text, float size = 13)
{
    auto& label = parent.Add<Label>(std::move(text));
    label.SetStyleProperty(StyleProperty::FontSize, size);
    label.SetHeight(SizeRule::Fixed(size + 10));
    return label;
}

std::vector<char> Bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void NativeLifetime(const std::filesystem::path& assets, bool headless)
{
    WindowOptions options;
    options.m_Headless = headless;
    options.m_Width = 320;
    options.m_Height = 180;
    auto owner = std::make_unique<Window>(options, assets / "NotoSans.ttf");
    ModelBuilder builder({ModelColumn{"Name"}});
    const auto id = builder.Add({std::string("Before")});
    auto model = std::make_shared<ItemModel>(owner->Ui().States(), std::move(builder).Build());
    bool observerAlive = false;
    ScopedConnection close(model->Subscribe(
        [&](const ModelChange&)
        {
            owner.reset();
            observerAlive = true;
        }));
    auto& list = owner->Ui().Root().Add<ListWidget>();
    list.SetModel(model);
    const WidgetRef witness(&list);
    const auto snapshot = model->GetSnapshot();
    const auto handle = model->GetHandle();
    model->SetValue(id, 0, std::string("After"));
    Check(observerAlive && !witness.Get() && !handle.IsConnected(),
          "model publication survives window deletion and retires endpoints");
    Check(snapshot.GetText(id) == "Before", "captured snapshot survives native owner destruction");
}
} // namespace

ModelGallery::ModelGallery(Window& window, Widget& page, std::filesystem::path assets)
    : m_Reads(std::make_shared<Reads>()), m_Window(window), m_Assets(std::move(assets))
{
    page.SetGap(7);
    Caption(page, "One model. Four views. Stable selection.", 23);
    Caption(page, "100,000 typed entities / CSS parts / numeric editing / independent sort, filter and expansion", 14);
    auto& commands = page.Add<ToolBar>();
    commands.SetHeight(SizeRule::Fixed(43));
    commands.AddButton("Reload on worker").Clicked().Connect([this] { Reload(); });
    commands.AddButton("Batch 10,000 edits").Clicked().Connect([this] { ApplyBatch(); });
    auto& filter = commands.AddButton("Invalid only");
    filter.SetCheckable(true);
    m_FilterToggle = WidgetRef(&filter);
    filter.Clicked().Connect([this] { ToggleFilter(); });
    commands.AddButton("Sort by length")
        .Clicked()
        .Connect([this] { Live<TableWidget>(m_ProjectTable).SortByColumn(2); });
    commands.AddButton("Remove selected").Clicked().Connect([this] { RemoveSelected(); });
    m_Status = WidgetRef(&Caption(page, "Ready / select a record in any view; double-click a table cell to edit."));
    m_Model = std::make_shared<ItemModel>(window.Ui().States(),
                                          ModelSnapshot(std::make_shared<const CountData<Reads>>(Entities(), m_Reads)));
    m_Filtered = SortFilterModel::Create(m_Model);
    m_Filtered->SetSort(2);
    m_Selection = SelectionModel::Create(m_Model);
    m_Selection->SetMode(SelectionMode::Multiple);
    auto& tables = page.Add<Widget>();
    tables.SetLayoutDirection(Axis::Horizontal);
    tables.SetGap(12);
    tables.SetHeight(SizeRule::Fill(1, 190));
    auto makeTable = [&](const std::string& title, std::shared_ptr<ItemModel> model) -> TableWidget&
    {
        auto& pane = tables.Add<Widget>();
        pane.SetWidth(SizeRule::Fill(1, 430));
        pane.SetHeight(SizeRule::Fill());
        pane.SetGap(5);
        Caption(pane, title);
        auto& table = pane.Add<TableWidget>();
        table.SetHeight(SizeRule::Fill());
        table.SetModel(std::move(model));
        table.SetSelectionModel(m_Selection);
        table.SetColumnSize(0, SizeRule::Fill(1, 120));
        table.SetColumnSize(1, SizeRule::Fixed(75));
        table.SetColumnSize(2, SizeRule::Fixed(105));
        table.SetColumnSize(3, SizeRule::Fixed(64));
        table.SetItemDelegate(2, std::make_shared<const LengthDelegate>());
        return table;
    };
    m_Table = WidgetRef(&makeTable("01 / CANONICAL RECORDS", m_Model));
    m_ProjectTable = WidgetRef(&makeTable("02 / SHARED DATA, SORTED PROJECTION", m_Filtered));
    auto& bottom = page.Add<Widget>();
    bottom.SetLayoutDirection(Axis::Horizontal);
    bottom.SetGap(12);
    bottom.SetHeight(SizeRule::Fixed(142));
    auto& names = bottom.Add<Widget>();
    names.SetWidth(SizeRule::Fill());
    names.SetHeight(SizeRule::Fill());
    names.SetGap(4);
    Caption(names, "03 / LIST / SAME SELECTION");
    auto& list = names.Add<ListWidget>();
    list.SetHeight(SizeRule::Fill());
    list.SetModel(m_Model);
    list.SetSelectionModel(m_Selection);
    m_List = WidgetRef(&list);
    auto& groups = bottom.Add<Widget>();
    groups.SetWidth(SizeRule::Fill());
    groups.SetHeight(SizeRule::Fill());
    groups.SetGap(4);
    Caption(groups, "04 / TREE / LOCAL EXPANSION");
    auto& tree = groups.Add<TreeWidget>();
    tree.SetHeight(SizeRule::Fill());
    tree.SetModel(m_Model);
    tree.SetSelectionModel(m_Selection);
    m_Tree = WidgetRef(&tree);
    m_Summary = WidgetRef(&Caption(page, "Select a record to inspect its canonical identity and raw value."));
    auto summary = [model = std::weak_ptr<ItemModel>(m_Model), selection = std::weak_ptr<SelectionModel>(m_Selection),
                    target = m_Summary]
    {
        auto source = model.lock();
        auto selected = selection.lock();
        auto* label = dynamic_cast<Label*>(target.Get());
        if (!source || !selected || !label)
            return;
        const auto item = selected->GetCurrent();
        const auto data = source->GetSnapshot();
        label->SetText(item && data.Contains(item)
                           ? "ID " + std::to_string(item) + " / " + data.GetText(item, 0) + " / " +
                                 data.GetText(item, 2) + " / selected " +
                                 std::to_string(selected->GetSnapshot()->m_Items.size())
                           : "No current record / model revision " + std::to_string(source->GetRevision()));
    };
    m_Connections.emplace_back(m_Model->Subscribe([summary](const ModelChange&) { summary(); }));
    m_Connections.emplace_back(m_Selection->Subscribe([summary](const StateChange<SelectionState>&) { summary(); }));
}

ModelGallery::~ModelGallery()
{
    m_Requests.Cancel();
    m_Task.Cancel();
    m_Tasks.reset();
}

void ModelGallery::Status(std::string message)
{
    if (auto* label = dynamic_cast<Label*>(m_Status.Get()))
        label->SetText(std::move(message));
}

void ModelGallery::ToggleFilter()
{
    const bool next = !m_InvalidOnly;
    m_Filtered->SetFilter(
        next ? std::function<bool(const ModelSnapshot&, ItemId)>([](const ModelSnapshot& data, ItemId item)
                                                                 { return !std::get<bool>(data.GetValue(item, 3)); })
             : std::function<bool(const ModelSnapshot&, ItemId)>{},
        {3});
    m_InvalidOnly = next;
    Live<ToolButton>(m_FilterToggle).SetChecked(next);
    Status(next ? "Invalid entities / filtered-out selections retain their identities."
                : "All entities / selection survives changes of order and visibility.");
}

void ModelGallery::ApplyBatch()
{
    const auto data = m_Model->GetSnapshot();
    std::vector<ModelEdit> edits;
    edits.reserve(10000);
    for (std::size_t index = 1; index <= 10000; ++index)
    {
        const auto item = data.GetItemByIndex(index);
        edits.push_back({item, 0, data.GetText(item, 0) + " *"});
    }
    m_Model->Apply(std::move(edits));
    Status("10,000 field edits / one coherent revision / unchanged sort and filter mappings reused.");
}

void ModelGallery::RemoveSelected()
{
    const auto item = m_Selection->GetCurrent();
    if (!item)
    {
        Status("Select a record first.");
        return;
    }
    m_Model->Restructure([item](ModelBuilder& builder) { builder.Remove(item); });
    Status("Record removed / shared selection pruned before model observers.");
}

void ModelGallery::Reload()
{
    if (!m_Tasks)
        m_Tasks = std::make_unique<BackgroundTasks>(BackgroundTaskOptions{1, 8});
    const auto before = m_Model->GetSnapshot();
    const auto revision = m_Model->GetRevision();
    const auto target = m_Status;
    const auto reads = m_Reads;
    Status("Worker building an owned snapshot / editing remains available; obsolete revisions are rejected.");
    m_Task = m_Tasks->RunLatest(
        m_Requests, m_Model->GetHandle(), [before](TaskContext& task) { return Entities(&before, &task); },
        [revision, target, reads](ItemModel& model, ModelSnapshot data)
        {
            try
            {
                model.Replace(ModelSnapshot(std::make_shared<const CountData<Reads>>(std::move(data), reads)),
                              revision);
                if (auto* label = dynamic_cast<Label*>(target.Get()))
                    label->SetText("Worker snapshot committed / 100,000 owned records / stable identities retained.");
            }
            catch (const std::exception& error)
            {
                if (auto* label = dynamic_cast<Label*>(target.Get()))
                    label->SetText(std::string("Worker result rejected: ") + error.what());
                throw;
            }
        });
}

void ModelGallery::Await()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!m_Task.IsReady() && std::chrono::steady_clock::now() < deadline)
    {
        m_Window.Ui().GetDispatcher().Drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(m_Task.IsReady(), "worker publication settles within its deadline");
    m_Task.RethrowIfFailed();
}

void ModelGallery::Verify(const std::filesystem::path& output)
{
    auto& ui = m_Window.Ui();
    ui.SetFocus(nullptr);
    m_Window.SetSize(1280, 820);
    Reload();
    const auto superseded = m_Task;
    Reload();
    Await();
    Check(superseded.GetStatus() == TaskStatus::Superseded, "new reload retires prior request");
    Check(m_Model->GetSnapshot().GetItemCount() == 100000, "worker publishes all records");
    const auto identityBefore = m_Model->GetSnapshot();
    const auto preserved = identityBefore.GetItem(3), removed = identityBefore.GetItem(2);
    m_Model->Restructure([removed](ModelBuilder& builder) { builder.Remove(removed); });
    Reload();
    Await();
    Check(m_Model->GetSnapshot().GetItem(3) == preserved && m_Model->GetSnapshot().GetItem(2) != removed,
          "reload preserves business-key identities after a record was removed");
    const auto before = m_Model->GetSnapshot();
    const auto target = before.GetItem(14);
    m_Selection->Select(target, 2);
    Check(Live<TableWidget>(m_Table).GetCurrentItemId() == target &&
              Live<ListWidget>(m_List).GetCurrentItemId() == target &&
              Live<TreeWidget>(m_Tree).GetCurrentItem() == target,
          "selection synchronizes table, list, and tree");
    ToggleFilter();
    Check(m_Filtered->GetSnapshot().Contains(target), "invalid target remains in filtered view");
    const auto valid = before.GetItem(1);
    m_Selection->Select(valid);
    Check(!m_Filtered->GetSnapshot().Contains(valid) && m_Selection->IsSelected(valid), "filtered selection retained");
    m_Selection->Select(target, 2);
    auto& table = Live<TableWidget>(m_Table);
    table.BeginEdit(14, 2);
    auto& editor = static_cast<TextBox&>(*table.GetChildren().front());
    editor.SelectAll();
    ui.TextInput("37.5");
    table.CommitEdit();
    Check(std::get<double>(m_Model->GetSnapshot().GetValue(target, 2)) == 37.5,
          "real text input commits a typed numeric edit");
    Check(std::get<double>(before.GetValue(target, 2)) != 37.5, "owned old snapshots remain unchanged");
    const auto revision = m_Model->GetRevision();
    bool rejected = false;
    try
    {
        m_Model->Apply({{target, 2, -1.0}, {valid, 0, std::string("Should roll back")}});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected && m_Model->GetRevision() == revision &&
              m_Model->GetSnapshot().GetText(valid) == before.GetText(valid),
          "invalid final candidate rolls back complete model batch");
    int notifications = 0;
    ScopedConnection count(m_Model->Subscribe([&](const ModelChange&) { ++notifications; }));
    ApplyBatch();
    Check(notifications == 1, "10000 edits deliver one model notification");
    Check(m_Filtered->GetPreparationStats().m_ReusedMapping, "unrelated field batch retains filtered/sorted mapping");
    Live<TreeWidget>(m_Tree).SetExpanded(before.GetItem(0), true);
    const auto group = before.GetItem(0);
    table.SetCurrentCell(0, 0);
    Live<ListWidget>(m_List).SetCurrentIndex(0);
    Live<TreeWidget>(m_Tree).SetCurrentItem(group);
    Live<TableWidget>(m_ProjectTable)
        .SetCurrentCell(static_cast<int>(*m_Filtered->GetSnapshot().GetPosition(group)), 0);
    Status("Verified / shared identities, typed editing, coherent batches, worker snapshots and bounded viewport "
           "drawing.");
    for (int frame = 0; frame < 3; ++frame)
        m_Window.RenderFrame(ui.GetTime());
    m_Reads->m_Values = 0;
    m_Reads->m_Items = 0;
    ui.RequestRepaint();
    m_Window.RenderFrame(ui.GetTime());
    const auto values = m_Reads->m_Values.load(), items = m_Reads->m_Items.load();
    Check(values < 2000 && items < 2000, "actual four-view recording queries only bounded visible records");
    Check(table.GetChildren().size() == 1 && Live<ListWidget>(m_List).GetChildren().empty() &&
              Live<TreeWidget>(m_Tree).GetChildren().empty(),
          "virtual items do not allocate row widgets");
    m_Window.Graphics().SavePpm(output / "models-foundation.ppm");
    m_Window.Graphics().SetPartialRedrawEnabled(false);
    ui.RequestRepaint();
    m_Window.RenderFrame(ui.GetTime());
    m_Window.Graphics().SavePpm(output / "models-full-reference.ppm");
    Check(Bytes(output / "models-foundation.ppm") == Bytes(output / "models-full-reference.ppm"),
          "retained and full actual framebuffers match exactly");
    m_Window.Graphics().SetPartialRedrawEnabled(true);
    for (int frame = 0; frame < 3; ++frame)
        m_Window.RenderFrame(ui.GetTime());
    Check(m_Window.Graphics().GetStats().m_DrawCalls == 0, "settled model gallery submits zero draw calls");
    NativeLifetime(m_Assets, m_Window.IsHeadless());
    std::ofstream report(output / "models-foundation.txt");
    report
        << "Shared models, selection and actual GPU integration: PASS\nRecords: 100000; views: two tables, list, "
           "tree.\n"
        << "Visible recording raw value/role queries: " << values << "; item queries: " << items << ".\n"
        << "10000 field edits: one model notification; unaffected projection mapping reused.\n"
        << "Stable selection survives filtering/sorting; typed text edits, rollback and immutable snapshots passed.\n"
        << "Worker reload supersession and revision-checked owned publication passed.\n"
        << "Native window deletion during model notification retires view/model endpoints safely.\n"
        << "Retained/full framebuffers match byte-for-byte; settled frame submits zero draw calls.\n"
        << "Software GL establishes correctness and work counts, not hardware throughput.\n";
}
} // namespace EStudioUI
