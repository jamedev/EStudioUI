#include <EStudioUI/TreeTable.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/NumericInput.h>
#include <iostream>
#include <limits>
using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void Reject(F action, const char* message)
{
    bool failed = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    Check(failed, message);
}

template <class T> T* Find(Widget& root)
{
    if (auto* value = dynamic_cast<T*>(&root))
    {
        return value;
    }
    for (const auto& child : root.GetChildren())
    {
        if (auto* value = Find<T>(*child))
        {
            return value;
        }
    }
    return nullptr;
}

ModelColumn Column(std::string name, ModelValueType type)
{
    ModelColumn column;
    column.m_Title = std::move(name);
    column.m_ValueType = type;
    column.m_EditorType = type;
    return column;
}

struct Fixture
{
    FontFace m_Font{ESTUDIOUI_FONT_PATH};
    UiContext m_Ui{m_Font};
    std::shared_ptr<ItemModel> m_Model;
    TreeTable* m_Tree;
    ListWidget* m_List;
    ItemId m_A, m_B, m_Child, m_Leaf;

    Fixture()
    {
        auto choice = Column("Material", ModelValueType::Text);
        choice.m_Choices = {std::string("Aluminium"), std::string("Steel"), std::string("Composite")};
        auto nullable = Column("Note", ModelValueType::Text);
        nullable.m_Nullable = true;
        ModelBuilder builder({Column("Name", ModelValueType::Text), Column("Count", ModelValueType::Integer),
                              Column("Length", ModelValueType::Real), Column("Enabled", ModelValueType::Boolean),
                              choice, Column("Color", ModelValueType::Color), Column("Bytes", ModelValueType::Blob),
                              nullable});
        const auto row = [](const char* name)
        {
            return std::vector<ModelValue>{std::string(name),
                                           std::int64_t{9223372036854775807LL},
                                           1.125,
                                           true,
                                           std::string("Aluminium"),
                                           Color{.2f, .3f, .4f, 1},
                                           ModelBlob{0, 255},
                                           std::string("")};
        };
        m_A = builder.Add(row("Wing"));
        m_Child = builder.Add(row("Spar"), m_A);
        m_Leaf = builder.Add(row("Web"), m_Child);
        m_B = builder.Add(row("Fuselage"));
        m_Model = std::make_shared<ItemModel>(m_Ui.States(), std::move(builder).Build());
        m_Tree = &m_Ui.Root().Add<TreeTable>();
        m_Tree->SetFloatingBounds({0, 0, 1080, 320});
        m_Tree->SetModel(m_Model);
        m_Tree->SetRowsMovable(true);
        m_Tree->SetColumnsMovable(true);
        m_List = &m_Ui.Root().Add<ListWidget>();
        m_List->SetFloatingBounds({0, 340, 400, 200});
        m_List->SetModel(m_Model);
        m_List->SetRowsMovable(true);
        m_List->SetSelectionModel(m_Tree->GetSelectionModel());
        m_Ui.Prepare(1100, 550);
    }

    Point Row(std::size_t row, float fraction = .5f) const
    {
        return {m_Tree->GetViewport().m_X + 70,
                m_Tree->GetViewport().m_Y + (static_cast<float>(row) + fraction) * m_Tree->GetStyle().m_RowHeight};
    }
};

