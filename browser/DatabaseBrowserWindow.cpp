#include "DatabaseBrowserWindow.h"
#include "ExampleDatabase.h"
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/Dialog.h>
#include <EStudioUI/FileDialog.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/Splitter.h>
#include <EStudioUI/SqliteAdapter.h>
#include <EStudioUI/ThemeController.h>
#include <EStudioUI/UiContext.h>
#include <fstream>
#include <sstream>
#include <map>

namespace EStudioUI
{
namespace
{
std::string PathText(const std::filesystem::path& path)
{
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string Failure(std::exception_ptr error)
{
    try
    {
        std::rethrow_exception(error);
    }
    catch (const std::exception& failure)
    {
        return failure.what();
    }
    catch (...)
    {
        return "The application operation failed.";
    }
}

ModelColumn Field(std::string name, ModelValueType type = ModelValueType::Text, bool editor = true)
{
    ModelColumn column;
    column.m_Title = std::move(name);
    column.m_ValueType = type;
    if (editor)
    {
        column.m_EditorType = type;
    }
    return column;
}

const std::vector<std::string>& FilterOperators()
{
    static const std::vector<std::string> s_Names{"Equals",       "Not equal",        "Less than", "Less or equal",
                                                  "Greater than", "Greater or equal", "Contains",  "Starts with",
                                                  "Is NULL",      "Not NULL"};
    return s_Names;
}

std::string FilterCaption(const ModelFilter& filter, const std::vector<ModelColumn>& columns)
{
    if (!filter.m_Column)
    {
        std::string group = "(";
        for (const auto& child : filter.m_Children)
        {
            if (group.size() > 1)
            {
                group += filter.m_Any ? " OR " : " AND ";
            }
            group += FilterCaption(child, columns);
        }
        return group + ")";
    }
    const auto column = *filter.m_Column;
    auto caption = column < columns.size() ? columns[column].m_Title : "Column " + std::to_string(column + 1);
    caption += " / " + FilterOperators().at(static_cast<std::size_t>(filter.m_Operator));
    if (filter.m_Operator != ModelFilterOperator::IsNull && filter.m_Operator != ModelFilterOperator::IsNotNull)
    {
        caption += " / " + FormatModelValue(filter.m_Value);
    }
    return caption;
}

std::string Csv(std::string value)
{
    std::string escaped("\"");
    for (const char c : value)
    {
        escaped += c;
        if (c == '"')
        {
            escaped += c;
        }
    }
    return escaped + '"';
}
} // namespace

struct DatabaseBrowserWindow::Data : std::enable_shared_from_this<Data>
{
    Application& m_Application;
    Window& m_Window;
    UiHandle<Window> m_Target;
    MainWindow* m_Shell = nullptr;
    TabWidget* m_Tabs = nullptr;
    TableWidget* m_Table = nullptr;
    TreeTable* m_Schema = nullptr;
    TreeTable* m_Playground = nullptr;
    ComboBox* m_FilterColumn = nullptr;
    ComboBox* m_FilterOperator = nullptr;
    ComboBox* m_Match = nullptr;
    ComboBox* m_PageSize = nullptr;
    TextBox* m_FilterValue = nullptr;
    CheckBox* m_Case = nullptr;
    CheckBox* m_Editing = nullptr;
    ListWidget* m_FilterList = nullptr;
    Label* m_PageLabel = nullptr;
    ToolButton* m_First = nullptr;
    ToolButton* m_Previous = nullptr;
    ToolButton* m_Next = nullptr;
    ToolButton* m_Last = nullptr;
    ToolButton* m_Cancel = nullptr;
    WidgetRef m_ColumnsDialog;
    std::shared_ptr<AdapterModel> m_Model;
    std::shared_ptr<const std::vector<ModelDataSet>> m_Catalog;
    std::shared_ptr<ItemModel> m_SchemaModel;
    std::map<ItemId, std::string> m_DataSets;
    std::vector<ScopedConnection> m_Connections;
    std::optional<ScopedConnection> m_ModelChanged;
    RequestScope m_OpenScope;
    State<ModelQuery> m_Draft;
    std::optional<ModelQuery> m_RestoreQuery;
    std::filesystem::path m_Path;
    std::string m_ControlsDataSet;
    bool m_RefreshPosted = false;
    bool m_Live = true, m_Refreshing = false, m_Syncing = false, m_CloseAfterWrite = false;

    Data(Application& application, Window& window)
        : m_Application(application), m_Window(window), m_Target(window.GetHandle()),
          m_Draft(window.Ui().States(), ModelQuery{})
    {
    }

    void Check() const
    {
        m_Target.GetDispatcher().VerifyAccess();
        if (!Live())
        {
            throw std::logic_error("The database browser owner has closed.");
        }
    }

    bool Live() const
    {
        return m_Live && m_Target.IsConnected();
    }

    void Message(std::string message)
    {
        if (Live())
        {
            m_Shell->Status().SetMessage(std::move(message));
        }
    }

    template <class Function> void Command(Function function)
    {
        if (!Live())
        {
            return;
        }
        try
        {
            function();
        }
        catch (const std::exception& failure)
        {
            Message(failure.what());
        }
    }

