#include <EStudioUI/SortFilterModel.h>
#include <EStudioUI/RecordModel.h>

#include <unordered_set>

namespace EStudioUI
{
namespace
{
struct Options
{
    std::uint64_t m_Key = AllocateItemId();
    std::optional<std::size_t> m_Sort;
    bool m_Ascending = true;
    std::vector<ModelSort> m_Order;
    std::function<bool(const ModelSnapshot&, ItemId)> m_Filter;
    std::vector<std::size_t> m_Dependencies;
};

class ProjectionData final : public ModelData
{
  public:
    ModelSnapshot m_Source;
    Options m_Options;
    std::shared_ptr<const Detail::RecordIndex> m_Index;
    ProjectionStats m_Stats;

    ProjectionData(ModelSnapshot source, Options options, const ProjectionData* reuse = nullptr)
        : m_Source(std::move(source)), m_Options(std::move(options))
    {
        if (reuse)
        {
            m_Index = reuse->m_Index;
            m_Stats.m_ReusedMapping = true;
            return;
        }
        if (!m_Options.m_Sort && !m_Options.m_Filter)
            return;
        if (m_Options.m_Sort)
            (void)m_Source.GetColumns().at(*m_Options.m_Sort);
        for (const auto& key : m_Options.m_Order)
        {
            (void)m_Source.GetColumns().at(key.m_Column);
        }
        for (const auto column : m_Options.m_Dependencies)
            (void)m_Source.GetColumns().at(column);
        std::unordered_set<ItemId> included;
        std::unordered_set<ItemId> visited;
        for (std::size_t index = 0; index < m_Source.GetItemCount(); ++index)
        {
            const auto item = m_Source.GetItemByIndex(index);
            ++m_Stats.m_Scanned;
            if (m_Options.m_Filter && !m_Options.m_Filter(m_Source, item))
                continue;
            auto current = item;
            visited.clear();
            while (current && !included.contains(current))
            {
                if (!visited.insert(current).second || !m_Source.Contains(current))
                    throw std::invalid_argument("Projection source contains an invalid parent chain.");
                included.insert(current);
                current = m_Source.GetParent(current);
            }
        }
        std::unordered_map<ItemId, std::vector<ItemId>> siblings;
        for (std::size_t index = 0; index < m_Source.GetItemCount(); ++index)
        {
            const auto item = m_Source.GetItemByIndex(index);
            if (included.contains(item))
                siblings[m_Source.GetParent(item)].push_back(item);
        }
        if (m_Options.m_Sort)
        {
            const auto keys = m_Options.m_Order.empty()
                                  ? std::vector<ModelSort>{{*m_Options.m_Sort, m_Options.m_Ascending, false}}
                                  : m_Options.m_Order;
            for (auto& [parent, children] : siblings)
            {
                (void)parent;
                std::stable_sort(children.begin(), children.end(),
                                 [&](ItemId a, ItemId b)
                                 {
                                     ++m_Stats.m_Comparisons;
                                     for (const auto& key : keys)
                                     {
                                         const auto left = m_Source.GetValue(a, key.m_Column),
                                                    right = m_Source.GetValue(b, key.m_Column);
                                         const bool leftNull = !left.index(), rightNull = !right.index();
                                         if (leftNull != rightNull)
                                         {
                                             return key.m_NullsLast ? !leftNull : leftNull;
                                         }
                                         const int result = CompareModelValues(left, right);
                                         if (result)
                                         {
                                             return key.m_Ascending ? result < 0 : result > 0;
                                         }
                                     }
                                     return false;
                                 });
            }
        }
        std::vector<ItemId> items, parents;
        // Traverse iteratively so deeply nested application hierarchies do not consume the call stack.
        std::vector<ItemId> pending;
        if (const auto found = siblings.find(0); found != siblings.end())
            pending.assign(found->second.rbegin(), found->second.rend());
        while (!pending.empty())
        {
            const auto item = pending.back();
            pending.pop_back();
            items.push_back(item);
            parents.push_back(m_Source.GetParent(item));
            if (const auto found = siblings.find(item); found != siblings.end())
                pending.insert(pending.end(), found->second.rbegin(), found->second.rend());
        }
        if (items.size() != included.size())
            throw std::invalid_argument("Projection source hierarchy does not reach its invisible root.");
        m_Index = Detail::MakeRecordIndex(std::move(items), std::move(parents));
    }

    const std::vector<ModelColumn>& GetColumns() const override
    {
        return m_Source.GetColumns();
    }

    std::size_t GetItemCount() const override
    {
        return m_Index ? m_Index->m_Items.size() : m_Source.GetItemCount();
    }

