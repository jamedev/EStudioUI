#include <EStudioUI/ItemModel.h>
#include <EStudioUI/RecordModel.h>
#include <EStudioUI/SelectionModel.h>
#include <EStudioUI/SortFilterModel.h>
#include <EStudioUI/TreeRowMap.h>

#include <limits>

#include <atomic>
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
    static inline std::atomic_size_t s_Copies = 0;

    Part(std::string name, double length, bool valid = true) : m_Name(std::move(name)), m_Length(length), m_Valid(valid)
    {
    }

    Part(const Part& other) : m_Name(other.m_Name), m_Length(other.m_Length), m_Valid(other.m_Valid)
    {
        ++s_Copies;
    }

    Part(Part&&) noexcept = default;
    Part& operator=(const Part&) = default;
    Part& operator=(Part&&) noexcept = default;
};

ModelSnapshot Snapshot(std::vector<Part> parts)
{
    auto length = Column("Length", &Part::m_Length);
    length.m_Schema.m_Validate = [](const ModelValue& value)
    {
        if (std::get<double>(value) < 0)
            throw std::invalid_argument("Length cannot be negative.");
    };
    return MakeTableSnapshot(std::move(parts),
                             std::vector<RecordColumn<Part>>{Column("Name", &Part::m_Name), std::move(length),
                                                             Column("Valid", &Part::m_Valid)});
}

void Core()
{
    UiDispatcher queue;
    StateStore store(queue);
    auto model = std::make_shared<ItemModel>(store, Snapshot({{"A", 20}, {"B", 3}, {"C", 10, false}}));
    const auto initial = model->GetSnapshot();
    const auto a = initial.GetItem(0), b = initial.GetItem(1), c = initial.GetItem(2);
    Check(a && a != b && b != c, "fresh stable identities");
    Check(initial.GetText(b, 1) == "3" && std::get<double>(initial.GetValue(b, 1)) == 3, "typed numeric fields");
    auto selection = SelectionModel::Create(model);
    selection->Select(b, 1);
    auto proxy = SortFilterModel::Create(model);
    proxy->SetSort(1);
    Check(proxy->GetSnapshot().GetItem(0) == b && proxy->GetSnapshot().GetItem(2) == a, "numeric sorting");
    Check(selection->GetCurrent() == b, "sorting preserves canonical selection");
    proxy->SetFilter([](const ModelSnapshot& data, ItemId id) { return std::get<bool>(data.GetValue(id, 2)); }, {2});
    Check(proxy->GetSnapshot().GetRowCount() == 2 && !proxy->GetSnapshot().Contains(c), "filtered mapping");
    selection->Select(c);
    Check(selection->IsSelected(c) && !proxy->GetSnapshot().Contains(c), "hidden selected identities retained");
    int events = 0;
    ScopedConnection observer(model->Subscribe(
        [&](const ModelChange& change)
        {
            ++events;
            Check(change.m_After.GetText(a) == model->GetSnapshot().GetText(a), "event owns coherent after snapshot");
        }));
    model->SetValue(a, 0, std::string("Renamed"));
    Check(events == 1 && initial.GetText(a) == "A", "owned old snapshot and one event");
    Check(proxy->GetPreparationStats().m_ReusedMapping, "unrelated edit reuses mapping");
    Check(proxy->GetSnapshot().GetText(a) == "Renamed", "projection reads new shared records");
    model->SetValue(c, 0, std::string("Hidden rename"));
    Check(proxy->GetSnapshot().GetRowCount() == 2, "hidden unrelated edits are safe");
    const auto revision = model->GetRevision();
    Reject([&] { model->SetValue(a, 1, -1.0); }, "invalid field rolls back");
    Check(model->GetRevision() == revision && std::get<double>(model->GetSnapshot().GetValue(a, 1)) == 20,
          "validation preserves data and revision");
    Reject([&] { model->Apply({{a, 0, std::string("Should roll back")}, {b, 1, -2.0}}); }, "atomic edit batch");
    Check(model->GetSnapshot().GetText(a) == "Renamed", "failed batch has no partial write");
    const auto beforeEvents = events;
    store.Batch(
        [&]
        {
            model->SetValue(a, 1, 25.0);
            model->SetValue(b, 1, 4.0);
        });
    Check(events == beforeEvents + 1, "nested model writes share one outer notification");
    const auto netRevision = model->GetRevision();
    store.Batch(
        [&]
        {
            model->SetValue(a, 1, 30.0);
            model->SetValue(a, 1, 25.0);
        });
    Check(model->GetRevision() == netRevision, "net-no-op model batch suppresses revision");
    Reject([&] { model->Apply({{a, 1, 50.0}}, revision); }, "stale revision rejected");
    proxy->SetValue(b, 1, 40.0);
    Check(std::get<double>(model->GetSnapshot().GetValue(b, 1)) == 40, "proxy edits canonical data");
    Check(proxy->GetSnapshot().GetItem(0) == a, "relevant edits update typed sort");
    bool coherentPrune = false;
    ScopedConnection pruneObserver(model->Subscribe(
        [&](const ModelChange& change)
        {
            if (change.m_Structure)
                coherentPrune = !selection->IsSelected(c) && selection->GetCurrent() == 0;
        }));
    model->Restructure([&](ModelBuilder& builder) { builder.Remove(c); });
    Check(coherentPrune && !model->GetSnapshot().Contains(c), "selection pruned before model observers");
    selection->SetMode(SelectionMode::Multiple);
    const std::vector<ItemId> order{a, b};
    selection->Select(a);
    selection->SelectRange(order, b);
    Check(selection->GetSnapshot()->m_Items.size() == 2, "range selection");
    selection->Select(a, 0, SelectionAction::Toggle);
    Check(!selection->IsSelected(a) && selection->IsSelected(b), "toggle selection");
    selection->SetMode(SelectionMode::Single);
    Check(selection->GetSnapshot()->m_Items.size() <= 1, "single policy reduces selection");
    Reject([&] { selection->Select(999999999); }, "foreign item rejected");
    const auto oldProjection = proxy->GetSnapshot();
    Reject([&] { proxy->SetFilter([](const ModelSnapshot&, ItemId) -> bool { throw std::runtime_error("filter"); }); },
           "throwing new filter preserves old projection");
    Check(proxy->GetSnapshot() == oldProjection, "failed filter is atomic");
    const auto endpoint = model->GetHandle();
    DispatchReceipt posted;
    std::thread worker([&] { posted = endpoint.Post([a](ItemModel& live) { live.SetValue(a, 1, 99.0); }); });
    worker.join();
    queue.Drain();
    Check(posted.GetStatus() == DispatchStatus::Completed &&
              std::get<double>(model->GetSnapshot().GetValue(a, 1)) == 99,
          "worker handle delivery");
    bool foreignRead = false;
    std::thread reader(
        [&]
        {
            try
            {
                (void)model->GetSnapshot();
            }
            catch (const std::logic_error&)
            {
                foreignRead = true;
            }
            Check(initial.GetText(a) == "A", "worker snapshot owns old data");
        });
    reader.join();
    Check(foreignRead, "owner-only model facade");
}