    void Query(ModelQuery query)
    {
        if (!m_Model)
        {
            return;
        }
        m_Draft.Set(query);
        m_Model->SetQuery(std::move(query));
    }

    void Select(std::string name)
    {
        if (!m_Model || m_Model->GetStatus().m_Operation == AdapterOperation::Committing)
        {
            return;
        }
        auto query = m_Draft.Get();
        if (query.m_DataSet == name)
        {
            return;
        }
        query = {};
        query.m_DataSet = std::move(name);
        Query(std::move(query));
    }

    void BuildSchema()
    {
        auto name = Field("Object");
        name.m_Editable = false;
        auto kind = Field("Kind / affinity");
        kind.m_Editable = false;
        auto mode = Field("Access");
        mode.m_Editable = false;
        ModelBuilder builder({name, kind, mode});
        m_DataSets.clear();
        for (const auto& dataSet : *m_Catalog)
        {
            const auto root = builder.Add({dataSet.m_Title, dataSet.m_Kind,
                                           dataSet.m_Editable ? std::string("Editable") : std::string("Read-only")});
            m_DataSets.emplace(root, dataSet.m_Id);
            for (const auto& column : dataSet.m_Columns)
            {
                const auto type = column.m_EditorType.value_or(column.m_ValueType);
                const std::string caption = type == ModelValueType::Integer   ? "Integer"
                                            : type == ModelValueType::Real    ? "Real"
                                            : type == ModelValueType::Blob    ? "Blob"
                                            : type == ModelValueType::Boolean ? "Boolean"
                                                                              : "Text";
                const auto child = builder.Add(
                    {column.m_Title, caption, column.m_Editable ? std::string("Editable") : std::string("Read-only")},
                    root);
                m_DataSets.emplace(child, dataSet.m_Id);
            }
        }
        m_SchemaModel = std::make_shared<ItemModel>(m_Window.Ui().States(), std::move(builder).Build());
        m_Syncing = true;
        m_Schema->SetModel(m_SchemaModel);
        m_Schema->SetColumnWidth(0, 170);
        m_Schema->SetColumnWidth(1, 110);
        m_Schema->SetColumnWidth(2, 90);
        m_Syncing = false;
    }

    void Refresh()
    {
        if (!Live() || !m_Model || m_Refreshing)
        {
            return;
        }

        struct Scope
        {
            bool& m_Flag;

            ~Scope()
            {
                m_Flag = false;
            }
        } scope{m_Refreshing};

        m_Refreshing = true;
        const auto status = m_Model->GetStatus();
        const auto catalog = m_Model->GetCatalog();
        if (catalog != m_Catalog)
        {
            m_Catalog = catalog;
            BuildSchema();
        }
        if (m_RestoreQuery && !catalog->empty())
        {
            auto query = std::exchange(m_RestoreQuery, {}).value();
            if (std::any_of(catalog->begin(), catalog->end(),
                            [&](const ModelDataSet& set) { return set.m_Id == query.m_DataSet; }))
            {
                query.m_SchemaRevision = 0;
                m_Model->SetQuery(std::move(query));
                return;
            }
        }
        const bool writing = status.m_Operation == AdapterOperation::Committing;
        m_Schema->SetEnabled(!writing);
        m_Editing->SetEnabled(!writing);
        m_Cancel->SetEnabled(status.m_Operation == AdapterOperation::Loading ||
                             status.m_Operation == AdapterOperation::Catalog);
        m_Table->SetEnabled(status.m_Operation == AdapterOperation::Idle);
        const auto pages = status.m_MatchedRows ? (status.m_MatchedRows - 1) / status.m_Query.m_Limit + 1 : 1;
        const auto page = status.m_Query.m_Offset / status.m_Query.m_Limit;
        m_First->SetEnabled(status.m_Operation == AdapterOperation::Idle && page > 0);
        m_Previous->SetEnabled(status.m_Operation == AdapterOperation::Idle && page > 0);
        m_Next->SetEnabled(status.m_Operation == AdapterOperation::Idle && page + 1 < pages);
        m_Last->SetEnabled(status.m_Operation == AdapterOperation::Idle && page + 1 < pages);
        m_PageLabel->SetText("Page " + std::to_string(page + 1) + " / " + std::to_string(pages));
        if (!status.m_Query.m_DataSet.empty())
        {
            if (m_ControlsDataSet != status.m_Query.m_DataSet)
            {
                const auto found = std::find_if(catalog->begin(), catalog->end(), [&](const ModelDataSet& item)
                                                { return item.m_Id == status.m_Query.m_DataSet; });
                if (found != catalog->end())
                {
                    std::vector<std::string> names;
                    for (const auto& column : found->m_Columns)
                    {
                        names.push_back(column.m_Title);
                    }
                    m_FilterColumn->SetItems(std::move(names));
                    if (!found->m_Columns.empty())
                    {
                        m_FilterColumn->SetCurrentIndex(0);
                    }
                }
                m_ControlsDataSet = status.m_Query.m_DataSet;
            }
            if (status.m_Operation == AdapterOperation::Idle)
            {
                m_Draft.Set(status.m_Query);
            }
            m_Table->SetSortIndicators(status.m_Query.m_Sort);
            m_Window.SetTitle("EStudioUI Database Browser / " + PathText(m_Path.filename()) + " / " +
                              status.m_Query.m_DataSet);
        }
        if (!status.m_Error.empty())
        {
            Message(status.m_Error);
        }
        else if (status.m_Operation == AdapterOperation::Catalog)
        {
            Message("Discovering database schemas on a worker...");
        }
        else if (status.m_Operation == AdapterOperation::Loading)
        {
            Message("Loading query page on a worker; the previous result stays intact.");
        }
        else if (writing)
        {
            Message("Committing the exact old-value checked edit on the database worker...");
        }
        else
        {
            Message(std::to_string(status.m_LoadedRows) + " rows loaded / " + std::to_string(status.m_MatchedRows) +
                    " matching / " + std::to_string(status.m_TotalRows) + " total — " +
                    (m_Editing->IsChecked() ? "editing enabled" : "read-only"));
        }
        if (m_CloseAfterWrite && status.m_Operation == AdapterOperation::Idle)
        {
            m_CloseAfterWrite = false;
            if (status.m_Error.empty())
            {
                m_Target.Post([](Window& window) { window.Close(); });
            }
        }
        const auto filters = status.m_Operation == AdapterOperation::Idle ? status.m_Query.m_Filter.m_Children
                                                                          : m_Draft.Get().m_Filter.m_Children;
        std::vector<std::string> captions;
        for (const auto& filter : filters)
        {
            captions.push_back(FilterCaption(filter, m_Model->GetSnapshot().GetColumns()));
        }
        m_FilterList->SetItems(std::move(captions));
    }

