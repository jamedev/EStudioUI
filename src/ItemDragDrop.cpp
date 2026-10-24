#include <EStudioUI/ItemDragDrop.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace EStudioUI
{
void MoveModelItems(std::shared_ptr<ItemModel> model, const std::vector<ItemId>& items, ItemId parent, ItemId before,
                    std::optional<std::uint64_t> expectedRevision)
{
    if (!model || items.empty() || items.size() > 4096)
    {
        throw std::invalid_argument("A move requires a model and 1..4096 identities.");
    }
    model = model->GetRootModel();
    if (expectedRevision && *expectedRevision != model->GetRevision())
    {
        throw std::logic_error("The source changed during the drag; retry against its current records.");
    }
    const auto snapshot = model->GetSnapshot();
    std::unordered_set<ItemId> selected;
    for (auto item : items)
    {
        if (!item || !snapshot.Contains(item) || !selected.insert(item).second)
        {
            throw std::invalid_argument("A move requires distinct existing identities.");
        }
    }
    if ((parent && !snapshot.Contains(parent)) ||
        (before && (!snapshot.Contains(before) || snapshot.GetParent(before) != parent)))
    {
        throw std::invalid_argument("Invalid drop parent or sibling.");
    }
    std::vector<ItemId> roots;
    for (auto item : items)
    {
        bool nested = false;
        for (auto ancestor = snapshot.GetParent(item); ancestor; ancestor = snapshot.GetParent(ancestor))
        {
            if (selected.contains(ancestor))
            {
                nested = true;
                break;
            }
        }
        if (!nested)
        {
            roots.push_back(item);
        }
    }
    for (auto ancestor = parent; ancestor; ancestor = snapshot.GetParent(ancestor))
    {
        if (selected.contains(ancestor))
        {
            throw std::invalid_argument("A record cannot move into its own descendant.");
        }
    }
    if (before && selected.contains(before))
    {
        auto position = *snapshot.GetPosition(before);
        before = 0;
        while (++position < snapshot.GetRowCount(parent))
        {
            const auto candidate = snapshot.GetItem(position, parent);
            if (!selected.contains(candidate))
            {
                before = candidate;
                break;
            }
        }
    }
    std::vector<std::pair<std::vector<std::size_t>, ItemId>> ordered;
    for (auto item : roots)
    {
        std::vector<std::size_t> path;
        for (auto current = item; current; current = snapshot.GetParent(current))
        {
            if (path.size() >= 4096)
            {
                throw std::length_error("A drag path accepts at most 4096 ancestors.");
            }
            path.push_back(*snapshot.GetPosition(current));
        }
        std::reverse(path.begin(), path.end());
        ordered.emplace_back(std::move(path), item);
    }
    std::sort(ordered.begin(), ordered.end());
    roots.clear();
    for (const auto& entry : ordered)
    {
        roots.push_back(entry.second);
    }
    model->Restructure(
        [&](ModelBuilder& builder)
        {
            for (auto item : roots)
            {
                builder.Move(item, parent, before);
            }
        });
}

struct ItemDragDrop::Data : std::enable_shared_from_this<Data>
{
    static thread_local std::vector<std::weak_ptr<Data>> s_Views;
    ThreadAffinity m_Affinity;
    WidgetRef m_Owner;
    std::function<std::shared_ptr<ItemModel>()> m_Model;
    std::function<std::vector<ItemId>()> m_Selection;
    std::function<std::optional<ModelDropTarget>(Point)> m_Target;
    std::function<void(const ModelDragData&, const ModelDropTarget&)> m_Handler;
    Signal<ModelDragData, ModelDropTarget> m_Dropped;
    Signal<std::string> m_Rejected;
    ItemId m_Item = 0;
    Point m_Origin;
    bool m_Dragging = false;
    ModelDragData m_Payload;
    std::weak_ptr<Data> m_Hover;
    std::optional<ModelDropTarget> m_Location;
    std::weak_ptr<Data> m_Source;

    void ClearPreview()
    {
        if (const auto previous = m_Hover.lock())
        {
            previous->m_Source.reset();
            previous->m_Location.reset();
            if (auto* owner = previous->m_Owner.Get())
            {
                owner->RequestRepaint();
            }
        }
        m_Hover.reset();
    }

    void Locate(Point windowPoint)
    {
        ClearPreview();
        const auto* source = m_Owner.Get();
        if (!source || !source->GetContext())
        {
            return;
        }
        const auto* hit = source->GetContext()->InputHitTest(windowPoint);
        for (auto position = s_Views.rbegin(); position != s_Views.rend(); ++position)
        {
            const auto view = position->lock();
            auto* owner = view ? view->m_Owner.Get() : nullptr;
            if (!owner || !owner->IsVisible() || !owner->IsEnabled() || owner->GetContext() != source->GetContext())
            {
                continue;
            }
            bool hitAncestor = false;
            for (const auto* node = hit; node; node = node->GetParent())
            {
                if (node == owner)
                {
                    hitAncestor = true;
                    break;
                }
            }
            if (!hitAncestor)
            {
                continue;
            }
            const auto point = owner->GetContext()->ToLayoutPoint(*owner, windowPoint);
            const auto model = view->m_Model();
            if (!model || (!view->m_Handler && model->GetRootModel()->GetIdentity() != m_Payload.m_Model))
            {
                continue;
            }
            if (!view->m_Owner.Get())
            {
                continue;
            }
            auto target = view->m_Target(point);
            owner = view->m_Owner.Get();
            if (!owner)
            {
                continue;
            }
            if (!target)
            {
                continue;
            }
            m_Hover = view;
            view->m_Source = shared_from_this();
            view->m_Location = *target;
            owner->RequestRepaint();
            return;
        }
    }
};

thread_local std::vector<std::weak_ptr<ItemDragDrop::Data>> ItemDragDrop::Data::s_Views;

ItemDragDrop::ItemDragDrop(Widget& owner, std::function<std::shared_ptr<ItemModel>()> model,
                           std::function<std::vector<ItemId>()> selection,
                           std::function<std::optional<ModelDropTarget>(Point)> target)
    : m_Data(std::make_shared<Data>())
{
    owner.VerifyAccess();
    owner.RegisterStylePart("drop-indicator", 0,
                            [](const Style& style, unsigned)
                            {
                                Style part;
                                part.m_Background = style.m_Color;
                                part.m_Opacity = .75f;
                                return part;
                            });
    if (!model || !selection || !target)
    {
        throw std::invalid_argument("A drag endpoint requires complete model/selection/target accessors.");
    }
    std::erase_if(Data::s_Views, [](const auto& view) { return view.expired(); });
    if (Data::s_Views.size() >= 1024)
    {
        throw std::length_error("An owner thread accepts at most 1024 active drag endpoints.");
    }
    m_Data->m_Owner = WidgetRef(&owner);
    m_Data->m_Model = std::move(model);
    m_Data->m_Selection = std::move(selection);
    m_Data->m_Target = std::move(target);
    Data::s_Views.push_back(m_Data);
}

ItemDragDrop::~ItemDragDrop()
{
    Cancel();
}

void ItemDragDrop::Press(ItemId item, Point windowPoint)
{
    m_Data->m_Affinity.VerifyAccess("Data-view drag access");
    Cancel();
    const auto model = m_Data->m_Model();
    if (!model || !item)
    {
        return;
    }
    m_Data->m_Item = item;
    m_Data->m_Origin = windowPoint;
    const auto root = model->GetRootModel();
    m_Data->m_Payload.m_Model = root->GetIdentity();
    m_Data->m_Payload.m_Revision = root->GetRevision();
    m_Data->m_Payload.m_Snapshot = root->GetSnapshot();
}

bool ItemDragDrop::Move(Point windowPoint)
{
    m_Data->m_Affinity.VerifyAccess("Data-view drag access");
    const auto data = m_Data;
    if (!data->m_Item)
    {
        return false;
    }
    if (!data->m_Dragging)
    {
        if (std::hypot(windowPoint.m_X - data->m_Origin.m_X, windowPoint.m_Y - data->m_Origin.m_Y) < 6)
        {
            return false;
        }
        try
        {
            auto items = data->m_Selection();
            if (items.empty() || std::find(items.begin(), items.end(), data->m_Item) == items.end())
            {
                items = {data->m_Item};
            }
            if (items.size() > 4096)
            {
                throw std::length_error("A drag accepts at most 4096 selected records.");
            }
            data->m_Payload.m_Items = std::move(items);
            data->m_Dragging = true;
        }
        catch (const std::exception& error)
        {
            Cancel();
            data->m_Rejected.Emit(error.what());
            return true;
        }
    }
    try
    {
        data->Locate(windowPoint);
    }
    catch (const std::exception& error)
    {
        Cancel();
        data->m_Rejected.Emit(error.what());
        return true;
    }
    return true;
}

bool ItemDragDrop::Release(Point windowPoint)
{
    m_Data->m_Affinity.VerifyAccess("Data-view drag access");
    const auto data = m_Data;
    if (!data->m_Dragging)
    {
        Cancel();
        return false;
    }
    try
    {
        data->Locate(windowPoint);
    }
    catch (const std::exception& error)
    {
        Cancel();
        data->m_Rejected.Emit(error.what());
        return true;
    }
    const auto target = data->m_Hover.lock();
    const auto location = target ? target->m_Location : std::nullopt;
    const auto payload = data->m_Payload;
    Cancel();
    if (!target || !location)
    {
        return true;
    }
    try
    {
        if (target->m_Handler)
        {
            target->m_Handler(payload, *location);
        }
        else
        {
            MoveModelItems(target->m_Model(), payload.m_Items, location->m_Parent, location->m_Before,
                           payload.m_Revision);
        }
        if (target->m_Owner.Get())
        {
            target->m_Dropped.Emit(payload, *location);
        }
    }
    catch (const std::exception& error)
    {
        if (target->m_Owner.Get())
        {
            target->m_Rejected.Emit(error.what());
        }
    }
    return true;
}

void ItemDragDrop::Cancel()
{
    m_Data->m_Affinity.VerifyAccess("Data-view drag access");
    m_Data->ClearPreview();
    m_Data->m_Item = 0;
    m_Data->m_Dragging = false;
    m_Data->m_Payload = {};
}

void ItemDragDrop::Paint(Renderer& renderer, float opacity) const
{
    m_Data->m_Affinity.VerifyAccess("Data-view drag access");
    auto* owner = m_Data->m_Owner.Get();
    if (!owner || !m_Data->m_Location || m_Data->m_Source.expired())
    {
        return;
    }
    auto bounds = m_Data->m_Location->m_Bounds;
    const auto& style = owner->GetPartStyle("drop-indicator", 0);
    if (!style.m_Displayed || style.m_Opacity <= 0)
    {
        return;
    }
    auto color = style.m_Background;
    color.m_A *= opacity * style.m_Opacity;
    renderer.PushState();
    renderer.SetWidgetPaint(bounds, owner->GetClip(), nullptr, false, 0);
    renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(m_Data.get()));
    renderer.DrawSolid({bounds.m_X, bounds.m_Y, bounds.m_Width, 2}, color, owner->GetClip());
    if (m_Data->m_Location->m_AsChild)
    {
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(m_Data.get()), 1);
        renderer.DrawSolid({bounds.m_X, bounds.m_Y + bounds.m_Height - 2, bounds.m_Width, 2}, color, owner->GetClip());
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(m_Data.get()), 2);
        renderer.DrawSolid({bounds.m_X, bounds.m_Y, 2, bounds.m_Height}, color, owner->GetClip());
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(m_Data.get()), 3);
        renderer.DrawSolid({bounds.m_X + bounds.m_Width - 2, bounds.m_Y, 2, bounds.m_Height}, color, owner->GetClip());
    }
    renderer.PopState();
}

void ItemDragDrop::SetDropHandler(std::function<void(const ModelDragData&, const ModelDropTarget&)> handler)
{
    m_Data->m_Affinity.VerifyAccess("Data-view drag access");
    m_Data->m_Handler = std::move(handler);
}

Signal<ModelDragData, ModelDropTarget>& ItemDragDrop::Dropped()
{
    m_Data->m_Affinity.VerifyAccess("Data-view drag access");
    return m_Data->m_Dropped;
}

Signal<std::string>& ItemDragDrop::Rejected()
{
    m_Data->m_Affinity.VerifyAccess("Data-view drag access");
    return m_Data->m_Rejected;
}
} // namespace EStudioUI
