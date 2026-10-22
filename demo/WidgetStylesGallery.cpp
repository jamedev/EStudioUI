#include "WidgetStylesGallery.h"

#include <EStudioUI/InputValidator.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <numbers>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
//! Raises a useful failure from the real-window gallery smoke checks.
void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Widget styles: " + message);
}

//! Reads a complete stylesheet for the independent verification entry point.
std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

//! Returns an input point inside the actual laid-out widget.
Point Center(const Widget& widget)
{
    const auto bounds = widget.GetBounds();
    return {bounds.m_X + bounds.m_Width * .5f, bounds.m_Y + bounds.m_Height * .5f};
}

//! Delivers an ordinary pointer click through hit testing and capture.
void Click(UiContext& ui, Point point)
{
    ui.PointerDown(point);
    ui.PointerUp(point);
}

//! Adds one compact line with a reusable CSS role.
Label& Caption(Widget& parent, std::string text, const char* role = "style-note")
{
    auto& label = parent.Add<Label>(std::move(text));
    label.AddClass(role);
    return label;
}

//! Adds a vertical surface card with its own intrinsic heading.
Widget& Card(Widget& parent, std::string title, float weight = 1)
{
    auto& card = parent.Add<Widget>();
    card.AddClass("style-card");
    card.SetWidth(SizeRule::Fill(weight));
    card.SetHeight(SizeRule::Fill());
    Caption(card, std::move(title), "style-eyebrow");
    return card;
}

//! Adds a content row that takes all remaining category height.
Widget& Body(Widget& parent)
{
    auto& body = parent.Add<Widget>();
    body.AddClass("style-body");
    body.SetLayoutDirection(Axis::Horizontal);
    body.SetHeight(SizeRule::Fill());
    body.SetGap(12);
    return body;
}
} // namespace

WidgetStylesGallery::WidgetStylesGallery(Window& window, Widget& page, const std::filesystem::path& assets)
    : m_Window(window), m_Page(page)
{
    (void)assets;
    page.AddClass("widget-styles");
    page.AddClass("aurora");
    page.SetGap(10);
    auto& heading = page.Add<Widget>();
    heading.AddClass("style-heading");
    heading.SetLayoutDirection(Axis::Horizontal);
    heading.SetHeight(SizeRule::Fixed(48));
    heading.SetGap(12);
    auto& title = heading.Add<Widget>();
    title.SetWidth(SizeRule::Fill());
    title.SetGap(1);
    Caption(title, "One toolkit. Three personalities.", "style-title");
    Caption(title, "Live controls, shared CSS, retained GPU drawing.");
    for (int i = 0; i < 3; ++i)
    {
        auto& button = heading.Add<ToolButton>(std::array{"Aurora", "Paper", "Ember"}[i]);
        button.AddClass("style-preset");
        button.SetWidth(SizeRule::Fixed(90));
        button.SetCheckable(true);
        m_Presets[i] = &button;
        m_Connections.push_back(button.Clicked().Connect([this, i] { SetPreset(i); }));
    }
    m_Presets[0]->SetChecked(true);
    m_Tabs = &page.Add<TabWidget>();
    m_Tabs->SetHeight(SizeRule::Fill());
    BuildInputs(m_Tabs->AddTab("Inputs & commands"));
    BuildCollections(m_Tabs->AddTab("Lists, trees & tables"));
    BuildSurfaces(m_Tabs->AddTab("Canvas & custom painting"));
    m_Status = &Caption(page, "Aurora / ready · choose a preset or interact with a control", "style-status");
    m_Connections.push_back(m_Tabs->CurrentChanged().Connect(
        [this](int index)
        {
            if (index == 2)
                m_Surface->RequestFrame();
        }));
}

WidgetStylesGallery::~WidgetStylesGallery()
{
    for (auto& connection : m_Connections)
        connection.Disconnect();
}