    void Open(std::filesystem::path path, bool write)
    {
        if (!Live())
        {
            return;
        }
        if (m_Model && m_Model->GetStatus().m_Operation == AdapterOperation::Committing)
        {
            throw std::logic_error("Wait for the current database edit before opening another file.");
        }
        m_RestoreQuery.reset();
        if (m_Model && path == m_Path && !m_Model->GetStatus().m_Query.m_DataSet.empty())
        {
            m_RestoreQuery = m_Model->GetStatus().m_Query;
        }
        m_ModelChanged.reset();
        m_Model.reset();
        m_Catalog.reset();
        m_DataSets.clear();
        m_ControlsDataSet.clear();
        m_Path = std::move(path);
        m_Draft.Set(ModelQuery{});
        m_Syncing = true;
        m_Editing->SetChecked(write);
        m_Syncing = false;
        m_Model =
            AdapterModel::Create(m_Window.Ui(), std::make_shared<SqliteAdapter>(m_Path, write), m_Application.Tasks());
        m_Table->SetModel(m_Model);
        const auto weak = weak_from_this();
        m_ModelChanged.emplace(m_Model->SubscribeStatus(
            [weak](AdapterStatus)
            {
                if (const auto data = weak.lock(); data && data->Live())
                {
                    data->m_RefreshPosted = true;
                    data->m_Window.Ui().GetDispatcher().Wake();
                }
            }));
        Refresh();
    }

    void Browse()
    {
        FileDialogOptions options;
        options.m_Title = "Open SQLite database";
        options.m_Filters = {{"SQLite databases", "*.sqlite;*.sqlite3;*.db"}, {"All files", "*.*"}};
        const auto weak = weak_from_this();
        FileDialog::Open(m_Window.Ui().Root(), options)
            .OnFinished(
                [weak](FileDialogResult result)
                {
                    if (const auto data = weak.lock(); data && data->Live() &&
                                                       result.m_Status == FileDialogStatus::Accepted &&
                                                       !result.m_Paths.empty())
                    {
                        data->Command([&] { data->Open(result.m_Paths.front(), false); });
                    }
                });
    }

    void AddFilter()
    {
        if (!m_Model)
        {
            return;
        }
        auto query = m_Draft.Get();
        if (query.m_DataSet.empty())
        {
            throw std::logic_error("Choose a dataset before filtering.");
        }
        const int index = m_FilterColumn->GetCurrentIndex();
        if (index < 0)
        {
            throw std::logic_error("Choose a filter column.");
        }
        ModelFilter filter;
        filter.m_Column = static_cast<std::size_t>(index);
        filter.m_Operator = static_cast<ModelFilterOperator>(m_FilterOperator->GetCurrentIndex());
        filter.m_CaseSensitive = m_Case->IsChecked();
        if (filter.m_Operator != ModelFilterOperator::IsNull && filter.m_Operator != ModelFilterOperator::IsNotNull)
        {
            auto column = m_Model->GetSnapshot().GetColumns().at(static_cast<std::size_t>(index));
            column.m_ValueType = (filter.m_Operator == ModelFilterOperator::Contains ||
                                  filter.m_Operator == ModelFilterOperator::StartsWith)
                                     ? ModelValueType::Text
                                     : column.m_EditorType.value_or(ModelValueType::Text);
            filter.m_Value = ParseModelValue(column, m_FilterValue->GetText());
        }
        query.m_Filter.m_Children.push_back(std::move(filter));
        query.m_Filter.m_Any = m_Match->GetCurrentIndex() == 1;
        query.m_Offset = 0;
        Query(std::move(query));
    }