    ItemId GetItemByIndex(std::size_t index) const override
    {
        return m_Index ? m_Index->m_Items.at(index) : m_Source.GetItemByIndex(index);
    }

    bool Contains(ItemId item) const override
    {
        return m_Index ? m_Index->m_Offsets.contains(item) : m_Source.Contains(item);
    }

    std::size_t GetRowCount(ItemId parent) const override
    {
        if (!m_Index)
            return m_Source.GetRowCount(parent);
        const auto found = m_Index->m_Children.find(parent);
        return found == m_Index->m_Children.end() ? 0 : found->second.size();
    }

    ItemId GetItem(std::size_t row, ItemId parent) const override
    {
        return m_Index ? m_Index->m_Children.at(parent).at(row) : m_Source.GetItem(row, parent);
    }

    ItemId GetParent(ItemId item) const override
    {
        if (!Contains(item))
            throw std::out_of_range("Item is absent from this projection.");
        return m_Source.GetParent(item);
    }

    std::optional<std::size_t> GetPosition(ItemId item) const override
    {
        if (!m_Index)
            return m_Source.GetPosition(item);
        const auto found = m_Index->m_Offsets.find(item);
        return found == m_Index->m_Offsets.end() ? std::nullopt
                                                 : std::optional<std::size_t>(m_Index->m_Positions[found->second]);
    }

