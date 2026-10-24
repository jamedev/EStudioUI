#include <EStudioUI/AdapterModel.h>
#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace EStudioUI
{
namespace
{
std::string ErrorText(std::exception_ptr error)
{
    try
    {
        if (error)
        {
            std::rethrow_exception(error);
        }
    }
    catch (const std::exception& failure)
    {
        return failure.what();
    }
    catch (...)
    {
        return "The model adapter failed with an unknown error.";
    }
    return {};
}

//! Paint queries copy bounded display strings, never a large underlying text/blob.
void PrepareDisplay(AdapterPage& page, std::stop_token stop)
{
    if (page.m_Rows.size() > 2048 || page.m_DataSet.m_Columns.size() > 2048)
    {
        throw std::length_error("The adapter returned an oversized result window.");
    }
    std::size_t bytes = 0;
    for (auto& row : page.m_Rows)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("The adapter request was cancelled.");
        }
        if (row.m_Values.size() != page.m_DataSet.m_Columns.size())
        {
            throw std::invalid_argument("Adapter values must be rectangular.");
        }
        if (!row.m_Display.empty() && row.m_Display.size() != row.m_Values.size())
        {
            throw std::invalid_argument("Adapter display values must be rectangular.");
        }
        if (row.m_Display.empty())
        {
            for (std::size_t column = 0; column < row.m_Values.size(); ++column)
            {
                const auto& schema = page.m_DataSet.m_Columns[column];
                row.m_Display.push_back(schema.m_Format ? schema.m_Format(row.m_Values[column])
                                                        : FormatModelValue(row.m_Values[column]));
            }
        }
        for (auto& text : row.m_Display)
        {
            if (text.size() > 1024)
            {
                std::size_t end = 1024;
                while (end && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                {
                    --end;
                }
                text.resize(end);
                text += "…";
            }
            bytes += text.size();
            if (bytes > 8 * 1024 * 1024)
            {
                throw std::length_error("Page presentation exceeds its 8 MiB budget.");
            }
        }
    }
}

class PageData final : public ModelData
{
  public:
    std::vector<ModelColumn> m_Columns;
    std::vector<AdapterRow> m_Rows;
    std::vector<ItemId> m_Ids;
    std::unordered_map<ItemId, std::size_t> m_Positions;

    PageData(std::vector<ModelColumn> columns, std::vector<AdapterRow> rows, std::vector<ItemId> ids)
        : m_Columns(std::move(columns)), m_Rows(std::move(rows)), m_Ids(std::move(ids))
    {
        for (std::size_t i = 0; i < m_Ids.size(); ++i)
        {
            m_Positions.emplace(m_Ids[i], i);
        }
    }

    const std::vector<ModelColumn>& GetColumns() const override
    {
        return m_Columns;
    }

    std::size_t GetItemCount() const override
    {
        return m_Rows.size();
    }

    ItemId GetItemByIndex(std::size_t index) const override
    {
        return m_Ids.at(index);
    }

    bool Contains(ItemId item) const override
    {
        return m_Positions.contains(item);
    }

    std::size_t GetRowCount(ItemId parent) const override
    {
        return parent ? 0 : m_Rows.size();
    }

    ItemId GetItem(std::size_t row, ItemId parent) const override
    {
        if (parent)
        {
            throw std::out_of_range("A result window has no nested rows.");
        }
        return m_Ids.at(row);
    }

    ItemId GetParent(ItemId item) const override
    {
        (void)m_Positions.at(item);
        return 0;
    }

    std::optional<std::size_t> GetPosition(ItemId item) const override
    {
        const auto found = m_Positions.find(item);
        return found == m_Positions.end() ? std::nullopt : std::optional<std::size_t>(found->second);
    }

    ModelValue GetValue(ItemId item, std::size_t column, ModelRole role) const override
    {
        const auto& row = m_Rows.at(m_Positions.at(item));
        if (column >= m_Columns.size())
        {
            throw std::out_of_range("Unknown page column.");
        }
        if (role == ModelRole::Display)
        {
            return row.m_Display.at(column);
        }
        if (role == ModelRole::Edit)
        {
            return row.m_Values.at(column);
        }
        if (role == ModelRole::Checked && std::holds_alternative<bool>(row.m_Values[column]))
        {
            return row.m_Values[column];
        }
        return {};
    }
};

} // namespace

