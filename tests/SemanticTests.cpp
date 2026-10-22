#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/Models.h>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
        throw std::runtime_error(message);
}

template <typename Callback> void Throws(Callback callback, const char* message)
{
    bool failed = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    Check(failed, message);
}

class Custom : public Widget
{
  public:
    mutable bool m_TryMutation = false;
    mutable bool m_TryNotify = false;
    int m_Invocations = 0;

    void PublishSemantics()
    {
        NotifySemanticsChanged();
    }

  protected:
    SemanticInfo DescribeSemantics() const override
    {
        if (m_TryMutation)
            const_cast<Custom*>(this)->Add<Widget>();
        if (m_TryNotify)
            const_cast<Custom*>(this)->NotifySemanticsChanged();
        SemanticInfo info;
        info.m_Role = SemanticRole::Button;
        info.m_Name = "Custom paint control";
        info.m_Actions = SemanticAction::Invoke;
        return info;
    }

    SemanticStatus OnSemanticAction(const SemanticCommand&) override
    {
        ++m_Invocations;
        return SemanticStatus::Succeeded;
    }
};

class ComputedRows final : public ModelData
{
  public:
    mutable std::size_t m_ItemReads = 0;
    mutable std::size_t m_ValueReads = 0;
    std::vector<ModelColumn> m_Columns{{"Name", ModelValueType::Text, false}};

    const std::vector<ModelColumn>& GetColumns() const override
    {
        return m_Columns;
    }

    std::size_t GetItemCount() const override
    {
        return 100000;
    }

    ItemId GetItemByIndex(std::size_t index) const override
    {
        return GetItem(index);
    }

    bool Contains(ItemId item) const override
    {
        return item >= 1 && item <= 100000;
    }

    std::size_t GetRowCount(ItemId parent = 0) const override
    {
        return parent ? 0 : 100000;
    }

    ItemId GetItem(std::size_t row, ItemId parent = 0) const override
    {
        ++m_ItemReads;
        if (parent || row >= 100000)
            throw std::out_of_range("Computed row out of range.");
        return row + 1;
    }

    ItemId GetParent(ItemId) const override
    {
        return 0;
    }

    std::optional<std::size_t> GetPosition(ItemId item) const override
    {
        return Contains(item) ? std::optional<std::size_t>(static_cast<std::size_t>(item - 1)) : std::nullopt;
    }

    ModelValue GetValue(ItemId item, std::size_t, ModelRole role = ModelRole::Edit) const override
    {
        ++m_ValueReads;
        if (role == ModelRole::Enabled)
            return true;
        return std::string("Entry ") + std::to_string(item - 1);
    }
};

class VirtualCustom final : public Widget
{
  public:
    mutable bool m_InvalidBounds = false;
    mutable bool m_InvalidParent = false;

  protected:
    std::size_t GetSemanticChildCount(SemanticKey parent) const override
    {
        return parent.m_Item ? 0 : 1;
    }

    std::optional<SemanticKey> GetSemanticChildAt(SemanticKey parent, std::size_t index) const override
    {
        return !parent.m_Item && !index ? std::optional<SemanticKey>({7, 0, 1}) : std::nullopt;
    }

    std::optional<SemanticVirtualNode> DescribeSemanticChild(SemanticKey key) const override
    {
        if (key != SemanticKey{7, 0, 1})
            return {};
        SemanticVirtualNode node;
        node.m_Info.m_Name = "Custom virtual item";
        node.m_Bounds = {0, 0, m_InvalidBounds ? std::numeric_limits<float>::quiet_NaN() : 30.0f, 20};
        if (m_InvalidParent)
            node.m_Parent = key;
        return node;
    }
};

