#include <EStudioUI/CanvasScene.h>

#include <algorithm>
#include <set>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
constexpr auto s_None = std::numeric_limits<std::uint32_t>::max();

void Validate(const CanvasBounds& bounds)
{
    if (!std::isfinite(bounds.m_X) || !std::isfinite(bounds.m_Y) || !std::isfinite(bounds.m_Width) ||
        !std::isfinite(bounds.m_Height) || bounds.m_Width < 0 || bounds.m_Height < 0 ||
        !std::isfinite(bounds.m_X + bounds.m_Width) || !std::isfinite(bounds.m_Y + bounds.m_Height))
        throw std::invalid_argument("Canvas bounds must be finite and nonnegative");
}

CanvasBounds Union(const CanvasBounds& a, const CanvasBounds& b)
{
    const auto x = std::min(a.m_X, b.m_X);
    const auto y = std::min(a.m_Y, b.m_Y);
    return {x, y, std::max(a.m_X + a.m_Width, b.m_X + b.m_Width) - x,
            std::max(a.m_Y + a.m_Height, b.m_Y + b.m_Height) - y};
}

long double Perimeter(const CanvasBounds& bounds)
{
    return static_cast<long double>(bounds.m_Width) + static_cast<long double>(bounds.m_Height);
}

template <typename T> void EnsureCapacity(std::vector<T>& storage, std::size_t required)
{
    if (storage.capacity() < required)
        storage.reserve(std::max(required, storage.capacity() + storage.capacity() / 2));
}
} // namespace

bool CanvasBounds::Contains(CanvasPoint point) const
{
    return point.m_X >= m_X && point.m_X <= m_X + m_Width && point.m_Y >= m_Y && point.m_Y <= m_Y + m_Height;
}

bool CanvasBounds::Intersects(const CanvasBounds& other) const
{
    return m_X <= other.m_X + other.m_Width && m_X + m_Width >= other.m_X && m_Y <= other.m_Y + other.m_Height &&
           m_Y + m_Height >= other.m_Y;
}

CanvasItemId::operator bool() const
{
    return m_Generation != 0;
}

struct CanvasScene::Impl
{
    struct Slot
    {
        std::uint32_t m_Dense = s_None;
        std::uint32_t m_Node = s_None;
        std::uint32_t m_Generation = 1;
        std::uint32_t m_Next = s_None;
    };

    struct Node
    {
        CanvasBounds m_Bounds;
        std::uint32_t m_Parent = s_None;
        std::uint32_t m_Left = s_None;
        std::uint32_t m_Right = s_None;
        std::uint32_t m_Slot = s_None;
        int m_Height = 0;

        bool IsLeaf() const
        {
            return m_Left == s_None;
        }
    };

    std::vector<CanvasItem> m_Items;
    std::set<std::uint64_t> m_Selected;
    std::uint64_t m_StructureRevision = 0;
    std::vector<Slot> m_Slots;
    std::vector<Node> m_Nodes;
    std::uint32_t m_FreeSlot = s_None;
    std::uint32_t m_FreeNode = s_None;
    std::uint32_t m_Root = s_None;
    std::uint32_t m_GenerationFloor = 1;
    std::uint64_t m_Revision = 0;
    std::uint64_t m_NextOrder = 0;
    mutable std::size_t m_QueryVisits = 0;

    Slot* Find(CanvasItemId id)
    {
        if (id.m_Index >= m_Slots.size())
            return nullptr;
        auto& slot = m_Slots[id.m_Index];
        return slot.m_Dense != s_None && id.m_Generation == slot.m_Generation ? &slot : nullptr;
    }

    std::uint32_t AllocateNode()
    {
        if (m_FreeNode != s_None)
        {
            const auto index = m_FreeNode;
            m_FreeNode = m_Nodes[index].m_Parent;
            m_Nodes[index] = {};
            return index;
        }
        if (m_Nodes.size() == s_None)
            throw std::length_error("Canvas BVH index capacity exceeded");
        const auto index = static_cast<std::uint32_t>(m_Nodes.size());
        m_Nodes.emplace_back();
        return index;
    }

    void ReleaseNode(std::uint32_t index)
    {
        m_Nodes[index].m_Parent = m_FreeNode;
        m_FreeNode = index;
    }

    void Refit(std::uint32_t index)
    {
        auto& node = m_Nodes[index];
        const auto& left = m_Nodes[node.m_Left];
        const auto& right = m_Nodes[node.m_Right];
        node.m_Bounds = Union(left.m_Bounds, right.m_Bounds);
        node.m_Height = 1 + std::max(left.m_Height, right.m_Height);
    }