void Hierarchy()
{
    ModelBuilder builder({ModelColumn{"Name"}});
    const auto root = builder.Add({std::string("Group")});
    const auto a = builder.Add({std::string("First")}, root);
    const auto b = builder.Add({std::string("Second")}, root);
    const auto other = builder.Add({std::string("Other")});
    auto snapshot = std::move(builder).Build();
    Check(snapshot.GetRowCount() == 2 && snapshot.GetRowCount(root) == 2, "hierarchical row queries");
    UiDispatcher queue;
    StateStore store(queue);
    auto model = std::make_shared<ItemModel>(store, snapshot);
    auto proxy = SortFilterModel::Create(model);
    proxy->SetFilter([b](const ModelSnapshot&, ItemId id) { return id == b; });
    Check(proxy->GetSnapshot().Contains(root) && proxy->GetSnapshot().Contains(b) && !proxy->GetSnapshot().Contains(a),
          "filter retains matching descendants' ancestors");
    model->Restructure([&](ModelBuilder& edit) { edit.Move(b, 0, other); });
    Check(model->GetSnapshot().GetParent(b) == 0 && model->GetSnapshot().GetItem(1) == b, "move preserves identity");
    const auto revision = model->GetRevision();
    Reject([&] { model->Restructure([&](ModelBuilder& edit) { edit.Move(root, a); }); }, "move cycle rejected");
    Check(model->GetRevision() == revision, "failed move preserves revision");
    ModelBuilder invalid({ModelColumn{"Name"}});
    invalid.Add({std::string("A")}, 2, 1);
    invalid.Add({std::string("B")}, 1, 2);
    Reject([&] { (void)std::move(invalid).Build(); }, "cyclic snapshot rejected");
}