void Hierarchy()
{
    Fixture f;
    auto& tree = *f.m_Tree;
    auto& ui = f.m_Ui;
    Check(tree.GetRowCount() == 2 && tree.GetVisibleRowCount() == 2,
          "Collapsed hierarchy stores no widgets for descendant rows");
    tree.SetExpanded(f.m_A, true);
    tree.SetExpanded(f.m_Child, true);
    ui.Prepare(1100, 550);
    Check(tree.GetRowCount() == 4 && tree.GetCell(2, 0) == "Web", "Branch prefixes resolve hierarchical table cells");
    tree.SetCurrentItemId(f.m_Leaf, 2);
    Check(tree.GetCurrentCell() == std::pair(2, 2) && f.m_List->GetCurrentItemId() == f.m_Leaf,
          "Shared selection follows canonical descendant IDs");
    tree.SetExpanded(f.m_A, false);
    Check(tree.GetCurrentCell().first == -1 && tree.GetCurrentItemId() == f.m_Leaf,
          "Collapsing preserves canonical hidden selection");
    tree.RevealItem(f.m_Leaf, 1);
    Check(tree.GetCurrentCell() == std::pair(2, 1) && tree.IsExpanded(f.m_A) && tree.IsExpanded(f.m_Child),
          "Reveal expands ancestors without changing source data");
    auto& semantics = ui.Semantics();
    const auto tableId = semantics.GetId(tree);
    const auto rowId = semantics.GetChildAt(tableId, 0);
    Check(semantics.GetNode(rowId)->m_Info.m_Expandable && semantics.GetNode(rowId)->m_Info.m_Expanded,
          "Accessible rows expose branch state");
    SemanticCommand collapse;
    collapse.m_Action = SemanticAction::Collapse;
    Check(semantics.Perform(rowId, collapse) == SemanticStatus::Succeeded && tree.GetRowCount() == 2,
          "Accessible collapse routes through the ordinary expansion path");
    tree.SetExpanded(f.m_A, true);
    tree.SetExpanded(f.m_Child, true);
    tree.SetColumnOrder({3, 0, 7, 2, 1, 4, 5, 6});
    tree.SetColumnVisible(7, false);
    ui.Prepare(1100, 550);
    const auto widths = tree.GetColumnWidths();
    Check(widths[7] == 0 && tree.GetCellBounds(0, 3).m_X < tree.GetCellBounds(0, 0).m_X,
          "Reordering and hiding change presentation without schema mutation");
    const auto cell = semantics.GetGridItem(tableId, 0, 0);
    Check(cell.m_Key.m_Part == 4 && semantics.GetNode(cell)->m_Info.m_Column == 0,
          "Accessible grid coordinates follow visible column order");
    tree.SetCurrentCell(0, 3);
    ui.SetFocus(&tree);
    ui.KeyEvent(Key::Right, true);
    Check(tree.GetCurrentCell().second == 0, "Keyboard navigation follows visible presentation order");
    Reject([&] { tree.SetColumnOrder({0, 0, 1, 2, 3, 4, 5, 6}); },
           "Invalid permutations reject before presentation changes");
    for (std::size_t col = 0; col < 6; ++col)
    {
        tree.SetColumnVisible(col, false);
    }
    Reject([&] { tree.SetColumnVisible(6, false); }, "An empty column view is diagnosed");
}