struct AdapterModel::Data : std::enable_shared_from_this<Data>
{
    struct Slot
    {
        std::mutex m_Mutex;
        std::optional<AdapterPage> m_Page;
        std::shared_ptr<const std::vector<ModelDataSet>> m_Catalog;
        std::exception_ptr m_Error;
        bool m_Ready = false, m_Write = false, m_Persisted = false;
    };

    AdapterModel* m_Owner;
    UiContext& m_Ui;
    UiDispatcher m_Dispatcher;
    std::shared_ptr<const ModelAdapter> m_Adapter;
    BackgroundTasks& m_Workers;
    State<AdapterStatus> m_Status;
    State<std::shared_ptr<const std::vector<ModelDataSet>>> m_Catalog;
    std::vector<ScopedConnection> m_Connections;
    RequestScope m_ReadScope;
    Task m_Job;
    std::shared_ptr<Slot> m_Slot;
    std::atomic_bool m_Ready = false;
    std::unordered_map<std::string, ItemId> m_Ids;
    std::unordered_map<ItemId, std::string> m_Rows;
    ModelEditSession m_Edit;
    ModelQuery m_Query;
    std::string m_IdentityDataSet;
    bool m_Pumping = false;
    std::exception_ptr m_CallbackError;

    Data(AdapterModel& owner, UiContext& ui, std::shared_ptr<const ModelAdapter> adapter, BackgroundTasks& workers)
        : m_Owner(&owner), m_Ui(ui), m_Dispatcher(ui.GetDispatcher()), m_Adapter(std::move(adapter)),
          m_Workers(workers), m_Status(ui.States(), AdapterStatus{}),
          m_Catalog(ui.States(), std::make_shared<const std::vector<ModelDataSet>>())
    {
    }

    void Check() const
    {
        m_Dispatcher.VerifyAccess();
        if (!m_Owner)
        {
            throw std::logic_error("The adapter model has closed.");
        }
        m_Owner->VerifyAccess();
    }

    void CheckMutation() const
    {
        Check();
        if (m_Ui.GetPhase() != UiPhase::Idle || StateStore::IsUpdatingOnThisThread())
        {
            throw std::logic_error("Request adapter operations from an idle owner command; defer requests made during "
                                   "traversal or state updates.");
        }
    }

    void Fail(std::string error, bool persisted = false)
    {
        auto status = m_Status.Get();
        status.m_Operation = AdapterOperation::Idle;
        status.m_Error = std::move(error);
        try
        {
            m_Status.Set(std::move(status));
        }
        catch (...)
        {
        }
        auto edit = std::exchange(m_Edit, {});
        edit.Complete({persisted ? ModelEditStatus::Committed : ModelEditStatus::Failed, m_Status.Get().m_Error});
    }

