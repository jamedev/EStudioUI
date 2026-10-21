#include <EStudioUI/SketchDocument.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <deque>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace EStudioUI
{
namespace
{
std::uint64_t KeyOf(CanvasItemId id)
{
    return (static_cast<std::uint64_t>(id.m_Generation) << 32) | id.m_Index;
}

bool IsFinite(CanvasPoint point)
{
    return std::isfinite(point.m_X) && std::isfinite(point.m_Y);
}

void ValidatePoint(CanvasPoint point)
{
    if (!IsFinite(point))
        throw std::invalid_argument("Sketch coordinates must be finite");
}

void ValidateTolerance(double tolerance)
{
    if (!std::isfinite(tolerance) || tolerance < 0)
        throw std::invalid_argument("Sketch picking tolerance must be finite and nonnegative");
}

bool ContainsBounds(CanvasBounds outer, CanvasBounds inner)
{
    return inner.m_X >= outer.m_X && inner.m_Y >= outer.m_Y && inner.m_X + inner.m_Width <= outer.m_X + outer.m_Width &&
           inner.m_Y + inner.m_Height <= outer.m_Y + outer.m_Height;
}
} // namespace

struct SketchDocument::Impl
{
    struct Record
    {
        CanvasItemId m_Current;
    };

    struct State
    {
        CanvasGeometry m_Geometry;
        std::uint32_t m_Style = 0;
        std::uint64_t m_Order = 0;
    };

    struct Change
    {
        std::shared_ptr<Record> m_Record;
        std::optional<State> m_Before;
        std::optional<State> m_After;
    };

    struct Command
    {
        std::vector<Change> m_Changes;
        std::vector<std::shared_ptr<Record>> m_BeforeSelection;
        std::vector<std::shared_ptr<Record>> m_AfterSelection;

        std::size_t Bytes() const
        {
            return sizeof(Command) + m_Changes.capacity() * (sizeof(Change) + sizeof(Record) + 2 * sizeof(void*)) +
                   (m_BeforeSelection.capacity() + m_AfterSelection.capacity()) * sizeof(std::shared_ptr<Record>);
        }
    };

    static constexpr std::size_t s_HistoryBudget = 128 * 1024 * 1024;
    static constexpr std::size_t s_HistoryLimit = 128;
    CanvasScene m_Scene;
    std::vector<CanvasItemId> m_Selection;
    std::unordered_map<std::uint64_t, std::weak_ptr<Record>> m_Records;
    std::deque<Command> m_Undo;
    std::deque<Command> m_Redo;
    std::size_t m_HistoryBytes = 0;

    static State Snapshot(const CanvasItem& item)
    {
        return {item.m_Geometry, item.m_Style, item.m_Order};
    }

    std::shared_ptr<Record> Track(CanvasItemId id)
    {
        auto& entry = m_Records[KeyOf(id)];
        auto record = entry.lock();
        if (!record)
        {
            record = std::make_shared<Record>();
            record->m_Current = id;
            entry = record;
        }
        return record;
    }

    void PruneRecords()
    {
        std::erase_if(m_Records, [](const auto& entry) { return entry.second.expired(); });
    }

    std::vector<std::shared_ptr<Record>> SelectionRecords()
    {
        std::vector<std::shared_ptr<Record>> result;
        result.reserve(m_Selection.size());
        for (auto id : m_Selection)
            if (m_Scene.Get(id))
                result.push_back(Track(id));
        return result;
    }

    void ClearSelection()
    {
        for (auto id : m_Selection)
            m_Scene.SetSelected(id, false);
        m_Selection.clear();
    }

    void RestoreSelection(const std::vector<std::shared_ptr<Record>>& selection)
    {
        ClearSelection();
        for (const auto& record : selection)
        {
            if (m_Scene.Get(record->m_Current))
            {
                m_Scene.SetSelected(record->m_Current, true);
                m_Selection.push_back(record->m_Current);
            }
        }
    }

    void Apply(const Change& change, bool forward)
    {
        const auto& state = forward ? change.m_After : change.m_Before;
        auto& record = *change.m_Record;
        const auto current = record.m_Current;
        if (!state)
        {
            m_Scene.Remove(current);
            m_Records.erase(KeyOf(current));
            record.m_Current = {};
        }
        else if (m_Scene.Get(current))
        {
            m_Scene.Update(current, state->m_Geometry);
            m_Scene.SetStyle(current, state->m_Style);
            m_Scene.SetOrder(current, state->m_Order);
        }
        else
        {
            record.m_Current = m_Scene.Add(state->m_Geometry, state->m_Style);
            m_Scene.SetOrder(record.m_Current, state->m_Order);
            m_Records[KeyOf(record.m_Current)] = change.m_Record;
        }
    }

    void Commit(Command command)
    {
        const auto bytes = command.Bytes();
        if (bytes > s_HistoryBudget)
            throw std::logic_error("Sketch command was not checked against the history budget");
        for (const auto& redo : m_Redo)
            m_HistoryBytes -= redo.Bytes();
        m_Redo.clear();
        while (!m_Undo.empty() && (m_Undo.size() >= s_HistoryLimit || m_HistoryBytes + bytes > s_HistoryBudget))
        {
            m_HistoryBytes -= m_Undo.front().Bytes();
            m_Undo.pop_front();
        }
        m_HistoryBytes += bytes;
        m_Undo.push_back(std::move(command));
        PruneRecords();
    }

    bool FitsHistory(Command& command)
    {
        if (command.Bytes() <= s_HistoryBudget)
            return true;
        command = {};
        PruneRecords();
        return false;
    }
};

SketchDocument::SketchDocument() : m_Impl(std::make_unique<Impl>())
{
}

SketchDocument::~SketchDocument() = default;

CanvasScene& SketchDocument::GetScene()
{
    return m_Impl->m_Scene;
}

const CanvasScene& SketchDocument::GetScene() const
{
    return m_Impl->m_Scene;
}

void SketchDocument::Clear()
{
    m_Impl->ClearSelection();
    m_Impl->m_Scene.Clear();
    ClearHistory();
}

void SketchDocument::ClearHistory()
{
    m_Impl->m_Undo.clear();
    m_Impl->m_Redo.clear();
    m_Impl->m_Records.clear();
    m_Impl->m_HistoryBytes = 0;
    std::erase_if(m_Impl->m_Selection, [this](auto id) { return !m_Impl->m_Scene.Get(id); });
}

void SketchDocument::Save(std::ostream& output) const
{
    std::vector<const CanvasItem*> ordered;
    ordered.reserve(m_Impl->m_Scene.Size());
    for (const auto& item : m_Impl->m_Scene.GetItems())
        ordered.push_back(&item);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto* first, const auto* second) { return first->m_Order < second->m_Order; });
    output << "ESTUDIOUI_SKETCH 1\n" << std::to_string(ordered.size()) << '\n';
    std::ostringstream row;
    row.imbue(std::locale::classic());
    row << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto* item : ordered)
    {
        row.str({});
        const auto& geometry = item->m_Geometry;
        const auto kind = geometry.m_Kind == CanvasPrimitive::Line        ? "line"
                          : geometry.m_Kind == CanvasPrimitive::Rectangle ? "rectangle"
                                                                          : "circle";
        row << kind << ' ' << geometry.m_A.m_X << ' ' << geometry.m_A.m_Y << ' ' << geometry.m_B.m_X << ' '
            << geometry.m_B.m_Y << ' ' << geometry.m_Radius << ' ' << item->m_Style << '\n';
        output << row.str();
        if (!output)
            throw std::runtime_error("Cannot write sketch document");
    }
    if (!output)
        throw std::runtime_error("Cannot write sketch document");
}

