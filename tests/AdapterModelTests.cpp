#include <EStudioUI/AdapterModel.h>
#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/SnapshotModelAdapter.h>
#include <EStudioUI/SelectionModel.h>
#include <EStudioUI/SortFilterModel.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/FontFace.h>
#include <deque>
#include <atomic>
#include <iostream>
#include <thread>
#include <limits>

using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void Reject(F action, const char* message)
{
    bool failed = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    Check(failed, message);
}

ModelColumn Column(std::string name, ModelValueType type)
{
    ModelColumn column;
    column.m_Title = std::move(name);
    column.m_ValueType = type;
    column.m_EditorType = type;
    return column;
}

class Provider final : public ModelAdapter
{
  public:
    mutable std::vector<std::vector<ModelValue>> m_Values{{std::int64_t{1}, std::string("first")},
                                                          {std::int64_t{2}, std::string("second")},
                                                          {std::int64_t{3}, std::string("third")}};
    mutable unsigned m_Loads = 0, m_Writes = 0;
    mutable bool m_Fail = false, m_Invalid = false, m_Empty = false;

    ModelDataSet Describe() const
    {
        ModelDataSet set;
        set.m_Id = set.m_Title = "records";
        set.m_Kind = "test";
        set.m_Editable = true;
        set.m_SchemaRevision = 1;
        set.m_Columns = {Column("Id", ModelValueType::Integer), Column("Text", ModelValueType::Text)};
        set.m_Columns[0].m_Editable = false;
        return set;
    }

    std::vector<ModelDataSet> GetDataSets(std::stop_token) const override
    {
        return m_Empty ? std::vector<ModelDataSet>{} : std::vector<ModelDataSet>{Describe()};
    }

    AdapterPage Load(const ModelQuery& query, std::stop_token) const override
    {
        ++m_Loads;
        if (m_Fail)
        {
            throw std::runtime_error("source read failure");
        }
        AdapterPage page;
        page.m_Query = query;
        page.m_DataSet = Describe();
        page.m_TotalRows = page.m_MatchedRows = m_Values.size();
        if (page.m_Query.m_Offset >= page.m_TotalRows)
        {
            page.m_Query.m_Offset = 0;
        }
        const auto end = std::min(m_Values.size(), static_cast<std::size_t>(page.m_Query.m_Offset) + query.m_Limit);
        for (auto i = static_cast<std::size_t>(page.m_Query.m_Offset); i < end; ++i)
        {
            page.m_Rows.push_back({std::to_string(i), m_Values[i], {}});
        }
        if (m_Invalid && !page.m_Rows.empty())
        {
            page.m_Rows.front().m_Values[0] = std::string("wrong type");
        }
        return page;
    }

    AdapterPage Commit(const ModelQuery& query, std::span<const AdapterEdit> edits, std::stop_token stop) const override
    {
        ++m_Writes;
        if (m_Fail)
        {
            throw std::runtime_error("constraint failure");
        }
        auto candidate = m_Values;
        for (const auto& edit : edits)
        {
            const auto row = static_cast<std::size_t>(std::stoul(edit.m_Key));
            if (candidate.at(row).at(edit.m_Column) != edit.m_Original)
            {
                throw std::runtime_error("conflict");
            }
            candidate[row][edit.m_Column] = edit.m_Value;
        }
        m_Values = std::move(candidate);
        return Load(query, stop);
    }
};

struct Runtime
{
    FontFace m_Font{ESTUDIOUI_FONT_PATH};
    UiContext m_Ui{m_Font};
    std::deque<std::function<void()>> m_Pending;
    BackgroundTasks m_Workers{[this](std::function<void()> work)
                              {
                                  m_Pending.push_back(std::move(work));
                                  return true;
                              }};
    std::shared_ptr<Provider> m_Provider = std::make_shared<Provider>();
    std::shared_ptr<AdapterModel> m_Model = AdapterModel::Create(m_Ui, m_Provider, m_Workers);

    void Work()
    {
        if (m_Pending.empty())
        {
            throw std::runtime_error("No queued adapter work");
        }
        auto work = std::move(m_Pending.front());
        m_Pending.pop_front();
        work();
        m_Ui.PumpPlatformServices();
    }

    void Ready()
    {
        for (int i = 0; i < 8 && m_Model->GetStatus().m_Operation != AdapterOperation::Idle; ++i)
        {
            Work();
        }
        Check(m_Model->GetStatus().m_Operation == AdapterOperation::Idle, "Adapter reaches an honest terminal state");
    }
};

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