    void Publish(AdapterPage page)
    {
        page.m_Query.Validate(page.m_DataSet.m_Columns.size());
        if (page.m_Query.m_DataSet != m_Query.m_DataSet || page.m_Rows.size() > m_Query.m_Limit ||
            page.m_DataSet.m_Columns.size() > 2048 || page.m_MatchedRows > page.m_TotalRows ||
            page.m_Query.m_Offset > page.m_MatchedRows ||
            page.m_Rows.size() > page.m_MatchedRows - page.m_Query.m_Offset)
        {
            throw std::invalid_argument("The adapter returned an inconsistent bounded page.");
        }
        auto expectedQuery = m_Query;
        expectedQuery.m_Offset = page.m_Query.m_Offset;
        if (page.m_Query != expectedQuery || page.m_DataSet.m_Id != page.m_Query.m_DataSet ||
            page.m_Query.m_SchemaRevision != page.m_DataSet.m_SchemaRevision)
        {
            throw std::invalid_argument("The adapter returned a page for another query/schema.");
        }
        const auto identityDomain = page.m_Query.m_DataSet + '\0' + std::to_string(page.m_Query.m_SchemaRevision);
        const bool sameDomain = page.m_DataSet.m_StableKeys && m_IdentityDataSet == identityDomain;
        std::vector<ItemId> ids;
        std::unordered_map<ItemId, std::string> rows;
        std::unordered_map<std::string, ItemId> pageIds;
        std::size_t bytes = 0;
        for (auto& row : page.m_Rows)
        {
            if (row.m_Key.empty() || row.m_Key.size() > 65536 ||
                row.m_Values.size() != page.m_DataSet.m_Columns.size() || pageIds.contains(row.m_Key))
            {
                throw std::invalid_argument("Adapter rows require bounded unique keys and rectangular values.");
            }
            bytes += row.m_Key.size();
            for (const auto& value : row.m_Values)
            {
                bytes += sizeof(ModelValue);
                if (const auto* text = std::get_if<std::string>(&value))
                {
                    bytes += text->size();
                }
                if (const auto* blob = std::get_if<ModelBlob>(&value))
                {
                    bytes += blob->size();
                }
                if (bytes > 32 * 1024 * 1024)
                {
                    throw std::length_error("An adapter page exceeds the 32 MiB publication budget.");
                }
            }
            const auto found = sameDomain ? m_Ids.find(row.m_Key) : m_Ids.end();
            const ItemId id = found == m_Ids.end() ? AllocateItemId() : found->second;
            pageIds.emplace(row.m_Key, id);
            ids.push_back(id);
            rows.emplace(id, row.m_Key);
        }
        const auto snapshot = ModelSnapshot(
            std::make_shared<const PageData>(page.m_DataSet.m_Columns, std::move(page.m_Rows), std::move(ids)));
        auto status = m_Status.Get();
        status.m_Operation = AdapterOperation::Idle;
        status.m_Query = page.m_Query;
        status.m_TotalRows = page.m_TotalRows;
        status.m_MatchedRows = page.m_MatchedRows;
        status.m_LoadedRows = snapshot.GetRowCount();
        status.m_Error.clear();
        auto previous = std::exchange(m_Rows, std::move(rows));
        const auto oldQuery = m_Query;
        const auto oldIdentity = m_IdentityDataSet;
        std::optional<std::unordered_map<std::string, ItemId>> previousIds;
        std::vector<std::string> inserted;
        m_Query = page.m_Query;
        m_IdentityDataSet = identityDomain;
        if (!sameDomain || m_Ids.size() + pageIds.size() > 32768)
        {
            previousIds.emplace(std::move(m_Ids));
            m_Ids = std::move(pageIds);
        }
        else
        {
            for (const auto& [key, id] : pageIds)
            {
                if (m_Ids.emplace(key, id).second)
                {
                    inserted.push_back(key);
                }
            }
        }
        try
        {
            m_Ui.States().Batch(
                [&]
                {
                    m_Owner->ItemModel::Replace(snapshot);
                    m_Status.Set(std::move(status));
                });
        }
        catch (...)
        {
            // A failed final-candidate validation rolls back every lookup too.
            // Post-commit observer errors preserve the confirmed publication.
            if (m_Owner && m_Owner->GetSnapshot() != snapshot)
            {
                m_Rows = std::move(previous);
                m_Query = oldQuery;
                m_IdentityDataSet = oldIdentity;
                if (previousIds)
                {
                    m_Ids = std::move(*previousIds);
                }
                else
                {
                    for (const auto& key : inserted)
                    {
                        m_Ids.erase(key);
                    }
                }
            }
            throw;
        }
    }