    ModelValue GetValue(ItemId item, std::size_t column, ModelRole role) const override
    {
        if (!Contains(item))
            throw std::out_of_range("Item is absent from this projection.");
        return m_Source.GetValue(item, column, role);
    }
};

std::shared_ptr<const ProjectionData> Data(const ModelSnapshot& snapshot)
{
    const auto result = std::dynamic_pointer_cast<const ProjectionData>(snapshot.GetData());
    if (!result)
        throw std::logic_error("Sort/filter storage was replaced outside its source publication contract.");
    return result;
}
} // namespace

SortFilterModel::SortFilterModel(std::shared_ptr<ItemModel> source)
    : ItemModel(source->GetStore(),
                ModelSnapshot(std::make_shared<const ProjectionData>(source->GetSnapshot(), Options{}))),
      m_Source(std::move(source))
{
}

std::shared_ptr<SortFilterModel> SortFilterModel::Create(std::shared_ptr<ItemModel> source)
{
    if (!source)
        throw std::invalid_argument("A sort/filter projection requires a live shared source model.");
    auto result = std::shared_ptr<SortFilterModel>(new SortFilterModel(std::move(source)));
    for (auto ancestor = result->m_Source; ancestor; ancestor = ancestor->GetSourceModel())
        result->m_SourceValidated.emplace_back(ancestor->AddChangeValidator(
            [weak = std::weak_ptr<SortFilterModel>(result)](const ModelChange& change)
            {
                if (auto live = weak.lock())
                    (void)live->PreviewChange(change);
            }));
    result->m_SourceChanged.emplace(result->m_Source->Subscribe(
        [weak = std::weak_ptr<SortFilterModel>(result)](const ModelChange& change)
        {
            if (auto live = weak.lock())
                live->Refresh(change);
        }));
    return result;
}

SortFilterModel::~SortFilterModel() = default;

std::uint64_t SortFilterModel::GetRevision() const
{
    VerifyAccess();
    return m_Source->GetRevision();
}

std::shared_ptr<ItemModel> SortFilterModel::GetRootModel()
{
    VerifyAccess();
    return m_Source->GetRootModel();
}

std::shared_ptr<ItemModel> SortFilterModel::GetSourceModel() const
{
    VerifyAccess();
    return m_Source;
}

bool SortFilterModel::Apply(std::vector<ModelEdit> edits, std::optional<std::uint64_t> expectedRevision)
{
    VerifyAccess();
    return m_Source->Apply(std::move(edits), expectedRevision);
}

ModelEditSession SortFilterModel::RequestEdit(std::vector<ModelEdit> edits,
                                              std::optional<std::uint64_t> expectedRevision)
{
    VerifyAccess();
    return m_Source->RequestEdit(std::move(edits), expectedRevision);
}

bool SortFilterModel::Replace(ModelSnapshot snapshot, std::optional<std::uint64_t> expectedRevision)
{
    VerifyAccess();
    return m_Source->Replace(std::move(snapshot), expectedRevision);
}

void SortFilterModel::Restructure(const std::function<void(ModelBuilder&)>& edit)
{
    VerifyAccess();
    m_Source->Restructure(edit);
}

void SortFilterModel::SetSort(std::optional<std::size_t> column, bool ascending)
{
    SetSortKeys(column ? std::vector<ModelSort>{{*column, ascending, false}} : std::vector<ModelSort>{});
}

std::vector<ModelSort> SortFilterModel::GetSortKeys() const
{
    VerifyAccess();
    return Data(GetSnapshot())->m_Options.m_Order;
}

void SortFilterModel::SetSortKeys(std::vector<ModelSort> keys)
{
    VerifyAccess();
    const auto old = Data(GetSnapshot());
    if (old->m_Options.m_Order == keys)
    {
        return;
    }
    ModelQuery query;
    query.m_DataSet = "projection";
    query.m_Sort = keys;
    query.Validate(m_Source->GetSnapshot().GetColumns().size());
    auto options = old->m_Options;
    options.m_Key = AllocateItemId();
    options.m_Order = std::move(keys);
    options.m_Sort =
        options.m_Order.empty() ? std::nullopt : std::optional<std::size_t>(options.m_Order.front().m_Column);
    options.m_Ascending = options.m_Order.empty() || options.m_Order.front().m_Ascending;
    const auto source = m_Source->GetSnapshot();
    auto next = ModelSnapshot(std::make_shared<const ProjectionData>(source, std::move(options)));
    PublishProjection(std::move(next), ModelChange{GetSnapshot(), source, GetRevision(), {}, false, true}, true);
}

void SortFilterModel::SetFilter(std::function<bool(const ModelSnapshot&, ItemId)> predicate,
                                std::vector<std::size_t> dependencies)
{
    auto options = Data(GetSnapshot())->m_Options;
    options.m_Key = AllocateItemId();
    options.m_Filter = std::move(predicate);
    options.m_Dependencies = std::move(dependencies);
    const auto source = m_Source->GetSnapshot();
    auto next = ModelSnapshot(std::make_shared<const ProjectionData>(source, std::move(options)));
    PublishProjection(std::move(next), ModelChange{GetSnapshot(), source, GetRevision(), {}, false, true}, true);
}

void SortFilterModel::Refresh(const ModelChange& change)
{
    const auto prepared = PreviewChange(change);
    auto published = prepared;
    published.m_Revision = change.m_Revision;
    PublishProjection(published.m_After, published, published.m_Structure);
}

ModelChange SortFilterModel::PreviewChange(const ModelChange& canonical) const
{
    if (canonical.m_ModelKey == GetIdentity())
        return canonical;
    const auto rules = GetSnapshot();
    const auto sourceRules = SourceRules();
    if (m_Prepared && m_Prepared->m_Canonical == canonical.m_After && m_Prepared->m_Rules == rules &&
        m_Prepared->m_SourceRules == sourceRules)
        return m_Prepared->m_Change;
    const auto change = m_Source->PreviewChange(canonical);
    if (m_Prepared && m_Prepared->m_Rules == rules && m_Prepared->m_SourceRules == sourceRules &&
        Data(m_Prepared->m_Change.m_After)->m_Source == change.m_After)
        return m_Prepared->m_Change;
    const auto old = Data(rules);
    if (old->m_Source == change.m_After)
        return ModelChange{rules, rules, canonical.m_Revision, change.m_Fields, change.m_Reset, change.m_Structure};
    bool rebuild = change.m_Reset || change.m_Structure;
    for (const auto& field : change.m_Fields)
    {
        rebuild = rebuild || std::any_of(old->m_Options.m_Order.begin(), old->m_Options.m_Order.end(),
                                         [&](const ModelSort& key) { return key.m_Column == field.m_Column; });
        if (old->m_Options.m_Filter)
            rebuild = rebuild || old->m_Options.m_Dependencies.empty() ||
                      std::find(old->m_Options.m_Dependencies.begin(), old->m_Options.m_Dependencies.end(),
                                field.m_Column) != old->m_Options.m_Dependencies.end();
    }
    const auto next = ModelSnapshot(
        std::make_shared<const ProjectionData>(change.m_After, old->m_Options, rebuild ? nullptr : old.get()));
    ModelChange result{rules, next, canonical.m_Revision, change.m_Fields, change.m_Reset, rebuild, GetIdentity()};
    std::erase_if(result.m_Fields, [&](const ModelField& field)
                  { return !rules.Contains(field.m_Item) && !next.Contains(field.m_Item); });
    m_Prepared = Prepared{canonical.m_After, rules, sourceRules, result};
    return result;
}

std::vector<std::uint64_t> SortFilterModel::SourceRules() const
{
    std::vector<std::uint64_t> result;
    auto source = m_Source;
    while (const auto* projection = dynamic_cast<const SortFilterModel*>(source.get()))
    {
        result.push_back(Data(projection->GetSnapshot())->m_Options.m_Key);
        source = projection->m_Source;
    }
    return result;
}

ProjectionStats SortFilterModel::GetPreparationStats() const
{
    return Data(GetSnapshot())->m_Stats;
}
} // namespace EStudioUI