void VirtualCollections(FontFace& font)
{
    UiContext ui(font);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetFloatingBounds({0, 0, 240, 180});
    auto counts = std::make_shared<ComputedRows>();
    auto model = std::make_shared<ItemModel>(ui.States(), ModelSnapshot(counts));
    list.SetModel(model);
    auto& hierarchy = ui.Root().Add<TreeWidget>();
    hierarchy.SetFloatingBounds({250, 0, 240, 180});
    const auto branch = hierarchy.AddItem(0, "Assembly");
    const auto leaf = hierarchy.AddItem(branch, "Part");
    auto& table = ui.Root().Add<TableWidget>();
    table.SetFloatingBounds({0, 200, 400, 180});
    table.SetColumns({{"Name", SizeRule::Fill()}, {"Value", SizeRule::Fill()}});
    table.SetRows({{"Zulu", "20"}, {"Alpha", "10"}});
    auto& canvas = ui.Root().Add<CanvasWidget>();
    canvas.SetFloatingBounds({410, 200, 350, 180});
    auto& scene = canvas.GetScene();
    const auto firstSceneItem = scene.Add({CanvasPrimitive::Rectangle, {-20, -20}, {20, 20}});
    const auto secondSceneItem = scene.Add(MakeCanvasText({1000, 1000, 100, 20}, "Annotation"));
    canvas.SceneChanged();
    auto& tabs = ui.Root().Add<TabWidget>();
    tabs.SetFloatingBounds({500, 0, 250, 180});
    tabs.AddTab("First");
    tabs.AddTab("Second");
    ui.Prepare(800, 600);
    auto& semantics = ui.Semantics();
    const auto listId = semantics.GetId(list);
    counts->m_ItemReads = counts->m_ValueReads = 0;
    const auto before = ui.GetStats();
    Check(semantics.GetChildCount(listId) == 100000 && list.GetChildren().empty(),
          "100000 virtual records have no row widgets");
    const auto page = semantics.GetChildren(listId, 99990, 10);
    Check(page.size() == 10, "far virtual page resolves directly by row");
    const auto last = page.back();
    const auto lastNode = semantics.GetNode(last);
    Check(lastNode && lastNode->m_Info.m_Name == "Entry 99999" && lastNode->m_Offscreen,
          "offscreen records expose values and clipped geometry without realization");
    for (int query = 0; query < 100; ++query)
    {
        Check(semantics.GetSibling(last, false) == page[8], "virtual sibling lookup retains direct row identity");
        semantics.GetNode(last);
    }
    Check(counts->m_ItemReads < 1000 && counts->m_ValueReads < 1000,
          "100000-row semantic queries have bounded record access, not repeated prefix scans");
    Check(ui.GetStats().m_LayoutPasses == before.m_LayoutPasses &&
              ui.GetStats().m_StyleResolutions == before.m_StyleResolutions,
          "virtual metadata does not prepare layout or resolve styles");
    Check(!semantics.GetNode(SemanticId{listId.m_Widget, {0, 1, 0}}) &&
              semantics.GetChildCount(SemanticId{listId.m_Widget, {0, 1, 0}}) == 0,
          "malformed virtual keys cannot alias an actual widget");
    Check(semantics.Perform(last, {SemanticAction::Select}) == SemanticStatus::Succeeded &&
              list.GetCurrentItemId() == 100000,
          "virtual selection updates canonical model identity");
    Check(semantics.GetSelectionCount(listId) == 1 && semantics.GetSelection(listId).front() == last,
          "bounded selected identities share the canonical selection");
    Check(semantics.Perform(last, {SemanticAction::ScrollIntoView}) == SemanticStatus::Succeeded &&
              !semantics.GetNode(last)->m_Offscreen,
          "realization updates stored scroll geometry without a semantic layout pass");
    const auto lastRect = semantics.GetNode(last)->m_Bounds;
    Check(semantics.HitTest({lastRect.m_X + 4, lastRect.m_Y + 4}) == last,
          "virtual hit testing uses scrolled row geometry");
    const auto hierarchyId = semantics.GetId(hierarchy);
    const auto branchId = semantics.GetChildAt(hierarchyId, 0);
    Check(semantics.GetChildCount(branchId) == 0 && semantics.GetNode(branchId)->m_Info.m_Expandable,
          "collapsed branches advertise expansion without enumerating descendants");
    Check(semantics.Perform(branchId, {SemanticAction::Expand}) == SemanticStatus::Succeeded &&
              hierarchy.IsExpanded(branch),
          "semantic expansion uses canonical tree behavior");
    const auto leafId = semantics.GetChildAt(branchId, 0);
    Check(leafId && semantics.GetNode(leafId)->m_Parent == branchId &&
              semantics.GetNode(leafId)->m_Info.m_Name == "Part",
          "virtual hierarchy retains logical parents");
    Check(semantics.Perform(leafId, {SemanticAction::Select}) == SemanticStatus::Succeeded &&
              hierarchy.GetCurrentItem() == leaf,
          "virtual tree selection follows stable node identity");
    hierarchy.RemoveItem(leaf);
    Check(!semantics.GetNode(leafId), "deleted virtual tree identities expire");
    const auto tableId = semantics.GetId(table);
    const auto zulu = semantics.GetGridItem(tableId, 0, 0);
    Check(semantics.GetNode(tableId)->m_Info.m_RowCount == 2 && semantics.GetNode(zulu)->m_GridContainer == tableId,
          "grid dimensions and cell container are queryable without expansion");
    table.SortByColumn(0);
    Check(semantics.GetGridItem(tableId, 1, 0) == zulu && semantics.GetNode(zulu)->m_Info.m_Row == 1,
          "convenience table sort preserves cell identity and remaps row coordinates");
    Check(semantics.Perform(zulu, {SemanticAction::SetText, "Renamed"}) == SemanticStatus::Succeeded &&
              table.GetCell(1, 0) == "Renamed",
          "virtual value editing reuses canonical table mutation");
    table.SetEditable(false);
    Check(semantics.Perform(zulu, {SemanticAction::SetText, "Denied"}) == SemanticStatus::Unsupported,
          "virtual value patterns respect read-only editing policy");
    table.SetRows({{"Replacement", "1"}});
    Check(!semantics.GetNode(zulu), "replaced convenience datasets cannot retarget retained cell identities");
    const auto canvasId = semantics.GetId(canvas);
    const auto firstCanvasId = semantics.GetChildAt(canvasId, 0);
    const auto secondCanvasId = semantics.GetChildAt(canvasId, 1);
    Check(semantics.GetNode(secondCanvasId)->m_Info.m_Name == "Annotation" &&
              semantics.GetNode(secondCanvasId)->m_Offscreen,
          "canvas annotations expose text without drawing offscreen items");
    Check(semantics.Perform(firstCanvasId, {SemanticAction::Select}) == SemanticStatus::Succeeded &&
              scene.Get(firstSceneItem)->m_Selected,
          "canvas semantic selection updates scene state");
    Check(semantics.Perform(secondCanvasId, {SemanticAction::AddToSelection}) == SemanticStatus::Succeeded &&
              semantics.GetSelectionCount(canvasId) == 2,
          "canvas multiple selection uses a selected-item index");
    scene.Remove(firstSceneItem);
    canvas.SceneChanged();
    Check(!semantics.GetNode(firstCanvasId) && semantics.GetChildAt(canvasId, 0) == secondCanvasId &&
              scene.GetIndex(secondSceneItem) == 0,
          "canvas dense removal updates position without retargeting stable identities");
    Check(semantics.Perform(secondCanvasId, {SemanticAction::ScrollIntoView}) == SemanticStatus::Succeeded &&
              !semantics.GetNode(secondCanvasId)->m_Offscreen,
          "canvas realization reframes an offscreen item");
    const auto tabsId = semantics.GetId(tabs);
    const auto firstTab = semantics.GetSelection(tabsId).front();
    Check(semantics.GetNode(firstTab)->m_Info.m_Role == SemanticRole::Tab &&
              semantics.GetNode(firstTab)->m_SelectionContainer == tabsId &&
              semantics.GetNode(tabsId)->m_Info.m_SelectionRequired,
          "composed actual tab selectors expose required single selection");
    const auto secondTab = semantics.GetSibling(firstTab, true);
    Check(secondTab && semantics.Perform(secondTab, {SemanticAction::Select}) == SemanticStatus::Succeeded &&
              tabs.GetCurrentIndex() == 1 && semantics.GetSelection(tabsId).front() == secondTab,
          "native tab selection reuses existing selector command");
    Check(semantics.Perform(secondTab, {SemanticAction::RemoveFromSelection}) == SemanticStatus::InvalidValue,
          "required tab selection cannot be removed");
    auto& custom = ui.Root().Add<VirtualCustom>();
    const auto customChild = semantics.GetChildAt(semantics.GetId(custom), 0);
    custom.m_InvalidBounds = true;
    Throws([&] { semantics.GetNode(customChild); }, "custom semantic child geometry must be finite");
    Check(ui.GetPhase() == UiPhase::Idle, "virtual inspection restores phase after geometry validation");
    custom.m_InvalidBounds = false;
    custom.m_InvalidParent = true;
    Throws([&] { semantics.GetNode(customChild); }, "custom semantic parents cannot form a self cycle");
}