    void ReplaceChild(std::uint32_t parent, std::uint32_t oldChild, std::uint32_t newChild)
    {
        if (parent == s_None)
            m_Root = newChild;
        else if (m_Nodes[parent].m_Left == oldChild)
            m_Nodes[parent].m_Left = newChild;
        else
            m_Nodes[parent].m_Right = newChild;
        m_Nodes[newChild].m_Parent = parent;
    }

    std::uint32_t Balance(std::uint32_t index)
    {
        auto& a = m_Nodes[index];
        if (a.IsLeaf() || a.m_Height < 2)
            return index;
        const auto leftIndex = a.m_Left;
        const auto rightIndex = a.m_Right;
        auto& b = m_Nodes[leftIndex];
        auto& c = m_Nodes[rightIndex];
        const auto balance = c.m_Height - b.m_Height;
        if (balance > 1)
        {
            const auto first = c.m_Left;
            const auto second = c.m_Right;
            ReplaceChild(a.m_Parent, index, rightIndex);
            c.m_Left = index;
            a.m_Parent = rightIndex;
            if (m_Nodes[first].m_Height > m_Nodes[second].m_Height)
            {
                c.m_Right = first;
                a.m_Right = second;
                m_Nodes[second].m_Parent = index;
            }
            else
            {
                c.m_Right = second;
                a.m_Right = first;
                m_Nodes[first].m_Parent = index;
            }
            Refit(index);
            Refit(rightIndex);
            return rightIndex;
        }
        if (balance < -1)
        {
            const auto first = b.m_Left;
            const auto second = b.m_Right;
            ReplaceChild(a.m_Parent, index, leftIndex);
            b.m_Left = index;
            a.m_Parent = leftIndex;
            if (m_Nodes[first].m_Height > m_Nodes[second].m_Height)
            {
                b.m_Right = first;
                a.m_Left = second;
                m_Nodes[second].m_Parent = index;
            }
            else
            {
                b.m_Right = second;
                a.m_Left = first;
                m_Nodes[first].m_Parent = index;
            }
            Refit(index);
            Refit(leftIndex);
            return leftIndex;
        }
        return index;
    }

    void Repair(std::uint32_t index)
    {
        while (index != s_None)
        {
            index = Balance(index);
            Refit(index);
            index = m_Nodes[index].m_Parent;
        }
    }

    void InsertLeaf(std::uint32_t leaf)
    {
        if (m_Root == s_None)
        {
            m_Root = leaf;
            m_Nodes[leaf].m_Parent = s_None;
            return;
        }
        const auto bounds = m_Nodes[leaf].m_Bounds;
        auto sibling = m_Root;
        while (!m_Nodes[sibling].IsLeaf())
        {
            const auto& node = m_Nodes[sibling];
            const auto cost = [&](std::uint32_t child)
            {
                const auto& candidate = m_Nodes[child];
                const auto mergedCost = Perimeter(Union(candidate.m_Bounds, bounds));
                return mergedCost - (candidate.IsLeaf() ? 0 : Perimeter(candidate.m_Bounds));
            };
            const auto leftCost = cost(node.m_Left);
            const auto rightCost = cost(node.m_Right);
            // Insert beside a leaf so every repaired ancestor changes height by at most one.
            sibling = leftCost < rightCost ? node.m_Left : node.m_Right;
        }
        const auto parent = m_Nodes[sibling].m_Parent;
        const auto newParent = AllocateNode();
        m_Nodes[newParent].m_Left = sibling;
        m_Nodes[newParent].m_Right = leaf;
        ReplaceChild(parent, sibling, newParent);
        m_Nodes[sibling].m_Parent = newParent;
        m_Nodes[leaf].m_Parent = newParent;
        Refit(newParent);
        Repair(newParent);
    }

    void DetachLeaf(std::uint32_t leaf)
    {
        if (leaf == m_Root)
        {
            m_Root = s_None;
            return;
        }
        const auto parent = m_Nodes[leaf].m_Parent;
        const auto grandparent = m_Nodes[parent].m_Parent;
        const auto sibling = m_Nodes[parent].m_Left == leaf ? m_Nodes[parent].m_Right : m_Nodes[parent].m_Left;
        ReplaceChild(grandparent, parent, sibling);
        ReleaseNode(parent);
        m_Nodes[leaf].m_Parent = s_None;
        if (grandparent != s_None)
            Repair(grandparent);
    }