void SketchDocument::Load(std::istream& input)
{
    const auto readLine = [&](std::string& line)
    {
        line.clear();
        char value = 0;
        while (input.get(value))
        {
            if (value == '\n')
                return true;
            if (line.size() == 1024)
                throw std::invalid_argument("Sketch document row is too long");
            line.push_back(value);
        }
        if (input.bad())
            throw std::runtime_error("Cannot read sketch document");
        return !line.empty();
    };
    const auto parseUnsigned = [](const std::string& token, std::uint64_t maximum)
    {
        std::uint64_t result = 0;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || result > maximum)
            throw std::invalid_argument("Invalid sketch document unsigned integer");
        return result;
    };
    const auto exhausted = [](std::istringstream& row)
    {
        row >> std::ws;
        return row.eof();
    };
    std::string line;
    std::string magic;
    std::string version;
    if (!readLine(line))
        throw std::invalid_argument("Missing sketch document header");
    std::istringstream header(line);
    header.imbue(std::locale::classic());
    if (!(header >> magic >> version) || magic != "ESTUDIOUI_SKETCH" || version != "1" || !exhausted(header))
        throw std::invalid_argument("Unsupported sketch document header");
    if (!readLine(line))
        throw std::invalid_argument("Missing sketch document item count");
    std::istringstream countRow(line);
    std::string countToken;
    if (!(countRow >> countToken) || !exhausted(countRow))
        throw std::invalid_argument("Invalid sketch document item count");
    const auto count = parseUnsigned(countToken, 1000000);
    auto replacement = std::make_unique<Impl>();
    replacement->m_Scene.Reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index)
    {
        if (!readLine(line))
            throw std::invalid_argument("Truncated sketch document");
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        std::string kind;
        std::string styleToken;
        CanvasGeometry geometry;
        if (!(row >> kind >> geometry.m_A.m_X >> geometry.m_A.m_Y >> geometry.m_B.m_X >> geometry.m_B.m_Y >>
              geometry.m_Radius >> styleToken) ||
            !exhausted(row))
            throw std::invalid_argument("Invalid sketch document geometry row");
        if (kind == "line")
            geometry.m_Kind = CanvasPrimitive::Line;
        else if (kind == "rectangle")
            geometry.m_Kind = CanvasPrimitive::Rectangle;
        else if (kind == "circle")
            geometry.m_Kind = CanvasPrimitive::Circle;
        else
            throw std::invalid_argument("Unsupported sketch document primitive");
        const auto style =
            static_cast<std::uint32_t>(parseUnsigned(styleToken, std::numeric_limits<std::uint32_t>::max()));
        replacement->m_Scene.Add(geometry, style);
    }
    while (readLine(line))
        if (line.find_first_not_of(" \t\r") != std::string::npos)
            throw std::invalid_argument("Unexpected trailing sketch document data");
    m_Impl->m_Scene.ReplaceWith(replacement->m_Scene);
    m_Impl->m_Selection.clear();
    ClearHistory();
}