    void Pump()
    {
        if (!m_Owner || m_Pumping || !m_Ready.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }
        const auto lease = shared_from_this();

        struct Scope
        {
            bool& m_Flag;

            ~Scope()
            {
                m_Flag = false;
            }
        } scope{m_Pumping};

        m_Pumping = true;
        const auto slot = m_Slot;
        if (!slot)
        {
            return;
        }
        std::optional<AdapterPage> page;
        std::shared_ptr<const std::vector<ModelDataSet>> catalog;
        std::exception_ptr error;
        bool persisted = false;
        {
            const std::lock_guard lock(slot->m_Mutex);
            if (!slot->m_Ready)
            {
                return;
            }
            page = std::move(slot->m_Page);
            catalog = std::move(slot->m_Catalog);
            error = slot->m_Error;
            persisted = slot->m_Persisted;
        }
        m_Slot.reset();
        if (error)
        {
            Fail(ErrorText(error), persisted);
            return;
        }
        try
        {
            if (catalog)
            {
                if (catalog->size() > 4096)
                {
                    throw std::length_error("An adapter catalog accepts at most 4096 datasets.");
                }
                std::unordered_set<std::string> names;
                for (const auto& dataSet : *catalog)
                {
                    if (dataSet.m_Id.empty() || dataSet.m_Id.size() > 4096 || !names.insert(dataSet.m_Id).second ||
                        dataSet.m_Columns.size() > 2048)
                    {
                        throw std::invalid_argument(
                            "Adapter catalogs require bounded unique dataset names and schemas.");
                    }
                }
                m_Catalog.Set(catalog);
                if (!m_Owner)
                {
                    return;
                }
                if (catalog->empty())
                {
                    // Retain the last field schema so compatible projections can
                    // discard all records without acquiring invalid sort columns.
                    const auto empty = std::move(ModelBuilder(m_Owner->GetSnapshot().GetColumns())).Build();
                    auto oldRows = std::exchange(m_Rows, {});
                    auto oldIds = std::exchange(m_Ids, {});
                    auto oldQuery = std::exchange(m_Query, {});
                    auto oldDomain = std::exchange(m_IdentityDataSet, {});
                    try
                    {
                        m_Ui.States().Batch(
                            [&]
                            {
                                m_Owner->ItemModel::Replace(empty);
                                m_Status.Set(AdapterStatus{});
                            });
                    }
                    catch (...)
                    {
                        if (m_Owner && m_Owner->GetSnapshot() != empty)
                        {
                            m_Rows = std::move(oldRows);
                            m_Ids = std::move(oldIds);
                            m_Query = std::move(oldQuery);
                            m_IdentityDataSet = std::move(oldDomain);
                        }
                        throw;
                    }
                }
                else
                {
                    auto query = m_Query;
                    if (std::none_of(catalog->begin(), catalog->end(),
                                     [&](const ModelDataSet& item) { return item.m_Id == query.m_DataSet; }))
                    {
                        query = {};
                        query.m_DataSet = catalog->front().m_Id;
                    }
                    query.m_SchemaRevision = 0;
                    m_Owner->SetQuery(std::move(query));
                }
            }
            else if (page)
            {
                Publish(std::move(*page));
                auto edit = std::exchange(m_Edit, {});
                edit.Complete({ModelEditStatus::Committed, {}});
            }
        }
        catch (...)
        {
            if (m_Owner)
            {
                Fail(ErrorText(std::current_exception()), persisted);
            }
        }
    }

    template <class Work> void Start(AdapterOperation operation, Work work)
    {
        CheckMutation();
        const auto slot = std::make_shared<Slot>();
        slot->m_Write = operation == AdapterOperation::Committing;
        m_Slot = slot;
        auto status = m_Status.Get();
        status.m_Operation = operation;
        status.m_Error.clear();
        const auto weak = weak_from_this();
        const auto dispatcher = m_Dispatcher;
        // The result has a reserved slot before admission. Normal task delivery is
        // merely an optimization; the existing idle turn also consumes the slot.
        m_Job = m_Workers.RunLatest(
            m_ReadScope, m_Owner->GetHandle(),
            [slot, weak, dispatcher, work = std::move(work)](std::stop_token stop)
            {
                try
                {
                    work(*slot, stop);
                    if (slot->m_Page)
                    {
                        PrepareDisplay(*slot->m_Page, stop);
                    }
                }
                catch (...)
                {
                    slot->m_Error = std::current_exception();
                }
                {
                    const std::lock_guard lock(slot->m_Mutex);
                    slot->m_Ready = true;
                }
                if (const auto live = weak.lock())
                {
                    live->m_Ready.store(true, std::memory_order_release);
                }
                dispatcher.Wake();
                return true;
            },
            [weak](ItemModel&, bool)
            {
                if (const auto live = weak.lock())
                {
                    live->Pump();
                }
            });
        if (m_Job.IsReady() && !m_Ready.load(std::memory_order_acquire))
        {
            const auto state = m_Job.GetStatus();
            if (state == TaskStatus::Rejected || state == TaskStatus::Failed || state == TaskStatus::Cancelled ||
                state == TaskStatus::TargetExpired)
            {
                m_Slot.reset();
                Fail("The application worker executor rejected the adapter request.");
                return;
            }
        }
        // Publish last: a status observer may start a newer request without this
        // call subsequently replacing its slot or task handle.
        try
        {
            m_Status.Set(std::move(status));
        }
        catch (...)
        {
            m_CallbackError = std::current_exception();
        }
    }
};

