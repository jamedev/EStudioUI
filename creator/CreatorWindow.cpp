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
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Keep the Win32 convenience macro out of the framework class namespace.
#ifdef MessageBox
#undef MessageBox
#endif
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
                       { return rule.m_Item == id && rule.m_Anchor != "Width" && rule.m_Anchor != "Height"; }),
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

void WriteCppPair(const std::filesystem::path& path, const UiCppClass& source)
{
    auto header = path;
    header.replace_extension(".h");
    if (header == path)
    {
        throw std::invalid_argument("Select a C++ source path; its companion header is written beside it.");
    }
    const auto token = ".estudioui-" + std::to_string(UiDispatcher::AllocateTargetKey());

    struct Entry
    {
        std::filesystem::path m_Target, m_Stage, m_Backup;
        bool m_Existed = false, m_Installed = false;
    };

    std::array<Entry, 2> files;
    files[0].m_Target = header;
    files[1].m_Target = path;
    const auto install = [](const std::filesystem::path& from, const std::filesystem::path& to)
    {
#ifdef _WIN32
        if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace a generated source file.");
#else
        std::filesystem::rename(from, to);
#endif
    };
    const auto clean = [&]
    {
        std::error_code ignored;
        for (const auto& file : files)
        {
            std::filesystem::remove(file.m_Stage, ignored);
            std::filesystem::remove(file.m_Backup, ignored);
        }
    };
    try
    {
        for (auto& file : files)
        {
            file.m_Stage = file.m_Target;
            file.m_Stage += token + ".new";
            file.m_Backup = file.m_Target;
            file.m_Backup += token + ".bak";
            file.m_Existed = std::filesystem::exists(file.m_Target);
            if (file.m_Existed)
            {
                std::filesystem::copy_file(file.m_Target, file.m_Backup);
            }
        }
        WriteFile(files[0].m_Stage, source.m_Header);
        WriteFile(files[1].m_Stage, source.m_Source);
        for (auto& file : files)
        {
            install(file.m_Stage, file.m_Target);
            file.m_Installed = true;
        }
    }
    catch (...)
    {
        const auto failure = std::current_exception();
        bool restored = true;
        for (auto& file : files)
        {
            if (file.m_Installed)
            {
                try
                {
                    if (file.m_Existed)
                    {
                        install(file.m_Backup, file.m_Target);
                    }
                    else
                    {
                        std::filesystem::remove(file.m_Target);
                    }
                }
                catch (...)
                {
                    restored = false;
                }
            }
        }
        if (restored)
        {
            clean();
        }
        else
        {
            throw std::runtime_error(
                "Source export failed and rollback was incomplete; retained .bak files contain the previous source.");
        }
        std::rethrow_exception(failure);
    }
    clean();
}

} // namespace