CanvasItemId SketchDocument::AddShape(const CanvasGeometry& geometry, std::uint32_t style)
{
    Impl::Command command;
    command.m_BeforeSelection = m_Impl->SelectionRecords();
    command.m_Changes.reserve(1);
    command.m_AfterSelection.reserve(1);
    if (!m_Impl->FitsHistory(command))
        return {};
    const auto id = m_Impl->m_Scene.Add(geometry, style);
    const auto record = m_Impl->Track(id);
    command.m_Changes.push_back({record, {}, Impl::Snapshot(*m_Impl->m_Scene.Get(id))});
    Select(id);
    command.m_AfterSelection = {record};
    m_Impl->Commit(std::move(command));
    return id;
}

CanvasItemId SketchDocument::Pick(CanvasPoint point, double tolerance) const
{
    ValidatePoint(point);
    ValidateTolerance(tolerance);
    std::vector<CanvasItemId> candidates;
    m_Impl->m_Scene.Query({point.m_X - tolerance, point.m_Y - tolerance, tolerance * 2, tolerance * 2}, candidates);
    CanvasItemId result;
    double bestDistance = tolerance;
    std::uint64_t bestOrder = 0;
    for (auto id : candidates)
    {
        const auto& item = *m_Impl->m_Scene.Get(id);
        const auto distance = DistanceToItem(item.m_Geometry, point);
        if (distance <= bestDistance && (distance < bestDistance || !result || item.m_Order > bestOrder))
        {
            bestDistance = distance;
            bestOrder = item.m_Order;
            result = id;
        }
    }
    return result;
}

void SketchDocument::Select(CanvasItemId item, bool additive)
{
    if (!additive)
        m_Impl->ClearSelection();
    if (!m_Impl->m_Scene.Get(item))
        return;
    const auto it = std::find(m_Impl->m_Selection.begin(), m_Impl->m_Selection.end(), item);
    if (it != m_Impl->m_Selection.end())
    {
        m_Impl->m_Scene.SetSelected(item, false);
        m_Impl->m_Selection.erase(it);
    }
    else
    {
        m_Impl->m_Scene.SetSelected(item, true);
        m_Impl->m_Selection.push_back(item);
    }
}