void Editors()
{
    Fixture f;
    auto& tree = *f.m_Tree;
    auto& ui = f.m_Ui;
    const auto item = f.m_A;
    tree.BeginEdit(0, 1);
    auto* integer = Find<TextBox>(*tree.GetCellEditor());
    Check(integer && integer->GetText() == "9223372036854775807", "The integer editor starts with all 64 bits");
    integer->SetText("-9223372036854775808");
    tree.CommitEdit();
    Check(std::get<std::int64_t>(f.m_Model->GetSnapshot().GetValue(item, 1)) ==
              std::numeric_limits<std::int64_t>::min(),
          "Integer commits retain the signed lower edge exactly");
    tree.BeginEdit(0, 1);
    Find<TextBox>(*tree.GetCellEditor())->SetText("9223372036854775808");
    tree.CommitEdit();
    Check(tree.GetCellEditor() && !tree.GetEditError().empty(), "Invalid typed drafts remain visible and uncommitted");
    tree.CancelEdit();
    tree.BeginEdit(0, 2);
    auto* number = Find<NumericInput>(*tree.GetCellEditor());
    Check(number != nullptr, "Real fields reuse NumericInput");
    number->SetValue(9.125);
    tree.CommitEdit();
    Check(std::get<double>(f.m_Model->GetSnapshot().GetValue(item, 2)) == 9.125,
          "Typed numeric values reach canonical data");
    const auto extreme = std::numeric_limits<double>::max();
    f.m_Model->SetValue(item, 2, extreme);
    tree.BeginEdit(0, 2);
    Check(tree.GetCellEditor() && !Find<NumericInput>(*tree.GetCellEditor()) &&
              Find<TextBox>(*tree.GetCellEditor())->GetText() == FormatModelValue(extreme),
          "Extreme finite reals use exact scientific text instead of a clamping spin control");
    tree.CommitEdit();
    Check(std::get<double>(f.m_Model->GetSnapshot().GetValue(item, 2)) == extreme,
          "Accepting an unchanged extreme real cannot silently change the canonical value");
    tree.BeginEdit(0, 3);
    auto* boolean = Find<CheckBox>(*tree.GetCellEditor());
    boolean->Trigger();
    Check(!std::get<bool>(f.m_Model->GetSnapshot().GetValue(item, 3)) && !tree.GetCellEditor(),
          "Boolean cell acceptance uses the ordinary checkbox signal");
    tree.BeginEdit(0, 4);
    Find<ComboBox>(*tree.GetCellEditor())->SetCurrentIndex(2);
    tree.CommitEdit();
    Check(std::get<std::string>(f.m_Model->GetSnapshot().GetValue(item, 4)) == "Composite",
          "Choice values remain typed, independently of captions");
    tree.BeginEdit(0, 6);
    Find<TextBox>(*tree.GetCellEditor())->SetText("0x00ff42");
    tree.CommitEdit();
    Check(std::get<ModelBlob>(f.m_Model->GetSnapshot().GetValue(item, 6)) == ModelBlob({0, 255, 66}),
          "A binary editor never routes through display text");
    tree.BeginEdit(0, 7);
    Find<CheckBox>(*tree.GetCellEditor())->SetChecked(true);
    tree.CommitEdit();
    Check(f.m_Model->GetSnapshot().GetValue(item, 7).index() == 0, "Explicit NULL stays distinct from empty text");
    tree.BeginEdit(0, 7);
    Find<CheckBox>(*tree.GetCellEditor())->SetChecked(false);
    Find<TextBox>(*tree.GetCellEditor())->SetText("");
    tree.CommitEdit();
    Check(f.m_Model->GetSnapshot().GetValue(item, 7) == ModelValue(std::string("")),
          "Empty text can replace NULL deliberately");
    tree.BeginEdit(0, 5);
    Find<ColorEditor>(*tree.GetCellEditor())->SetColor({.4f, .7f, .2f, 1});
    tree.CommitEdit();
    Check(std::get<Color>(f.m_Model->GetSnapshot().GetValue(item, 5)).m_G > .6f,
          "Color fields reuse the public composed editor");
    tree.BeginEdit(0, 0);
    Find<TextBox>(*tree.GetCellEditor())->SetText("draft");
    f.m_Model->SetValue(item, 0, std::string("external"));
    tree.CommitEdit();
    Check(tree.GetCellEditor() && !tree.GetEditError().empty() && f.m_Model->GetSnapshot().GetText(item) == "external",
          "A stale editor cannot overwrite another canonical edit");
    tree.CancelEdit();
    ui.Prepare(1100, 550);
    Check(ui.Root().GetChildren().size() == 2 && tree.GetChildren().size() < 5,
          "Editor reuse does not allocate per-row or per-cell widget trees");
}