CreatorWindow::CreatorWindow(Application& application, Window& window, std::string baseCss)
    : m_Application(application), m_Window(window), m_BaseCss(std::move(baseCss))
{
    m_Theme = &window.Themes();
    m_Theme->Configure(Theme{}, m_BaseCss);
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
    if (m_AppearanceGrid)
    {
        m_AppearanceGrid->SetModel({});
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
    auto& marker = m_Shell->TitleBar().Content().Add<Label>("Layout designer");
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
    command("Export C++ class...", Key::Unknown, [this] { Save(true); }, file);
    command("Export selected component...", Key::Unknown, [this] { Save(true, true); }, file);
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
    m_Palette = &palette.Add<WidgetPalette>();
    m_Palette->SetId("creator-widget-palette");
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
    auto& componentName = palette.Add<TextBox>("CompositePanel");
    componentName.SetId("component-name");
    componentName.SetPlaceholder("Reusable component class name");
    const WidgetRef componentNameRef(&componentName);
    palette.Add<Button>("Add composition to palette")
        .Clicked()
        .Connect(Alive(lifetime,
                       [this, componentNameRef]
                       {
                           try
                           {
                               if (auto* input = dynamic_cast<TextBox*>(componentNameRef.Get()))
                               {
                                   RegisterComponent(input->GetText());
                               }
                           }
                           catch (const std::exception& error)
                           {
                               Status(error.what());
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
    m_Surface->GestureCancelled().Connect(Alive(lifetime, [this] { CancelGeometry(); }));
    m_Palette->Dragged().Connect(Alive(lifetime,
                                       [this](const std::string& type, Point point, bool finished)
                                       {
                                           const auto container =
                                               type.empty() ? std::string{} : m_Surface->ContainerAt(point);
                                           m_Surface->SetDropTarget(finished ? std::string{} : container);
                                           if (finished && !type.empty() && !container.empty())
                                           {
                                               const auto local = m_Surface->ContainerPoint(container, point);
                                               const std::weak_ptr<void> receiver = m_Lifetime;
                                               m_Window.Ui().Defer(
                                                   [this, receiver, type, container, local](UiContext&)
                                                   {
                                                       if (receiver.expired())
                                                       {
                                                           return;
                                                       }
                                                       try
                                                       {
                                                           AddWidget(type, container, local);
                                                       }
                                                       catch (const std::exception& error)
                                                       {
                                                           Status(error.what());
                                                       }
                                                   });
                                           }
                                           else if (finished && !type.empty())
                                           {
                                               Status("Drop onto a visible container in Design mode.");
                                           }
                                       }));
    auto& snapping = toolbar.AddButton("Snap guides");
    snapping.SetCheckable(true);
    snapping.SetChecked(true);
    snapping.Toggled().Connect(Alive(lifetime, [this](bool checked) { m_Snapping.m_Enabled = checked; }));
    auto& grid = toolbar.AddButton("8 px grid");
    grid.SetCheckable(true);
    grid.Toggled().Connect(Alive(lifetime, [this](bool checked) { m_Snapping.m_Grid = checked; }));
    auto& keep = toolbar.AddButton("Keep alignment constraints");
    keep.SetCheckable(true);
    keep.Toggled().Connect(Alive(lifetime, [this](bool checked) { m_Snapping.m_KeepRelations = checked; }));
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
    BuildAppearance();
    m_Shell->Docks().SetDockVisible(paletteDock, true);
    if (auto* inspector = m_Shell->Docks().FindDock("creator-properties"))
    {
        m_Shell->Docks().SetDockVisible(*inspector, true);
    }
    m_Shell->Status().SetMessage("Design mode / add widgets, edit properties, export JSON or C++");
}

void CreatorWindow::BuildAppearance()
{
    auto& dock = m_Shell->Docks().AddDock("creator-appearance", "Appearance", DockArea::Right);
    auto* inspector = m_Shell->Docks().FindDock("creator-properties");
    if (inspector)
    {
        m_Shell->Docks().Tabify(dock, *inspector);
    }
    const auto palette = m_Theme->GetTheme().Resolve(m_Theme->GetSystemAppearance());
    std::vector<PropertyDefinition> definitions;
    PropertyDefinition family{"family", "Style preset", "Appearance", PropertyKind::Choice, std::string("Minimal")};
    family.m_Choices = {"Minimal", "Engineering", "Office", "Studio", "Game", "System"};
    definitions.push_back(std::move(family));
    PropertyDefinition mode{"mode", "Color mode", "Appearance", PropertyKind::Choice, std::string("System")};
    mode.m_Choices = {"System", "Light", "Dark"};
    definitions.push_back(std::move(mode));
    PropertyDefinition font{"font", "UI font size", "Typography", PropertyKind::Number, 14.0};
    font.m_Minimum = 9;
    font.m_Maximum = 32;
    font.m_Decimals = 0;
    definitions.push_back(std::move(font));
    PropertyDefinition density{"density", "Control density", "Typography", PropertyKind::Number, 1.0};
    density.m_Minimum = .75;
    density.m_Maximum = 1.5;
    density.m_Step = .05;
    definitions.push_back(std::move(density));
    definitions.emplace_back("useAccent", "Custom accent", "Colors", PropertyKind::Boolean, false);
    definitions.emplace_back("accent", "Accent", "Colors", PropertyKind::Color, palette.m_Accent);
    definitions.emplace_back("useBackground", "Custom background", "Colors", PropertyKind::Boolean, false);
    definitions.emplace_back("background", "Background", "Colors", PropertyKind::Color, palette.m_Background);
    definitions.emplace_back("useForeground", "Custom foreground", "Colors", PropertyKind::Boolean, false);
    definitions.emplace_back("foreground", "Foreground", "Colors", PropertyKind::Color, palette.m_Foreground);
    definitions.emplace_back("useRadius", "Custom rounding", "Geometry", PropertyKind::Boolean, false);
    PropertyDefinition radius{"radius", "Corner radius", "Geometry", PropertyKind::Number, 6.0};
    radius.m_Minimum = 0;
    radius.m_Maximum = 24;
    definitions.push_back(std::move(radius));
    PropertyDefinition motion{"motion", "Reduced motion", "Accessibility", PropertyKind::Choice, std::string("System")};
    motion.m_Choices = {"System", "Reduce", "Allow"};
    definitions.push_back(std::move(motion));
    m_AppearanceModel = std::make_shared<PropertyModel>(m_Window.Ui().States(), std::move(definitions));
    m_AppearanceGrid = &dock.Content().Add<PropertyGrid>();
    m_AppearanceGrid->SetId("creator-appearance-grid");
    m_AppearanceGrid->SetHeight(SizeRule::Fill());
    m_AppearanceGrid->SetModel(m_AppearanceModel);
    const auto candidate = [this](const std::vector<PropertyValue>& values)
    {
        if (values.size() != 13)
        {
            throw std::invalid_argument("Appearance schema changed during editing.");
        }
        Theme theme;
        const auto& familyName = std::get<std::string>(values[0]);
        for (int i = 0; i < 6; ++i)
        {
            if (Theme::Name(static_cast<ThemeFamily>(i)) == familyName)
            {
                theme.m_Family = static_cast<ThemeFamily>(i);
            }
        }
        const auto& modeName = std::get<std::string>(values[1]);
        theme.m_Mode = modeName == "Dark"    ? ThemeMode::Dark
                       : modeName == "Light" ? ThemeMode::Light
                                             : ThemeMode::System;
        theme.m_FontSize = static_cast<float>(std::get<double>(values[2]));
        theme.m_Density = static_cast<float>(std::get<double>(values[3]));
        if (std::get<bool>(values[4]))
        {
            theme.m_Accent = std::get<Color>(values[5]);
        }
        if (std::get<bool>(values[6]))
        {
            theme.m_Background = std::get<Color>(values[7]);
        }
        if (std::get<bool>(values[8]))
        {
            theme.m_Foreground = std::get<Color>(values[9]);
        }
        if (std::get<bool>(values[10]))
        {
            theme.m_Radius = static_cast<float>(std::get<double>(values[11]));
        }
        const auto& reduce = std::get<std::string>(values[12]);
        if (reduce != "System")
        {
            theme.m_ReducedMotion = reduce == "Reduce";
        }
        (void)theme.Resolve(m_Theme->GetSystemAppearance());
        return theme;
    };
    const std::weak_ptr<void> lifetime = m_Lifetime;
    m_Connections.emplace_back(m_AppearanceModel->Values().AddValidator(
        Alive(lifetime, [candidate](const std::vector<PropertyValue>& values) { (void)candidate(values); })));
    m_Connections.emplace_back(m_AppearanceModel->Values().Subscribe(
        Alive(lifetime,
              [this, candidate](const StateChange<std::vector<PropertyValue>>& change)
              {
                  m_PendingTheme = candidate(change.GetAfter());
                  m_Window.RequestRepaint();
              })));
    m_AppearanceGrid->EditRejected().Connect(Alive(lifetime, [this](const std::string& message) { Status(message); }));
    dock.Content()
        .Add<Label>("System follows available OS colors and contrast preferences. Custom color switches retain "
                    "independent overrides.")
        .SetWordWrap(true);
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
    for (const auto& [name, component] : m_Components)
    {
        (void)component;
        if (query.empty() || name.find(query) != std::string::npos)
        {
            items.push_back(name);
            m_PaletteIds.push_back(name);
        }
    }
    m_Palette->SetEntries(std::move(items));
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

std::string CreatorWindow::AddWidget(std::string type, std::string parent, std::optional<Point> position)
{
    const auto component = m_Components.find(type);
    const auto& descriptor = m_Registry.Find(component == m_Components.end() ? type : component->second.m_Root.m_Type);
    auto candidate = m_Document->Get();
    if (parent.empty())
    {
        auto* selected = candidate.Find(m_Selected);
        parent = selected && m_Registry.Find(selected->m_Type).m_Content ? m_Selected : candidate.m_Root.m_Id;
    }
    auto* container = candidate.Find(parent);
    if (!container || !m_Registry.Find(container->m_Type).m_Content)
    {
        throw std::invalid_argument("Choose a registered content slot before adding children.");
    }
    UiNode node;
    node.m_Type = type;
    std::string id;
    for (std::size_t suffix = 1;; ++suffix)
    {
        id = type + std::to_string(suffix);
        if (!candidate.Find(id))
        {
            break;
        }
    }
    if (component != m_Components.end())
    {
        node = component->second.m_Root;
        std::map<std::string, std::string> ids;
        std::size_t serial = 0;
        const auto rename = [&](const auto& self, UiNode& item) -> void
        {
            const auto fresh = serial++ == 0 ? id : id + "_n" + std::to_string(serial);
            ids[item.m_Id] = fresh;
            item.m_Id = fresh;
            for (auto& child : item.m_Children)
            {
                self(self, child);
            }
        };
        rename(rename, node);
        const auto rules = [&](const auto& self, UiNode& item) -> void
        {
            for (auto& rule : item.m_Constraints)
            {
                rule.m_Item = ids.at(rule.m_Item);
                if (rule.m_Target != "parent")
                {
                    rule.m_Target = ids.at(rule.m_Target);
                }
            }
            for (auto& child : item.m_Children)
            {
                self(self, child);
            }
        };
        rules(rules, node);
        auto css = component->second.m_StyleSheet;
        int depth = 0;
        bool quoted = false, comment = false;
        char quote = 0;
        for (std::size_t i = 0; i < css.size(); ++i)
        {
            if (comment)
            {
                if (i && css[i - 1] == '*' && css[i] == '/')
                {
                    comment = false;
                }
                continue;
            }
            if (quoted)
            {
                if (css[i] == '\\')
                {
                    ++i;
                }
                else if (css[i] == quote)
                {
                    quoted = false;
                }
                continue;
            }
            if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*')
            {
                comment = true;
                ++i;
                continue;
            }
            if (css[i] == '\'' || css[i] == '"')
            {
                quoted = true;
                quote = css[i];
                continue;
            }
            if (css[i] == '{')
            {
                ++depth;
                continue;
            }
            if (css[i] == '}')
            {
                --depth;
                continue;
            }
            if (depth == 0 && css[i] == '#')
            {
                const auto start = i + 1;
                auto end = start;
                while (end < css.size() &&
                       ((css[end] >= 'a' && css[end] <= 'z') || (css[end] >= 'A' && css[end] <= 'Z') ||
                        (css[end] >= '0' && css[end] <= '9') || css[end] == '_' || css[end] == '-'))
                {
                    ++end;
                }
                const auto found = ids.find(css.substr(start, end - start));
                if (found != ids.end())
                {
                    css.replace(start, end - start, found->second);
                    i = start + found->second.size() - 1;
                }
                else
                {
                    i = end ? end - 1 : i;
                }
            }
        }
        candidate.m_StyleSheet += '\n' + css;
    }
    else
    {
        node.m_Id = id;
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
    }
    std::size_t insertion = container->m_Children.size();
    if (position && container->m_Layout != "Constraints")
    {
        const auto owner = m_Built.m_Widgets.find(parent);
        if (owner != m_Built.m_Widgets.end() && owner->second.Get())
        {
            const auto area = m_Registry.Find(container->m_Type).m_Content(*owner->second.Get()).GetContentBounds();
            for (std::size_t i = 0; i < container->m_Children.size(); ++i)
            {
                const auto sibling = m_Built.m_Widgets.find(container->m_Children[i].m_Id);
                if (sibling == m_Built.m_Widgets.end() || !sibling->second.Get())
                {
                    continue;
                }
                const auto bounds = sibling->second.Get()->GetBounds();
                const bool before = container->m_Layout == "Row"
                                        ? position->m_X + area.m_X < bounds.m_X + bounds.m_Width / 2
                                        : position->m_Y + area.m_Y < bounds.m_Y + bounds.m_Height / 2;
                if (before)
                {
                    insertion = i;
                    break;
                }
            }
        }
    }
    container->m_Children.insert(container->m_Children.begin() + static_cast<std::ptrdiff_t>(insertion),
                                 std::move(node));
    if (container->m_Layout == "Constraints")
    {
        const float offset = 24 + static_cast<float>(container->m_Children.size() % 8) * 24;
        auto point = position.value_or(Point{offset, offset});
        Position(*container, id, point.m_X, point.m_Y);
    }
    m_Document->Set(std::move(candidate));
    SelectNode(id);
    return id;
}

void CreatorWindow::RegisterComponent(std::string name, std::string id)
{
    const auto* selected = m_Document->Get().Find(id.empty() ? m_Selected : id);
    if (!selected)
    {
        throw std::invalid_argument("Select a composition before adding it to the palette.");
    }
    if (m_Components.size() >= 64)
    {
        throw std::length_error("A session palette supports at most 64 reusable compositions.");
    }
    if (m_Components.contains(name))
    {
        throw std::invalid_argument("That session component name already exists.");
    }
    UiDocument component;
    component.m_Root = *selected;
    component.m_StyleSheet = m_Document->Get().m_StyleSheet;
    (void)component.ToCppClass(m_Registry, name);
    m_Components.emplace(std::move(name), std::move(component));
    FilterPalette({});
    Status("Composition added to the session palette; export its class or save JSON to retain it independently.");
}

void CreatorWindow::SetSnapping(DesignSnapSettings settings)
{
    (void)SnapDesign({}, false, {}, {}, settings);
    m_Snapping = settings;
}

const DesignSnapSettings& CreatorWindow::GetSnapping() const
{
    return m_Snapping;
}

ThemeController& CreatorWindow::Themes() const
{
    return *m_Theme;
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
    if (m_PendingTheme)
    {
        m_Theme->SetTheme(*m_PendingTheme);
        m_PendingTheme.reset();
    }
    if (m_Gesture && m_Document->GetSnapshot() != m_Gesture)
    {
        CancelGeometry();
        m_AbortGesture = true;
    }
    if (!m_PreviewDirty)
    {
        return;
    }
    const auto document = m_Document->GetSnapshot();
    const bool cssChanged = !m_Previewed || document->m_StyleSheet != m_AppliedCss;
    if (cssChanged)
    {
        m_Theme->SetOverrideCss(document->m_StyleSheet);
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
    std::unordered_map<std::string, WidgetRef> containers;
    for (const auto& id : order)
    {
        const auto* node = document->Find(id);
        const auto& descriptor = m_Registry.Find(node->m_Type);
        if (descriptor.m_Content)
        {
            containers.emplace(id, WidgetRef(&descriptor.m_Content(*m_Built.m_Widgets.at(id).Get())));
        }
    }
    m_Surface->SetWidgets(m_Built, std::move(order), std::move(containers));
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

void CreatorWindow::CancelGeometry()
{
    if (auto* target = m_GestureTarget.Get())
    {
        target->ClearFloatingBounds();
    }
    m_Gesture.reset();
    m_GestureTarget = {};
    m_GestureReferences.clear();
    if (m_Surface)
    {
        m_Surface->SetGuides({});
    }
}

void CreatorWindow::EditGeometry(std::string id, Point delta, bool resize, bool finished)
{
    if (m_AbortGesture)
    {
        if (finished)
        {
            m_AbortGesture = false;
        }
        return;
    }
    const auto found = m_Built.m_Widgets.find(id);
    auto* widget = found == m_Built.m_Widgets.end() ? nullptr : found->second.Get();
    if (!widget)
    {
        return;
    }
    auto copy = m_Document->Get();
    auto* owner = ParentOf(copy.m_Root, id);
    if (!owner || (!resize && owner->m_Layout != "Constraints"))
    {
        Status("Movement follows layout ordering; choose Constraints for free placement and alignment guides.");
        return;
    }
    if (!m_Gesture)
    {
        m_Gesture = m_Document->GetSnapshot();
        m_GestureBounds = widget->GetBounds();
        m_GestureTarget = WidgetRef(widget);
        auto* parentWidget = m_Built.m_Widgets.at(owner->m_Id).Get();
        m_GestureParent = m_Registry.Find(owner->m_Type).m_Content(*parentWidget).GetContentBounds();
        m_GestureReferences.clear();
        for (const auto& sibling : owner->m_Children)
        {
            if (sibling.m_Id == id)
            {
                continue;
            }
            auto* target = m_Built.m_Widgets.at(sibling.m_Id).Get();
            if (target && target->IsVisible())
            {
                m_GestureReferences.push_back({sibling.m_Id, target->GetBounds()});
            }
        }
    }
    auto candidate = *m_Gesture;
    auto* node = candidate.Find(id);
    auto* parent = ParentOf(candidate.m_Root, id);
    auto bounds = m_GestureBounds;
    if (resize)
    {
        bounds.m_Width = std::clamp(bounds.m_Width + delta.m_X, 24.0f, 10000.0f);
        bounds.m_Height = std::clamp(bounds.m_Height + delta.m_Y, 24.0f, 10000.0f);
    }
    else
    {
        bounds.m_X += delta.m_X;
        bounds.m_Y += delta.m_Y;
    }
    const auto snapped = parent->m_Layout == "Constraints"
                             ? SnapDesign(bounds, resize, m_GestureParent, m_GestureReferences, m_Snapping)
                             : DesignSnapResult{bounds, {}};
    bounds = snapped.m_Bounds;
    if (resize)
    {
        node->m_Properties["width"] = static_cast<double>(bounds.m_Width);
        node->m_Properties["height"] = static_cast<double>(bounds.m_Height);
        node->m_Properties["widthPolicy"] = std::string("Fixed");
        node->m_Properties["heightPolicy"] = std::string("Fixed");
    }
    if (parent->m_Layout == "Constraints")
    {
        Position(*parent, id, bounds.m_X - m_GestureParent.m_X, bounds.m_Y - m_GestureParent.m_Y);
        if (m_Snapping.m_KeepRelations)
        {
            for (const auto& guide : snapped.m_Guides)
            {
                if (guide.m_Rule.m_Target.empty())
                {
                    continue;
                }
                std::erase_if(parent->m_Constraints,
                              [&](const UiConstraint& rule)
                              {
                                  return rule.m_Item == id &&
                                         (guide.m_X ? rule.m_Anchor == "Left" || rule.m_Anchor == "Right" ||
                                                          rule.m_Anchor == "CenterX"
                                                    : rule.m_Anchor == "Top" || rule.m_Anchor == "Bottom" ||
                                                          rule.m_Anchor == "CenterY");
                              });
                auto rule = guide.m_Rule;
                rule.m_Item = id;
                rule.m_Name = id + (guide.m_X ? ".snap-x" : ".snap-y");
                parent->m_Constraints.push_back(std::move(rule));
            }
        }
    }
    widget->SetFloatingBounds(bounds);
    m_Surface->SetGuides(snapped.m_Guides);
    if (finished)
    {
        CancelGeometry();
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

void CreatorWindow::Save(bool cpp, bool selected)
{
    m_Tasks.erase(std::remove_if(m_Tasks.begin(), m_Tasks.end(), [](const Task& task) { return task.IsReady(); }),
                  m_Tasks.end());
    if (!m_Tasks.empty())
    {
        Status("Wait for a pending file operation to finish.");
        return;
    }
    const auto snapshot = m_Document->GetSnapshot();
    auto design = *snapshot;
    if (selected)
    {
        const auto* node = design.Find(m_Selected);
        if (!node)
        {
            Status("Select a component before exporting.");
            return;
        }
        auto root = *node;
        design.m_Root = std::move(root);
    }
    const auto json = cpp ? std::string{} : design.ToJson(m_Registry);
    FileDialogOptions options;
    options.m_Title = cpp ? "Export composed C++ widget (.h and .cpp)" : "Save EStudioUI JSON";
    options.m_DefaultExtension = cpp ? "cpp" : "json";
    options.m_FileName =
        cpp ? "GeneratedWidget.cpp" : (m_Path.empty() ? std::filesystem::path("dashboard.json") : m_Path.filename());
    options.m_Filters = {{cpp ? "C++ widget source pair" : "EStudioUI JSON", cpp ? "*.cpp" : "*.json"}};
    const auto generation = ++m_FileGeneration;
    const std::weak_ptr<void> lifetime = m_Lifetime;
    FileDialog::Save(*m_Shell, std::move(options))
        .OnFinished(Alive(
            lifetime,
            [this, snapshot, design = std::move(design), json, cpp, generation, lifetime](FileDialogResult result)
            {
                if (generation != m_FileGeneration)
                {
                    return;
                }
                if (result.m_Status != FileDialogStatus::Accepted || result.m_Paths.empty())
                {
                    Status(result.m_Message.empty() ? "Save cancelled or unavailable" : result.m_Message);
                    return;
                }
                const auto path = result.m_Paths.front();
                UiCppClass source;
                try
                {
                    if (cpp)
                    {
                        source = design.ToCppClass(m_Registry, path.stem().string());
                    }
                }
                catch (const std::exception& error)
                {
                    Status(error.what());
                    return;
                }
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
                    [path, json, source = std::move(source), cpp]
                    {
                        if (cpp)
                        {
                            WriteCppPair(path, source);
                        }
                        else
                        {
                            WriteFile(path, json);
                        }
                        return path;
                    },
                    Alive(
                        lifetime,
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
                            Status(
                                cpp ? "Editable .h/.cpp class exported / complete ConnectEvents with application logic"
                                : IsDirty() ? "Snapshot saved / newer edits remain modified"
                                            : "UI JSON saved");
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
    document.m_StyleSheet = "#design-root { padding: 12px; }\n"
                            "#design-root groupbox { padding: 12px; "
                            "gap: 10px; }\n"
                            "#design-root label { font-size: 14px; }\n";
    return document;
}
} // namespace EStudioUI