void Bridge()
{
    Runtime runtime;
    auto& model = runtime.m_Model;
    auto& ui = runtime.m_Ui;
    Check(model->GetSnapshot().GetItemCount() == 0 && model->GetStatus().m_Operation == AdapterOperation::Catalog,
          "Construction does not perform source I/O inline");
    runtime.Ready();
    Check(runtime.m_Provider->m_Loads == 1 && model->GetSnapshot().GetItemCount() == 3,
          "Worker pages enter the existing ItemModel boundary");
    const auto first = model->GetSnapshot().GetItem(0);
    const auto key = model->GetRowKey(first);
    auto selection = SelectionModel::Create(model);
    selection->Select(first);
    auto projection = SortFilterModel::Create(model);
    projection->SetSortKeys({{1, false, false}, {0, true, false}});
    bool coherent = true;
    auto observerConnection = model->Subscribe(
        [&](const ModelChange& change)
        {
            coherent = coherent && model->GetStatus().m_LoadedRows == change.m_After.GetItemCount() &&
                       model->GetRowKey(change.m_After.GetItem(0)) == key;
        });
    ScopedConnection observer(observerConnection);
    auto session = model->RequestEdit({{first, 1, std::string("edited")}});
    Check(session.IsPending() && model->GetSnapshot().GetText(first, 1) == "first",
          "Queued writes never masquerade as committed values");
    Reject([&] { model->CancelLoading(); }, "A write requires a confirmed terminal outcome");
    runtime.Ready();
    Check(!session.IsPending() && session.GetResult()->m_Status == ModelEditStatus::Committed,
          "Edit session settles after actual publication");
    Check(model->GetSnapshot().GetText(first, 1) == "edited" && model->GetRowKey(first) == key &&
              selection->GetCurrent() == first && coherent,
          "Values, keys, counts, projections and selection publish coherently");
    runtime.m_Provider->m_Fail = true;
    session = model->RequestEdit({{first, 1, std::string("not stored")}});
    runtime.Ready();
    Check(session.GetResult()->m_Status == ModelEditStatus::Failed &&
              model->GetSnapshot().GetText(first, 1) == "edited",
          "A failed backend write retains canonical values");
    runtime.m_Provider->m_Fail = false;
    const auto old = model->GetSnapshot();
    ScopedConnection validator(model->AddValidator(
        [](const ModelSnapshot& data)
        {
            if (data.GetItemCount() && data.GetText(data.GetItem(0), 1) == "rejected")
            {
                throw std::invalid_argument("publication rule");
            }
        }));
    runtime.m_Provider->m_Values[0][1] = std::string("rejected");
    model->Refresh();
    runtime.Ready();
    Check(model->GetSnapshot() == old && model->GetRowKey(first) == key && !model->GetStatus().m_Error.empty(),
          "Final-candidate rejection restores private key maps and the old page");
    runtime.m_Provider->m_Values[0][1] = std::string("accepted");
    model->Refresh();
    runtime.Ready();
    Check(model->GetSnapshot().GetText(first, 1) == "accepted", "A rejected publication does not poison later reads");
    runtime.m_Provider->m_Invalid = true;
    model->Refresh();
    runtime.Ready();
    Check(!model->GetStatus().m_Error.empty() && model->GetSnapshot().GetValue(first, 0).index() == 2,
          "Malformed provider values cannot bypass model type validation");
    runtime.m_Provider->m_Invalid = false;
    auto query = model->GetStatus().m_Query;
    query.m_Limit = 1;
    model->SetQuery(query);
    auto newer = query;
    newer.m_Offset = 2;
    model->SetQuery(newer);
    runtime.Ready();
    Check(model->GetStatus().m_Query.m_Offset == 2 && model->GetSnapshot().GetItemCount() == 1 &&
              model->GetRowKey(model->GetSnapshot().GetItem(0)) == "2",
          "A superseded result cannot overwrite the latest request");
    observerConnection.Disconnect();
    model->SetQuery(query);
    runtime.Ready();
    Check(model->GetSnapshot().GetItem(0) == first, "Returning to a bounded cached page preserves stable identities");
    const auto accepted = runtime.m_Workers.GetStats().m_Accepted;
    Reject([&] { ui.States().Batch([&] { model->Refresh(); }); },
           "Adapter admission during a state transaction is rejected");
    auto& probe = ui.Root().Add<PhaseProbe>();
    probe.m_Probe = [&] { Reject([&] { model->Refresh(); }, "Adapter I/O admission during measurement is rejected"); };
    ui.Prepare(400, 200);
    Check(runtime.m_Workers.GetStats().m_Accepted == accepted, "Rejected phase requests admit no worker operation");
    std::atomic_bool rejected = false;
    std::jthread foreign(
        [&]
        {
            try
            {
                model->GetStatus();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    foreign.join();
    Check(rejected, "Direct adapter access retains owner affinity");
    auto dispatcher = ui.GetDispatcher();
    while (dispatcher.Post([] {}).GetStatus() != DispatchStatus::Rejected)
    {
    }
    model->Refresh();
    runtime.Work();
    Check(model->GetStatus().m_Operation == AdapterOperation::Idle && model->GetStatus().m_Error.empty(),
          "Reserved page results complete even while ordinary delivery is full");
    dispatcher.Drain(10000, std::chrono::seconds(1));
    auto throwingConnection = model->SubscribeStatus([](AdapterStatus) { throw std::runtime_error("observer"); });
    ScopedConnection throwing(throwingConnection);
    model->Refresh();
    runtime.Ready();
    Check(model->GetCallbackError() != nullptr, "Observer failures cannot strand admitted source work");
    throwingConnection.Disconnect();
    runtime.m_Provider->m_Empty = true;
    model->RefreshCatalog();
    runtime.Ready();
    Check(model->GetSnapshot().GetItemCount() == 0 && model->GetStatus().m_Query.m_DataSet.empty() &&
              model->GetCatalog()->empty(),
          "An empty catalog clears stale data and selection coherently");
}

void Lifetime()
{
    Runtime runtime;
    runtime.Ready();
    const auto item = runtime.m_Model->GetSnapshot().GetItem(0);
    const auto session = runtime.m_Model->RequestEdit({{item, 1, std::string("late")}});
    const auto started = std::chrono::steady_clock::now();
    runtime.m_Model.reset();
    Check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100) &&
              session.GetResult()->m_Status == ModelEditStatus::OwnerClosed,
          "Owner retirement settles sessions without joining application workers");
    runtime.Work();
    Check(runtime.m_Provider->m_Writes == 0, "An unstarted owner-expired write is suppressed");
}

void ConcurrentRetirement()
{
    FontFace font(ESTUDIOUI_FONT_PATH);
    BackgroundTasks workers({2, 256});
    auto ui = std::make_unique<UiContext>(font);
    auto adapter = std::make_shared<SnapshotModelAdapter>("empty", ModelSnapshot{});
    for (unsigned round = 0; round < 512; ++round)
    {
        auto model = AdapterModel::Create(*ui, adapter, workers);
        if (round % 8 == 0)
        {
            ui->PumpPlatformServices();
        }
        else
        {
            std::this_thread::yield();
        }
        model.reset();
    }
    // Producers may finish after both their model and the containing UI are gone.
    ui.reset();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (workers.GetStats().m_Running || workers.GetStats().m_Queued)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            throw std::runtime_error("Retired adapter work did not quiesce.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(workers.GetStats().m_Accepted > 0 && workers.GetStats().m_Running == 0,
          "Real workers finish after rapid model/context retirement without retaining or destroying UI state");
}

void SnapshotProvider()
{
    ModelColumn text = Column("Name", ModelValueType::Text);
    ModelColumn count = Column("Count", ModelValueType::Integer);
    ModelBuilder builder({text, count});
    builder.Add({std::string("Alpha"), std::int64_t{3}});
    builder.Add({std::string("alpha"), std::int64_t{1}});
    builder.Add({std::string("Beta"), std::int64_t{2}});
    const auto snapshot = std::move(builder).Build();
    SnapshotModelAdapter adapter("items", snapshot);
    ModelQuery query;
    query.m_DataSet = "items";
    query.m_Filter.m_Column = 0;
    query.m_Filter.m_Operator = ModelFilterOperator::StartsWith;
    query.m_Filter.m_Value = std::string("AL");
    query.m_Filter.m_CaseSensitive = false;
    query.m_Sort = {{1, true, false}};
    query.m_Limit = 1;
    const auto page = adapter.Load(query, {});
    Check(page.m_MatchedRows == 2 && page.m_Rows.size() == 1 && std::get<std::int64_t>(page.m_Rows[0].m_Values[1]) == 1,
          "Application snapshots share portable filters, stable multi-key ordering and bounded pages");
    ModelColumn blob = Column("Bytes", ModelValueType::Blob);
    blob.m_Nullable = true;
    Check(ParseModelValue(blob, "0x00ff42") == ModelValue(ModelBlob{0, 255, 66}) &&
              EncodeModelBlob({0, 255, 66}) == "0x00FF42",
          "Exact binary editing is independent of bounded display formatting");
    ValidateModelValue(blob, {});
    Reject([&] { DecodeModelBlob("0xF"); }, "Odd hexadecimal payloads are rejected");
    Check(CompareModelValues(std::int64_t{9223372036854775807LL}, 9223372036854775808.0) < 0,
          "Mixed numeric ordering retains the signed-64-bit edge");
    query.m_Filter.m_Children.push_back({});
    Reject([&] { query.Validate(2); }, "A filter cannot be both leaf and group");
}
} // namespace

int main()
{
    try
    {
        Bridge();
        Lifetime();
        ConcurrentRetirement();
        SnapshotProvider();
        std::cout << "Adapter models: " << s_Checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
