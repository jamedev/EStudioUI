#include <EStudioUI/ItemModel.h>
#include <EStudioUI/SelectionModel.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace EStudioUI
{
namespace
{
void ValidateSchema(const Detail::ModelState& state)
{
    const auto& snapshot = state.m_Snapshot;
    if (snapshot.GetItemCount() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        snapshot.GetColumns().size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("This model exceeds the supported signed 32-bit view index domain.");
    for (const auto& column : snapshot.GetColumns())
    {
        if (column.m_ValueType > ModelValueType::Color ||
            (column.m_WidthHint && (!std::isfinite(*column.m_WidthHint) || *column.m_WidthHint < 0)))
            throw std::invalid_argument("Model schema requires a known value type and finite nonnegative width hints.");
    }
    for (const auto& field : state.m_Fields)
        if (field.m_Role == ModelRole::Edit && snapshot.Contains(field.m_Item))
            ValidateModelValue(snapshot.GetColumns().at(field.m_Column),
                               snapshot.GetValue(field.m_Item, field.m_Column));
}

void NormalizeFields(std::vector<ModelField>& fields)
{
    std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b)
              { return std::tie(a.m_Item, a.m_Column, a.m_Role) < std::tie(b.m_Item, b.m_Column, b.m_Role); });
    fields.erase(std::unique(fields.begin(), fields.end()), fields.end());
}
} // namespace

ItemModel::ItemModel(StateStore& store, ModelSnapshot initial)
    : m_Store(store), m_State(
                          store,
                          [&]
                          {
                              Detail::ModelState value;
                              value.m_Snapshot = initial;
                              value.m_Base = initial;
                              return value;
                          }(),
                          ValidateSchema, Equivalent)
{
}

ItemModel::~ItemModel()
{
    // State teardown checks owner affinity even after the containing store closes.
    if (!m_Affinity.IsOwnerThread())
        std::terminate();
    *m_Alive = false;
}

void ItemModel::VerifyAccess() const
{
    (void)m_State.GetDispatcher();
}

ModelSnapshot ItemModel::GetSnapshot() const
{
    return m_State.Get().m_Snapshot;
}

std::uint64_t ItemModel::GetRevision() const
{
    return m_State.GetRevision();
}

std::shared_ptr<ItemModel> ItemModel::GetRootModel()
{
    VerifyAccess();
    return shared_from_this();
}

std::shared_ptr<ItemModel> ItemModel::GetSourceModel() const
{
    VerifyAccess();
    return {};
}

std::uint64_t ItemModel::GetIdentity() const
{
    return m_State.GetHandle().GetTargetKey();
}

StateStore& ItemModel::GetStore() const
{
    VerifyAccess();
    return m_Store;
}

bool ItemModel::Equivalent(const Detail::ModelState& left, const Detail::ModelState& right)
{
    if (left.m_Snapshot == right.m_Snapshot)
        return true;
    if (right.m_SourceRevision)
        return false;
    if (right.m_Reset || right.m_Structure || !(right.m_Base == left.m_Snapshot))
        return false;
    for (const auto& field : right.m_Fields)
    {
        if (!left.m_Snapshot.Contains(field.m_Item) && !right.m_Snapshot.Contains(field.m_Item))
            continue;
        if (left.m_Snapshot.Contains(field.m_Item) != right.m_Snapshot.Contains(field.m_Item) ||
            left.m_Snapshot.GetValue(field.m_Item, field.m_Column, field.m_Role) !=
                right.m_Snapshot.GetValue(field.m_Item, field.m_Column, field.m_Role))
            return false;
    }
    return true;
}

bool ItemModel::Publish(ModelSnapshot snapshot, std::vector<ModelField> fields, bool reset, bool structure,
                        std::optional<std::uint64_t> expectedRevision, std::optional<std::uint64_t> sourceRevision)
{
    VerifyAccess();
    bool changed = false;
    // Keep the complete operation within the existing transaction/frame/lifetime guard.
    m_Store.Batch(
        [&]
        {
            if (expectedRevision && *expectedRevision != GetRevision())
                throw std::logic_error("Obsolete model publication: the expected source revision no longer matches.");
            auto candidate = m_State.Get();
            const auto revision = m_State.GetRevision();
            if (candidate.m_BaseRevision != revision)
            {
                candidate.m_Base = candidate.m_Snapshot;
                candidate.m_BaseRevision = revision;
                candidate.m_Fields.clear();
                candidate.m_Reset = candidate.m_Structure = false;
            }
            candidate.m_Snapshot = std::move(snapshot);
            candidate.m_Fields.insert(candidate.m_Fields.end(), fields.begin(), fields.end());
            NormalizeFields(candidate.m_Fields);
            candidate.m_Reset = candidate.m_Reset || reset;
            candidate.m_Structure = candidate.m_Structure || structure;
            candidate.m_SourceRevision = sourceRevision;
            std::vector<std::shared_ptr<SelectionModel>> selections;
            if (!sourceRevision && (reset || structure))
            {
                std::erase_if(m_Selections, [](const auto& entry) { return entry.expired(); });
                for (const auto& entry : m_Selections)
                    if (auto selection = entry.lock())
                        selections.push_back(std::move(selection));
            }
            const auto data = candidate.m_Snapshot;
            changed = m_State.Set(std::move(candidate));
            for (const auto& selection : selections)
                selection->Prune(data);
        });
    return changed;
}