void WidgetStylesGallery::BuildInputs(Widget& page)
{
    auto& body = Body(page);
    auto& editors = Card(body, "01 / TEXT & VALIDATION", 1.1f);
    Caption(editors, "Name", "style-field-label");
    m_Name = &editors.Add<TextBox>();
    m_Name->SetPlaceholder("Name your next idea…");
    Caption(editors, "Whole number · 1 to 100", "style-field-label");
    m_Validated = &editors.Add<TextBox>("pending");
    m_Validated->SetValidator(std::make_shared<IntegerValidator>(1, 100));
    Caption(editors, "Replace pending with 42. Try a sign alone.");
    Caption(editors, "Read-only", "style-field-label");
    editors.Add<TextBox>("Drawing_042.estudio").SetReadOnly(true);
    Caption(editors, "Disabled", "style-field-label");
    editors.Add<TextBox>("No active document").SetEnabled(false);
    m_Connections.push_back(
        m_Name->TextChanged().Connect([this](const std::string& value) { SetStatus("Name changed / " + value); }));
    m_Connections.push_back(m_Validated->TextChanged().Connect(
        [this](const std::string&)
        {
            const auto state = m_Validated->GetValidationState();
            SetStatus(state == ValidationState::Acceptable     ? "Validation / accepted integer"
                      : state == ValidationState::Intermediate ? "Validation / editable prefix or out-of-range value"
                                                               : "Validation / invalid existing value");
        }));

    auto& scalars = Card(body, "02 / PRECISE VALUES");
    Caption(scalars, "Stroke intensity", "style-field-label");
    m_Number = &scalars.Add<NumericInput>(62.5);
    m_Number->SetRange(0, 100);
    m_Number->SetStep(.5);
    m_Number->SetDecimals(2);
    m_Slider = &scalars.Add<Slider>();
    m_Slider->SetRange(0, 100);
    m_Slider->SetStep(.5);
    m_Slider->SetValue(62.5);
    Caption(scalars, "Drag, use arrows, or type a precise value.");
    auto& verticalRow = scalars.Add<Widget>();
    verticalRow.SetLayoutDirection(Axis::Horizontal);
    verticalRow.SetHeight(SizeRule::Fill());
    verticalRow.SetGap(16);
    m_Vertical = &verticalRow.Add<Slider>(Axis::Vertical);
    m_Vertical->SetWidth(SizeRule::Fixed(48));
    m_Vertical->SetHeight(SizeRule::Fill());
    m_Vertical->SetRange(0, 100);
    m_Vertical->SetValue(74);
    auto& legend = verticalRow.Add<Widget>();
    legend.SetWidth(SizeRule::Fill());
    legend.SetGap(10);
    Caption(legend, "Vertical gain", "style-field-label");
    Caption(legend, "Styled groove, fill and handle.");
    Caption(legend, "Focus follows the same theme.");
    auto& disabled = legend.Add<Slider>();
    disabled.SetValue(35);
    disabled.SetEnabled(false);
    Caption(legend, "Disabled controls stay legible.");
    m_Connections.push_back(m_Number->ValueChanged().Connect([this](double value) { SetValue(value); }));
    m_Connections.push_back(m_Slider->ValueChanged().Connect([this](double value) { SetValue(value); }));
    m_Connections.push_back(m_Vertical->ValueChanged().Connect(
        [this](double value) { SetStatus("Vertical gain / " + std::to_string(static_cast<int>(value)) + "%"); }));

    auto& actions = Card(body, "03 / COMPOSED COMMANDS");
    Caption(actions, "ToolBar + split tool button", "style-field-label");
    auto& toolbar = actions.Add<ToolBar>();
    auto& save = toolbar.AddButton("Save");
    m_Connections.push_back(save.Clicked().Connect(
        [this]
        {
            ++m_Actions;
            SetStatus("Save command invoked");
        }));
    toolbar.AddSeparator();
    m_Split = &toolbar.AddSplitButton("Line");
    for (const char* tool : {"Line", "Arc", "Spline"})
        m_Split->AddAction(tool);
    m_Connections.push_back(m_Split->Triggered().Connect(
        [this](std::size_t index)
        {
            ++m_Actions;
            SetStatus("Drawing tool / " + std::string(std::array{"Line", "Arc", "Spline"}[index]));
        }));
    m_MenuButton = &actions.Add<ToolButton>("Context actions  …");
    m_MenuButton->AddClass("style-menu-button");
    auto popup = std::make_unique<ContextMenu>();
    popup->AddClass("style-context");
    m_Menu = popup.get();
    for (const char* command : {"Duplicate selection", "Rename selection"})
        m_Connections.push_back(m_Menu->AddAction(command).Clicked().Connect(
            [this, command]
            {
                ++m_Actions;
                SetStatus(command);
            }));
    m_Menu->AddSeparator();
    m_Menu->AddAction("Delete locked item", false);
    m_Window.Ui().AddOwnedPopup(std::move(popup), actions);
    m_Connections.push_back(m_MenuButton->Clicked().Connect(
        [this]
        {
            const auto bounds = m_MenuButton->GetBounds();
            m_Menu->Open({bounds.m_X, bounds.m_Y + bounds.m_Height + 4});
        }));
    Caption(actions, "Motion is optional", "style-field-label");
    m_MotionSample = &actions.Add<Button>("Create something");
    m_MotionSample->AddClass("motion-sample");
    m_Connections.push_back(m_MotionSample->Clicked().Connect(
        [this]
        {
            ++m_Actions;
            SetStatus("Create command / " + std::to_string(m_Actions) + " actions");
        }));
    m_Motion = &actions.Add<ToolButton>("Animate this button");
    m_Motion->SetCheckable(true);
    m_Connections.push_back(m_Motion->Toggled().Connect(
        [this](bool animated)
        {
            if (animated)
                m_MotionSample->AddClass("animating");
            else
                m_MotionSample->RemoveClass("animating");
            SetStatus(animated ? "One visible button is animating / switch tabs to suspend it"
                               : "Animation stopped / idle frames do no GPU work");
        }));
    actions.Add<Button>("Unavailable action").SetEnabled(false);
    Caption(actions, "Hover, press, focus and popup states.");
}

