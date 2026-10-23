#include "CreatorWindow.h"

#include <EStudioUI/Bindings.h>
#include <EStudioUI/CollapsiblePanel.h>
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/FileDialog.h>
#include <EStudioUI/Frame.h>
#include <EStudioUI/MessageBox.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UnitEditor.h>
#include <EStudioUI/VectorEditor.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace EStudioUI
{
namespace
{
template <typename F> auto Alive(std::weak_ptr<void> lifetime, F callback)
{
    return [lifetime, callback = std::move(callback)](auto&&... values)
    {
        if (!lifetime.expired())
        {
            callback(std::forward<decltype(values)>(values)...);
        }
    };
}

UiNode* ParentOf(UiNode& root, std::string_view id)
{
    for (auto& child : root.m_Children)
    {
        if (child.m_Id == id)
        {
            return &root;
        }
        if (auto* parent = ParentOf(child, id))
        {
            return parent;
        }
    }
    return nullptr;
}

void Position(UiNode& parent, const std::string& id, double x, double y)
{
    parent.m_Constraints.erase(
        std::remove_if(parent.m_Constraints.begin(), parent.m_Constraints.end(), [&](const UiConstraint& rule)
                       { return rule.m_Item == id && (rule.m_Anchor == "Left" || rule.m_Anchor == "Top"); }),
        parent.m_Constraints.end());
    for (const auto& [anchor, offset] : {std::pair{"Left", x}, std::pair{"Top", y}})
    {
        UiConstraint rule;
        rule.m_Item = id;
        rule.m_Anchor = anchor;
        rule.m_TargetAnchor = anchor;
        rule.m_Offset = offset;
        rule.m_Name = id + "." + anchor;
        parent.m_Constraints.push_back(std::move(rule));
    }
}

VectorValue PositionOf(const UiNode& parent, std::string_view id)
{
    VectorValue value{0, 0};
    for (const auto& rule : parent.m_Constraints)
    {
        if (rule.m_Item == id && rule.m_Target == "parent" && rule.m_Relation == ConstraintRelation::Equal &&
            rule.m_Multiplier == 1)
        {
            if (rule.m_Anchor == "Left" && rule.m_TargetAnchor == "Left")
            {
                value[0] = rule.m_Offset;
            }
            if (rule.m_Anchor == "Top" && rule.m_TargetAnchor == "Top")
            {
                value[1] = rule.m_Offset;
            }
        }
    }
    return value;
}

bool SameStructure(const UiNode& before, const UiNode& after)
{
    if (before.m_Id != after.m_Id || before.m_Type != after.m_Type || before.m_Layout != after.m_Layout ||
        before.m_Columns != after.m_Columns || before.m_Gap != after.m_Gap ||
        before.m_Constraints != after.m_Constraints || before.m_Children.size() != after.m_Children.size())
    {
        return false;
    }
    // Removing an explicit override must reveal the theme or native control default.
    for (const auto& [name, value] : before.m_Properties)
    {
        (void)value;
        if (!after.m_Properties.contains(name))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < before.m_Children.size(); ++index)
    {
        if (!SameStructure(before.m_Children[index], after.m_Children[index]))
        {
            return false;
        }
    }
    return true;
}

void UpdateProperties(const UiNode& before, const UiNode& after, const WidgetRegistry& registry,
                      const UiBuildResult& built)
{
    auto* widget = built.m_Widgets.at(after.m_Id).Get();
    if (!widget)
    {
        throw std::runtime_error("The preview widget expired; reload the design.");
    }
    for (const auto& [name, value] : after.m_Properties)
    {
        const auto old = before.m_Properties.find(name);
        if (old == before.m_Properties.end() || old->second != value)
        {
            registry.FindProperty(after.m_Type, name).m_Apply(*widget, value);
        }
    }
    for (const bool width : {true, false})
    {
        const auto policy = after.m_Properties.find(width ? "widthPolicy" : "heightPolicy");
        if (policy == after.m_Properties.end())
        {
            continue;
        }
        const auto& name = std::get<std::string>(policy->second);
        SizeRule size = name == "Fill" ? SizeRule::Fill() : SizeRule::Content();
        if (name == "Fixed")
        {
            const auto found = after.m_Properties.find(width ? "width" : "height");
            size = SizeRule::Fixed(found == after.m_Properties.end()
                                       ? (width ? 160.0f : 36.0f)
                                       : static_cast<float>(std::get<double>(found->second)));
        }
        if (width)
        {
            widget->SetWidth(size);
        }
        else
        {
            widget->SetHeight(size);
        }
    }
    for (std::size_t index = 0; index < after.m_Children.size(); ++index)
    {
        UpdateProperties(before.m_Children[index], after.m_Children[index], registry, built);
    }
}

double Number(std::string_view text)
{
    double value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
    {
        throw std::invalid_argument("Constraint numbers must be finite decimal values.");
    }
    return value;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 4 * 1024 * 1024)
    {
        throw std::runtime_error("Cannot read UI file, or its size exceeds 4 MiB.");
    }
    std::string text(static_cast<std::size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!text.empty() && !input.read(text.data(), static_cast<std::streamsize>(text.size())))
    {
        throw std::runtime_error("Could not read the complete UI file.");
    }
    return text;
}

void WriteFile(const std::filesystem::path& path, const std::string& text)
{
    auto temporary = path;
    temporary += ".estudioui-" + std::to_string(UiDispatcher::AllocateTargetKey()) + ".tmp";
    try
    {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.close();
            if (!output)
            {
                throw std::runtime_error("Could not write the complete UI output.");
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw std::runtime_error("Could not atomically replace the UI output.");
        }
#else
        std::filesystem::rename(temporary, path);
#endif
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
} // namespace

CreatorWindow::CreatorWindow(Application& application, Window& window, std::string baseCss)
    : m_Application(application), m_Window(window), m_BaseCss(std::move(baseCss))
{
    auto initial = ExampleDocument();
    initial.Validate(m_Registry);
    m_Document = std::make_unique<State<UiDocument>>(
        window.Ui().States(), std::move(initial), [this](const UiDocument& candidate)
        { candidate.Validate(m_Registry, candidate.m_StyleSheet != m_AppliedCss); });
    m_Saved = m_Document->GetSnapshot();
    BuildShell();
    const std::weak_ptr<void> lifetime = m_Lifetime;
    m_Connections.emplace_back(
        m_Document->Subscribe(Alive(lifetime,
                                    [this](const StateChange<UiDocument>& change)
                                    {
                                        if (!m_Replaying)
                                        {
                                            m_Undo.push_back(change.GetBeforeSnapshot());
                                            if (m_Undo.size() > 32)
                                            {
                                                m_Undo.erase(m_Undo.begin());
                                            }
                                            m_Redo.clear();
                                        }
                                        m_PreviewDirty = true;
                                        Status(IsDirty() ? "Modified / preview updates at the next frame" : "Ready");
                                    })));
    m_Connections.emplace_back(window.FramePreparing().Connect(Alive(lifetime, [this] { Refresh(); })));
    Refresh();
    SelectNode("submit");
}

CreatorWindow::~CreatorWindow()
{
    m_Lifetime.reset();
    for (auto& task : m_Tasks)
    {
        task.Cancel();
    }
    m_InspectorConnections.clear();
    m_Connections.clear();
    // PropertyGrid may otherwise retain a State after this controller's validator captures expire.
    if (m_Inspector)
    {
        m_Inspector->SetModel({});
    }
}

State<UiDocument>& CreatorWindow::Document()
{
    return *m_Document;
}

WidgetRegistry& CreatorWindow::Registry()
{
    return m_Registry;
}

void CreatorWindow::BuildShell()
{
    auto& ui = m_Window.Ui();
    m_Shell = &ui.Root().Add<MainWindow>(m_Window);
    m_Shell->TitleBar().SetCaption("EStudioUI Creator");
    auto& marker = m_Shell->TitleBar().Content().Add<Label>("UI AUTHORING / 0.23");
    marker.AddClass("creator-mark");
    const std::weak_ptr<void> lifetime = m_Lifetime;
    auto& file = m_Shell->TitleBar().Menus().AddMenu("File");
    auto& edit = m_Shell->TitleBar().Menus().AddMenu("Edit");
    auto& toolbar = m_Shell->AddToolBar();
    const auto command = [&](std::string title, Key key, auto callback, ContextMenu& menu)
    {
        auto action = ui.CreateAction(std::move(title));
        if (key != Key::Unknown)
        {
            action->SetShortcut(Shortcut{key, ShortcutModifier::Primary});
        }
        action->Triggered().Connect(Alive(lifetime, std::move(callback)));
        menu.AddAction(action);
        toolbar.AddAction(action);
        m_Shell->AddAction(action);
        return action;
    };
    command("Open UI...", Key::O, [this] { Open(); }, file);
    command("Save JSON...", Key::S, [this] { Save(false); }, file);
    command("Export C++...", Key::Unknown, [this] { Save(true); }, file);
    toolbar.AddSeparator();
    command("Undo", Key::Z, [this] { Undo(); }, edit);
    command("Redo", Key::Y, [this] { Redo(); }, edit);
    command("Delete", Key::Unknown, [this] { DeleteSelected(); }, edit);
    auto& preview = toolbar.AddButton("Live preview");
    preview.SetCheckable(true);
    preview.Toggled().Connect(Alive(lifetime,
                                    [this](bool live)
                                    {
                                        m_Surface->SetDesignMode(!live);
                                        Status(live ? "Live preview / interact with actual widgets"
                                                    : "Design mode / select, drag or resize the selected widget");
                                    }));
    auto& reset = toolbar.AddButton("Clear CSS overrides");
    reset.Clicked().Connect(Alive(lifetime,
                                  [this]
                                  {
                                      auto candidate = m_Document->Get();
                                      auto* node = candidate.Find(m_Selected);
                                      if (!node)
                                      {
                                          return;
                                      }
                                      for (auto property = node->m_Properties.begin();
                                           property != node->m_Properties.end();)
                                      {
                                          if (property->first.starts_with("style."))
                                          {
                                              property = node->m_Properties.erase(property);
                                          }
                                          else
                                          {
                                              ++property;
                                          }
                                      }
                                      m_Document->Set(std::move(candidate));
                                  }));
    auto& paletteDock = m_Shell->Docks().AddDock("creator-palette", "Widgets", DockArea::Left);
    auto& palette = paletteDock.Content();
    auto& search = palette.Add<TextBox>();
    search.SetPlaceholder("Filter widget types");
    search.TextChanged().Connect(Alive(lifetime, [this](const std::string& query) { FilterPalette(query); }));
    m_Palette = &palette.Add<ListWidget>();
    m_Palette->SetHeight(SizeRule::Fill());
    auto& add = palette.Add<Button>("Add selected widget");
    add.Clicked().Connect(Alive(lifetime,
                                [this]
                                {
                                    const int index = m_Palette->GetCurrentIndex();
                                    if (index >= 0 && static_cast<std::size_t>(index) < m_PaletteIds.size())
                                    {
                                        AddWidget(m_PaletteIds[static_cast<std::size_t>(index)]);
                                    }
                                }));
    FilterPalette({});
    auto& outlineDock = m_Shell->Docks().AddDock("creator-outline", "Document", DockArea::Left);
    m_Shell->Docks().Tabify(outlineDock, paletteDock);
    m_Outline = &outlineDock.Content().Add<ListWidget>();
    m_Outline->SetHeight(SizeRule::Fill());
    m_Outline->CurrentChanged().Connect(Alive(lifetime,
                                              [this](int index)
                                              {
                                                  if (!m_Syncing && index >= 0 &&
                                                      static_cast<std::size_t>(index) < m_OutlineIds.size())
                                                  {
                                                      SelectNode(m_OutlineIds[static_cast<std::size_t>(index)]);
                                                  }
                                              }));
    auto& properties = m_Shell->Docks().AddDock("creator-properties", "Inspector", DockArea::Right).Content();
    auto& section = properties.Add<CollapsiblePanel>("Selected widget");
    section.SetHeight(SizeRule::Fill());
    section.Content().SetHeight(SizeRule::Fill());
    m_Inspector = &section.Content().Add<PropertyGrid>();
    m_Inspector->SetHeight(SizeRule::Fill());
    m_Inspector->EditRejected().Connect(Alive(lifetime, [this](const std::string& error) { Status(error); }));
    m_Surface = &m_Shell->Central().Add<DesignerSurface>();
    m_Surface->Selected().Connect(Alive(lifetime, [this](const std::string& id) { SelectNode(id); }));
    m_Surface->GeometryEdited().Connect(Alive(lifetime,
                                              [this](const std::string& id, Point delta, bool resize, bool finished)
                                              { EditGeometry(id, delta, resize, finished); }));
    auto& cssDock = m_Shell->Docks().AddDock("creator-css", "CSS source", DockArea::Bottom);
    auto& css = cssDock.Content();
    auto& cssTools = css.Add<ToolBar>();
    cssTools.AddButton("Apply CSS")
        .Clicked()
        .Connect(Alive(lifetime,
                       [this]
                       {
                           m_Css->CommitEdit();
                           std::string text;
                           for (int row = 0; row < m_Css->GetRowCount(); ++row)
                           {
                               text += m_Css->GetCell(row, 0) + '\n';
                           }
                           try
                           {
                               ApplyCss(std::move(text));
                           }
                           catch (const std::exception& error)
                           {
                               Status(error.what());
                           }
                       }));
    cssTools.AddButton("Add line")
        .Clicked()
        .Connect(Alive(lifetime,
                       [this]
                       {
                           const int row = m_Css->AddRow({""});
                           m_Css->BeginEdit(row, 0);
                       }));
    cssTools.AddButton("Remove line")
        .Clicked()
        .Connect(Alive(lifetime,
                       [this]
                       {
                           const int selected = m_Css->GetCurrentCell().first;
                           std::vector<std::vector<std::string>> rows;
                           for (int row = 0; row < m_Css->GetRowCount(); ++row)
                           {
                               if (row != selected)
                               {
                                   rows.push_back({m_Css->GetCell(row, 0)});
                               }
                           }
                           m_Css->SetRows(std::move(rows));
                       }));
    m_Css = &css.Add<TableWidget>();
    m_Css->SetColumns(
        {{"CSS / double-click a line to edit; Apply commits the complete source", SizeRule::Fill(1, 200)}});
    m_Css->SetHeight(SizeRule::Fill());
    auto& constraintDock = m_Shell->Docks().AddDock("creator-constraints", "Constraints", DockArea::Bottom);
    m_Shell->Docks().Tabify(constraintDock, cssDock);
    auto& constraints = constraintDock.Content();
    auto& constraintTools = constraints.Add<ToolBar>();
    constraintTools.AddButton("Apply rules")
        .Clicked()
        .Connect(Alive(lifetime,
                       [this]
                       {
                           try
                           {
                               ApplyConstraints();
                           }
                           catch (const std::exception& error)
                           {
                               Status(error.what());
                           }
                       }));
    constraintTools.AddButton("Add rule")
        .Clicked()
        .Connect(Alive(lifetime,
                       [this]
                       {
                           m_Constraints->AddRow(
                               {m_Selected, "Left", "Equal", "parent", "Left", "1", "16", "Required", "new-rule"});
                       }));
    constraintTools.AddButton("Remove rule")
        .Clicked()
        .Connect(Alive(lifetime,
                       [this]
                       {
                           const int selected = m_Constraints->GetCurrentCell().first;
                           std::vector<std::vector<std::string>> rows;
                           for (int row = 0; row < m_Constraints->GetRowCount(); ++row)
                           {
                               if (row != selected)
                               {
                                   std::vector<std::string> cells;
                                   for (int column = 0; column < 9; ++column)
                                   {
                                       cells.push_back(m_Constraints->GetCell(row, column));
                                   }
                                   rows.push_back(std::move(cells));
                               }
                           }
                           m_Constraints->SetRows(std::move(rows));
                       }));
    m_Constraints = &constraints.Add<TableWidget>();
    m_Constraints->SetColumns({{"Item", SizeRule::Fixed(130)},
                               {"Anchor", SizeRule::Fixed(90)},
                               {"Relation", SizeRule::Fixed(100)},
                               {"Target", SizeRule::Fixed(130)},
                               {"Target anchor", SizeRule::Fixed(100)},
                               {"Factor", SizeRule::Fixed(70)},
                               {"Offset", SizeRule::Fixed(80)},
                               {"Strength", SizeRule::Fixed(90)},
                               {"Rule name", SizeRule::Fill(1, 140)}});
    m_Constraints->SetHeight(SizeRule::Fill());
    m_Shell->Status().SetMessage("Design mode / add widgets, edit properties, export JSON or C++");
}

void CreatorWindow::FilterPalette(std::string_view query)
{
    std::vector<std::string> items;
    m_PaletteIds.clear();
    for (const auto& [name, descriptor] : m_Registry.GetTypes())
    {
        (void)descriptor;
        if (query.empty() || name.find(query) != std::string::npos)
        {
            items.push_back(name);
            m_PaletteIds.push_back(name);
        }
    }
    m_Palette->SetItems(std::move(items));
    if (!m_PaletteIds.empty())
    {
        m_Palette->SetCurrentIndex(0);
    }
}

void CreatorWindow::LoadDocument(UiDocument document)
{
    document.Validate(m_Registry);
    m_Replaying = true;
    try
    {
        m_Document->Set(std::move(document));
    }
    catch (...)
    {
        m_Replaying = false;
        throw;
    }
    m_Replaying = false;
    ++m_FileGeneration;
    m_Undo.clear();
    m_Redo.clear();
    m_Saved = m_Document->GetSnapshot();
    m_Selected = m_Document->Get().m_Root.m_Id;
    m_PreviewDirty = true;
}

void CreatorWindow::SelectNode(std::string id)
{
    if (!m_Document->Get().Find(id))
    {
        throw std::invalid_argument("Cannot select an absent design node.");
    }
    m_Selected = std::move(id);
    m_Surface->SetSelected(m_Selected);
    const auto selected = std::find(m_OutlineIds.begin(), m_OutlineIds.end(), m_Selected);
    if (selected != m_OutlineIds.end())
    {
        m_Syncing = true;
        m_Outline->SetCurrentIndex(static_cast<int>(selected - m_OutlineIds.begin()));
        m_Syncing = false;
    }
    RefreshInspector();
    Status("Selected " + m_Selected + " / properties use canonical validated values");
}

std::string CreatorWindow::AddWidget(std::string type, std::string parent)
{
    const auto& descriptor = m_Registry.Find(type);
    auto candidate = m_Document->Get();
    if (parent.empty())
    {
        auto* selected = candidate.Find(m_Selected);
        parent = selected && m_Registry.Find(selected->m_Type).m_Content ? m_Selected : candidate.m_Root.m_Id;
    }
    auto* container = candidate.Find(parent);
    if (!container || !m_Registry.Find(container->m_Type).m_Content)
    {
        throw std::invalid_argument("Choose a widget with a registered content slot before adding children.");
    }
    UiNode node;
    node.m_Type = type;
    for (std::size_t suffix = 1;; ++suffix)
    {
        node.m_Id = type + std::to_string(suffix);
        if (!candidate.Find(node.m_Id))
        {
            break;
        }
    }
    for (const auto& property : descriptor.m_Properties)
    {
        if (property.m_Definition.m_Id == "text" || property.m_Definition.m_Id == "title" ||
            property.m_Definition.m_Id == "placeholder")
        {
            node.m_Properties.emplace(property.m_Definition.m_Id, property.m_Definition.m_Default);
        }
    }
    node.m_Properties["width"] = 180.0;
    node.m_Properties["height"] = descriptor.m_Content ? 180.0 : 38.0;
    const auto id = node.m_Id;
    container->m_Children.push_back(std::move(node));
    if (container->m_Layout == "Constraints")
    {
        const double offset = 24 + static_cast<double>(container->m_Children.size() % 8) * 24;
        Position(*container, id, offset, offset);
    }
    m_Document->Set(std::move(candidate));
    SelectNode(id);
    return id;
}

void CreatorWindow::DeleteSelected()
{
    auto candidate = m_Document->Get();
    auto* parent = ParentOf(candidate.m_Root, m_Selected);
    if (!parent)
    {
        Status("The design root is retained; select a child widget to delete.");
        return;
    }
    parent->m_Children.erase(std::remove_if(parent->m_Children.begin(), parent->m_Children.end(),
                                            [&](const UiNode& node) { return node.m_Id == m_Selected; }),
                             parent->m_Children.end());
    parent->m_Constraints.erase(std::remove_if(parent->m_Constraints.begin(), parent->m_Constraints.end(),
                                               [&](const UiConstraint& rule)
                                               { return rule.m_Item == m_Selected || rule.m_Target == m_Selected; }),
                                parent->m_Constraints.end());
    const auto next = parent->m_Id;
    m_Document->Set(std::move(candidate));
    SelectNode(next);
}

void CreatorWindow::SetProperty(std::string_view id, std::string name, PropertyValue value)
{
    auto candidate = m_Document->Get();
    auto* node = candidate.Find(id);
    if (!node)
    {
        throw std::invalid_argument("Cannot edit an absent design node.");
    }
    ValidatePropertyValue(m_Registry.FindProperty(node->m_Type, name).m_Definition, value);
    node->m_Properties[std::move(name)] = std::move(value);
    m_Document->Set(std::move(candidate));
}

void CreatorWindow::Undo()
{
    if (m_Undo.empty())
    {
        return;
    }
    const auto snapshot = m_Undo.back();
    m_Redo.push_back(m_Document->GetSnapshot());
    m_Replaying = true;
    try
    {
        m_Document->Set(*snapshot);
    }
    catch (...)
    {
        m_Replaying = false;
        m_Redo.pop_back();
        throw;
    }
    m_Replaying = false;
    m_Undo.pop_back();
    m_PreviewDirty = true;
}

void CreatorWindow::Redo()
{
    if (m_Redo.empty())
    {
        return;
    }
    const auto snapshot = m_Redo.back();
    m_Undo.push_back(m_Document->GetSnapshot());
    m_Replaying = true;
    try
    {
        m_Document->Set(*snapshot);
    }
    catch (...)
    {
        m_Replaying = false;
        m_Undo.pop_back();
        throw;
    }
    m_Replaying = false;
    m_Redo.pop_back();
    m_PreviewDirty = true;
}

bool CreatorWindow::IsDirty() const
{
    return !m_Saved || m_Document->Get() != *m_Saved;
}

void CreatorWindow::ApplyCss(std::string css)
{
    auto candidate = m_Document->Get();
    candidate.m_StyleSheet = std::move(css);
    candidate.Validate(m_Registry);
    m_Document->Set(std::move(candidate));
}

void CreatorWindow::Refresh()
{
    if (!m_PreviewDirty)
    {
        return;
    }
    const auto document = m_Document->GetSnapshot();
    const bool cssChanged = !m_Previewed || document->m_StyleSheet != m_AppliedCss;
    if (cssChanged)
    {
        std::string error;
        if (!m_Window.Ui().SetStyleSheet(m_BaseCss + '\n' + document->m_StyleSheet, error))
        {
            throw std::invalid_argument("Creator stylesheet: " + error);
        }
        m_AppliedCss = document->m_StyleSheet;
        std::vector<std::vector<std::string>> lines;
        std::istringstream source(document->m_StyleSheet);
        for (std::string line; std::getline(source, line);)
        {
            lines.push_back({std::move(line)});
        }
        m_Css->SetRows(std::move(lines));
    }
    const bool rebuild = !m_Previewed || !SameStructure(m_Previewed->m_Root, document->m_Root);
    if (rebuild)
    {
        // New construction is staged first; a factory failure leaves the prior preview alive.
        Widget staging;
        auto built = BuildUiDocument(staging, *document, m_Registry);
        m_Surface->Host().ClearChildren();
        built.m_Root.Get()->ReparentTo(m_Surface->Host());
        m_Built = std::move(built);
    }
    else
    {
        UpdateProperties(m_Previewed->m_Root, document->m_Root, m_Registry, m_Built);
    }
    if (!document->Find(m_Selected))
    {
        m_Selected = document->m_Root.m_Id;
    }
    std::vector<std::string> order;
    const auto gather = [&](const auto& self, const UiNode& node) -> void
    {
        order.push_back(node.m_Id);
        for (const auto& child : node.m_Children)
        {
            self(self, child);
        }
    };
    gather(gather, document->m_Root);
    m_Surface->SetWidgets(m_Built, std::move(order));
    m_Surface->SetSelected(m_Selected);
    m_Previewed = document;
    m_PreviewDirty = false;
    RefreshOutline();
    if (rebuild || m_InspectorTarget != m_Selected || !m_Inspector->GetActiveEditor())
    {
        RefreshInspector();
    }
    else if (m_Properties)
    {
        const auto* node = document->Find(m_Selected);
        auto copy = *document;
        const auto* parent = ParentOf(copy.m_Root, m_Selected);
        std::vector<PropertyValue> values;
        for (const auto& definition : m_Properties->GetDefinitions())
        {
            const auto found = node->m_Properties.find(definition.m_Id);
            if (definition.m_Id == "layout")
            {
                values.emplace_back(node->m_Layout);
            }
            else if (definition.m_Id == "gap")
            {
                values.emplace_back(static_cast<double>(node->m_Gap));
            }
            else if (definition.m_Id == "columns")
            {
                values.emplace_back(static_cast<double>(node->m_Columns));
            }
            else if (definition.m_Id == "position" && parent)
            {
                values.emplace_back(PositionOf(*parent, m_Selected));
            }
            else
            {
                values.push_back(found == node->m_Properties.end() ? definition.m_Default : found->second);
            }
        }
        m_Syncing = true;
        try
        {
            m_Properties->Values().Set(std::move(values));
        }
        catch (...)
        {
            m_Syncing = false;
            throw;
        }
        m_Syncing = false;
    }
    m_Shell->TitleBar().SetCaption(std::string("EStudioUI Creator") + (IsDirty() ? " *" : ""));
}

void CreatorWindow::RefreshOutline()
{
    m_Syncing = true;
    std::vector<std::string> lines;
    m_OutlineIds.clear();
    const auto visit = [&](const auto& self, const UiNode& node, std::size_t depth) -> void
    {
        lines.push_back(std::string(depth * 2, ' ') + node.m_Id + " : " + node.m_Type);
        m_OutlineIds.push_back(node.m_Id);
        for (const auto& child : node.m_Children)
        {
            self(self, child, depth + 1);
        }
    };
    visit(visit, m_Document->Get().m_Root, 0);
    m_Outline->SetItems(std::move(lines));
    const auto selected = std::find(m_OutlineIds.begin(), m_OutlineIds.end(), m_Selected);
    if (selected != m_OutlineIds.end())
    {
        m_Outline->SetCurrentIndex(static_cast<int>(selected - m_OutlineIds.begin()));
    }
    m_Syncing = false;
}

void CreatorWindow::RefreshInspector()
{
    if (m_Syncing)
    {
        return;
    }
    const auto document = m_Document->GetSnapshot();
    const auto* node = document->Find(m_Selected);
    if (!node)
    {
        return;
    }
    m_InspectorConnections.clear();
    m_InspectorTarget = m_Selected;
    std::vector<PropertyDefinition> definitions;
    PropertyDefinition identity{"identity", "ID / type", "Identity", PropertyKind::String,
                                node->m_Id + " : " + node->m_Type};
    identity.m_ReadOnly = true;
    definitions.push_back(std::move(identity));
    for (const auto& adapter : m_Registry.Find(node->m_Type).m_Properties)
    {
        auto definition = adapter.m_Definition;
        if (const auto value = node->m_Properties.find(definition.m_Id); value != node->m_Properties.end())
        {
            definition.m_Default = value->second;
        }
        else if (definition.m_Id == "widthPolicy" && node->m_Properties.contains("width"))
        {
            definition.m_Default = std::string("Fixed");
        }
        else if (definition.m_Id == "heightPolicy" && node->m_Properties.contains("height"))
        {
            definition.m_Default = std::string("Fixed");
        }
        definitions.push_back(std::move(definition));
    }
    if (m_Registry.Find(node->m_Type).m_Content)
    {
        PropertyDefinition layout{"layout", "Layout", "Layout", PropertyKind::Choice, node->m_Layout};
        layout.m_Choices = {"Column", "Row", "Grid", "Flow", "Constraints"};
        definitions.push_back(std::move(layout));
        PropertyDefinition gap{"gap", "Gap", "Layout", PropertyKind::Number, static_cast<double>(node->m_Gap)};
        gap.m_Minimum = 0;
        gap.m_Maximum = 10000;
        definitions.push_back(std::move(gap));
        PropertyDefinition columns{"columns", "Grid columns", "Layout", PropertyKind::Number,
                                   static_cast<double>(node->m_Columns)};
        columns.m_Minimum = 1;
        columns.m_Maximum = 64;
        columns.m_Decimals = 0;
        definitions.push_back(std::move(columns));
    }
    auto copy = *document;
    auto* parent = ParentOf(copy.m_Root, m_Selected);
    if (parent && parent->m_Layout == "Constraints")
    {
        PropertyDefinition position{"position", "Position X / Y", "Geometry", PropertyKind::Vector,
                                    PositionOf(*parent, m_Selected)};
        position.m_Minimum = -1e6;
        position.m_Maximum = 1e6;
        definitions.push_back(std::move(position));
    }
    m_Properties = std::make_shared<PropertyModel>(m_Window.Ui().States(), std::move(definitions));
    const std::weak_ptr<void> lifetime = m_Lifetime;
    m_InspectorConnections.emplace_back(
        m_Properties->Values().AddValidator(Alive(lifetime, [this](const std::vector<PropertyValue>& values)
                                                  { FromInspector(values).Validate(m_Registry, false); })));
    m_InspectorConnections.emplace_back(
        m_Properties->Values().Subscribe(Alive(lifetime,
                                               [this](const StateChange<std::vector<PropertyValue>>& change)
                                               {
                                                   if (!m_Syncing)
                                                   {
                                                       m_Document->Set(FromInspector(change.GetAfter()));
                                                   }
                                               })));
    m_Inspector->SetModel(m_Properties);
    const auto* owner = m_Registry.Find(node->m_Type).m_Content ? node : parent;
    m_ConstraintOwner = owner ? owner->m_Id : std::string{};
    std::vector<std::vector<std::string>> rules;
    if (owner)
    {
        for (const auto& rule : owner->m_Constraints)
        {
            rules.push_back({rule.m_Item, rule.m_Anchor,
                             rule.m_Relation == ConstraintRelation::Equal       ? "Equal"
                             : rule.m_Relation == ConstraintRelation::LessEqual ? "LessEqual"
                                                                                : "GreaterEqual",
                             rule.m_Target, rule.m_TargetAnchor, FormatPropertyValue(rule.m_Multiplier),
                             FormatPropertyValue(rule.m_Offset),
                             rule.m_Strength == ConstraintStrength::Required ? "Required"
                             : rule.m_Strength == ConstraintStrength::Strong ? "Strong"
                             : rule.m_Strength == ConstraintStrength::Medium ? "Medium"
                                                                             : "Weak",
                             rule.m_Name});
        }
    }
    m_Constraints->SetRows(std::move(rules));
}

UiDocument CreatorWindow::FromInspector(const std::vector<PropertyValue>& values) const
{
    auto candidate = m_Document->Get();
    auto* node = candidate.Find(m_Selected);
    if (!node || values.size() != m_Properties->GetDefinitions().size())
    {
        throw std::invalid_argument("Inspector target or schema changed during editing.");
    }
    const auto& definitions = m_Properties->GetDefinitions();
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const auto& definition = definitions[index];
        if (definition.m_Id == "identity")
        {
            continue;
        }
        if (definition.m_Id == "layout")
        {
            node->m_Layout = std::get<std::string>(values[index]);
            if (node->m_Layout != "Constraints")
            {
                node->m_Constraints.clear();
            }
        }
        else if (definition.m_Id == "gap")
        {
            node->m_Gap = static_cast<float>(std::get<double>(values[index]));
        }
        else if (definition.m_Id == "columns")
        {
            const double number = std::get<double>(values[index]);
            if (std::floor(number) != number)
            {
                throw std::invalid_argument("Grid column count must be an integer.");
            }
            node->m_Columns = static_cast<int>(number);
        }
        else if (definition.m_Id == "position")
        {
            auto* parent = ParentOf(candidate.m_Root, m_Selected);
            const auto& position = std::get<VectorValue>(values[index]);
            if (parent && position != PositionOf(*parent, m_Selected))
            {
                Position(*parent, m_Selected, position[0], position[1]);
            }
        }
        else
        {
            const auto existing = node->m_Properties.find(definition.m_Id);
            const auto& current = existing == node->m_Properties.end() ? definition.m_Default : existing->second;
            if (values[index] != current)
            {
                node->m_Properties[definition.m_Id] = values[index];
            }
        }
    }
    return candidate;
}

void CreatorWindow::EditGeometry(std::string id, Point delta, bool resize, bool finished)
{
    const auto found = m_Built.m_Widgets.find(id);
    auto* widget = found == m_Built.m_Widgets.end() ? nullptr : found->second.Get();
    if (!widget)
    {
        return;
    }
    if (!m_Gesture)
    {
        m_Gesture = m_Document->GetSnapshot();
        m_GestureBounds = widget->GetBounds();
    }
    auto candidate = *m_Gesture;
    auto* node = candidate.Find(id);
    auto* parent = ParentOf(candidate.m_Root, id);
    if (!node || (!resize && (!parent || parent->m_Layout != "Constraints")))
    {
        Status("Free movement uses a Constraints parent; flow layouts retain their ordering.");
        if (finished)
        {
            m_Gesture.reset();
        }
        return;
    }
    auto bounds = m_GestureBounds;
    if (resize)
    {
        bounds.m_Width = std::clamp(bounds.m_Width + delta.m_X, 24.0f, 10000.0f);
        bounds.m_Height = std::clamp(bounds.m_Height + delta.m_Y, 24.0f, 10000.0f);
        node->m_Properties["width"] = static_cast<double>(bounds.m_Width);
        node->m_Properties["height"] = static_cast<double>(bounds.m_Height);
        node->m_Properties["widthPolicy"] = std::string("Fixed");
        node->m_Properties["heightPolicy"] = std::string("Fixed");
    }
    else
    {
        const auto origin = PositionOf(*parent, id);
        Position(*parent, id, std::clamp(origin[0] + delta.m_X, -1e6, 1e6),
                 std::clamp(origin[1] + delta.m_Y, -1e6, 1e6));
        bounds.m_X += delta.m_X;
        bounds.m_Y += delta.m_Y;
    }
    widget->SetFloatingBounds(bounds);
    m_Surface->SetSelected(id);
    if (finished)
    {
        widget->ClearFloatingBounds();
        m_Gesture.reset();
        m_Document->Set(std::move(candidate));
        m_PreviewDirty = true;
    }
}

void CreatorWindow::ApplyConstraints()
{
    m_Constraints->CommitEdit();
    auto candidate = m_Document->Get();
    auto* owner = candidate.Find(m_ConstraintOwner);
    if (!owner || !m_Registry.Find(owner->m_Type).m_Content)
    {
        throw std::invalid_argument("Select a container or its child to author constraints.");
    }
    owner->m_Layout = "Constraints";
    owner->m_Constraints.clear();
    for (int row = 0; row < m_Constraints->GetRowCount(); ++row)
    {
        UiConstraint rule;
        rule.m_Item = m_Constraints->GetCell(row, 0);
        rule.m_Anchor = m_Constraints->GetCell(row, 1);
        const auto relation = m_Constraints->GetCell(row, 2);
        if (relation != "Equal" && relation != "LessEqual" && relation != "GreaterEqual")
        {
            throw std::invalid_argument("Unknown constraint relation.");
        }
        rule.m_Relation = relation == "Equal"       ? ConstraintRelation::Equal
                          : relation == "LessEqual" ? ConstraintRelation::LessEqual
                                                    : ConstraintRelation::GreaterEqual;
        rule.m_Target = m_Constraints->GetCell(row, 3);
        rule.m_TargetAnchor = m_Constraints->GetCell(row, 4);
        rule.m_Multiplier = Number(m_Constraints->GetCell(row, 5));
        rule.m_Offset = Number(m_Constraints->GetCell(row, 6));
        const auto strength = m_Constraints->GetCell(row, 7);
        if (strength != "Required" && strength != "Strong" && strength != "Medium" && strength != "Weak")
        {
            throw std::invalid_argument("Unknown constraint strength.");
        }
        rule.m_Strength = strength == "Required" ? ConstraintStrength::Required
                          : strength == "Strong" ? ConstraintStrength::Strong
                          : strength == "Medium" ? ConstraintStrength::Medium
                                                 : ConstraintStrength::Weak;
        rule.m_Name = m_Constraints->GetCell(row, 8);
        owner->m_Constraints.push_back(std::move(rule));
    }
    m_Document->Set(std::move(candidate));
    Status("Constraint rules applied / solver diagnostics remain available in the runtime layout report");
}

void CreatorWindow::Open(bool discardConfirmed)
{
    if (IsDirty() && !discardConfirmed)
    {
        auto& question =
            MessageBox::Create(*m_Shell, "Open another design?",
                               "The current design has unsaved changes. Discard them and open another design?");
        question.SetKind(MessageKind::Warning);
        question.SetButtons({StandardButton::Discard, StandardButton::Cancel});
        const auto snapshot = m_Document->GetSnapshot();
        const auto generation = ++m_FileGeneration;
        const std::weak_ptr<void> lifetime = m_Lifetime;
        question.Open().OnFinished(
            Alive(lifetime,
                  [this, snapshot, generation](PopupResult result)
                  {
                      if (generation == m_FileGeneration && result.m_Value == static_cast<int>(StandardButton::Discard))
                      {
                          if (m_Document->GetSnapshot() == snapshot)
                          {
                              Open(true);
                          }
                          else
                          {
                              Status("The design changed while confirming; newer edits were preserved.");
                          }
                      }
                  }));
        return;
    }
    m_Tasks.erase(std::remove_if(m_Tasks.begin(), m_Tasks.end(), [](const Task& task) { return task.IsReady(); }),
                  m_Tasks.end());
    if (!m_Tasks.empty())
    {
        Status("Wait for a pending file operation to finish.");
        return;
    }
    FileDialogOptions options;
    options.m_Title = "Open EStudioUI design";
    options.m_Filters = {{"EStudioUI JSON", "*.json"}};
    const auto revision = m_Document->GetSnapshot();
    const auto generation = ++m_FileGeneration;
    const std::weak_ptr<void> lifetime = m_Lifetime;
    FileDialog::Open(*m_Shell, std::move(options))
        .OnFinished(
            Alive(lifetime,
                  [this, revision, generation, lifetime](FileDialogResult result)
                  {
                      if (generation != m_FileGeneration)
                      {
                          return;
                      }
                      if (result.m_Status != FileDialogStatus::Accepted || result.m_Paths.empty())
                      {
                          Status(result.m_Message.empty() ? "Open cancelled or unavailable on this platform"
                                                          : result.m_Message);
                          return;
                      }
                      const auto path = result.m_Paths.front();
                      UiTaskOptions<MainWindow> taskOptions;
                      taskOptions.m_Error = Alive(lifetime,
                                                  [this](MainWindow&, std::exception_ptr error)
                                                  {
                                                      try
                                                      {
                                                          std::rethrow_exception(error);
                                                      }
                                                      catch (const std::exception& failure)
                                                      {
                                                          Status(failure.what());
                                                      }
                                                  });
                      m_Tasks.push_back(m_Application.Tasks().Run(
                          m_Window.Ui().GetHandle(*m_Shell), [path] { return ReadFile(path); },
                          Alive(lifetime,
                                [this, revision, generation, path](MainWindow&, std::string bytes)
                                {
                                    if (generation != m_FileGeneration || m_Document->GetSnapshot() != revision)
                                    {
                                        Status("The document changed while opening; the newer edits were preserved.");
                                        return;
                                    }
                                    try
                                    {
                                        LoadDocument(UiDocument::FromJson(bytes, m_Registry));
                                        m_Path = path;
                                        Status("UI design loaded");
                                    }
                                    catch (const std::exception& error)
                                    {
                                        Status(error.what());
                                    }
                                }),
                          std::move(taskOptions)));
                  }));
}

void CreatorWindow::Save(bool cpp)
{
    m_Tasks.erase(std::remove_if(m_Tasks.begin(), m_Tasks.end(), [](const Task& task) { return task.IsReady(); }),
                  m_Tasks.end());
    if (!m_Tasks.empty())
    {
        Status("Wait for a pending file operation to finish.");
        return;
    }
    const auto snapshot = m_Document->GetSnapshot();
    const auto text = cpp ? snapshot->ToCpp(m_Registry) : snapshot->ToJson(m_Registry);
    FileDialogOptions options;
    options.m_Title = cpp ? "Export editable C++" : "Save EStudioUI JSON";
    options.m_DefaultExtension = cpp ? "cpp" : "json";
    options.m_FileName =
        cpp ? "GeneratedUi.cpp" : (m_Path.empty() ? std::filesystem::path("dashboard.json") : m_Path.filename());
    options.m_Filters = {{cpp ? "C++ source" : "EStudioUI JSON", cpp ? "*.cpp" : "*.json"}};
    const auto generation = ++m_FileGeneration;
    const std::weak_ptr<void> lifetime = m_Lifetime;
    FileDialog::Save(*m_Shell, std::move(options))
        .OnFinished(
            Alive(lifetime,
                  [this, snapshot, text, cpp, generation, lifetime](FileDialogResult result)
                  {
                      if (generation != m_FileGeneration)
                      {
                          return;
                      }
                      if (result.m_Status != FileDialogStatus::Accepted || result.m_Paths.empty())
                      {
                          Status(result.m_Message.empty() ? "Save cancelled or unavailable on this platform"
                                                          : result.m_Message);
                          return;
                      }
                      const auto path = result.m_Paths.front();
                      UiTaskOptions<MainWindow> taskOptions;
                      taskOptions.m_Error = Alive(lifetime,
                                                  [this](MainWindow&, std::exception_ptr error)
                                                  {
                                                      try
                                                      {
                                                          std::rethrow_exception(error);
                                                      }
                                                      catch (const std::exception& failure)
                                                      {
                                                          Status(failure.what());
                                                      }
                                                  });
                      m_Tasks.push_back(m_Application.Tasks().Run(
                          m_Window.Ui().GetHandle(*m_Shell),
                          [path, text]
                          {
                              WriteFile(path, text);
                              return path;
                          },
                          Alive(lifetime,
                                [this, snapshot, cpp, generation](MainWindow&, const std::filesystem::path& saved)
                                {
                                    if (generation != m_FileGeneration)
                                    {
                                        return;
                                    }
                                    if (!cpp)
                                    {
                                        m_Saved = snapshot;
                                        m_Path = saved;
                                    }
                                    Status(cpp ? "Editable C++ exported / compile BuildUi in your project"
                                               : (IsDirty() ? "Snapshot saved / newer edits remain modified"
                                                            : "UI JSON saved"));
                                }),
                          std::move(taskOptions)));
                  }));
}

void CreatorWindow::Status(std::string message)
{
    if (m_Shell)
    {
        m_Shell->Status().SetMessage(std::move(message));
    }
}

DesignerSurface& CreatorWindow::Surface() const
{
    return *m_Surface;
}

PropertyGrid& CreatorWindow::Inspector() const
{
    return *m_Inspector;
}

UiDocument CreatorWindow::ExampleDocument()
{
    UiDocument document;
    auto& root = document.m_Root;
    root.m_Layout = "Constraints";
    root.m_Properties = {{"widthPolicy", std::string("Fill")}, {"heightPolicy", std::string("Fill")}};
    const auto add = [&](std::string id, std::string type, double x, double y, double width, double height) -> UiNode&
    {
        UiNode node;
        node.m_Id = id;
        node.m_Type = std::move(type);
        node.m_Properties = {{"width", width}, {"height", height}};
        root.m_Children.push_back(std::move(node));
        Position(root, id, x, y);
        return root.m_Children.back();
    };
    auto& title = add("headline", "Label", 24, 20, 500, 40);
    title.m_Properties["text"] = std::string("Build your interface. Keep your code.");
    title.m_Properties["style.font-size"] = 24.0;
    auto& subtitle = add("subtitle", "Label", 24, 65, 540, 30);
    subtitle.m_Properties["text"] = std::string("A live EStudioUI dashboard / select a widget to inspect it");
    auto& settings = add("settings", "GroupBox", 24, 112, 280, 292);
    settings.m_Properties["title"] = std::string("Acquisition");
    settings.m_Children = {{"channel",
                            "TextBox",
                            {{"placeholder", std::string("Channel name")}, {"text", std::string("Temperature / A")}}},
                           {"gain", "NumericInput", {{"value", 24.5}}},
                           {"level", "Slider", {{"value", 62.0}}},
                           {"enabled", "CheckBox", {{"text", std::string("Enable acquisition")}, {"checked", true}}},
                           {"submit", "Button", {{"text", std::string("Apply configuration")}, {"height", 42.0}}}};
    auto& appearance = add("appearance", "GroupBox", 324, 112, 280, 292);
    appearance.m_Properties["title"] = std::string("Appearance / real controls");
    appearance.m_Children = {{"accent", "ColorEditor", {{"color", Color{0.25f, 0.7f, 1, 1}}}},
                             {"position-vector", "VectorEditor", {{"value", VectorValue{12, 24, 0}}}},
                             {"distance", "UnitEditor", {{"value", 25.4}}},
                             {"progress", "ProgressBar", {{"value", 68.0}, {"height", 20.0}}}};
    auto& note = add("export-note", "Label", 24, 430, 570, 70);
    note.m_Properties["text"] = std::string("JSON for runtime dashboards. Direct C++ for your own application.");
    document.m_StyleSheet =
        "#design-root { background: #111827; padding: 12px; }\n"
        "#design-root groupbox { background: #1b2537; border: 1px solid #344661; border-radius: 10px; padding: 12px; "
        "gap: 10px; }\n"
        "#submit { background: linear-gradient(120deg, #1477c9, #5e55dd); color: white; border-radius: 8px; }\n"
        "#submit:hover { background: #347cce; }\n";
    return document;
}
} // namespace EStudioUI