void StableProjectionIdentity(FontFace& font)
{
    UiContext ui(font);
    ModelBuilder builder({{"Name"}});
    const auto a = builder.Add({std::string("A")});
    const auto b = builder.Add({std::string("B")});
    const auto c = builder.Add({std::string("C")});
    auto model = std::make_shared<ItemModel>(ui.States(), std::move(builder).Build());
    auto projection = SortFilterModel::Create(model);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetModel(projection);
    ui.Prepare(400, 300);
    auto& semantics = ui.Semantics();
    const auto listId = semantics.GetId(list);
    const auto id = semantics.GetChildAt(listId, 1);
    projection->SetSort(0, false);
    Check(semantics.GetChildAt(listId, 1) == id, "projection sorting retains canonical semantic identity");
    model->Restructure([&](ModelBuilder& edit) { edit.Move(b, 0, a); });
    Check(semantics.GetNode(id)->m_Info.m_Name == "B", "structural moves retain the original semantic record");
    semantics.Perform(id, {SemanticAction::Select});
    projection->SetFilter([b](const ModelSnapshot&, ItemId item) { return item != b; });
    Check(semantics.GetNode(id) && semantics.GetNode(id)->m_Offscreen && semantics.GetSelection(listId).front() == id,
          "filtered selection remains an owned canonical offscreen semantic node");
    projection->SetFilter({});
    Check(semantics.GetChildAt(listId, 1) == id, "restoring filtered rows restores the same identity");
    model->Restructure([&](ModelBuilder& edit) { edit.Remove(b); });
    Check(!semantics.GetNode(id) && semantics.GetSelectionCount(listId) == 0,
          "canonical deletion expires providers and selection coherently");
    (void)c;
}