void WidgetStylesGallery::BuildCollections(Widget& page)
{
    auto& body = Body(page);
    auto& navigation = Card(body, "NAVIGATION / VIRTUALIZED", .9f);
    m_Tree = &navigation.Add<TreeWidget>();
    m_Tree->SetHeight(SizeRule::Fill());
    m_TreeRoot = m_Tree->AddItem(0, "Assembly — revision 04");
    for (const char* group : {"Sketches", "Features", "Annotations"})
    {
        const auto branch = m_Tree->AddItem(m_TreeRoot, group);
        for (int i = 1; i <= 8; ++i)
            m_Tree->AddItem(branch, std::string(group) + " " + std::to_string(i));
        if (std::string_view(group) == "Sketches")
            m_Tree->SetExpanded(branch, true);
    }
    m_Tree->SetExpanded(m_TreeRoot, true);
    m_Tree->SetCurrentItem(m_TreeRoot);
    m_Tree->SetScrollOffset({});
    Caption(navigation, "2,000 named materials", "style-field-label");
    m_List = &navigation.Add<ListWidget>();
    m_List->SetHeight(SizeRule::Fill());
    std::vector<std::string> names;
    for (int i = 0; i < 2000; ++i)
        names.push_back(std::string(std::array{"Aluminium", "Titanium", "Carbon fibre", "Steel"}[i % 4]) +
                        " / finish " + std::to_string(i + 1));
    m_List->SetItems(std::move(names));
    m_List->SetCurrentIndex(1);
    m_List->SetScrollOffset({});
    auto& records = Card(body, "10,000 RECORDS / EDITABLE TABLE", 2);
    Caption(records, "Click a header to sort · double-click or F2 to edit · wheel to scroll");
    m_Table = &records.Add<TableWidget>();
    m_Table->SetHeight(SizeRule::Fill());
    m_Table->SetColumns({{"ID", SizeRule::Fixed(72)},
                         {"Part name", SizeRule::Fill(2, 100)},
                         {"Status", SizeRule::Fill(1, 100)},
                         {"Mass / g", SizeRule::Fill(1, 90)}});
    std::vector<std::vector<std::string>> rows;
    rows.reserve(10000);
    for (int i = 0; i < 10000; ++i)
        rows.push_back({std::to_string(1001 + i), "Part_" + std::to_string(i + 1), i % 5 == 0 ? "Review" : "Ready",
                        std::to_string(24 + (i * 17) % 1200)});
    m_Table->SetRows(std::move(rows));
    m_Table->SetCurrentCell(1, 1);
    m_Table->SetScrollOffset({});
    m_Connections.push_back(m_List->CurrentChanged().Connect(
        [this](int row)
        {
            if (row >= 0)
                SetStatus("Material / " + m_List->GetItem(row));
        }));
    m_Connections.push_back(m_Tree->CurrentChanged().Connect(
        [this](TreeWidget::NodeId item)
        {
            if (item)
                SetStatus("Hierarchy / " + m_Tree->GetItemText(item));
        }));
    m_Connections.push_back(m_Table->CellChanged().Connect(
        [this](int row, int column) { SetStatus("Committed cell / " + m_Table->GetCell(row, column)); }));
}