    void ReleaseSlot(std::uint32_t index)
    {
        auto& slot = m_Slots[index];
        slot.m_Dense = s_None;
        slot.m_Node = s_None;
        if (slot.m_Generation == std::numeric_limits<std::uint32_t>::max())
            return;
        ++slot.m_Generation;
        slot.m_Next = m_FreeSlot;
        m_FreeSlot = index;
    }
};

CanvasScene::CanvasScene() : m_Impl(std::make_unique<Impl>())
{
}

CanvasScene::~CanvasScene() = default;

void CanvasScene::Reserve(std::size_t count)
{
    if (count > (s_None - 1) / 2)
        throw std::length_error("Canvas item capacity exceeded");
    m_Impl->m_Items.reserve(count);
    m_Impl->m_Slots.reserve(count);
    m_Impl->m_Nodes.reserve(count * 2);
}

CanvasItemId CanvasScene::Add(const CanvasGeometry& geometry, std::uint32_t style)
{
    const auto bounds = geometry.Bounds();
    auto& state = *m_Impl;
    if (state.m_Root != s_None)
        Validate(Union(state.m_Nodes[state.m_Root].m_Bounds, bounds));
    if (state.m_Items.size() >= (s_None - 1) / 2 || state.m_NextOrder == std::numeric_limits<std::uint64_t>::max())
        throw std::length_error("Canvas item capacity exceeded");
    // Finish all potentially throwing allocations before touching the live tree or identity free lists.
    EnsureCapacity(state.m_Items, state.m_Items.size() + 1);
    EnsureCapacity(state.m_Nodes, state.m_Nodes.size() + 2);
    if (state.m_FreeSlot == s_None)
        EnsureCapacity(state.m_Slots, state.m_Slots.size() + 1);
    auto slotIndex = state.m_FreeSlot;
    if (slotIndex == s_None)
    {
        if (state.m_Slots.size() == s_None)
            throw std::length_error("Canvas identity capacity exceeded");
        slotIndex = static_cast<std::uint32_t>(state.m_Slots.size());
        state.m_Slots.emplace_back();
        state.m_Slots.back().m_Generation = state.m_GenerationFloor;
    }
    else
        state.m_FreeSlot = state.m_Slots[slotIndex].m_Next;
    auto& slot = state.m_Slots[slotIndex];
    const CanvasItemId id{slotIndex, slot.m_Generation};
    slot.m_Node = state.AllocateNode();
    slot.m_Dense = static_cast<std::uint32_t>(state.m_Items.size());
    state.m_Items.push_back({id, geometry, style, false, state.m_NextOrder++});
    state.m_Nodes[slot.m_Node].m_Bounds = bounds;
    state.m_Nodes[slot.m_Node].m_Slot = slotIndex;
    state.InsertLeaf(slot.m_Node);
    ++state.m_Revision;
    ++state.m_StructureRevision;
    return id;
}

bool CanvasScene::Remove(CanvasItemId id)
{
    auto& state = *m_Impl;
    auto* slot = state.Find(id);
    if (!slot)
        return false;
    state.m_Selected.erase((static_cast<std::uint64_t>(id.m_Generation) << 32) | id.m_Index);
    state.DetachLeaf(slot->m_Node);
    state.ReleaseNode(slot->m_Node);
    const auto dense = slot->m_Dense;
    if (dense + 1 != state.m_Items.size())
    {
        state.m_Items[dense] = state.m_Items.back();
        state.m_Slots[state.m_Items[dense].m_Id.m_Index].m_Dense = dense;
    }
    state.m_Items.pop_back();
    state.ReleaseSlot(id.m_Index);
    ++state.m_Revision;
    ++state.m_StructureRevision;
    return true;
}

bool CanvasScene::Update(CanvasItemId id, const CanvasGeometry& geometry)
{
    auto& state = *m_Impl;
    auto* slot = state.Find(id);
    if (!slot)
        return false;
    const auto bounds = geometry.Bounds();
    Validate(Union(state.m_Nodes[state.m_Root].m_Bounds, bounds));
    auto& item = state.m_Items[slot->m_Dense];
    if (item.m_Geometry == geometry)
        return true;
    if (state.m_Nodes[slot->m_Node].m_Bounds != bounds)
    {
        state.DetachLeaf(slot->m_Node);
        state.m_Nodes[slot->m_Node].m_Bounds = bounds;
        state.InsertLeaf(slot->m_Node);
    }
    item.m_Geometry = geometry;
    ++state.m_Revision;
    return true;
}

bool CanvasScene::SetStyle(CanvasItemId id, std::uint32_t style)
{
    auto* slot = m_Impl->Find(id);
    if (!slot)
        return false;
    auto& item = m_Impl->m_Items[slot->m_Dense];
    if (item.m_Style != style)
    {
        item.m_Style = style;
        ++m_Impl->m_Revision;
    }
    return true;
}