void FocusRevalidation(FontFace& font)
{
    UiContext ui(font);
    ModelBuilder firstBuilder({{"Name"}});
    firstBuilder.Add({std::string("Original")}, 0, 77);
    ModelBuilder replacementBuilder({{"Name"}});
    replacementBuilder.Add({std::string("Replacement")}, 0, 77);
    auto first = std::make_shared<ItemModel>(ui.States(), std::move(firstBuilder).Build());
    auto replacement = std::make_shared<ItemModel>(ui.States(), std::move(replacementBuilder).Build());
    auto& list = ui.Root().Add<ListWidget>();
    list.SetModel(first);
    PlatformHost host;
    host.m_Dispatcher = ui.GetDispatcher();
    host.m_FocusWindow = [&]
    {
        list.SetModel(replacement);
        return PlatformResult{};
    };
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(host));
    ui.Prepare(400, 200);
    auto& semantics = ui.Semantics();
    const auto oldId = semantics.GetChildAt(semantics.GetId(list), 0);
    Check(semantics.Perform(oldId, {SemanticAction::Focus}) == SemanticStatus::Unavailable &&
              list.GetModel() == replacement && list.GetCurrentItemId() == 0,
          "focus callbacks replacing a dataset cannot select a reused record in the replacement domain");
    Check(!semantics.GetNode(oldId) && semantics.GetChildAt(semantics.GetId(list), 0) != oldId &&
              ui.GetPhase() == UiPhase::Idle,
          "focus revalidation occurs outside input inspection and expires the original virtual identity");
}