void SketchDocument::SelectBox(CanvasBounds bounds, bool additive)
{
    if (!std::isfinite(bounds.m_X) || !std::isfinite(bounds.m_Y) || !std::isfinite(bounds.m_Width) ||
        !std::isfinite(bounds.m_Height) || !std::isfinite(bounds.m_X + bounds.m_Width) ||
        !std::isfinite(bounds.m_Y + bounds.m_Height) || bounds.m_Width < 0 || bounds.m_Height < 0)
        throw std::invalid_argument("Sketch selection bounds must be finite and normalized");
    if (!additive)
        m_Impl->ClearSelection();
    std::vector<CanvasItemId> candidates;
    m_Impl->m_Scene.Query(bounds, candidates);
    std::unordered_set<std::uint64_t> selected;
    selected.reserve(m_Impl->m_Selection.size() + candidates.size());
    for (auto id : m_Impl->m_Selection)
        selected.insert(KeyOf(id));
    std::sort(candidates.begin(), candidates.end(), [this](auto first, auto second)
              { return m_Impl->m_Scene.Get(first)->m_Order < m_Impl->m_Scene.Get(second)->m_Order; });
    for (auto id : candidates)
    {
        if (ContainsBounds(bounds, m_Impl->m_Scene.Get(id)->m_Geometry.Bounds()) && selected.insert(KeyOf(id)).second)
        {
            m_Impl->m_Selection.push_back(id);
            m_Impl->m_Scene.SetSelected(id, true);
        }
    }
}

void SketchDocument::ClearSelection()
{
    m_Impl->ClearSelection();
}

std::vector<CanvasItemId> SketchDocument::GetSelection() const
{
    std::vector<CanvasItemId> result;
    result.reserve(m_Impl->m_Selection.size());
    for (auto id : m_Impl->m_Selection)
        if (m_Impl->m_Scene.Get(id))
            result.push_back(id);
    return result;
}

bool SketchDocument::MoveSelection(CanvasPoint delta)
{
    ValidatePoint(delta);
    if (delta.m_X == 0 && delta.m_Y == 0)
        return false;
    Impl::Command command;
    command.m_BeforeSelection = m_Impl->SelectionRecords();
    command.m_AfterSelection = command.m_BeforeSelection;
    command.m_Changes.reserve(command.m_BeforeSelection.size());
    auto intermediateBounds = m_Impl->m_Scene.GetBounds();
    for (const auto& record : command.m_BeforeSelection)
    {
        const auto before = Impl::Snapshot(*m_Impl->m_Scene.Get(record->m_Current));
        auto after = before;
        after.m_Geometry = TranslateGeometry(before.m_Geometry, delta);
        const auto bounds = after.m_Geometry.Bounds();
        if (!IsFinite(after.m_Geometry.m_A) || !IsFinite(after.m_Geometry.m_B) || !std::isfinite(bounds.m_X) ||
            !std::isfinite(bounds.m_Y) || !std::isfinite(bounds.m_Width) || !std::isfinite(bounds.m_Height) ||
            !std::isfinite(bounds.m_X + bounds.m_Width) || !std::isfinite(bounds.m_Y + bounds.m_Height))
            throw std::invalid_argument("Sketch translation exceeds finite geometry");
        const auto left = std::min(intermediateBounds.m_X, bounds.m_X);
        const auto top = std::min(intermediateBounds.m_Y, bounds.m_Y);
        const auto right = std::max(intermediateBounds.m_X + intermediateBounds.m_Width, bounds.m_X + bounds.m_Width);
        const auto bottom =
            std::max(intermediateBounds.m_Y + intermediateBounds.m_Height, bounds.m_Y + bounds.m_Height);
        if (!std::isfinite(right - left) || !std::isfinite(bottom - top))
            throw std::invalid_argument("Sketch translation exceeds finite scene bounds");
        intermediateBounds = {left, top, right - left, bottom - top};
        command.m_Changes.push_back({record, before, after});
    }
    if (command.m_Changes.empty())
        return false;
    if (!m_Impl->FitsHistory(command))
        return false;
    for (const auto& change : command.m_Changes)
        m_Impl->Apply(change, true);
    m_Impl->Commit(std::move(command));
    return true;
}

