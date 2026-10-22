#include <EStudioUI/TreeRowMap.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
void TreeRowMap::Reset(ModelSnapshot snapshot, const std::unordered_set<ItemId>& expanded)
{
    TreeRowMap next;
    next.m_Snapshot = std::move(snapshot);
    next.m_Groups.emplace(0, Group{next.m_Snapshot.GetRowCount(), 0, {}});
    for (const auto item : expanded)
        if (next.m_Snapshot.Contains(item))
            next.m_Groups.emplace(item, Group{next.m_Snapshot.GetRowCount(item), 0, {}});
    for (const auto item : expanded)
    {
        if (!next.m_Groups.contains(item))
            continue;
        const auto parent = next.m_Snapshot.GetParent(item);
        if (const auto found = next.m_Groups.find(parent); found != next.m_Groups.end())
        {
            const auto position = next.m_Snapshot.GetPosition(item);
            if (!position || *position >= found->second.m_Children)
                throw std::invalid_argument("Expanded model item has an invalid sibling position.");
            found->second.m_Branches.push_back({item, *position, 0, 0});
        }
    }
    std::unordered_set<ItemId> done;
    for (const auto& [start, ignored] : next.m_Groups)
    {
        (void)ignored;
        std::vector<std::pair<ItemId, bool>> pending{{start, false}};
        while (!pending.empty())
        {
            const auto [item, finishing] = pending.back();
            pending.pop_back();
            if (done.contains(item))
                continue;
            auto& group = next.m_Groups.at(item);
            if (!finishing)
            {
                pending.emplace_back(item, true);
                for (const auto& branch : group.m_Branches)
                    if (!done.contains(branch.m_Item))
                        pending.emplace_back(branch.m_Item, false);
                continue;
            }
            std::sort(group.m_Branches.begin(), group.m_Branches.end(),
                      [](const auto& a, const auto& b) { return a.m_Position < b.m_Position; });
            std::size_t extra = 0;
            for (auto& branch : group.m_Branches)
            {
                branch.m_Start = branch.m_Position + extra;
                branch.m_Extra = next.m_Groups.at(branch.m_Item).m_Total;
                extra += branch.m_Extra;
            }
            if (extra > static_cast<std::size_t>(std::numeric_limits<int>::max()) - group.m_Children)
                throw std::length_error("Expanded tree exceeds the supported view row domain.");
            group.m_Total = group.m_Children + extra;
            done.insert(item);
        }
    }
    *this = std::move(next);
}

std::size_t TreeRowMap::GetCount() const
{
    const auto found = m_Groups.find(0);
    return found == m_Groups.end() ? 0 : found->second.m_Total;
}

void TreeRowMap::UpdateSnapshot(ModelSnapshot snapshot)
{
    m_Snapshot = std::move(snapshot);
}

TreeRowMap::Row TreeRowMap::GetRow(std::size_t row) const
{
    if (row >= GetCount())
        throw std::out_of_range("Invalid visible tree row.");
    ItemId parent = 0;
    unsigned depth = 0;
    for (;;)
    {
        const auto& group = m_Groups.at(parent);
        auto after = std::upper_bound(group.m_Branches.begin(), group.m_Branches.end(), row,
                                      [](std::size_t value, const Branch& branch) { return value < branch.m_Start; });
        std::size_t extra = 0;
        if (after != group.m_Branches.begin())
        {
            const auto& branch = *std::prev(after);
            if (row == branch.m_Start)
                return {branch.m_Item, depth};
            if (row <= branch.m_Start + branch.m_Extra)
            {
                row -= branch.m_Start + 1;
                parent = branch.m_Item;
                ++depth;
                continue;
            }
            extra = branch.m_Start - branch.m_Position + branch.m_Extra;
        }
        return {m_Snapshot.GetItem(row - extra, parent), depth};
    }
}

std::optional<std::size_t> TreeRowMap::GetIndex(ItemId item) const
{
    if (!m_Snapshot.Contains(item))
        return {};
    std::size_t result = 0;
    while (item)
    {
        const auto parent = m_Snapshot.GetParent(item);
        const auto found = m_Groups.find(parent);
        const auto position = m_Snapshot.GetPosition(item);
        if (found == m_Groups.end() || !position)
            return {};
        const auto& branches = found->second.m_Branches;
        auto after =
            std::lower_bound(branches.begin(), branches.end(), *position,
                             [](const Branch& branch, std::size_t value) { return branch.m_Position < value; });
        std::size_t extra = 0;
        if (after != branches.begin())
        {
            const auto& before = *std::prev(after);
            extra = before.m_Start - before.m_Position + before.m_Extra;
        }
        result += *position + extra;
        if (parent)
            ++result;
        item = parent;
    }
    return result;
}
} // namespace EStudioUI