void SemanticRevisions(FontFace& font)
{
    UiContext ui(font);
    auto& list = ui.Root().Add<ListWidget>();
    list.SetFloatingBounds({0, 0, 240, 180});
    std::vector<std::string> rows(20, "Row");
    list.SetItems(rows);
    auto& hierarchy = ui.Root().Add<TreeWidget>();
    hierarchy.SetFloatingBounds({250, 0, 240, 180});
    const auto branch = hierarchy.AddItem(0, "First");
    hierarchy.AddItem(0, "Second");
    auto& table = ui.Root().Add<TableWidget>();
    table.SetFloatingBounds({0, 200, 300, 180});
    table.SetColumns({{"Name", SizeRule::Fill()}});
    table.SetRows({{"First"}, {"Second"}});
    auto& canvas = ui.Root().Add<CanvasWidget>();
    canvas.SetFloatingBounds({310, 200, 300, 180});
    const auto shape = canvas.GetScene().Add({CanvasPrimitive::Rectangle, {-20, -20}, {20, 20}});
    canvas.SceneChanged();
    auto& custom = ui.Root().Add<Custom>();
    custom.SetFloatingBounds({650, 200, 100, 100});
    ui.Prepare(800, 600);
    auto& semantics = ui.Semantics();
    auto revision = semantics.GetRevision();
    list.SetCurrentIndex(0);
    Check(semantics.GetRevision() > revision,
          "visible convenience-list selection wakes semantic clients without scrolling");
    revision = semantics.GetRevision();
    hierarchy.SetCurrentItem(branch);
    Check(semantics.GetRevision() > revision, "visible convenience-tree selection publishes semantic revision");
    revision = semantics.GetRevision();
    hierarchy.SetItemText(branch, "Renamed");
    Check(semantics.GetRevision() > revision, "paint-only tree caption changes publish semantic revision");
    revision = semantics.GetRevision();
    table.SetCurrentCell(0, 0);
    Check(semantics.GetRevision() > revision, "visible convenience-table selection publishes semantic revision");
    revision = semantics.GetRevision();
    list.SetScrollOffset({0, 40});
    Check(semantics.GetRevision() > revision,
          "changed scroll geometry publishes semantic revision through targeted arrangement");
    ui.Prepare(800, 600);
    const auto viewport = list.GetViewport();
    ui.PointerMove({viewport.m_X + 20, viewport.m_Y + 3});
    ui.Prepare(800, 600);
    revision = semantics.GetRevision();
    ui.PointerMove({viewport.m_X + 20, viewport.m_Y + 3 + list.GetStyle().m_RowHeight});
    Check(semantics.GetRevision() == revision, "ordinary row-hover repaint does not publish semantic changes");
    revision = semantics.GetRevision();
    canvas.GetScene().SetSelected(shape, true);
    canvas.SceneChanged();
    Check(semantics.GetRevision() > revision, "scene selection publication wakes semantic clients");
    revision = semantics.GetRevision();
    canvas.GetScene().Update(shape, {CanvasPrimitive::Rectangle, {-10, -10}, {30, 30}});
    canvas.SceneChanged();
    Check(semantics.GetRevision() > revision, "scene geometry publication wakes semantic bounds clients");
    const auto canvasId = semantics.GetId(canvas);
    const auto beforeStructure = semantics.GetNode(canvasId)->m_Info.m_StructureRevision;
    revision = semantics.GetRevision();
    canvas.GetScene().Add({CanvasPrimitive::Circle, {80, 0}, {}, 10});
    canvas.SceneChanged();
    Check(semantics.GetRevision() > revision &&
              semantics.GetNode(canvasId)->m_Info.m_StructureRevision > beforeStructure,
          "scene structural publication advances both cheap and structural revisions");
    revision = semantics.GetRevision();
    canvas.SetCamera({10, 20}, 2);
    Check(semantics.GetRevision() > revision, "camera geometry wakes semantic bounds clients without hosted widgets");
    revision = semantics.GetRevision();
    canvas.SetCamera({10, 20}, 2);
    Check(semantics.GetRevision() == revision, "unchanged camera does not publish semantic work");
    custom.m_TryNotify = true;
    Throws([&] { semantics.GetNode(semantics.GetId(custom)); },
           "metadata hooks cannot publish semantic changes while inspected");
    Check(semantics.GetRevision() == revision && ui.GetPhase() == UiPhase::Idle,
          "rejected semantic publication preserves revision and restores inspection phase");
    custom.m_TryNotify = false;
    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                custom.PublishSemantics();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    Check(rejected && semantics.GetRevision() == revision,
          "semantic publication verifies the owner before touching shared tree state");
    custom.PublishSemantics();
    Check(semantics.GetRevision() > revision, "custom controls can explicitly publish committed semantic metadata");
}