bool SketchDocument::DeleteSelection()
{
    Impl::Command command;
    command.m_BeforeSelection = m_Impl->SelectionRecords();
    command.m_Changes.reserve(command.m_BeforeSelection.size());
    for (const auto& record : command.m_BeforeSelection)
        command.m_Changes.push_back({record, Impl::Snapshot(*m_Impl->m_Scene.Get(record->m_Current)), {}});
    if (command.m_Changes.empty())
        return false;
    if (!m_Impl->FitsHistory(command))
        return false;
    m_Impl->ClearSelection();
    for (const auto& change : command.m_Changes)
        m_Impl->Apply(change, true);
    m_Impl->Commit(std::move(command));
    return true;
}

bool SketchDocument::Undo()
{
    if (m_Impl->m_Undo.empty())
        return false;
    auto command = std::move(m_Impl->m_Undo.back());
    m_Impl->m_Undo.pop_back();
    for (auto it = command.m_Changes.rbegin(); it != command.m_Changes.rend(); ++it)
        m_Impl->Apply(*it, false);
    m_Impl->RestoreSelection(command.m_BeforeSelection);
    m_Impl->m_Redo.push_back(std::move(command));
    return true;
}

bool SketchDocument::Redo()
{
    if (m_Impl->m_Redo.empty())
        return false;
    auto command = std::move(m_Impl->m_Redo.back());
    m_Impl->m_Redo.pop_back();
    for (const auto& change : command.m_Changes)
        m_Impl->Apply(change, true);
    m_Impl->RestoreSelection(command.m_AfterSelection);
    m_Impl->m_Undo.push_back(std::move(command));
    return true;
}

bool SketchDocument::CanUndo() const
{
    return !m_Impl->m_Undo.empty();
}

bool SketchDocument::CanRedo() const
{
    return !m_Impl->m_Redo.empty();
}

CanvasPoint SketchDocument::Snap(CanvasPoint point, double worldTolerance, double gridSpacing, bool gridEnabled) const
{
    ValidatePoint(point);
    ValidateTolerance(worldTolerance);
    if (!std::isfinite(gridSpacing) || gridSpacing <= 0)
        throw std::invalid_argument("Sketch grid spacing must be finite and positive");
    std::vector<CanvasItemId> candidates;
    m_Impl->m_Scene.Query(
        {point.m_X - worldTolerance, point.m_Y - worldTolerance, worldTolerance * 2, worldTolerance * 2}, candidates);
    CanvasPoint result = point;
    double bestDistance = worldTolerance;
    bool found = false;
    const auto consider = [&](CanvasPoint candidate)
    {
        const auto distance = std::hypot(candidate.m_X - point.m_X, candidate.m_Y - point.m_Y);
        const bool lexicographicallyFirst =
            candidate.m_X < result.m_X || (candidate.m_X == result.m_X && candidate.m_Y < result.m_Y);
        if (distance <= bestDistance && (distance < bestDistance || !found || lexicographicallyFirst))
        {
            bestDistance = distance;
            result = candidate;
            found = true;
        }
    };
    for (auto id : candidates)
    {
        const auto& geometry = m_Impl->m_Scene.Get(id)->m_Geometry;
        consider(geometry.m_A);
        if (geometry.m_Kind == CanvasPrimitive::Circle)
        {
            consider({geometry.m_A.m_X + geometry.m_Radius, geometry.m_A.m_Y});
            consider({geometry.m_A.m_X - geometry.m_Radius, geometry.m_A.m_Y});
            consider({geometry.m_A.m_X, geometry.m_A.m_Y + geometry.m_Radius});
            consider({geometry.m_A.m_X, geometry.m_A.m_Y - geometry.m_Radius});
        }
        else
        {
            consider(geometry.m_B);
            consider(
                {geometry.m_A.m_X * 0.5 + geometry.m_B.m_X * 0.5, geometry.m_A.m_Y * 0.5 + geometry.m_B.m_Y * 0.5});
            if (geometry.m_Kind == CanvasPrimitive::Rectangle)
            {
                consider({geometry.m_A.m_X, geometry.m_B.m_Y});
                consider({geometry.m_B.m_X, geometry.m_A.m_Y});
            }
        }
    }
    if (!found && gridEnabled)
    {
        const auto x = std::round(point.m_X / gridSpacing) * gridSpacing;
        const auto y = std::round(point.m_Y / gridSpacing) * gridSpacing;
        if (std::isfinite(x) && std::isfinite(y))
            result = {x, y};
    }
    return result;
}

std::size_t SketchDocument::GetHistoryBytes() const
{
    return m_Impl->m_HistoryBytes;
}
} // namespace EStudioUI
