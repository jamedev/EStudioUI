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
            std::size_t bytes = sizeof(Command) +
                                m_Changes.capacity() * (sizeof(Change) + sizeof(Record) + 2 * sizeof(void*)) +
                                (m_BeforeSelection.capacity() + m_AfterSelection.capacity()) *
                                    (sizeof(std::shared_ptr<Record>) + sizeof(Record) + 2 * sizeof(void*));
            for (const auto& change : m_Changes)
            {
                if (change.m_Before)
                    bytes += GeometryBytes(change.m_Before->m_Geometry);
                if (change.m_After)
                    bytes += GeometryBytes(change.m_After->m_Geometry);
            }
            return bytes;
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

    bool Execute(Command command)
    {
        if (command.m_Changes.empty() || !FitsHistory(command))
            return false;
        auto aggregate = m_Scene.GetBounds();
        std::size_t newItems = 0;
        for (const auto& change : command.m_Changes)
        {
            if (!change.m_After)
                continue;
            const auto bounds = change.m_After->m_Geometry.Bounds();
            const double left = std::min(aggregate.m_X, bounds.m_X);
            const double top = std::min(aggregate.m_Y, bounds.m_Y);
            const double right = std::max(aggregate.m_X + aggregate.m_Width, bounds.m_X + bounds.m_Width);
            const double bottom = std::max(aggregate.m_Y + aggregate.m_Height, bounds.m_Y + bounds.m_Height);
            if (!std::isfinite(right - left) || !std::isfinite(bottom - top))
                throw std::invalid_argument("Sketch edit exceeds finite scene bounds");
            aggregate = {left, top, right - left, bottom - top};
            if (!m_Scene.Get(change.m_Record->m_Current))
                ++newItems;
        }
        m_Scene.Reserve(m_Scene.Size() + newItems);
        m_Records.reserve(m_Records.size() + newItems);
        const bool selectionChanged = command.m_BeforeSelection != command.m_AfterSelection;
        if (selectionChanged)
            ClearSelection();
        for (const auto& change : command.m_Changes)
            Apply(change, true);
        if (selectionChanged)
            RestoreSelection(command.m_AfterSelection);
        Commit(std::move(command));
        return true;
    }

    template <typename Transform> bool EditSelected(Transform transform)
    {
        Command command;
        command.m_BeforeSelection = SelectionRecords();
        command.m_AfterSelection = command.m_BeforeSelection;
        command.m_Changes.reserve(command.m_BeforeSelection.size());
        for (const auto& record : command.m_BeforeSelection)
        {
            const auto before = Snapshot(*m_Scene.Get(record->m_Current));
            auto after = before;
            if (transform(after.m_Geometry) && after.m_Geometry != before.m_Geometry)
            {
                after.m_Geometry.Bounds();
                command.m_Changes.push_back({record, before, after});
            }
        }
        return Execute(std::move(command));
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
    std::size_t remainingElements = 1000000;
    std::size_t remainingText = 16 * 1024 * 1024;
    const auto checkLimits = [&](const auto& self, const CanvasGeometry& geometry, unsigned depth) -> void
    {
        if (depth > 32 || remainingElements == 0)
            throw std::invalid_argument("Sketch document exceeds geometry nesting or element limits");
        --remainingElements;
        if (!geometry.m_Data)
            return;
        const auto& data = *geometry.m_Data;
        if (data.m_Points.size() > remainingElements || data.m_Text.size() > remainingText)
            throw std::invalid_argument("Sketch document exceeds point or text limits");
        remainingElements -= data.m_Points.size();
        remainingText -= data.m_Text.size();
        for (const auto& child : data.m_Children)
            self(self, child, depth + 1);
    };
    for (const auto* item : ordered)
        checkLimits(checkLimits, item->m_Geometry, 0);
    output << "ESTUDIOUI_SKETCH 2\n" << std::to_string(ordered.size()) << '\n';
    const auto writeGeometry = [&](const auto& self, const CanvasGeometry& geometry, std::uint32_t style) -> void
    {
        static constexpr const char* names[] = {"line",   "rectangle", "circle", "arc",
                                                "spline", "path",      "text",   "group"};
        std::ostringstream row;
        row.imbue(std::locale::classic());
        row << std::setprecision(std::numeric_limits<double>::max_digits10);
        row << names[static_cast<unsigned>(geometry.m_Kind)] << ' ' << geometry.m_A.m_X << ' ' << geometry.m_A.m_Y
            << ' ' << geometry.m_B.m_X << ' ' << geometry.m_B.m_Y << ' ' << geometry.m_Radius << ' ' << style;
        const auto* data = geometry.m_Data.get();
        switch (geometry.m_Kind)
        {
        case CanvasPrimitive::Arc:
            row << ' ' << data->m_StartAngle << ' ' << data->m_SweepAngle;
            break;
        case CanvasPrimitive::Spline:
            row << ' ' << data->m_Points.size();
            break;
        case CanvasPrimitive::Path:
            row << ' ' << data->m_Points.size() << ' ' << (data->m_Closed ? 1 : 0);
            break;
        case CanvasPrimitive::Text:
            row << ' ' << data->m_FontSize << ' ' << data->m_Text.size();
            break;
        case CanvasPrimitive::Group:
            row << ' ' << data->m_Children.size() << ' ' << (data->m_ChildStyles.empty() ? 0 : 1);
            break;
        default:
            break;
        }
        output << row.str() << '\n';
        if (geometry.m_Kind == CanvasPrimitive::Spline || geometry.m_Kind == CanvasPrimitive::Path)
        {
            for (const auto point : data->m_Points)
            {
                row.str({});
                row << point.m_X << ' ' << point.m_Y << '\n';
                output << row.str();
            }
        }
        else if (geometry.m_Kind == CanvasPrimitive::Text)
        {
            output.write(data->m_Text.data(), static_cast<std::streamsize>(data->m_Text.size()));
            output.put('\n');
        }
        else if (geometry.m_Kind == CanvasPrimitive::Group)
        {
            for (std::size_t i = 0; i < data->m_Children.size(); ++i)
                self(self, data->m_Children[i], data->m_ChildStyles.empty() ? 0 : data->m_ChildStyles[i]);
        }
        if (!output)
            throw std::runtime_error("Cannot write sketch document");
    };
    for (const auto* item : ordered)
        writeGeometry(writeGeometry, item->m_Geometry, item->m_Style);
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
    const auto readUnsigned = [&](std::istringstream& row, std::uint64_t maximum)
    {
        std::string token;
        if (!(row >> token))
            throw std::invalid_argument("Missing sketch document unsigned integer");
        return parseUnsigned(token, maximum);
    };
    std::string line;
    std::string magic;
    std::string version;
    if (!readLine(line))
        throw std::invalid_argument("Missing sketch document header");
    std::istringstream header(line);
    header.imbue(std::locale::classic());
    if (!(header >> magic >> version) || magic != "ESTUDIOUI_SKETCH" || (version != "1" && version != "2") ||
        !exhausted(header))
        throw std::invalid_argument("Unsupported sketch document header");
    if (!readLine(line))
        throw std::invalid_argument("Missing sketch document item count");
    std::istringstream countRow(line);
    countRow.imbue(std::locale::classic());
    const auto count = readUnsigned(countRow, 1000000);
    if (!exhausted(countRow))
        throw std::invalid_argument("Invalid sketch document item count");
    auto replacement = std::make_unique<Impl>();
    replacement->m_Scene.Reserve(static_cast<std::size_t>(count));
    std::uint64_t remainingElements = 1000000;
    std::uint64_t remainingText = 16 * 1024 * 1024;
    const auto readGeometry = [&](const auto& self, unsigned depth) -> std::pair<CanvasGeometry, std::uint32_t>
    {
        if (depth > 32 || remainingElements == 0)
            throw std::invalid_argument("Sketch document exceeds geometry nesting or element limits");
        --remainingElements;
        if (!readLine(line))
            throw std::invalid_argument("Truncated sketch document");
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        std::string kind;
        CanvasGeometry geometry;
        if (!(row >> kind >> geometry.m_A.m_X >> geometry.m_A.m_Y >> geometry.m_B.m_X >> geometry.m_B.m_Y >>
              geometry.m_Radius))
            throw std::invalid_argument("Invalid sketch document geometry row");
        const auto style = static_cast<std::uint32_t>(readUnsigned(row, std::numeric_limits<std::uint32_t>::max()));
        if (kind == "line")
            geometry.m_Kind = CanvasPrimitive::Line;
        else if (kind == "rectangle")
            geometry.m_Kind = CanvasPrimitive::Rectangle;
        else if (kind == "circle")
            geometry.m_Kind = CanvasPrimitive::Circle;
        else if (version == "2" && kind == "arc")
            geometry.m_Kind = CanvasPrimitive::Arc;
        else if (version == "2" && kind == "spline")
            geometry.m_Kind = CanvasPrimitive::Spline;
        else if (version == "2" && kind == "path")
            geometry.m_Kind = CanvasPrimitive::Path;
        else if (version == "2" && kind == "text")
            geometry.m_Kind = CanvasPrimitive::Text;
        else if (version == "2" && kind == "group")
            geometry.m_Kind = CanvasPrimitive::Group;
        else
            throw std::invalid_argument("Unsupported sketch document primitive");
        std::shared_ptr<CanvasGeometryData> data;
        if (geometry.m_Kind >= CanvasPrimitive::Arc)
            data = std::make_shared<CanvasGeometryData>();
        std::uint64_t payloadCount = 0;
        bool childStyles = false;
        switch (geometry.m_Kind)
        {
        case CanvasPrimitive::Arc:
            if (!(row >> data->m_StartAngle >> data->m_SweepAngle))
                throw std::invalid_argument("Invalid sketch document arc");
            break;
        case CanvasPrimitive::Spline:
        case CanvasPrimitive::Path:
            payloadCount = readUnsigned(row, std::min<std::uint64_t>(65536, remainingElements));
            remainingElements -= payloadCount;
            if (geometry.m_Kind == CanvasPrimitive::Path)
                data->m_Closed = readUnsigned(row, 1) != 0;
            break;
        case CanvasPrimitive::Text:
            if (!(row >> data->m_FontSize))
                throw std::invalid_argument("Invalid sketch document text size");
            payloadCount = readUnsigned(row, std::min<std::uint64_t>(65536, remainingText));
            remainingText -= payloadCount;
            break;
        case CanvasPrimitive::Group:
            payloadCount = readUnsigned(row, remainingElements);
            childStyles = readUnsigned(row, 1) != 0;
            break;
        default:
            break;
        }
        if (!exhausted(row))
            throw std::invalid_argument("Unexpected sketch document geometry fields");
        if (geometry.m_Kind == CanvasPrimitive::Spline || geometry.m_Kind == CanvasPrimitive::Path)
        {
            data->m_Points.reserve(static_cast<std::size_t>(payloadCount));
            for (std::uint64_t i = 0; i < payloadCount; ++i)
            {
                if (!readLine(line))
                    throw std::invalid_argument("Truncated sketch document curve");
                std::istringstream pointRow(line);
                pointRow.imbue(std::locale::classic());
                CanvasPoint point;
                if (!(pointRow >> point.m_X >> point.m_Y) || !exhausted(pointRow))
                    throw std::invalid_argument("Invalid sketch document curve point");
                data->m_Points.push_back(point);
            }
        }
        else if (geometry.m_Kind == CanvasPrimitive::Text)
        {
            data->m_Text.resize(static_cast<std::size_t>(payloadCount));
            if (!input.read(data->m_Text.data(), static_cast<std::streamsize>(payloadCount)))
                throw std::invalid_argument("Truncated sketch document text");
            char delimiter = 0;
            if (!input.get(delimiter))
                throw std::invalid_argument("Missing sketch document text delimiter");
            if (delimiter == '\r' && !input.get(delimiter))
                throw std::invalid_argument("Missing sketch document text delimiter");
            if (delimiter != '\n')
                throw std::invalid_argument("Invalid sketch document text delimiter");
        }
        else if (geometry.m_Kind == CanvasPrimitive::Group)
        {
            // A declared child count is untrusted until its rows have been consumed.
            // Bounded initial growth prevents nested truncated groups reserving the
            // entire file budget independently at every nesting level.
            const auto initialCapacity = static_cast<std::size_t>(std::min<std::uint64_t>(payloadCount, 1024));
            data->m_Children.reserve(initialCapacity);
            if (childStyles)
                data->m_ChildStyles.reserve(initialCapacity);
            for (std::uint64_t i = 0; i < payloadCount; ++i)
            {
                auto [child, styleIndex] = self(self, depth + 1);
                data->m_Children.push_back(std::move(child));
                if (childStyles)
                    data->m_ChildStyles.push_back(styleIndex);
                else if (styleIndex != 0)
                    throw std::invalid_argument("Inherited group child style must be zero");
            }
        }
        if (geometry.m_Kind == CanvasPrimitive::Group)
            geometry.m_Data =
                MakeCanvasGroup(geometry.m_A, std::move(data->m_Children), std::move(data->m_ChildStyles)).m_Data;
        else
            geometry.m_Data = std::move(data);
        geometry.Bounds();
        return {std::move(geometry), style};
    };
    for (std::uint64_t index = 0; index < count; ++index)
    {
        auto [geometry, style] = readGeometry(readGeometry, 0);
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
    geometry.Bounds();
    Impl::Command command;
    command.m_BeforeSelection = m_Impl->SelectionRecords();
    const auto record = std::make_shared<Impl::Record>();
    command.m_Changes.push_back({record, {}, Impl::State{geometry, style, 0}});
    command.m_AfterSelection = {record};
    if (!m_Impl->FitsHistory(command))
        return {};
    record->m_Current = m_Impl->m_Scene.Add(geometry, style);
    m_Impl->m_Records[KeyOf(record->m_Current)] = record;
    command.m_Changes.front().m_After = Impl::Snapshot(*m_Impl->m_Scene.Get(record->m_Current));
    Select(record->m_Current);
    m_Impl->Commit(std::move(command));
    return record->m_Current;
}

bool SketchDocument::UpdateShape(CanvasItemId item, const CanvasGeometry& geometry)
{
    const auto* current = m_Impl->m_Scene.Get(item);
    if (!current)
        return false;
    geometry.Bounds();
    if (current->m_Geometry == geometry)
        return false;
    Impl::Command command;
    command.m_BeforeSelection = m_Impl->SelectionRecords();
    command.m_AfterSelection = command.m_BeforeSelection;
    const auto before = Impl::Snapshot(*current);
    auto after = before;
    after.m_Geometry = geometry;
    command.m_Changes.push_back({m_Impl->Track(item), before, after});
    return m_Impl->Execute(std::move(command));
}

CanvasItemId SketchDocument::Pick(CanvasPoint point, double tolerance) const
{
    ValidatePoint(point);
    ValidateTolerance(tolerance);
    const CanvasBounds query{point.m_X - tolerance, point.m_Y - tolerance, tolerance * 2, tolerance * 2};
    std::vector<CanvasItemId> candidates;
    m_Impl->m_Scene.Query(query, candidates);
    const auto boundedDistance = [&](const auto& self, const CanvasGeometry& geometry) -> double
    {
        if (geometry.m_Kind != CanvasPrimitive::Group)
            return DistanceToItem(geometry, point);
        std::vector<std::size_t> children;
        QueryCanvasGroup(geometry, query, children);
        double distance = std::numeric_limits<double>::infinity();
        for (const auto index : children)
            distance =
                std::min(distance, self(self, TranslateGeometry(geometry.m_Data->m_Children[index], geometry.m_A)));
        return distance;
    };
    CanvasItemId result;
    double bestDistance = tolerance;
    std::uint64_t bestOrder = 0;
    for (auto id : candidates)
    {
        const auto& item = *m_Impl->m_Scene.Get(id);
        const auto distance = boundedDistance(boundedDistance, item.m_Geometry);
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

std::optional<CanvasBounds> SketchDocument::GetSelectionBounds() const
{
    std::optional<CanvasBounds> result;
    for (auto id : m_Impl->m_Selection)
    {
        const auto* item = m_Impl->m_Scene.Get(id);
        if (!item)
            continue;
        const auto bounds = item->m_Geometry.Bounds();
        if (!result)
            result = bounds;
        else
        {
            const auto left = std::min(result->m_X, bounds.m_X);
            const auto top = std::min(result->m_Y, bounds.m_Y);
            const auto right = std::max(result->m_X + result->m_Width, bounds.m_X + bounds.m_Width);
            const auto bottom = std::max(result->m_Y + result->m_Height, bounds.m_Y + bounds.m_Height);
            result = CanvasBounds{left, top, right - left, bottom - top};
        }
    }
    return result;
}

bool SketchDocument::MoveSelection(CanvasPoint delta)
{
    ValidatePoint(delta);
    if (delta.m_X == 0 && delta.m_Y == 0)
        return false;
    return m_Impl->EditSelected(
        [delta](CanvasGeometry& geometry)
        {
            geometry = TranslateGeometry(geometry, delta);
            return true;
        });
}

bool SketchDocument::DuplicateSelection(CanvasPoint delta)
{
    ValidatePoint(delta);
    Impl::Command command;
    command.m_BeforeSelection = m_Impl->SelectionRecords();
    auto ordered = command.m_BeforeSelection;
    std::sort(
        ordered.begin(), ordered.end(), [this](const auto& first, const auto& second)
        { return m_Impl->m_Scene.Get(first->m_Current)->m_Order < m_Impl->m_Scene.Get(second->m_Current)->m_Order; });
    std::uint64_t order = 0;
    for (const auto& item : m_Impl->m_Scene.GetItems())
        order = std::max(order, item.m_Order);
    if (!ordered.empty() && ordered.size() >= std::numeric_limits<std::uint64_t>::max() - order)
        throw std::invalid_argument("Sketch painter order is exhausted");
    command.m_Changes.reserve(ordered.size());
    command.m_AfterSelection.reserve(ordered.size());
    for (const auto& original : ordered)
    {
        auto after = Impl::Snapshot(*m_Impl->m_Scene.Get(original->m_Current));
        after.m_Geometry = TranslateGeometry(after.m_Geometry, delta);
        after.m_Order = ++order;
        const auto record = std::make_shared<Impl::Record>();
        command.m_Changes.push_back({record, {}, after});
        command.m_AfterSelection.push_back(record);
    }
    return m_Impl->Execute(std::move(command));
}

CanvasItemId SketchDocument::GroupSelection()
{
    Impl::Command command;
    command.m_BeforeSelection = m_Impl->SelectionRecords();
    if (command.m_BeforeSelection.size() < 2)
        return {};
    auto ordered = command.m_BeforeSelection;
    std::sort(
        ordered.begin(), ordered.end(), [this](const auto& first, const auto& second)
        { return m_Impl->m_Scene.Get(first->m_Current)->m_Order < m_Impl->m_Scene.Get(second->m_Current)->m_Order; });
    const auto bounds = *GetSelectionBounds();
    const CanvasPoint origin{bounds.m_X, bounds.m_Y};
    std::vector<CanvasGeometry> children;
    std::vector<std::uint32_t> styles;
    children.reserve(ordered.size());
    styles.reserve(ordered.size());
    command.m_Changes.reserve(ordered.size() + 1);
    for (const auto& record : ordered)
    {
        const auto before = Impl::Snapshot(*m_Impl->m_Scene.Get(record->m_Current));
        children.push_back(TranslateGeometry(before.m_Geometry, {-origin.m_X, -origin.m_Y}));
        styles.push_back(before.m_Style);
        command.m_Changes.push_back({record, before, {}});
    }
    const auto group = std::make_shared<Impl::Record>();
    const auto order = m_Impl->m_Scene.Get(ordered.back()->m_Current)->m_Order;
    command.m_Changes.push_back(
        {group, {}, Impl::State{MakeCanvasGroup(origin, std::move(children), std::move(styles)), 0, order}});
    command.m_AfterSelection = {group};
    return m_Impl->Execute(std::move(command)) ? group->m_Current : CanvasItemId{};
}

bool SketchDocument::UngroupSelection()
{
    Impl::Command command;
    command.m_BeforeSelection = m_Impl->SelectionRecords();
    std::unordered_set<std::uint64_t> groups;
    std::unordered_set<std::uint64_t> selected;
    for (const auto& record : command.m_BeforeSelection)
    {
        const auto* item = m_Impl->m_Scene.Get(record->m_Current);
        selected.insert(KeyOf(item->m_Id));
        if (item->m_Geometry.m_Kind == CanvasPrimitive::Group)
            groups.insert(KeyOf(item->m_Id));
    }
    if (groups.empty())
        return false;
    std::vector<const CanvasItem*> ordered;
    ordered.reserve(m_Impl->m_Scene.Size());
    for (const auto& item : m_Impl->m_Scene.GetItems())
        ordered.push_back(&item);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto* first, const auto* second) { return first->m_Order < second->m_Order; });
    std::uint64_t order = 0;
    for (const auto* item : ordered)
    {
        const auto key = KeyOf(item->m_Id);
        const auto record = m_Impl->Track(item->m_Id);
        if (groups.contains(key))
        {
            command.m_Changes.push_back({record, Impl::Snapshot(*item), {}});
            const auto& data = *item->m_Geometry.m_Data;
            for (std::size_t i = 0; i < data.m_Children.size(); ++i)
            {
                const auto child = std::make_shared<Impl::Record>();
                const auto style = data.m_ChildStyles.empty() ? item->m_Style : data.m_ChildStyles[i];
                const auto geometry = TranslateGeometry(data.m_Children[i], item->m_Geometry.m_A);
                command.m_Changes.push_back({child, {}, Impl::State{geometry, style, ++order}});
                command.m_AfterSelection.push_back(child);
            }
        }
        else
        {
            const auto before = Impl::Snapshot(*item);
            auto after = before;
            after.m_Order = ++order;
            if (before.m_Order != after.m_Order)
                command.m_Changes.push_back({record, before, after});
            if (selected.contains(key))
                command.m_AfterSelection.push_back(record);
        }
    }
    return m_Impl->Execute(std::move(command));
}

bool SketchDocument::SetHorizontal(bool horizontal)
{
    return m_Impl->EditSelected(
        [horizontal](CanvasGeometry& geometry)
        {
            if (geometry.m_Kind != CanvasPrimitive::Line)
                return false;
            if (horizontal)
                geometry.m_B.m_Y = geometry.m_A.m_Y;
            else
                geometry.m_B.m_X = geometry.m_A.m_X;
            return true;
        });
}

bool SketchDocument::SetLineLength(double length)
{
    if (!std::isfinite(length) || length <= 0)
        throw std::invalid_argument("Sketch line length must be finite and positive");
    return m_Impl->EditSelected(
        [length](CanvasGeometry& geometry)
        {
            if (geometry.m_Kind != CanvasPrimitive::Line)
                return false;
            const auto dx = geometry.m_B.m_X - geometry.m_A.m_X;
            const auto dy = geometry.m_B.m_Y - geometry.m_A.m_Y;
            const auto scale = std::max(std::abs(dx), std::abs(dy));
            const auto normalizedLength = scale == 0 ? 1 : std::hypot(dx / scale, dy / scale);
            const auto x = scale == 0 ? 1 : (dx / scale) / normalizedLength;
            const auto y = scale == 0 ? 0 : (dy / scale) / normalizedLength;
            geometry.m_B = {geometry.m_A.m_X + x * length, geometry.m_A.m_Y + y * length};
            return true;
        });
}

bool SketchDocument::SetCircleRadius(double radius)
{
    if (!std::isfinite(radius) || radius <= 0)
        throw std::invalid_argument("Sketch circle radius must be finite and positive");
    return m_Impl->EditSelected(
        [radius](CanvasGeometry& geometry)
        {
            if (geometry.m_Kind != CanvasPrimitive::Circle && geometry.m_Kind != CanvasPrimitive::Arc)
                return false;
            geometry.m_Radius = radius;
            return true;
        });
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
    std::vector<CanvasPoint> snapPoints;
    for (auto id : candidates)
    {
        GeometrySnapPoints(
            m_Impl->m_Scene.Get(id)->m_Geometry,
            {point.m_X - worldTolerance, point.m_Y - worldTolerance, worldTolerance * 2, worldTolerance * 2},
            snapPoints);
        for (const auto candidate : snapPoints)
            consider(candidate);
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