void WidgetStylesGallery::BuildSurfaces(Widget& page)
{
    auto& body = Body(page);
    auto& canvas = Card(body, "CANVAS / INFINITE WORLD", 1.3f);
    Caption(canvas, "Drag to pan · wheel to zoom");
    m_Canvas = &canvas.Add<CanvasWidget>();
    m_Canvas->SetHeight(SizeRule::Fill());
    m_Canvas->SetPanMode(true);
    m_Canvas->SetCamera({0, 0}, 1);
    const auto line = m_Canvas->RegisterItemStyle("path");
    const auto accent = m_Canvas->RegisterItemStyle("path", {"accent"});
    const auto text = m_Canvas->RegisterItemStyle("annotation");
    std::string error;
    Check(m_Canvas->SetItemStyleSheet(R"CSS(
        path { color: #5adac7; border: 2px solid #5adac7; background: transparent; }
        path.accent { color: #b89aff; border-color: #b89aff; }
        annotation { color: #b1c7d7; background: transparent; }
        path:selected { color: #ffc978; border-color: #ffc978; }
    )CSS",
                                      error),
          error);
    auto& scene = m_Canvas->GetScene();
    scene.Add(MakeCanvasPath({}, {{-110, -60}, {60, -60}, {110, -10}, {110, 65}, {-110, 65}}, true), line);
    scene.Add({CanvasPrimitive::Circle, {-65, 0}, {}, 21}, line);
    scene.Add({CanvasPrimitive::Circle, {60, 10}, {}, 29}, line);
    scene.Add(MakeCanvasSpline({}, {{-110, 92}, {-55, -10}, {45, 150}, {110, 92}}), accent);
    scene.Add(MakeCanvasArc({60, 10}, 46, -.5 * std::numbers::pi, .65 * std::numbers::pi), accent);
    scene.Add(MakeCanvasText({-112, -106, 224, 25}, "PLATE / 220 × 125", 15), text);
    scene.Add(MakeCanvasPath({}, {{-110, -75}, {-110, -88}, {110, -88}, {110, -75}}), accent);
    m_Canvas->SceneChanged();
    Caption(canvas, "Shared item palette · cached paths · BVH culling");

    auto& sceneCard = Card(body, "OPENGL / SHARED CONTEXT", 1.15f);
    Caption(sceneCard, "Drag to orbit · wheel to dolly");
    m_Surface = &sceneCard.Add<OpenGLWidget>();
    m_Surface->SetHeight(SizeRule::Fill());
    m_Scene = std::make_shared<DemoScene>();
    const auto sceneState = m_Scene;
    m_Surface->SetCallbacks([sceneState] { sceneState->Initialize(); }, [sceneState](const OpenGLFrame& frame)
                            { sceneState->Render(frame); }, [sceneState] { sceneState->Release(); });
    const WidgetRef surfaceRef(m_Surface);
    m_Connections.push_back(m_Surface->PointerPressed().Connect([sceneState](const PointerEvent& event)
                                                                { sceneState->BeginDrag(event.m_Position); }));
    m_Connections.push_back(m_Surface->PointerDragged().Connect(
        [sceneState, surfaceRef](const PointerEvent& event)
        {
            sceneState->Drag(event.m_Position);
            if (auto* live = dynamic_cast<OpenGLWidget*>(surfaceRef.Get()))
                live->RequestFrame();
        }));
    m_Connections.push_back(m_Surface->Scrolled().Connect(
        [sceneState, surfaceRef](Point delta)
        {
            sceneState->Zoom(delta.m_Y);
            if (auto* live = dynamic_cast<OpenGLWidget*>(surfaceRef.Get()))
                live->RequestFrame();
        }));
    auto& reset = sceneCard.Add<ToolButton>("Reset camera");
    m_Connections.push_back(reset.Clicked().Connect(
        [sceneState, surfaceRef]
        {
            sceneState->ResetCamera();
            if (auto* live = dynamic_cast<OpenGLWidget*>(surfaceRef.Get()))
                live->RequestFrame();
        }));

    auto& custom = Card(body, "PAINTER / DERIVED CONTROL");
    Caption(custom, "Click the dial · arrow keys to adjust");
    m_Dial = &custom.Add<DemoDial>();
    m_Dial->SetHeight(SizeRule::Fill());
    m_Dial->SetValue(62.5);
    Caption(custom, "CSS colors meet custom GPU painting.");
    Caption(custom, "No textures needed for these vectors.");
    m_Connections.push_back(m_Dial->ValueChanged().Connect(
        [this](double value) { SetStatus("Custom painted dial / " + std::to_string(static_cast<int>(value)) + "%"); }));
}

void WidgetStylesGallery::SetPreset(int index)
{
    constexpr std::array names{"aurora", "paper", "ember"};
    if (index < 0 || index >= static_cast<int>(names.size()))
        return;
    if (m_Preset != index)
    {
        m_Page.RemoveClass(names[m_Preset]);
        m_Page.AddClass(names[index]);
        m_Preset = index;
    }
    for (int i = 0; i < 3; ++i)
        m_Presets[i]->SetChecked(i == index);
    SetStatus(std::string(std::array{"Aurora", "Paper", "Ember"}[index]) + " / compiled theme selected");
}

void WidgetStylesGallery::SetValue(double value)
{
    if (m_Updating)
        return;
    m_Updating = true;
    m_Number->SetValue(value);
    m_Slider->SetValue(value);
    m_Updating = false;
    SetStatus("Synchronized numeric input + slider / " + std::to_string(value));
}

void WidgetStylesGallery::SetStatus(std::string message)
{
    if (m_Status)
        m_Status->SetText(std::move(message));
}

void WidgetStylesGallery::Verify(const std::filesystem::path& assets, const std::filesystem::path& output)
{
    auto& ui = m_Window.Ui();
    std::string error;
    Check(ui.SetStyleSheet(Read(assets / "dark.css") + "\n" + Read(assets / "rich-buttons.css") + "\n" +
                               Read(assets / "rich-widgets.css"),
                           error),
          error);
    m_Window.SetSize(1180, 820);
    double clock = ui.GetTime();
    const auto frame = [&](double duration = .35) { m_Window.RenderFrame(clock += duration); };
    m_Tabs->SetCurrentIndex(0);
    SetPreset(0);
    m_Motion->SetChecked(false);
    frame();
    Check(m_Name->GetBounds().m_Width > 160 && m_Vertical->GetBounds().m_Height > 60,
          "input cards receive usable layout space at 1180 x 820");
    Click(ui, Center(*m_Name));
    ui.TextInput("A GPU toolkit");
    Check(m_Name->GetText() == "A GPU toolkit", "placeholder editor accepts native text input");
    Click(ui, Center(*m_Validated));
    m_Validated->SelectAll();
    ui.TextInput("42");
    Check(m_Validated->GetValidationState() == ValidationState::Acceptable, "validator reaches acceptable state");
    m_Validated->SelectAll();
    ui.TextInput("-");
    Check(m_Validated->GetValidationState() == ValidationState::Intermediate, "validator retains editable prefix");
    m_Validated->SelectAll();
    ui.TextInput("42");
    const auto sliderContent = m_Slider->GetContentBounds();
    Click(ui, {sliderContent.m_X + sliderContent.m_Width * .25f, sliderContent.m_Y + sliderContent.m_Height * .5f});
    Check(m_Slider->GetValue() > 10 && m_Slider->GetValue() < 40, "styled slider keeps a full-width usable track");
    ui.KeyEvent(Key::Right, true);
    ui.KeyEvent(Key::Right, false);
    Check(std::abs(m_Slider->GetValue() - m_Number->GetValue()) < .001,
          "pointer and keyboard slider input synchronizes the composed numeric editor");
    const auto actions = m_Actions;
    Click(ui, Center(m_Split->GetPrimaryButton()));
    Check(m_Actions == actions + 1, "composed primary command invokes its default action");
    Click(ui, Center(*m_MenuButton));
    frame();
    Check(m_Menu->IsOpen(), "styled logical-owner context menu opens through pointer input");
    m_Window.Graphics().SavePpm(output / "widget-styles-context-menu.ppm");
    ui.KeyEvent(Key::Enter, true);
    ui.KeyEvent(Key::Enter, false);
    Check(!m_Menu->IsOpen() && m_Actions == actions + 2, "context menu keyboard action closes and dispatches");
    ui.SetFocus(nullptr);
    ui.PointerMove({0, 0});
    for (int preset = 0; preset < 3; ++preset)
    {
        Click(ui, Center(*m_Presets[preset]));
        ui.SetFocus(nullptr);
        ui.PointerMove({0, 0});
        frame();
        frame();
        m_Window.Graphics().SavePpm(
            output / ("widget-styles-inputs-" + std::string(std::array{"aurora", "paper", "ember"}[preset]) + ".ppm"));
    }
    SetPreset(0);
    m_Tabs->SetCurrentIndex(1);
    frame();
    m_List->SetScrollOffset({});
    const auto listView = m_List->GetViewport();
    Click(ui, {listView.m_X + 40, listView.m_Y + 12});
    Check(m_List->GetCurrentIndex() == 0, "styled list row remains selectable");
    ui.SetFocus(m_Tree);
    m_Tree->SetCurrentItem(m_TreeRoot);
    ui.KeyEvent(Key::Left, true);
    ui.KeyEvent(Key::Left, false);
    Check(!m_Tree->IsExpanded(m_TreeRoot), "styled tree branch collapses with keyboard input");
    ui.KeyEvent(Key::Right, true);
    ui.KeyEvent(Key::Right, false);
    Check(m_Tree->IsExpanded(m_TreeRoot), "styled tree branch expands with keyboard input");
    m_Table->SetCurrentCell(0, 1);
    ui.SetFocus(m_Table);
    ui.KeyEvent(Key::F2, true);
    ui.KeyEvent(Key::F2, false);
    ui.TextInput("Edited part");
    ui.KeyEvent(Key::Enter, true);
    ui.KeyEvent(Key::Enter, false);
    Check(m_Table->GetCell(0, 1) == "Edited part", "styled table edit commits through actual input");
    ui.SetFocus(nullptr);
    ui.PointerMove({0, 0});
    for (int preset = 0; preset < 3; ++preset)
    {
        SetPreset(preset);
        frame();
        const auto stats = m_Window.Graphics().GetStats();
        Check(stats.m_SubmittedInstances < 4000, "collection submission stays bounded with 12,000 model rows");
        frame();
        m_Window.Graphics().SavePpm(output / ("widget-styles-collections-" +
                                              std::string(std::array{"aurora", "paper", "ember"}[preset]) + ".ppm"));
    }
    SetPreset(0);
    m_Tabs->SetCurrentIndex(2);
    frame();
    const double zoom = m_Canvas->GetZoom();
    ui.Scroll(Center(*m_Canvas), {0, 1});
    Check(m_Canvas->GetZoom() > zoom, "styled infinite canvas retains wheel zoom");
    Click(ui, Center(*m_Dial));
    ui.KeyEvent(Key::Home, true);
    ui.KeyEvent(Key::Home, false);
    Check(m_Dial->GetValue() == 0, "derived painter control retains keyboard interaction");
    ui.SetFocus(nullptr);
    ui.PointerMove({0, 0});
    frame();
    m_Window.Graphics().SavePpm(output / "widget-styles-surfaces.ppm");
    const auto sceneFrames = m_Scene->GetRenderCount();
    Check(sceneFrames > 0, "separate OpenGL context rendered the actual scene");
    frame();
    Check(m_Scene->GetRenderCount() == sceneFrames, "static 3D surface does not redraw at idle");
    frame();
    Check(m_Window.Graphics().GetStats().m_DrawCalls == 0, "settled styled surfaces submit no GPU work");
    m_Tabs->SetCurrentIndex(0);
    m_Motion->SetChecked(true);
    frame();
    Check(ui.HasAnimations(), "explicit motion option starts one CSS animation");
    m_Tabs->SetCurrentIndex(1);
    ui.SetFocus(nullptr);
    ui.PointerMove({0, 0});
    frame();
    frame();
    Check(!ui.HasAnimations(), "hidden animation example releases animation demand");
    m_Motion->SetChecked(false);
    m_Tabs->SetCurrentIndex(0);
    frame();
    frame();
    Check(!ui.HasAnimations(), "disabling the example releases the animation clock");
    frame();
    Check(m_Window.Graphics().GetStats().m_DrawCalls == 0, "settled controls submit no GPU work");
    std::ofstream report(output / "widget-styles-verification.txt");
    report << "Three compiled presets exercised using actual window input and framebuffer captures.\n"
           << "Validated text, scalar synchronization, split command, context popup, list selection, tree expansion, "
              "table editing, canvas zoom, custom paint input and static OpenGL reuse passed.\n"
           << "12,000 collection records; bounded visible-instance submission.\n"
           << "Optional motion suspends when hidden; settled controls and surfaces submit zero GPU draw calls.\n";
}
} // namespace EStudioUI