AdapterModel::AdapterModel(UiContext& ui, std::shared_ptr<const ModelAdapter> adapter, BackgroundTasks& workers)
    : ItemModel(ui.States()), m_Data(std::make_shared<Data>(*this, ui, std::move(adapter), workers))
{
}

std::shared_ptr<AdapterModel> AdapterModel::Create(UiContext& ui, std::shared_ptr<const ModelAdapter> adapter,
                                                   BackgroundTasks& workers)
{
    if (!adapter)
    {
        throw std::invalid_argument("A model adapter cannot be empty.");
    }
    auto model = std::shared_ptr<AdapterModel>(new AdapterModel(ui, std::move(adapter), workers));
    model->m_Data->m_Connections.emplace_back(model->AddValidator(
        [](const ModelSnapshot& snapshot)
        {
            if (snapshot.GetItemCount() > 2048 || snapshot.GetColumns().size() > 2048)
            {
                throw std::length_error("An adapter publishes only bounded result windows.");
            }
            for (std::size_t row = 0; row < snapshot.GetItemCount(); ++row)
            {
                for (std::size_t column = 0; column < snapshot.GetColumns().size(); ++column)
                {
                    ValidateModelValue(snapshot.GetColumns()[column],
                                       snapshot.GetValue(snapshot.GetItemByIndex(row), column));
                }
            }
        }));
    const auto weak = std::weak_ptr<Data>(model->m_Data);
    model->m_Data->m_Connections.emplace_back(ui.IdleTurn().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                live->Pump();
            }
        }));
    model->Refresh();
    return model;
}

AdapterModel::~AdapterModel()
{
    const auto data = m_Data;
    data->m_Owner = nullptr;
    data->m_Connections.clear();
    data->m_ReadScope.Cancel();
    data->m_Job.Cancel();
    data->m_Edit.Complete({ModelEditStatus::OwnerClosed, "The adapter model closed."});
}

std::shared_ptr<const std::vector<ModelDataSet>> AdapterModel::GetCatalog() const
{
    m_Data->Check();
    return m_Data->m_Catalog.Get();
}

AdapterStatus AdapterModel::GetStatus() const
{
    m_Data->Check();
    return m_Data->m_Status.Get();
}

std::exception_ptr AdapterModel::GetCallbackError() const
{
    m_Data->Check();
    return m_Data->m_CallbackError;
}

Connection AdapterModel::SubscribeStatus(std::function<void(AdapterStatus)> callback)
{
    m_Data->Check();
    if (!callback)
    {
        throw std::invalid_argument("An adapter status observer cannot be empty.");
    }
    return m_Data->m_Status.Subscribe([callback = std::move(callback)](const StateChange<AdapterStatus>& change)
                                      { callback(change.GetAfter()); });
}

void AdapterModel::SetQuery(ModelQuery query)
{
    const auto data = m_Data;
    data->CheckMutation();
    if (data->m_Status.Get().m_Operation == AdapterOperation::Committing)
    {
        throw std::logic_error("Wait for the current edit to finish before changing the query.");
    }
    const auto catalog = data->m_Catalog.Get();
    const auto found = std::find_if(catalog->begin(), catalog->end(),
                                    [&](const ModelDataSet& item) { return item.m_Id == query.m_DataSet; });
    if (found == catalog->end())
    {
        throw std::invalid_argument("Unknown adapter dataset.");
    }
    query.Validate(found->m_Columns.size());
    if (query.m_SchemaRevision && query.m_SchemaRevision != found->m_SchemaRevision)
    {
        throw std::logic_error("The query schema is stale; refresh the catalog.");
    }
    query.m_SchemaRevision = found->m_SchemaRevision;
    data->m_Query = query;
    const auto adapter = data->m_Adapter;
    data->Start(AdapterOperation::Loading, [adapter, query = std::move(query)](Data::Slot& slot, std::stop_token stop)
                { slot.m_Page = adapter->Load(query, stop); });
}

void AdapterModel::RefreshCatalog()
{
    const auto data = m_Data;
    data->CheckMutation();
    if (data->m_Status.Get().m_Operation == AdapterOperation::Committing)
    {
        throw std::logic_error("Wait for the current edit before reloading schemas.");
    }
    const auto adapter = data->m_Adapter;
    data->Start(AdapterOperation::Catalog, [adapter](Data::Slot& slot, std::stop_token stop)
                { slot.m_Catalog = std::make_shared<const std::vector<ModelDataSet>>(adapter->GetDataSets(stop)); });
}