void Dragging()
{
    Fixture f;
    auto& ui = f.m_Ui;
    auto& tree = *f.m_Tree;
    tree.SetExpanded(f.m_A, true);
    tree.SetExpanded(f.m_Child, true);
    ui.Prepare(1100, 550);
    ui.PointerDown(f.Row(1));
    ui.PointerMove(f.Row(3));
    ui.PointerUp(f.Row(3));
    Check(f.m_Model->GetSnapshot().GetParent(f.m_Child) == f.m_B &&
              f.m_Model->GetSnapshot().GetParent(f.m_Leaf) == f.m_Child,
          "Captured tree drops move complete subtrees atomically");
    Reject([&] { MoveModelItems(f.m_Model, {f.m_B}, f.m_Leaf); }, "Cycles are rejected before canonical mutation");
    Reject([&] { MoveModelItems(f.m_Model, {f.m_A}, 0, 0, f.m_Model->GetRevision() - 1); },
           "Stale drag revisions cannot reorder newer data");
    tree.SetExpanded(f.m_B, true);
    ui.Prepare(1100, 550);
    const auto area = f.m_List->GetViewport();
    const Point target{area.m_X + 50, area.m_Y + 3.5f * f.m_List->GetStyle().m_RowHeight};
    ui.PointerDown(f.Row(2));
    ui.PointerMove(target);
    ui.PointerUp(target);
    Check(f.m_Model->GetSnapshot().GetParent(f.m_Child) == 0 && f.m_List->GetItemCount() == 3,
          "Ordinary compatible list views accept captured tree transfers");
    const auto before = f.m_Model->GetSnapshot();
    MoveModelItems(f.m_Model, {f.m_Child, f.m_Leaf}, 0, f.m_A);
    Check(f.m_Model->GetSnapshot().GetItem(0) == f.m_Child && f.m_Model->GetSnapshot().GetParent(f.m_Leaf) == f.m_Child,
          "A multi-selection forest moves each selected subtree once");
    tree.GetSelectionModel()->SetMode(SelectionMode::Multiple);
    tree.GetSelectionModel()->SetItems({f.m_A, f.m_B}, f.m_A);
    ui.Prepare(1100, 550);
    const auto retained = tree.GetSelectionModel()->GetSnapshot()->m_Items.size();
    tree.RevealItem(f.m_A); // Reveal intentionally replaces selection; restore a multiple set for the drag.
    tree.GetSelectionModel()->SetItems({f.m_A, f.m_B}, f.m_A);
    const auto rootOrder = f.m_Model->GetSnapshot();
    const auto row = static_cast<std::size_t>(tree.GetCurrentCell().first);
    ui.PointerDown(f.Row(row));
    Check(tree.GetSelectionModel()->GetSnapshot()->m_Items.size() == retained,
          "Pressing a selected row preserves a multi-row drag set");
    ui.PointerUp(f.Row(row));
    Check(tree.GetSelectionModel()->GetSnapshot()->m_Items.size() == 1,
          "A click without a drag retains ordinary single-row selection behavior");
    (void)before;
    (void)rootOrder;
}

class DelayedModel final : public ItemModel
{
  public:
    ModelEditSession m_Pending;
    std::vector<ModelEdit> m_Edits;

    DelayedModel(StateStore& store, ModelSnapshot snapshot) : ItemModel(store, std::move(snapshot))
    {
    }

    ModelEditSession RequestEdit(std::vector<ModelEdit> edits, std::optional<std::uint64_t>) override
    {
        m_Edits = std::move(edits);
        m_Pending = BeginEditSession();
        return m_Pending;
    }

    void Finish(bool success)
    {
        if (success)
        {
            Apply(std::move(m_Edits));
        }
        CompleteEditSession(m_Pending, {success ? ModelEditStatus::Committed : ModelEditStatus::Failed,
                                        success ? "" : "server rejected draft"});
    }
};

void AsyncEditor()
{
    Fixture f;
    auto model = std::make_shared<DelayedModel>(f.m_Ui.States(), f.m_Model->GetSnapshot());
    f.m_Tree->SetModel(model);
    f.m_Ui.Prepare(1100, 550);
    f.m_Tree->BeginEdit(0, 0);
    Find<TextBox>(*f.m_Tree->GetCellEditor())->SetText("stored later");
    f.m_Tree->CommitEdit();
    Check(f.m_Tree->GetCellEditor() && !f.m_Tree->GetCellEditor()->IsEnabled() &&
              model->GetSnapshot().GetText(f.m_A) == "Wing",
          "Pending confirmed edits retain a disabled draft");
    model->Finish(false);
    Check(f.m_Tree->GetCellEditor()->IsEnabled() && !f.m_Tree->GetEditError().empty() &&
              Find<TextBox>(*f.m_Tree->GetCellEditor())->GetText() == "stored later",
          "Failed async edits restore the exact user draft");
    f.m_Tree->CommitEdit();
    model->Finish(true);
    Check(!f.m_Tree->GetCellEditor() && model->GetSnapshot().GetText(f.m_A) == "stored later",
          "Confirmed async publication closes its editor only after success");
    f.m_Tree->BeginEdit(0, 0);
    Find<TextBox>(*f.m_Tree->GetCellEditor())->SetText("expired view");
    f.m_Tree->CommitEdit();
    f.m_Ui.Root().RemoveChild(*f.m_Tree);
    model->Finish(true);
    Check(model->GetSnapshot().GetText(f.m_A) == "expired view",
          "A destroyed view does not invalidate an independent confirmed model edit");
}
} // namespace

int main()
{
    try
    {
        Hierarchy();
        Editors();
        Dragging();
        AsyncEditor();
        std::cout << "Data views: " << s_Checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