bool CanvasScene::SetSelected(CanvasItemId id, bool selected)
{
    auto* slot = m_Impl->Find(id);
    if (!slot)
        return false;
    auto& item = m_Impl->m_Items[slot->m_Dense];
    if (item.m_Selected != selected)
    {
        const auto key = (static_cast<std::uint64_t>(id.m_Generation) << 32) | id.m_Index;
        if (selected)
            m_Impl->m_Selected.insert(key);
        else
            m_Impl->m_Selected.erase(key);
        item.m_Selected = selected;
        ++m_Impl->m_Revision;
    }
    return true;
}

bool CanvasScene::SetOrder(CanvasItemId id, std::uint64_t order)
{
    auto* slot = m_Impl->Find(id);
    if (!slot)
        return false;
    if (order == std::numeric_limits<std::uint64_t>::max())
        throw std::invalid_argument("Canvas painter order exceeds its capacity");
    auto& item = m_Impl->m_Items[slot->m_Dense];
    if (item.m_Order != order)
    {
        item.m_Order = order;
        ++m_Impl->m_Revision;
    }
    m_Impl->m_NextOrder = std::max(m_Impl->m_NextOrder, order + 1);
    return true;
}

const CanvasItem* CanvasScene::Get(CanvasItemId id) const
{
    const auto* slot = m_Impl->Find(id);
    return slot ? &m_Impl->m_Items[slot->m_Dense] : nullptr;
}

std::optional<std::size_t> CanvasScene::GetIndex(CanvasItemId id) const
{
    const auto* slot = m_Impl->Find(id);
    return slot ? std::optional<std::size_t>(slot->m_Dense) : std::nullopt;
}

std::size_t CanvasScene::GetSelectedCount() const
{
    return m_Impl->m_Selected.size();
}

std::vector<CanvasItemId> CanvasScene::GetSelected(std::size_t offset, std::size_t limit) const
{
    if (limit > 256)
        throw std::length_error("Canvas selection pages are limited to 256 items.");
    std::vector<CanvasItemId> result;
    auto item = m_Impl->m_Selected.begin();
    std::advance(item, std::min(offset, m_Impl->m_Selected.size()));
    for (; item != m_Impl->m_Selected.end() && result.size() < limit; ++item)
        result.push_back({static_cast<std::uint32_t>(*item), static_cast<std::uint32_t>(*item >> 32)});
    return result;
}

void CanvasScene::ClearSelection()
{
    if (m_Impl->m_Selected.empty())
        return;
    for (const auto key : m_Impl->m_Selected)
        if (auto* slot = m_Impl->Find({static_cast<std::uint32_t>(key), static_cast<std::uint32_t>(key >> 32)}))
            m_Impl->m_Items[slot->m_Dense].m_Selected = false;
    m_Impl->m_Selected.clear();
    ++m_Impl->m_Revision;
}

std::uint64_t CanvasScene::GetStructureRevision() const
{
    return m_Impl->m_StructureRevision;
}

CanvasItemId CanvasScene::HitTest(CanvasPoint point, double tolerance) const
{
    if (!std::isfinite(point.m_X) || !std::isfinite(point.m_Y) || !std::isfinite(tolerance) || tolerance < 0)
        throw std::invalid_argument("Canvas hit testing requires finite coordinates and nonnegative tolerance.");
    if (m_Impl->m_Root == s_None)
        return {};
    // A balanced binary tree over uint32 leaves cannot have 128 live ancestor siblings.
    std::array<std::uint32_t, 128> pending{};
    std::size_t count = 1;
    pending[0] = m_Impl->m_Root;
    CanvasItemId result;
    std::uint64_t order = 0;
    while (count)
    {
        const auto& node = m_Impl->m_Nodes[pending[--count]];
        const auto& bounds = node.m_Bounds;
        if (point.m_X < bounds.m_X - tolerance || point.m_X > bounds.m_X + bounds.m_Width + tolerance ||
            point.m_Y < bounds.m_Y - tolerance || point.m_Y > bounds.m_Y + bounds.m_Height + tolerance)
            continue;
        if (!node.IsLeaf())
        {
            if (count + 2 > pending.size())
                throw std::length_error("Canvas BVH depth exceeded the hit-test stack budget.");
            pending[count++] = node.m_Left;
            pending[count++] = node.m_Right;
            continue;
        }
        const auto& item = m_Impl->m_Items[m_Impl->m_Slots[node.m_Slot].m_Dense];
        if ((!result || item.m_Order >= order) &&
            DistanceToItem(item.m_Geometry, point, std::max(1e-6, tolerance * 0.25)) <= tolerance)
        {
            result = item.m_Id;
            order = item.m_Order;
        }
    }
    return result;
}