void AdapterModel::Refresh()
{
    const auto data = m_Data;
    data->CheckMutation();
    if (!data->m_Query.m_DataSet.empty())
    {
        SetQuery(data->m_Query);
        return;
    }
    RefreshCatalog();
}

void AdapterModel::CancelLoading()
{
    const auto data = m_Data;
    data->CheckMutation();
    if (data->m_Status.Get().m_Operation == AdapterOperation::Committing)
    {
        throw std::logic_error("A database write needs its confirmed terminal outcome.");
    }
    data->m_ReadScope.Cancel();
    data->m_Job.Cancel();
    data->m_Slot.reset();
    auto status = data->m_Status.Get();
    status.m_Operation = AdapterOperation::Idle;
    status.m_Error.clear();
    data->m_Query = status.m_Query;
    data->m_Status.Set(status);
}

void AdapterModel::SetPage(std::uint64_t page)
{
    auto query = GetStatus().m_Query;
    if (page > static_cast<std::uint64_t>(INT64_MAX) / query.m_Limit)
    {
        throw std::out_of_range("Page offset overflow.");
    }
    query.m_Offset = page * query.m_Limit;
    SetQuery(std::move(query));
}

std::string AdapterModel::GetRowKey(ItemId item) const
{
    m_Data->Check();
    return m_Data->m_Rows.at(item);
}

bool AdapterModel::Apply(std::vector<ModelEdit>, std::optional<std::uint64_t>)
{
    VerifyAccess();
    throw std::logic_error("Adapter edits are asynchronous; use RequestEdit and its confirmed result.");
}

ModelEditSession AdapterModel::RequestEdit(std::vector<ModelEdit> edits, std::optional<std::uint64_t> expectedRevision)
{
    const auto data = m_Data;
    data->CheckMutation();
    auto session = ModelEditSession::Create();
    try
    {
        if (data->m_Status.Get().m_Operation != AdapterOperation::Idle)
        {
            throw std::logic_error("Wait for the active adapter request before editing.");
        }
        if (expectedRevision && *expectedRevision != GetRevision())
        {
            throw std::logic_error("The adapter page changed before the edit was accepted.");
        }
        if (edits.empty() || edits.size() > 4096)
        {
            throw std::length_error("An adapter edit accepts 1..4096 fields.");
        }
        const auto snapshot = GetSnapshot();
        std::vector<AdapterEdit> writes;
        std::set<std::pair<ItemId, std::size_t>> fields;
        for (auto& edit : edits)
        {
            if (!fields.emplace(edit.m_Item, edit.m_Column).second || edit.m_Role != ModelRole::Edit)
            {
                throw std::invalid_argument("An adapter edit requires distinct raw fields.");
            }
            const auto& column = snapshot.GetColumns().at(edit.m_Column);
            if (!column.m_Editable)
            {
                throw std::logic_error("The source column is read-only.");
            }
            ValidateModelValue(column, edit.m_Value);
            writes.push_back({GetRowKey(edit.m_Item), edit.m_Column, snapshot.GetValue(edit.m_Item, edit.m_Column),
                              std::move(edit.m_Value)});
        }
        const auto adapter = data->m_Adapter;
        const auto query = data->m_Query;
        data->m_Edit = session;
        data->Start(AdapterOperation::Committing,
                    [adapter, query, writes = std::move(writes)](Data::Slot& slot, std::stop_token stop)
                    {
                        slot.m_Page = adapter->Commit(query, writes, stop);
                        slot.m_Persisted = true;
                    });
    }
    catch (...)
    {
        session.Complete({ModelEditStatus::Failed, ErrorText(std::current_exception())});
    }
    return session;
}

bool AdapterModel::Replace(ModelSnapshot, std::optional<std::uint64_t>)
{
    VerifyAccess();
    throw std::logic_error("Refresh the adapter instead of replacing source-owned rows.");
}

void AdapterModel::Restructure(const std::function<void(ModelBuilder&)>&)
{
    VerifyAccess();
    throw std::logic_error("This source has no implicit row order; use an explicit source structural adapter.");
}
} // namespace EStudioUI