void SharedSemanticRevisions(FontFace& font)
{
    for (int family = 0; family < 3; ++family)
    {
        UiContext ui(font);
        ModelBuilder builder({{"Name"}});
        const auto first = builder.Add({std::string("First")});
        const auto second = builder.Add({std::string("Second")});
        auto model = std::make_shared<ItemModel>(ui.States(), std::move(builder).Build());
        std::shared_ptr<SelectionModel> selection;
        if (!family)
        {
            auto& view = ui.Root().Add<ListWidget>();
            view.SetModel(model);
            selection = view.GetSelectionModel();
        }
        else if (family == 1)
        {
            auto& view = ui.Root().Add<TreeWidget>();
            view.SetModel(model);
            selection = view.GetSelectionModel();
        }
        else
        {
            auto& view = ui.Root().Add<TableWidget>();
            view.SetModel(model);
            selection = view.GetSelectionModel();
        }
        ui.Prepare(400, 250);
        auto& semantics = ui.Semantics();
        selection->SetMode(SelectionMode::Multiple);
        selection->Select(second);
        auto revision = semantics.GetRevision();
        selection->SetItems({first, second}, second);
        Check(selection->GetCurrent() == second && semantics.GetRevision() > revision,
              "shared selected-set changes publish semantics even when the current item is unchanged");
        revision = semantics.GetRevision();
        const auto before = ui.GetStats();
        model->SetText(second, 0, "Renamed");
        Check(semantics.GetRevision() > revision && ui.GetStats().m_LayoutPasses == before.m_LayoutPasses,
              "paint-only shared model captions publish semantics without a layout pass");
    }
}