std::size_t CanvasScene::Size() const
{
    return m_Impl->m_Items.size();
}

void CanvasScene::Clear()
{
    auto& state = *m_Impl;
    if (state.m_Items.empty())
        return;
    for (const auto& item : state.m_Items)
        state.ReleaseSlot(item.m_Id.m_Index);
    state.m_Selected.clear();
    ++state.m_StructureRevision;
    state.m_Items.clear();
    state.m_Nodes.clear();
    state.m_FreeNode = s_None;
    state.m_Root = s_None;
    state.m_QueryVisits = 0;
    ++state.m_Revision;
}

void CanvasScene::ReplaceWith(CanvasScene& source)
{
    if (&source == this)
        throw std::invalid_argument("A canvas scene cannot replace itself");
    auto generation = m_Impl->m_GenerationFloor;
    for (const auto& slot : m_Impl->m_Slots)
        generation = std::max(generation, slot.m_Generation);
    if (generation == std::numeric_limits<std::uint32_t>::max() ||
        m_Impl->m_Revision == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Canvas scene replacement identity capacity exceeded");
    ++generation;

    std::set<std::uint64_t> replacementSelection;
    for (const auto& item : source.m_Impl->m_Items)
        if (item.m_Selected)
            replacementSelection.insert((static_cast<std::uint64_t>(generation) << 32) | item.m_Id.m_Index);

    // All throwing checks precede mutation. The floor also protects old handles whose slots
    // are beyond the replacement's size and may be allocated again after a later insertion.
    auto& replacement = *source.m_Impl;
    replacement.m_GenerationFloor = generation;
    for (auto& slot : replacement.m_Slots)
        slot.m_Generation = generation;
    for (auto& item : replacement.m_Items)
        item.m_Id.m_Generation = generation;
    replacement.m_Selected = std::move(replacementSelection);
    replacement.m_StructureRevision = m_Impl->m_StructureRevision + 1;
    replacement.m_Revision = m_Impl->m_Revision + 1;
    replacement.m_QueryVisits = 0;
    m_Impl.swap(source.m_Impl);
}

void CanvasScene::Query(const CanvasBounds& bounds, std::vector<CanvasItemId>& output) const
{
    Validate(bounds);
    output.clear();
    auto& state = *m_Impl;
    state.m_QueryVisits = 0;
    if (state.m_Root == s_None)
        return;
    // The height-balanced tree cannot reach 128 levels within the 32-bit node capacity.
    std::array<std::uint32_t, 128> stack;
    std::size_t count = 0;
    stack[count++] = state.m_Root;
    while (count)
    {
        const auto& node = state.m_Nodes[stack[--count]];
        ++state.m_QueryVisits;
        if (!bounds.Intersects(node.m_Bounds))
            continue;
        if (node.IsLeaf())
        {
            const auto& slot = state.m_Slots[node.m_Slot];
            output.push_back({node.m_Slot, slot.m_Generation});
        }
        else
        {
            stack[count++] = node.m_Left;
            stack[count++] = node.m_Right;
        }
    }
}

std::uint64_t CanvasScene::GetRevision() const
{
    return m_Impl->m_Revision;
}

std::size_t CanvasScene::GetLastQueryVisits() const
{
    return m_Impl->m_QueryVisits;
}

std::size_t CanvasScene::GetStorageBytes() const
{
    // Include conservative ordered-set node overhead; geometry payloads remain separately accounted.
    constexpr auto selectedNodeBytes = sizeof(std::uint64_t) + 5 * sizeof(void*);
    return sizeof(CanvasScene) + sizeof(Impl) + m_Impl->m_Items.capacity() * sizeof(CanvasItem) +
           m_Impl->m_Slots.capacity() * sizeof(Impl::Slot) + m_Impl->m_Nodes.capacity() * sizeof(Impl::Node) +
           m_Impl->m_Selected.size() * selectedNodeBytes;
}

CanvasBounds CanvasScene::GetBounds() const
{
    return m_Impl->m_Root == s_None ? CanvasBounds{} : m_Impl->m_Nodes[m_Impl->m_Root].m_Bounds;
}

const std::vector<CanvasItem>& CanvasScene::GetItems() const
{
    return m_Impl->m_Items;
}
} // namespace EStudioUI