void Contracts()
{
    UiDispatcher queue;
    StateStore store(queue);
    auto model = std::make_shared<ItemModel>(store, Snapshot({{"A", 2}, {"B", 4}, {"C", 6}}));
    const auto initial = model->GetSnapshot();
    const auto a = initial.GetItem(0), b = initial.GetItem(1), c = initial.GetItem(2);
    const auto rev = model->GetRevision();
    store.Batch(
        [&]
        {
            model->SetValue(a, 1, -5.0);
            model->SetValue(a, 1, 8.0);
        });
    Check(std::get<double>(model->GetSnapshot().GetValue(a, 1)) == 8,
          "domain validation sees final outer-batch candidates");
    Reject([&] { (void)initial.WithEdits(std::vector<ModelEdit>{{a, 1, -1.0}}); },
           "standalone immutable edit validates its final snapshot");
    State<double> limit(store, 10);
    ScopedConnection rule(model->AddValidator(
        [&](const ModelSnapshot& candidate)
        {
            if (std::get<double>(candidate.GetValue(a, 1)) > limit.Get())
                throw std::invalid_argument("limit");
        }));
    bool coherent = false;
    ScopedConnection observe(model->Subscribe(
        [&](const ModelChange& change)
        {
            coherent = change.m_ModelKey == model->GetIdentity() && limit.Get() == 30 &&
                       std::get<double>(change.m_After.GetValue(a, 1)) == 20;
        }));
    store.Batch(
        [&]
        {
            model->SetValue(a, 1, 20.0);
            limit.Set(30);
        });
    Check(coherent, "model and scalar state publish before model observers");
    rule = ScopedConnection(Connection{});
    auto first = SortFilterModel::Create(model);
    auto second = SortFilterModel::Create(first);
    first->SetSort(1, false);
    Check(second->GetSnapshot().GetItem(0) == a, "chained projections follow rule-only upstream reorder");
    first->SetFilter([c](const ModelSnapshot&, ItemId item) { return item != c; });
    Check(!second->GetSnapshot().Contains(c), "chained projections follow rule-only upstream filter");
    bool fail = false;
    second->SetFilter(
        [&](const ModelSnapshot&, ItemId item)
        {
            if (fail && item == c)
                throw std::runtime_error("predicate");
            return true;
        });
    fail = true;
    const auto oldFirst = first->GetSnapshot();
    Reject([&] { first->SetFilter({}); }, "downstream predicate rejects an upstream rule change before publication");
    Check(first->GetSnapshot() == oldFirst && !second->GetSnapshot().Contains(c),
          "failed projection rule change preserves all mappings");
    fail = false;
    first->SetFilter({});
    fail = true;
    const auto oldRoot = model->GetSnapshot();
    Reject([&] { model->SetValue(a, 0, std::string("Reject")); },
           "downstream predicate rejects canonical publication before commit");
    Check(model->GetSnapshot() == oldRoot && second->GetSnapshot().GetText(a) == oldRoot.GetText(a),
          "predicate failure has no partially committed source");
    fail = false;
    model->SetValue(b, 0, std::string("Updated"));
    Check(second->GetSnapshot().GetText(b) == "Updated" && first->GetPreparationStats().m_ReusedMapping,
          "chained unrelated edits retain mapping and refresh shared values");
    Check(CompareModelValues(std::int64_t{9223372036854775806LL}, std::int64_t{9223372036854775807LL}) < 0,
          "64-bit integer sort preserves precision on MSVC long-double platforms");
    Check(CompareModelValues(std::int64_t{9223372036854775807LL}, 9223372036854775808.0) < 0,
          "mixed integer/double sort preserves boundary ordering");
    Check(CompareModelValues(1.5, std::int64_t{1}) > 0 && CompareModelValues(-1.5, std::int64_t{-1}) < 0,
          "mixed fractional ordering");
    auto transient = std::make_shared<ItemModel>(store, initial);
    const auto handle = transient->GetHandle();
    const auto posted = handle.Post([](ItemModel&) {});
    transient.reset();
    queue.Drain();
    Check(posted.GetStatus() == DispatchStatus::TargetExpired, "destroyed model rejects queued target resolution");
    auto closingStore = std::make_unique<StateStore>(queue);
    auto surviving = std::make_shared<ItemModel>(*closingStore, initial);
    const auto closedHandle = surviving->GetHandle();
    closingStore.reset();
    Reject([&] { (void)surviving->GetSnapshot(); }, "closed store diagnoses retained facade access");
    Check(!closedHandle.IsConnected(), "store teardown expires model endpoints");
    auto multi = SelectionModel::Create(model);
    multi->SetMode(SelectionMode::Multiple);
    multi->SetItems({a, b, c}, b, 1);
    Check(multi->GetSnapshot()->m_Items.size() == 3 && multi->GetCurrent() == b,
          "bulk selected identities publish coherently");
    auto single = SelectionModel::Create(model);
    single->Select(a);
    single->Select(a, 0, SelectionAction::Toggle);
    Check(!single->IsSelected(a) && single->GetCurrent() == a,
          "single ctrl-toggle can retain an unselected current item");
    auto color = Column("Name", &Part::m_Name);
    color.m_Role = [](const Part&, ModelRole role) -> ModelValue
    { return role == ModelRole::Foreground ? ModelValue(Color{1, 0, 0, 1}) : ModelValue{}; };
    auto typed =
        std::make_shared<ItemModel>(store, MakeTableSnapshot(std::vector<Part>{{"X", 1}, {"Y", 2}},
                                                             std::vector<RecordColumn<Part>>{std::move(color)}));
    const auto typedId = typed->GetSnapshot().GetItem(0);
    typed->Restructure([&](ModelBuilder& edit) { edit.Move(typedId, 0); });
    Check(typed->GetSnapshot().GetRecord<Part>(typedId).m_Length == 1,
          "typed records retain unexposed application fields after structural changes");
    Check(std::get<Color>(typed->GetSnapshot().GetValue(typedId, 0, ModelRole::Foreground)) == Color{1, 0, 0, 1},
          "typed structural changes preserve custom role adapters");
    Check(rev == 0, "initial canonical model has revision zero");
}