    void Columns()
    {
        if (!m_Table->GetColumnCount())
        {
            return;
        }
        if (auto* old = m_ColumnsDialog.Get())
        {
            m_Window.Ui().Root().RemoveChild(*old);
        }
        auto& dialog = m_Window.Ui().Root().Add<Dialog>();
        m_ColumnsDialog = WidgetRef(&dialog);
        dialog.SetTitle("Columns / visibility and order");
        dialog.SetWidth(SizeRule::Fixed(560));
        dialog.SetHeight(SizeRule::Fixed(430));
        auto& list = dialog.Content().Add<ListWidget>();
        list.SetHeight(SizeRule::Fill());
        list.SetId("browser-column-list");
        const auto weak = weak_from_this();
        const WidgetRef listRef(&list);
        const auto update = [weak, listRef]
        {
            const auto data = weak.lock();
            auto* view = dynamic_cast<ListWidget*>(listRef.Get());
            if (!data || !data->Live() || !view)
            {
                return;
            }
            const int selected = view->GetCurrentIndex();
            std::vector<std::string> names;
            const auto snapshot = data->m_Model->GetSnapshot();
            for (auto column : data->m_Table->GetColumnOrder())
            {
                names.push_back((data->m_Table->IsColumnVisible(column) ? "✓ " : "○ ") +
                                snapshot.GetColumns()[column].m_Title);
            }
            view->SetItems(std::move(names));
            if (view->GetItemCount())
            {
                view->SetCurrentIndex(std::clamp(selected, 0, view->GetItemCount() - 1));
            }
        };
        auto& tools = dialog.Content().Add<ToolBar>();
        for (int command = 0; command < 5; ++command)
        {
            const char* text = command == 0   ? "Toggle visible"
                               : command == 1 ? "Move left"
                               : command == 2 ? "Move right"
                               : command == 3 ? "Show all"
                                              : "Fit widths";
            tools.AddButton(text).Clicked().Connect(
                [weak, listRef, update, command]
                {
                    if (const auto data = weak.lock(); data && data->Live())
                    {
                        data->Command(
                            [&]
                            {
                                auto* view = dynamic_cast<ListWidget*>(listRef.Get());
                                if (!view)
                                {
                                    return;
                                }
                                const auto order = data->m_Table->GetColumnOrder();
                                const int selected = view->GetCurrentIndex();
                                if (command == 3)
                                {
                                    for (std::size_t column = 0; column < order.size(); ++column)
                                    {
                                        data->m_Table->SetColumnVisible(column, true);
                                    }
                                }
                                else if (command == 4)
                                {
                                    data->m_Table->FitColumnsToContents();
                                }
                                else if (selected >= 0)
                                {
                                    const auto column = order.at(static_cast<std::size_t>(selected));
                                    if (command == 0)
                                    {
                                        data->m_Table->SetColumnVisible(column,
                                                                        !data->m_Table->IsColumnVisible(column));
                                    }
                                    else
                                    {
                                        const auto destination = static_cast<std::size_t>(std::clamp(
                                            selected + (command == 1 ? -1 : 1), 0, static_cast<int>(order.size()) - 1));
                                        data->m_Table->MoveColumn(column, destination);
                                        update();
                                        view->SetCurrentIndex(static_cast<int>(destination));
                                        return;
                                    }
                                }
                                update();
                            });
                    }
                });
        }
        dialog.SetButtons({StandardButton::Close});
        update();
        dialog.Open();
    }

    void Export()
    {
        if (!m_Model)
        {
            return;
        }
        const auto snapshot = m_Model->GetSnapshot();
        FileDialogOptions options;
        options.m_Title = "Export loaded result page";
        options.m_DefaultExtension = "csv";
        options.m_FileName = "query-page.csv";
        options.m_Filters = {{"CSV", "*.csv"}};
        const auto weak = weak_from_this();
        FileDialog::Save(m_Window.Ui().Root(), options)
            .OnFinished(
                [weak, snapshot](FileDialogResult result)
                {
                    const auto data = weak.lock();
                    if (!data || !data->Live() || result.m_Status != FileDialogStatus::Accepted ||
                        result.m_Paths.empty())
                    {
                        return;
                    }
                    UiTaskOptions<Window> task;
                    task.m_Error = [weak](Window&, std::exception_ptr error)
                    {
                        if (const auto live = weak.lock())
                        {
                            live->Message(Failure(error));
                        }
                    };
                    data->m_Application.Tasks().Run(
                        data->m_Target,
                        [snapshot, path = result.m_Paths.front()]
                        {
                            std::ofstream output(path, std::ios::binary | std::ios::trunc);
                            if (!output)
                            {
                                throw std::runtime_error("Cannot create the page export.");
                            }
                            for (std::size_t c = 0; c < snapshot.GetColumns().size(); ++c)
                            {
                                if (c)
                                {
                                    output << ',';
                                }
                                output << Csv(snapshot.GetColumns()[c].m_Title);
                            }
                            output << "\r\n";
                            for (std::size_t r = 0; r < snapshot.GetRowCount(); ++r)
                            {
                                for (std::size_t c = 0; c < snapshot.GetColumns().size(); ++c)
                                {
                                    if (c)
                                    {
                                        output << ',';
                                    }
                                    const auto value = snapshot.GetValue(snapshot.GetItem(r), c);
                                    output << Csv(std::holds_alternative<ModelBlob>(value)
                                                      ? EncodeModelBlob(std::get<ModelBlob>(value))
                                                      : FormatModelValue(value));
                                }
                                output << "\r\n";
                            }
                            output.flush();
                            if (!output)
                            {
                                throw std::runtime_error("Writing the page export failed.");
                            }
                        },
                        [weak](Window&)
                        {
                            if (const auto live = weak.lock())
                            {
                                live->Message("The loaded page was exported to CSV.");
                            }
                        },
                        std::move(task));
                });
    }