void Semantics(FontFace& font)
{
    UiContext ui(font);
    auto& root = ui.Root();
    auto& button = root.Add<Button>("Launch");
    button.SetId("launch");
    auto& text = root.Add<TextBox>("Original");
    text.SetAccessibleName("Project name");
    auto& toggle = root.Add<ToolButton>("Grid");
    toggle.SetCheckable(true);
    auto& slider = root.Add<Slider>();
    slider.SetRange(-10, 20);
    auto& number = root.Add<NumericInput>(2);
    auto& custom = root.Add<Custom>();
    ui.Prepare(800, 600);
    auto& tree = ui.Semantics();
    const auto buttonId = tree.GetId(button);
    const auto rootId = tree.GetRoot();
    auto node = tree.GetNode(buttonId);
    Check(node && node->m_Info.m_Name == "Launch" && node->m_Info.m_Role == SemanticRole::Button,
          "button caption and role");
    Check(node->m_Parent == rootId && node->m_AutomationId == "launch" && node->m_Enabled,
          "logical hierarchy and automation id");
    Check(tree.GetNode(rootId)->m_Info.m_Role == SemanticRole::Window, "window root role");
    Check(!node->m_ScreenBounds && !node->m_Offscreen, "headless bounds do not pretend to be desktop coordinates");
    Check(tree.GetChildren(rootId, 1, 2).size() == 2, "bounded child pages");
    Throws([&] { tree.GetChildren(rootId, 0, 257); }, "oversized pages rejected");
    const auto bounds = node->m_Bounds;
    Check(tree.HitTest({bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2}) == buttonId,
          "semantic hit test shares widget input geometry");
    const auto before = ui.GetStats();
    for (int i = 0; i < 50; ++i)
        tree.GetNode(buttonId);
    Check(ui.GetStats().m_LayoutPasses == before.m_LayoutPasses &&
              ui.GetStats().m_StyleResolutions == before.m_StyleResolutions,
          "semantic queries do not prepare frames");
    int invoked = 0;
    button.Clicked().Connect([&] { ++invoked; });
    Check(tree.Perform(buttonId, {SemanticAction::Invoke}) == SemanticStatus::Succeeded && invoked == 1,
          "canonical button activation");
    button.SetEnabled(false);
    Check(tree.Perform(buttonId, {SemanticAction::Invoke}) == SemanticStatus::Disabled && invoked == 1,
          "disabled semantic activation rejected");
    button.SetEnabled(true);
    Check(tree.Perform(tree.GetId(text), {SemanticAction::Focus}) == SemanticStatus::Succeeded &&
              ui.GetFocus() == &text,
          "semantic focus uses canonical focus path");
    Check(tree.GetFocus() == tree.GetId(text) && tree.GetNode(tree.GetId(text))->m_Focused, "queried real focus");
    Check(tree.Perform(tree.GetId(text), {SemanticAction::SetText, "Renamed Ω"}) == SemanticStatus::Succeeded &&
              text.GetText() == "Renamed Ω",
          "Unicode value action");
    Check(tree.Perform(tree.GetId(text), {SemanticAction::SetText, std::string(ClipboardData::s_MaxBytes + 1, 'x')}) ==
                  SemanticStatus::InvalidValue &&
              text.GetText() == "Renamed Ω",
          "oversized semantic values reject atomically");
    text.SetReadOnly(true);
    Check(tree.GetNode(tree.GetId(text))->m_Info.m_ReadOnly &&
              tree.Perform(tree.GetId(text), {SemanticAction::SetText, "Denied"}) == SemanticStatus::Unsupported,
          "read-only values cannot be changed");
    Check(tree.Perform(tree.GetId(toggle), {SemanticAction::Toggle}) == SemanticStatus::Succeeded && toggle.IsChecked(),
          "canonical toggle activation");
    Check(tree.Perform(tree.GetId(slider), {SemanticAction::SetNumber, {}, 7.5}) == SemanticStatus::Succeeded &&
              slider.GetValue() == 7.5,
          "slider range action");
    Check(tree.Perform(tree.GetId(slider), {SemanticAction::SetNumber, {}, 30}) == SemanticStatus::InvalidValue &&
              slider.GetValue() == 7.5,
          "out of range changes rejected without clamping");
    Check(tree.Perform(tree.GetId(number), {SemanticAction::SetNumber, {}, 9}) == SemanticStatus::Succeeded &&
              number.GetValue() == 9,
          "composite numeric control action");
    Check(tree.Perform(tree.GetId(custom), {SemanticAction::Invoke}) == SemanticStatus::Succeeded &&
              custom.m_Invocations == 1,
          "custom control semantics and interaction hooks");
    custom.m_TryMutation = true;
    Throws([&] { tree.GetNode(tree.GetId(custom)); }, "semantic hooks reject tree mutation");
    Check(ui.GetPhase() == UiPhase::Idle && custom.GetChildren().empty(), "inspection phase restored after error");
    custom.m_TryMutation = false;
    const auto customId = tree.GetId(custom);
    root.RemoveChild(custom);
    Check(!tree.GetNode(customId) && tree.Perform(customId, {SemanticAction::Invoke}) == SemanticStatus::Unavailable,
          "removed providers expire by lifetime identity");
    auto& replacement = root.Add<Custom>();
    Check(tree.GetId(replacement) != customId, "replacement cannot retarget an old identity");
    replacement.SetVisible(false);
    Check(tree.GetNode(tree.GetId(replacement))->m_Offscreen, "hidden state");
    auto& owned = ui.AddOwnedPopup(std::make_unique<ContextMenu>(), button);
    ui.ShowPopup(owned, {50, 100});
    ui.Prepare(800, 600);
    const auto children = tree.GetChildren(buttonId);
    Check(children.size() == 1 && children.front() == tree.GetId(owned) &&
              tree.GetNode(tree.GetId(owned))->m_Parent == buttonId,
          "owned popup has one logical parent");
    ui.CloseAllPopups();
    Check(tree.GetChildren(buttonId).empty() && tree.GetNode(tree.GetId(owned))->m_Offscreen,
          "closed popup disappears from child navigation");
    auto& modal = root.Add<Widget>();
    ui.ShowPopup(modal, {100, 100}, PopupOptions::Modal());
    Check(!tree.GetNode(buttonId)->m_Enabled &&
              tree.Perform(buttonId, {SemanticAction::Invoke}) == SemanticStatus::Disabled,
          "modal barrier blocks semantic actions");
    ui.CloseAllPopups();
    PlatformHost host;
    host.m_Dispatcher = ui.GetDispatcher();
    host.m_GetMetrics = []
    {
        WindowMetrics metrics;
        metrics.m_LogicalSize = {800, 600};
        metrics.m_FramebufferSize = {1200, 900};
        metrics.m_PixelScale = {1.5f, 1.5f};
        metrics.m_ScreenOrigin = {-900, 40};
        metrics.m_HasScreenCoordinates = true;
        return metrics;
    };
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(host));
    ui.Prepare(800, 600);
    node = tree.GetNode(buttonId);
    Check(node->m_ScreenBounds && node->m_ScreenBounds->m_X == -900 + node->m_Bounds.m_X * 1.5f,
          "physical desktop bounds use DPI and negative monitor origin");
    bool crossThreadRejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                tree.GetNode(buttonId);
            }
            catch (const std::logic_error&)
            {
                crossThreadRejected = true;
            }
        });
    worker.join();
    Check(crossThreadRejected, "direct worker semantic queries rejected before touching widgets");
    auto& deleting = root.Add<Button>("Delete self");
    const auto deletingId = tree.GetId(deleting);
    deleting.Clicked().Connect([&] { root.RemoveChild(deleting); });
    Check(tree.Perform(deletingId, {SemanticAction::Invoke}) == SemanticStatus::Succeeded && !tree.GetNode(deletingId),
          "semantic activation permits receiver deletion");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Semantics(font);
        VirtualCollections(font);
        StableProjectionIdentity(font);
        FocusRevalidation(font);
        SemanticRevisions(font);
        SharedSemanticRevisions(font);
        std::cout << "Semantics: " << s_Checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