void TreePrefixes()
{
    ModelBuilder builder({ModelColumn{"Name"}});
    std::vector<ItemId> ids;
    std::unordered_set<ItemId> expanded;
    std::uint32_t random = 7;
    for (std::size_t index = 0; index < 1000; ++index)
    {
        random = random * 1664525U + 1013904223U;
        const auto parent = index && index % 4 ? ids[random % index] : 0;
        const auto item = builder.Add({std::to_string(index)}, parent);
        ids.push_back(item);
        if (index % 3 == 0)
            expanded.insert(item);
    }
    const auto data = std::move(builder).Build();
    TreeRowMap map;
    map.Reset(data, expanded);
    std::vector<TreeRowMap::Row> expected, pending;
    for (std::size_t row = data.GetRowCount(); row > 0; --row)
        pending.push_back({data.GetItem(row - 1), 0});
    while (!pending.empty())
    {
        const auto item = pending.back();
        pending.pop_back();
        expected.push_back(item);
        if (expanded.contains(item.m_Item))
            for (std::size_t row = data.GetRowCount(item.m_Item); row > 0; --row)
                pending.push_back({data.GetItem(row - 1, item.m_Item), item.m_Depth + 1});
    }
    Check(map.GetCount() == expected.size(), "branch prefixes equal a full traversal's visible count");
    for (std::size_t row = 0; row < expected.size(); ++row)
    {
        const auto actual = map.GetRow(row);
        Check(actual.m_Item == expected[row].m_Item && actual.m_Depth == expected[row].m_Depth,
              "branch prefix row/depth matches traversal");
        Check(map.GetIndex(actual.m_Item) == row, "branch prefix inverse matches visible row");
    }
}

void Scale()
{
    std::vector<Part> records;
    records.reserve(100000);
    for (int i = 0; i < 100000; ++i)
        records.emplace_back("Entity " + std::to_string(i), i + 1.0);
    auto snapshot = Snapshot(std::move(records));
    UiDispatcher queue;
    StateStore store(queue);
    auto model = std::make_shared<ItemModel>(store, snapshot);
    Part::s_Copies = 0;
    const auto first = snapshot.GetItem(50000), second = snapshot.GetItem(50001);
    model->Apply({{first, 1, 100.0}, {second, 1, 200.0}});
    Check(Part::s_Copies == 256, "two edits on one page copy exactly one bounded record page");
    Check(std::get<double>(snapshot.GetValue(first, 1)) == 50001, "old large snapshot remains immutable");
    int notifications = 0;
    ScopedConnection listener(model->Subscribe([&](const ModelChange&) { ++notifications; }));
    std::vector<ModelEdit> edits;
    for (std::size_t i = 0; i < 10000; ++i)
        edits.push_back({snapshot.GetItem(i), 1, 123.0});
    model->Apply(std::move(edits));
    Check(notifications == 1, "10000 edits publish once");
    std::cout << "100000 records: two same-page edits copied 256 records; 10000 edits published one model revision.\n";
}
} // namespace

int main()
{
    try
    {
        Core();
        Hierarchy();
        Contracts();
        TreePrefixes();
        Scale();
        std::cout << "Models PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Models FAIL: " << error.what() << '\n';
        return 1;
    }
}