    void BuildPlayground(Widget& page)
    {
        auto& tools = page.Add<ToolBar>();
        auto& split = page.Add<Splitter>();
        split.SetHeight(SizeRule::Fill());
        auto& tree = split.Add<TreeTable>();
        m_Playground = &tree;
        tree.SetId("browser-playground-tree");
        tree.SetHeight(SizeRule::Fill());
        auto& list = split.Add<ListWidget>();
        list.SetId("browser-playground-list");
        list.SetHeight(SizeRule::Fill());
        auto name = Field("Assembly / part");
        name.m_EditorType = ModelValueType::Text;
        auto quantity = Field("Quantity", ModelValueType::Integer);
        quantity.m_EditorType = ModelValueType::Integer;
        auto mass = Field("Mass / kg", ModelValueType::Real);
        mass.m_EditorType = ModelValueType::Real;
        auto released = Field("Released", ModelValueType::Boolean);
        released.m_EditorType = ModelValueType::Boolean;
        auto material = Field("Material");
        material.m_Choices = {std::string("Aluminium"), std::string("Steel"), std::string("Composite")};
        auto color = Field("Color", ModelValueType::Color);
        color.m_EditorType = ModelValueType::Color;
        auto binary = Field("Binary", ModelValueType::Blob);
        binary.m_EditorType = ModelValueType::Blob;
        ModelBuilder builder({name, quantity, mass, released, material, color, binary});
        const auto wing = builder.Add({std::string("Wing"), std::int64_t{1}, 420.0, true, std::string("Aluminium"),
                                       Color{.3f, .6f, .9f, 1}, ModelBlob{1, 2, 3}});
        builder.Add({std::string("Spar"), std::int64_t{2}, 125.0, true, std::string("Aluminium"),
                     Color{.4f, .7f, .6f, 1}, ModelBlob{}},
                    wing);
        builder.Add({std::string("Skin"), std::int64_t{4}, 180.0, false, std::string("Composite"),
                     Color{.8f, .5f, .3f, 1}, ModelBlob{0, 255}},
                    wing);
        const auto fuselage = builder.Add({std::string("Fuselage"), std::int64_t{1}, 740.0, true,
                                           std::string("Aluminium"), Color{.5f, .5f, .8f, 1}, ModelBlob{42}});
        builder.Add({std::string("Frame"), std::int64_t{12}, 75.0, false, std::string("Steel"), Color{.7f, .5f, .6f, 1},
                     ModelBlob{}},
                    fuselage);
        auto source = std::make_shared<ItemModel>(m_Window.Ui().States(), std::move(builder).Build());
        auto projection = SortFilterModel::Create(source);
        auto selection = SelectionModel::Create(source);
        selection->SetMode(SelectionMode::Multiple);
        tree.SetModel(projection);
        tree.SetSelectionModel(selection);
        tree.SetRowsMovable(true);
        tree.SetColumnsMovable(true);
        tree.SetExpanded(wing, true);
        tree.SetExpanded(fuselage, true);
        list.SetModel(source);
        list.SetSelectionModel(selection);
        list.SetRowsMovable(true);
        split.SetSizes({5, 1});
        const WidgetRef treeRef(&tree);
        const auto weak = weak_from_this();
        tools.AddButton("Source order")
            .Clicked()
            .Connect(
                [projection, treeRef]
                {
                    projection->SetSortKeys({});
                    if (auto* live = dynamic_cast<TreeTable*>(treeRef.Get()))
                    {
                        live->SetSortIndicators({});
                    }
                });
        tools.AddButton("Add child")
            .Clicked()
            .Connect(
                [source, selection]
                {
                    const auto parent = selection->GetCurrent();
                    source->Restructure(
                        [&](ModelBuilder& edit)
                        {
                            edit.Add({std::string("New part"), std::int64_t{1}, 1.0, false, std::string("Composite"),
                                      Color{.5f, .5f, .5f, 1}, ModelBlob{}},
                                     parent);
                        });
                });
        tools.AddButton("Remove selected")
            .Clicked()
            .Connect(
                [source, selection]
                {
                    const auto selected = selection->GetSnapshot();
                    const auto snapshot = source->GetSnapshot();
                    std::vector<ItemId> roots;
                    for (auto item : selected->m_Items)
                    {
                        bool nested = false;
                        for (auto parent = snapshot.GetParent(item); parent; parent = snapshot.GetParent(parent))
                        {
                            if (selected->m_Items.contains(parent))
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
                    source->Restructure(
                        [&](ModelBuilder& edit)
                        {
                            for (auto item : roots)
                            {
                                edit.Remove(item);
                            }
                        });
                });
        tree.SortRequested().Connect(
            [projection, treeRef](std::size_t column, bool ascending, bool additive)
            {
                auto keys = additive ? projection->GetSortKeys() : std::vector<ModelSort>{};
                std::erase_if(keys, [&](const ModelSort& key) { return key.m_Column == column; });
                keys.push_back({column, ascending, false});
                projection->SetSortKeys(keys);
                if (auto* view = dynamic_cast<TreeTable*>(treeRef.Get()))
                {
                    view->SetSortIndicators(std::move(keys));
                }
            });
        tree.DropRejected().Connect(
            [weak](std::string message)
            {
                if (const auto data = weak.lock())
                {
                    data->Message(std::move(message));
                }
            });
        list.DropRejected().Connect(
            [weak](std::string message)
            {
                if (const auto data = weak.lock())
                {
                    data->Message(std::move(message));
                }
            });
        auto& hint =
            page.Add<Label>("Drag records between these views or into a hierarchy parent. Ctrl/Shift selects multiple "
                            "rows. Clear sorting to see manual source order. Double-click cells for typed editors.");
        hint.SetWordWrap(true);
    }
};

DatabaseBrowserWindow::DatabaseBrowserWindow(Application& application, Window& window)
    : m_Data(std::make_shared<Data>(application, window))
{
    auto& data = *m_Data;
    auto& shell = window.Ui().Root().Add<MainWindow>(window);
    data.m_Shell = &shell;
    const auto weak = std::weak_ptr<Data>(m_Data);
    auto& file = shell.TitleBar().Menus().AddMenu("File");
    auto open = window.Ui().CreateAction("Open database...");
    open->SetShortcut({Key::O, ShortcutModifier::Primary});
    open->Triggered().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                live->Command([&] { live->Browse(); });
            }
        });
    file.AddAction(open);
    shell.AddAction(open);
    auto exportPage = window.Ui().CreateAction("Export page CSV...");
    exportPage->Triggered().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                live->Command([&] { live->Export(); });
            }
        });
    file.AddAction(exportPage);
    file.AddAction("Close").Clicked().Connect([target = window.GetHandle()]
                                              { target.Post([](Window& live) { live.Close(); }); });
    auto& appearance = shell.TitleBar().Menus().AddMenu("Appearance");
    for (const bool dark : {false, true})
    {
        appearance.AddAction(dark ? "Dark" : "Light")
            .Clicked()
            .Connect(
                [target = window.GetHandle(), dark]
                {
                    target.Post(
                        [dark](Window& live)
                        {
                            auto theme = live.Themes().GetTheme();
                            theme.m_Mode = dark ? ThemeMode::Dark : ThemeMode::Light;
                            live.Themes().SetTheme(theme);
                        });
                });
    }
    auto& tools = shell.AddToolBar();
    tools.AddAction(open);
    tools.AddAction(exportPage);
    auto& columns = tools.AddButton("Columns...");
    columns.SetId("browser-columns");
    columns.Clicked().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                live->Command([&] { live->Columns(); });
            }
        });
    auto& refresh = tools.AddButton("Refresh schemas");
    refresh.Clicked().Connect(
        [weak]
        {
            if (const auto live = weak.lock(); live && live->m_Model)
            {
                live->Command([&] { live->m_Model->RefreshCatalog(); });
            }
        });
    data.m_Cancel = &tools.AddButton("Cancel loading");
    data.m_Cancel->Clicked().Connect(
        [weak]
        {
            if (const auto live = weak.lock(); live && live->m_Model)
            {
                live->Command([&] { live->m_Model->CancelLoading(); });
            }
        });
    data.m_Editing = &tools.Add<CheckBox>("Enable editing");
    data.m_Editing->SetId("browser-edit-mode");
    data.m_Editing->Toggled().Connect(
        [weak](bool write)
        {
            if (const auto live = weak.lock(); live && !live->m_Syncing && !live->m_Path.empty())
            {
                live->Command([&] { live->Open(live->m_Path, write); });
            }
        });
    auto& tabs = shell.Central().Add<TabWidget>();
    data.m_Tabs = &tabs;
    tabs.SetHeight(SizeRule::Fill());
    auto& browse = tabs.AddTab("Browse SQLite");
    auto& split = browse.Add<Splitter>();
    split.SetHeight(SizeRule::Fill());
    auto& schema = split.Add<TreeTable>();
    data.m_Schema = &schema;
    schema.SetId("browser-schema");
    schema.SetEditable(false);
    schema.SetSortingEnabled(false);
    schema.SetHeight(SizeRule::Fill());
    schema.SetWidth(SizeRule::Fill(1, 220));
    auto& panel = split.Add<Widget>("database-panel");
    panel.SetWidth(SizeRule::Fill(4, 400));
    panel.SetHeight(SizeRule::Fill());
    auto& filters = panel.Add<ToolBar>();
    data.m_FilterColumn = &filters.Add<ComboBox>();
    data.m_FilterColumn->SetWidth(SizeRule::Fixed(140));
    data.m_FilterOperator = &filters.Add<ComboBox>();
    data.m_FilterOperator->SetItems(FilterOperators());
    data.m_FilterOperator->SetCurrentIndex(0);
    data.m_FilterOperator->SetWidth(SizeRule::Fixed(135));
    data.m_FilterValue = &filters.Add<TextBox>();
    data.m_FilterValue->SetPlaceholder("Typed filter value");
    data.m_FilterValue->SetWidth(SizeRule::Fixed(180));
    data.m_Case = &filters.Add<CheckBox>("Match case");
    data.m_Case->SetChecked(true);
    filters.AddButton("Add filter")
        .Clicked()
        .Connect(
            [weak]
            {
                if (const auto live = weak.lock())
                {
                    live->Command([&] { live->AddFilter(); });
                }
            });
    data.m_Match = &filters.Add<ComboBox>();
    data.m_Match->SetItems({"Match all / AND", "Match any / OR"});
    data.m_Match->SetCurrentIndex(0);
    data.m_Match->SetWidth(SizeRule::Fixed(170));
    auto& filterRow = panel.Add<Widget>();
    filterRow.SetLayoutDirection(Axis::Horizontal);
    filterRow.SetHeight(SizeRule::Fixed(66));
    data.m_FilterList = &filterRow.Add<ListWidget>();
    data.m_FilterList->SetWidth(SizeRule::Fill());
    data.m_FilterList->SetHeight(SizeRule::Fill());
    auto& filterTools = filterRow.Add<Widget>();
    filterTools.Add<ToolButton>("Remove filter")
        .Clicked()
        .Connect(
            [weak]
            {
                if (const auto live = weak.lock())
                {
                    live->Command(
                        [&]
                        {
                            auto query = live->m_Draft.Get();
                            const int selected = live->m_FilterList->GetCurrentIndex();
                            if (selected >= 0 && static_cast<std::size_t>(selected) < query.m_Filter.m_Children.size())
                            {
                                query.m_Filter.m_Children.erase(query.m_Filter.m_Children.begin() + selected);
                            }
                            query.m_Offset = 0;
                            live->Query(std::move(query));
                        });
                }
            });
    filterTools.Add<ToolButton>("Clear filters")
        .Clicked()
        .Connect(
            [weak]
            {
                if (const auto live = weak.lock())
                {
                    live->Command(
                        [&]
                        {
                            auto query = live->m_Draft.Get();
                            query.m_Filter = {};
                            query.m_Offset = 0;
                            live->Query(std::move(query));
                        });
                }
            });
    data.m_Table = &panel.Add<TableWidget>();
    data.m_Table->SetId("browser-results");
    data.m_Table->SetHeight(SizeRule::Fill());
    data.m_Table->SetColumnsMovable(true);
    data.m_Table->SortRequested().Connect(
        [weak](std::size_t column, bool ascending, bool additive)
        {
            if (const auto live = weak.lock())
            {
                live->Command(
                    [&]
                    {
                        auto query = live->m_Draft.Get();
                        if (!additive)
                        {
                            query.m_Sort.clear();
                        }
                        std::erase_if(query.m_Sort, [&](const ModelSort& key) { return key.m_Column == column; });
                        query.m_Sort.push_back({column, ascending, false});
                        query.m_Offset = 0;
                        live->Query(std::move(query));
                    });
            }
        });
    data.m_Table->EditRejected().Connect(
        [weak](const std::string& error)
        {
            if (const auto live = weak.lock())
            {
                live->Message(error);
            }
        });
    auto& pages = panel.Add<ToolBar>();
    data.m_First = &pages.AddButton("First");
    data.m_Previous = &pages.AddButton("Previous");
    data.m_Next = &pages.AddButton("Next");
    data.m_Last = &pages.AddButton("Last");
    data.m_PageLabel = &pages.Add<Label>("Page 1 / 1");
    data.m_PageSize = &pages.Add<ComboBox>();
    data.m_PageSize->SetItems({"64 rows", "256 rows", "1024 rows"});
    data.m_PageSize->SetCurrentIndex(1);
    data.m_PageSize->SetWidth(SizeRule::Fixed(110));
    for (int command = 0; command < 4; ++command)
    {
        auto* button = command == 0   ? data.m_First
                       : command == 1 ? data.m_Previous
                       : command == 2 ? data.m_Next
                                      : data.m_Last;
        button->Clicked().Connect(
            [weak, command]
            {
                if (const auto live = weak.lock(); live && live->m_Model)
                {
                    live->Command(
                        [&]
                        {
                            const auto status = live->m_Model->GetStatus();
                            const auto page = status.m_Query.m_Offset / status.m_Query.m_Limit;
                            const auto last =
                                status.m_MatchedRows ? (status.m_MatchedRows - 1) / status.m_Query.m_Limit : 0;
                            live->m_Model->SetPage(command == 0   ? 0
                                                   : command == 1 ? (page ? page - 1 : 0)
                                                   : command == 2 ? std::min(last, page + 1)
                                                                  : last);
                        });
                }
            });
    }
    data.m_PageSize->Activated().Connect(
        [weak](int index)
        {
            if (const auto live = weak.lock())
            {
                live->Command(
                    [&]
                    {
                        auto query = live->m_Draft.Get();
                        query.m_Limit = index == 0 ? 64 : index == 2 ? 1024 : 256;
                        query.m_Offset = 0;
                        live->Query(std::move(query));
                    });
            }
        });
    data.m_Match->Activated().Connect(
        [weak](int index)
        {
            if (const auto live = weak.lock())
            {
                live->Command(
                    [&]
                    {
                        auto query = live->m_Draft.Get();
                        query.m_Filter.m_Any = index == 1;
                        query.m_Offset = 0;
                        live->Query(std::move(query));
                    });
            }
        });
    schema.CurrentCellChanged().Connect(
        [weak](int, int)
        {
            if (const auto live = weak.lock(); live && !live->m_Syncing)
            {
                const auto found = live->m_DataSets.find(live->m_Schema->GetCurrentItemId());
                if (found != live->m_DataSets.end())
                {
                    live->Command([&] { live->Select(found->second); });
                }
            }
        });
    split.SetSizes({1, 4});
    data.BuildPlayground(tabs.AddTab("Models / TreeTable / drag and editors"));
    data.m_Connections.emplace_back(window.CloseRequested().Connect(
        [weak](WindowCloseRequest& request)
        {
            if (const auto live = weak.lock(); live && live->Live() && live->m_Model)
            {
                if (live->m_Table->GetCellEditor())
                {
                    live->m_Table->CommitEdit();
                }
                if (live->m_Model->GetStatus().m_Operation == AdapterOperation::Committing)
                {
                    request.Cancel();
                    live->m_CloseAfterWrite = true;
                }
                else if (live->m_Table->GetCellEditor())
                {
                    request.Cancel();
                    live->Message("Correct or cancel the cell draft before closing.");
                }
                else
                {
                    live->m_Model->CancelLoading();
                }
            }
        }));
    data.m_Connections.emplace_back(window.Ui().IdleTurn().Connect(
        [weak]
        {
            if (const auto live = weak.lock(); live && live->Live() && live->m_RefreshPosted)
            {
                live->m_RefreshPosted = false;
                live->Refresh();
            }
        }));
    shell.Status().SetMessage(
        "Open an existing SQLite file or wait for the engineering sample. External files open read-only.");
}