bool ItemModel::SetValue(ItemId item, std::size_t column, ModelValue value)
{
    return Apply({ModelEdit{item, column, std::move(value)}});
}

bool ItemModel::SetText(ItemId item, std::size_t column, std::string_view text)
{
    const auto snapshot = GetSnapshot();
    auto parser = snapshot.GetColumns().at(column);
    parser.m_Validate = {};
    return SetValue(item, column, ParseModelValue(parser, text));
}

bool ItemModel::Apply(std::vector<ModelEdit> edits, std::optional<std::uint64_t> expectedRevision)
{
    VerifyAccess();
    bool changed = false;
    m_Store.Batch(
        [&]
        {
            if (expectedRevision && *expectedRevision != GetRevision())
                throw std::logic_error("Obsolete model edit batch: expected revision no longer matches.");
            const auto before = GetSnapshot();
            const auto after = before.StageEdits(edits);
            std::vector<ModelField> fields;
            std::set<ItemId> touched;
            for (const auto& edit : edits)
            {
                touched.insert(edit.m_Item);
                if (edit.m_Role != ModelRole::Edit)
                    fields.push_back({edit.m_Item, edit.m_Column, edit.m_Role});
            }
            for (const auto item : touched)
                for (std::size_t column = 0; column < after.GetColumns().size(); ++column)
                    if (before.GetValue(item, column) != after.GetValue(item, column))
                        fields.push_back({item, column, ModelRole::Edit});
            changed = Publish(after, std::move(fields), false, false, expectedRevision);
        });
    return changed;
}

bool ItemModel::Replace(ModelSnapshot snapshot, std::optional<std::uint64_t> expectedRevision)
{
    return Publish(std::move(snapshot), {}, true, true, expectedRevision);
}

void ItemModel::Restructure(const std::function<void(ModelBuilder&)>& edit)
{
    VerifyAccess();
    m_Store.Batch(
        [&]
        {
            ModelBuilder builder(GetSnapshot());
            edit(builder);
            Publish(std::move(builder).Build(), {}, false, true, {});
        });
}

UiHandle<ItemModel> ItemModel::GetHandle() const
{
    const auto endpoint = m_State.GetHandle();
    return UiHandle<ItemModel>::ForTarget(
        endpoint.GetDispatcher(), endpoint.GetTargetKey(),
        [alive = std::weak_ptr<bool>(m_Alive), self = const_cast<ItemModel*>(this)]
        {
            const auto current = alive.lock();
            return current && *current ? self : nullptr;
        },
        endpoint.GetTargetStopToken());
}

Connection ItemModel::AddValidator(std::function<void(const ModelSnapshot&)> validator)
{
    return m_State.AddValidator([validator = std::move(validator)](const Detail::ModelState& value)
                                { validator(value.m_Snapshot); });
}

Connection ItemModel::AddChangeValidator(std::function<void(const ModelChange&)> validator)
{
    return m_State.AddValidator(
        [key = GetIdentity(), validator = std::move(validator)](const Detail::ModelState& value)
        {
            validator(
                ModelChange{value.m_Base, value.m_Snapshot, 0, value.m_Fields, value.m_Reset, value.m_Structure, key});
        });
}

ModelChange ItemModel::PreviewChange(const ModelChange& canonical) const
{
    VerifyAccess();
    if (canonical.m_ModelKey && canonical.m_ModelKey != GetIdentity())
        throw std::invalid_argument("A model preview must originate in this model's adapter chain.");
    return canonical;
}

bool ItemModel::PublishProjection(ModelSnapshot snapshot, const ModelChange& change, bool reordered)
{
    return Publish(std::move(snapshot), change.m_Fields, change.m_Reset, change.m_Structure || reordered, {},
                   change.m_Revision);
}
} // namespace EStudioUI
