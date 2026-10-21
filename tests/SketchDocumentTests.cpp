#include <EStudioUI/SketchDocument.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Callback> void Throws(Callback callback, const std::string& message)
{
    bool threw = false;
    try
    {
        callback();
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    Check(threw, message);
}

CanvasGeometry Line(CanvasPoint from = {0, 0}, CanvasPoint to = {10, 0})
{
    return {CanvasPrimitive::Line, from, to, 0};
}

bool Near(CanvasPoint point, CanvasPoint expected)
{
    return std::hypot(point.m_X - expected.m_X, point.m_Y - expected.m_Y) < 0.000001;
}

CanvasItemId OnlySelection(const SketchDocument& document)
{
    const auto selection = document.GetSelection();
    Check(selection.size() == 1, "one live item selected");
    return selection.front();
}

void PrimitiveEditing()
{
    SketchDocument document;
    const auto line = document.AddShape(Line({0, 0}, {20, 20}), 3);
    const auto rectangle = document.AddShape({CanvasPrimitive::Rectangle, {50, 30}, {30, 10}, 0}, 5);
    const auto circle = document.AddShape({CanvasPrimitive::Circle, {80, 20}, {}, 10}, 7);
    Check(document.GetScene().Size() == 3 && OnlySelection(document) == circle, "all three primitives created");
    Check(document.GetScene().Get(circle)->m_Selected && !document.GetScene().Get(line)->m_Selected,
          "selection flags match document selection");
    Check(document.Pick({10, 11}, 1) == line, "segment narrow phase picks near diagonal stroke");
    Check(!document.Pick({10, 11}, 0.5), "diagonal AABB candidate rejected outside stroke tolerance");
    Check(document.Pick({30, 20}, 0) == rectangle, "rectangle exact boundary can be picked");
    Check(!document.Pick({40, 20}, 2), "rectangle interior does not select its boundary");
    Check(document.Pick({90, 20}, 0) == circle && !document.Pick({80, 20}, 1), "circle outline picking is analytic");
    document.Select(line);
    document.Select(rectangle, true);
    Check(document.GetSelection().size() == 2, "additive selection adds an item");
    document.Select(line, true);
    Check(OnlySelection(document) == rectangle, "additive selection toggles existing item");
    document.Select({}, true);
    Check(OnlySelection(document) == rectangle, "additive background click leaves selection intact");
    document.Select({});
    Check(document.GetSelection().empty(), "background click clears replacement selection");
    document.SelectBox({-1, -1, 52, 32});
    Check(document.GetSelection().size() == 2, "box selects fully enclosed segment and rectangle");
    document.SelectBox({79, 19, 11, 11});
    Check(document.GetSelection().empty(), "box crossing only part of a circle does not enclose it");
    document.SelectBox({70, 10, 20, 20});
    document.SelectBox({-1, -1, 22, 22}, true);
    Check(document.GetSelection().size() == 2, "additive box selection retains previous items");
    document.SelectBox({-1, -1, 22, 22}, true);
    Check(document.GetSelection().size() == 2, "additive box selection does not duplicate IDs");
    Check(document.MoveSelection({10, -10}), "group move is one edit");
    Check(document.GetScene().Get(line)->m_Geometry.m_A == CanvasPoint{10, -10} &&
              document.GetScene().Get(circle)->m_Geometry.m_A == CanvasPoint{90, 10},
          "group move updates all selected geometry");
    Check(document.GetScene().Get(rectangle)->m_Geometry.m_A == CanvasPoint{50, 30},
          "group move leaves unselected geometry alone");
    Check(document.Pick({100, 10}, 0) == circle && !document.Pick({80, 30}, 0),
          "moved geometry updates BVH searches immediately");
    Check(document.Undo(), "group move undo");
    Check(document.GetScene().Get(line)->m_Geometry.m_A == CanvasPoint{0, 0} &&
              document.GetScene().Get(circle)->m_Geometry.m_A == CanvasPoint{80, 20},
          "one undo restores both selected primitives");
}

void IdentityAndCommandHistory()
{
    SketchDocument document;
    const auto original = document.AddShape(Line(), 11);
    const auto order = document.GetScene().Get(original)->m_Order;
    Check(document.MoveSelection({20, 30}), "move after create");
    Check(document.DeleteSelection(), "delete after move");
    Check(!document.GetScene().Get(original) && document.GetSelection().empty(), "delete invalidates public ID");
    Check(document.Undo(), "undo deletion");
    const auto restored = OnlySelection(document);
    Check(restored != original && !document.GetScene().Get(original), "restoration cannot resurrect stale IDs");
    Check(document.GetScene().Get(restored)->m_Geometry.m_A == CanvasPoint{20, 30} &&
              document.GetScene().Get(restored)->m_Style == 11 && document.GetScene().Get(restored)->m_Order == order,
          "restoration preserves shape style and painter order");
    Check(document.Undo(), "undo move references restored logical item");
    Check(document.GetScene().Get(restored)->m_Geometry == Line(), "old command targets new generational ID");
    Check(document.Undo() && document.GetScene().Size() == 0 && !document.CanUndo(), "undo creation after restoration");
    Check(document.Redo() && document.GetScene().Size() == 1, "redo creation");
    const auto readded = OnlySelection(document);
    Check(document.Redo() && document.GetScene().Get(readded)->m_Geometry.m_A == CanvasPoint{20, 30},
          "redo move targets newly recreated ID");
    Check(document.Redo() && document.GetScene().Size() == 0 && !document.CanRedo(), "redo deletion");
    Check(document.Undo(), "restore before branching history");
    document.AddShape(Line({-30, 0}, {-20, 0}));
    Check(!document.CanRedo(), "new edit discards redo branch");
    Check(document.Undo() && document.GetSelection().size() == 1, "undo new creation restores previous selection");

    document.Clear();
    const auto bottom = document.AddShape(Line());
    const auto top = document.AddShape(Line());
    Check(document.Pick({5, 0}, 0) == top, "equal-distance picking prefers painter top");
    document.Select(bottom);
    document.DeleteSelection();
    document.Undo();
    Check(document.Pick({5, 0}, 0) == top, "restoring old geometry preserves painter and picking order");
    document.Clear();
    Check(document.GetScene().Size() == 0 && document.GetSelection().empty() && !document.CanUndo() &&
              !document.CanRedo() && document.GetHistoryBytes() == 0,
          "clear discards geometry selection and commands");
}

void SnappingAndLargeCoordinates()
{
    SketchDocument document;
    document.AddShape(Line({3, 4}, {23, 4}));
    document.AddShape({CanvasPrimitive::Rectangle, {40, 10}, {60, 30}, 0});
    document.AddShape({CanvasPrimitive::Circle, {90, 30}, {}, 10});
    Check(Near(document.Snap({3.2, 3.8}, 1, 10), {3, 4}), "endpoint snap takes precedence over grid");
    Check(Near(document.Snap({12.9, 4.2}, 1, 10), {13, 4}), "line midpoint snap");
    Check(Near(document.Snap({39.8, 29.8}, 1, 10), {40, 30}), "derived rectangle corner snap");
    Check(Near(document.Snap({50.2, 20.2}, 1, 10), {50, 20}), "rectangle center snap");
    Check(Near(document.Snap({89.8, 30.2}, 1, 10), {90, 30}), "circle center snap");
    Check(Near(document.Snap({100.2, 30.1}, 1, 10), {100, 30}), "circle cardinal snap");
    Check(Near(document.Snap({-23, -16}, 1, 10), {-20, -20}), "negative grid coordinates round symmetrically");
    Check(Near(document.Snap({-23, -16}, 1, 10, false), {-23, -16}), "disabled grid preserves unsnapped point");
    Check(Near(document.Snap({3, 4}, 0, 10, false), {3, 4}), "zero tolerance exact endpoint snap");
    Check(Near(document.Snap({0, 0}, 4.99, 10, false), {0, 0}), "snap uses Euclidean tolerance after BVH query");
    document.AddShape(Line({1e12 + 0.25, -1e12 + 0.5}, {1e12 + 10.25, -1e12 + 0.5}));
    Check(Near(document.Snap({1e12 + 0.3, -1e12 + 0.6}, 0.2, 1, false), {1e12 + 0.25, -1e12 + 0.5}),
          "double precision preserves subunit snapping far from origin");
}

void SpatialFilteringAndBudgets()
{
    SketchDocument document;
    auto& scene = document.GetScene();
    scene.Reserve(200000);
    for (int i = 0; i < 200000; ++i)
    {
        const auto x = static_cast<double>(i % 500) * 50;
        const auto y = static_cast<double>(i / 500) * 50;
        scene.Add(Line({x, y}, {x + 10, y + 10}));
    }
    document.ClearHistory();
    Check(static_cast<bool>(document.Pick({4, 4}, 1)), "pick in 200000-item imported scene");
    Check(scene.GetLastQueryVisits() < 1000, "pick traverses local BVH candidates rather than all items");
    Check(Near(document.Snap({0.2, 0.3}, 1, 10, false), {0, 0}), "snap in 200000-item imported scene");
    Check(scene.GetLastQueryVisits() < 1000, "snapping shares BVH filtering");
    document.SelectBox({-1, -1, 12, 12});
    const auto selection = OnlySelection(document);
    Check(scene.GetLastQueryVisits() < 1000, "selection box uses local BVH candidates");
    scene.Remove(selection);
    Check(document.GetSelection().empty(), "direct removals do not expose stale selected IDs");
    document.ClearHistory();
    Check(!document.DeleteSelection(), "stale selection cannot generate a deletion command");
    scene.Add(Line());
    document.SelectBox(scene.GetBounds());
    Check(document.GetSelection().size() == 200000, "large whole-scene selection");
    Check(document.MoveSelection({20, 30}) && document.CanUndo(), "200000-item move remains undoable");
    Check(document.Undo() && document.GetSelection().size() == 200000 && static_cast<bool>(document.Pick({0, 0}, 0)),
          "200000-item move undo restores geometry and selection");
    Check(document.DeleteSelection() && scene.Size() == 0 && document.CanUndo(), "200000-item delete remains undoable");
    Check(document.Undo() && scene.Size() == 200000 && document.GetSelection().size() == 200000,
          "200000-item delete undo restores geometry and selection");
    Check(document.GetHistoryBytes() <= 128 * 1024 * 1024, "large commands obey the payload budget");
    document.Clear();
    for (int i = 0; i < 160; ++i)
        document.AddShape(Line({i * 20.0, 0}, {i * 20.0 + 10, 0}));
    int undos = 0;
    while (document.Undo())
        ++undos;
    Check(undos == 128 && scene.Size() == 32, "history retains at most 128 edit commands");
    Check(document.GetHistoryBytes() <= 128 * 1024 * 1024, "command payload stays within documented budget");
    for (int i = 0; i < undos; ++i)
        Check(document.Redo(), "bounded commands remain replayable");
    Check(scene.Size() == 160, "bounded history does not drop live items");
}

void InvalidInputAndNoOps()
{
    SketchDocument document;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto infinity = std::numeric_limits<double>::infinity();
    Check(!document.Undo() && !document.Redo() && !document.MoveSelection({1, 2}) && !document.DeleteSelection(),
          "empty edits are no-ops");
    const auto id = document.AddShape(Line());
    document.ClearHistory();
    Check(!document.MoveSelection({0, 0}) && !document.CanUndo(), "zero translation does not create history");
    Throws([&] { document.AddShape(Line({nan, 0}, {10, 0})); }, "nonfinite creation rejected");
    Throws([&] { document.MoveSelection({infinity, 0}); }, "nonfinite translation rejected");
    Throws([&] { document.Pick({nan, 0}, 1); }, "nonfinite picking point rejected");
    Throws([&] { document.Pick({0, 0}, -1); }, "negative picking tolerance rejected");
    Throws([&] { document.Snap({0, 0}, 1, 0); }, "zero grid spacing rejected");
    Throws([&] { document.Snap({0, 0}, nan, 1); }, "nonfinite snap tolerance rejected");
    Throws([&] { document.SelectBox({0, 0, -1, 1}); }, "negative selection extent rejected");
    Check(document.GetScene().Size() == 1 && document.GetScene().Get(id)->m_Geometry == Line() &&
              OnlySelection(document) == id && !document.CanUndo(),
          "invalid edits preserve geometry selection and history");
}

void DocumentPersistence()
{
    SketchDocument document;
    const auto original = document.AddShape(Line({1e12 + 0.25, -1e12 + 0.5}, {1e12 + 10.25, -1e12 + 0.5}), 42);
    document.AddShape({CanvasPrimitive::Rectangle, {-2.125, 9.875}, {17.123456789012345, -18.75}, 0}, 17);
    const auto circle = document.AddShape({CanvasPrimitive::Circle, {1.0 / 3, 1.0 / 7}, {}, 3.141592653589793}, 23);
    document.Select(original);
    document.DeleteSelection();
    document.Undo();
    const auto restored = OnlySelection(document);
    const auto before = document.GetScene().Get(restored)->m_Geometry;
    std::ostringstream saved;
    document.Save(saved);
    Check(saved.str().starts_with("ESTUDIOUI_SKETCH 1\n3\nline"),
          "versioned file emits painter order after restoration");
    Check(saved.str().find("rectangle") != std::string::npos && saved.str().find("circle") != std::string::npos,
          "document format uses readable primitive names");
    auto* sceneAddress = &document.GetScene();
    const auto revision = sceneAddress->GetRevision();
    std::istringstream input(saved.str());
    document.Load(input);
    Check(&document.GetScene() == sceneAddress && document.GetScene().GetRevision() > revision,
          "load preserves scene references and advances invalidation revision");
    Check(document.GetScene().Size() == 3 && document.GetSelection().empty() && !document.CanUndo() &&
              !document.CanRedo() && document.GetHistoryBytes() == 0,
          "successful load replaces geometry and clears selection and history");
    Check(!document.GetScene().Get(restored) && !document.GetScene().Get(circle),
          "load invalidates prior scene identities");
    const auto loaded = document.Pick(before.m_A, 0);
    Check(document.GetScene().Get(loaded)->m_Geometry == before && document.GetScene().Get(loaded)->m_Style == 42,
          "saved doubles and CSS style indices round trip exactly");
    std::ostringstream secondSave;
    document.Save(secondSave);
    Check(secondSave.str() == saved.str(), "repeated save/load is deterministic");

    document.AddShape(Line({100, 200}, {110, 210}));
    const auto selected = OnlySelection(document);
    const auto count = document.GetScene().Size();
    const auto currentRevision = document.GetScene().GetRevision();
    const std::string malformed[] = {"",
                                     "ESTUDIOUI_SKETCH 2\n0\n",
                                     "ESTUDIOUI_SKETCH 1\n1000001\n",
                                     "ESTUDIOUI_SKETCH 1\n-1\n",
                                     "ESTUDIOUI_SKETCH 1\n1\nline 0 0 1 1 0 0 trailing\n",
                                     "ESTUDIOUI_SKETCH 1\n2\nline 0 0 1 1 0 0\n",
                                     "ESTUDIOUI_SKETCH 1\n1\nline 0 0 1 1 0 -1\n",
                                     "ESTUDIOUI_SKETCH 1\n1\nline 0 0 1 1 0 4294967296\n",
                                     "ESTUDIOUI_SKETCH 1\n1\ncircle 0 0 0 0 -1 0\n",
                                     "ESTUDIOUI_SKETCH 1\n1\nline nan 0 1 1 0 0\n",
                                     "ESTUDIOUI_SKETCH 1\n1\nline 0 0 1e999 1 0 0\n",
                                     "ESTUDIOUI_SKETCH 1\n1\narc 0 0 1 1 0 0\n",
                                     "ESTUDIOUI_SKETCH 1\n0\ntrailing data\n",
                                     "ESTUDIOUI_SKETCH 1\n1\n" + std::string(1025, '1')};
    for (const auto& text : malformed)
    {
        std::istringstream invalid(text);
        Throws([&] { document.Load(invalid); }, "malformed persisted sketch is rejected");
        Check(document.GetScene().Size() == count && document.GetScene().GetRevision() == currentRevision &&
                  OnlySelection(document) == selected && document.CanUndo(),
              "failed load preserves scene selection and command history atomically");
    }

    struct Commas final : std::numpunct<char>
    {
        char do_decimal_point() const override
        {
            return ',';
        }

        char do_thousands_sep() const override
        {
            return '_';
        }

        std::string do_grouping() const override
        {
            return "\1";
        }
    };

    document.Clear();
    for (int i = 0; i < 12; ++i)
        document.AddShape(Line({i + 0.25, 0.75}, {i + 0.5, 1.5}));
    std::ostringstream localized;
    localized.imbue(std::locale(std::locale::classic(), new Commas));
    document.Save(localized);
    Check(localized.str().starts_with("ESTUDIOUI_SKETCH 1\n12\n"), "file item count is independent of caller locale");
    std::istringstream localizedInput(localized.str());
    localizedInput.imbue(std::locale(std::locale::classic(), new Commas));
    document.Load(localizedInput);
    Check(document.GetScene().Size() == 12 && document.GetScene().GetItems().front().m_Geometry.m_A.m_X == 0.25,
          "file coordinates are independent of caller locale");
    std::istringstream empty("ESTUDIOUI_SKETCH 1\n0\n");
    document.Load(empty);
    Check(document.GetScene().Size() == 0, "empty persisted sketch replaces current scene");
}
} // namespace

int main()
{
    try
    {
        PrimitiveEditing();
        IdentityAndCommandHistory();
        SnappingAndLargeCoordinates();
        SpatialFilteringAndBudgets();
        InvalidInputAndNoOps();
        DocumentPersistence();
        std::cout << "Sketch document checks passed: " << s_Checks << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Sketch document check failed: " << exception.what() << '\n';
        return 1;
    }
}