DatabaseBrowserWindow::~DatabaseBrowserWindow()
{
    m_Data->m_Live = false;
    m_Data->m_OpenScope.Cancel();
    m_Data->m_Connections.clear();
    m_Data->m_ModelChanged.reset();
    m_Data->m_Model.reset();
}

void DatabaseBrowserWindow::Open(std::filesystem::path path, bool writable)
{
    m_Data->Check();
    m_Data->Open(std::move(path), writable);
}

void DatabaseBrowserWindow::OpenExample(std::filesystem::path path, std::size_t rows)
{
    m_Data->Check();
    const auto data = m_Data;
    const auto weak = std::weak_ptr<Data>(data);
    data->Message("Creating the engineering SQLite sample on a worker...");
    UiTaskOptions<Window> options;
    options.m_Error = [weak](Window&, std::exception_ptr error)
    {
        if (const auto live = weak.lock())
        {
            live->Message(Failure(error));
        }
    };
    data->m_Application.Tasks().RunLatest(
        data->m_OpenScope, data->m_Target,
        [path = std::move(path), rows](std::stop_token stop) { return CreateExampleDatabase(path, rows, stop); },
        [weak](Window&, const std::filesystem::path& created)
        {
            if (const auto live = weak.lock(); live && live->Live())
            {
                live->Open(created, true);
            }
        },
        std::move(options));
}

std::shared_ptr<AdapterModel> DatabaseBrowserWindow::GetModel() const
{
    m_Data->Check();
    return m_Data->m_Model;
}

TableWidget& DatabaseBrowserWindow::DataView() const
{
    m_Data->Check();
    return *m_Data->m_Table;
}

TreeTable& DatabaseBrowserWindow::SchemaView() const
{
    m_Data->Check();
    return *m_Data->m_Schema;
}

TreeTable& DatabaseBrowserWindow::Playground() const
{
    m_Data->Check();
    return *m_Data->m_Playground;
}

TabWidget& DatabaseBrowserWindow::Tabs() const
{
    m_Data->Check();
    return *m_Data->m_Tabs;
}
} // namespace EStudioUI
