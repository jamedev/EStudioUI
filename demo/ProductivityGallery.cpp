#include "ProductivityGallery.h"
#include "GpuProbeWindow.h"
#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/Bindings.h>
#include <EStudioUI/CommandPalette.h>
#include <EStudioUI/NotificationCenter.h>
#include <EStudioUI/LogView.h>
#include <EStudioUI/SegmentedControl.h>
#include <EStudioUI/TagEditor.h>
#include <EStudioUI/RangeSlider.h>
#include <EStudioUI/LinkLabel.h>
#include <EStudioUI/FontPicker.h>
#include <EStudioUI/DateTimeEdit.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/UiContext.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace EStudioUI
{
struct ProductivityGallery::Data
{
    explicit Data(UiContext& ui)
        : m_Interval(ui.States(), {20, 80}), m_Tags(ui.States(), {"Steel", "Load case A"}),
          m_Font(ui.States(), FontSelection{})
    {
    }

    State<RangeValues> m_Interval;
    State<std::vector<std::string>> m_Tags;
    State<FontSelection> m_Font;
    std::vector<ScopedBinding> m_Bindings;
    WidgetRef m_Notifications, m_Log, m_Search, m_Range, m_DateTime, m_FontEditor, m_Palette;
    std::vector<std::shared_ptr<Action>> m_Actions;
    BackgroundTasks m_Workers{BackgroundTaskOptions{1, 8}};
    Task m_Task;
};

namespace
{
Widget& Body(Widget& page)
{
    auto& area = page.Add<ScrollArea>();
    area.SetWidth(SizeRule::Fill());
    area.SetHeight(SizeRule::Fill());
    auto& body = area.Content();
    body.SetWidth(SizeRule::Fill());
    body.SetGap(12);
    body.SetStyleProperty(StyleProperty::Padding, Insets{16, 16, 16, 16});
    return body;
}

void Heading(Widget& parent, std::string text)
{
    auto& label = parent.Add<Label>(std::move(text));
    label.SetStyleProperty(StyleProperty::FontSize, 18.0f);
    label.SetWidth(SizeRule::Fill());
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace

ProductivityGallery::ProductivityGallery(Window& window, Widget& searchPage, Widget& controlsPage)
    : m_Data(std::make_shared<Data>(window.Ui())), m_Window(window)
{
    const auto data = m_Data;
    const std::weak_ptr<Data> weak(data);
    auto& ui = window.Ui();
    auto& notices = NotificationCenter::Create(ui);
    data->m_Notifications = WidgetRef(&notices);
    auto& searchBody = Body(searchPage);
    Heading(searchBody, "Search & application feedback");
    auto& description = searchBody.Add<Label>("Search suggestions keep editor focus. Commands reuse Actions. Static "
                                              "messages expire without continuous rendering.");
    description.SetWordWrap(true);
    description.SetWidth(SizeRule::Fill());
    auto& search = searchBody.Add<SearchBox>();
    search.SetWidth(SizeRule::Fill());
    search.SetHeight(SizeRule::Fixed(36));
    data->m_Search = WidgetRef(&search);
    search.SetSuggestions({{"mesh", "Generate mesh", "Prepare finite elements"},
                           {"solve", "Solve model", "Run the active analysis"},
                           {"steel", "Steel properties", "Material properties"},
                           {"export", "Export drawing", "Write a drawing file"},
                           {"epure", "Épure des efforts", "Unicode matching"}});
    auto& results = searchBody.Add<Label>("Try mesh, STEEL, or épure. Enter accepts the highlighted suggestion.");
    results.SetWordWrap(true);
    results.SetWidth(SizeRule::Fill());
    auto& commands = searchBody.Add<Widget>();
    commands.SetLayoutDirection(Axis::Horizontal);
    commands.SetWidth(SizeRule::Fill());
    auto& paletteButton = commands.Add<ToolButton>("Command palette");
    auto& notify = commands.Add<ToolButton>("Show notification");
    auto& warning = commands.Add<ToolButton>("Show warning");
    auto& palette = CommandPalette::Create(paletteButton);
    data->m_Palette = WidgetRef(&palette);
    for (const auto* caption : {"Generate mesh", "Solve model", "Export drawing", "Save workspace"})
    {
        auto action = ui.CreateAction(caption);
        action->Triggered().Connect(
            [weak, caption = std::string(caption)]
            {
                if (const auto live = weak.lock())
                {
                    if (auto* center = dynamic_cast<NotificationCenter*>(live->m_Notifications.Get()))
                    {
                        center->Show(caption + " completed", FeedbackSeverity::Success);
                    }
                    if (auto* log = dynamic_cast<LogView*>(live->m_Log.Get()))
                    {
                        log->Append({{caption, "Commands", FeedbackSeverity::Information}});
                    }
                }
            });
        data->m_Actions.push_back(std::move(action));
    }
    palette.SetActions(data->m_Actions);
    const WidgetRef paletteRef(&palette);
    paletteButton.Clicked().Connect(
        [paletteRef]
        {
            if (auto* live = dynamic_cast<CommandPalette*>(paletteRef.Get()))
            {
                live->Show();
            }
        });
    notify.Clicked().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                if (auto* center = dynamic_cast<NotificationCenter*>(live->m_Notifications.Get()))
                {
                    center->Show("Analysis saved successfully", FeedbackSeverity::Success);
                }
            }
        });
    warning.Clicked().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                if (auto* center = dynamic_cast<NotificationCenter*>(live->m_Notifications.Get()))
                {
                    center->Show("One load case has no supports", FeedbackSeverity::Warning);
                }
            }
        });
    Heading(searchBody, "Validation and bounded worker logging");
    auto& numeric = searchBody.Add<TextBox>();
    numeric.SetWidth(SizeRule::Fixed(280));
    numeric.SetPlaceholder("Iterations: 1–1000");
    numeric.SetValidator(std::make_shared<IntegerValidator>(1, 1000));
    auto& validation = searchBody.Add<ValidationMessage>();
    validation.SetWidth(SizeRule::Fill());
    validation.Bind(numeric, "Invalid iteration count", "Enter an iteration count from 1 to 1000");
    auto& log = searchBody.Add<LogView>();
    log.SetHeight(SizeRule::Fixed(220));
    log.SetWidth(SizeRule::Fill());
    log.SetCapacity(200);
    data->m_Log = WidgetRef(&log);
    log.Append({{"Ready. Worker output is batched on idle turns.", "Application", FeedbackSeverity::Information}});
    auto& logging = searchBody.Add<Widget>();
    logging.SetLayoutDirection(Axis::Horizontal);
    auto& produce = logging.Add<ToolButton>("Generate worker log");
    auto& clear = logging.Add<ToolButton>("Clear log");
    auto& pause = logging.Add<CheckBox>("Pause following");
    produce.Clicked().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                if (auto* view = dynamic_cast<LogView*>(live->m_Log.Get()))
                {
                    const auto sink = view->GetSink();
                    live->m_Task = live->m_Workers.Run(
                        view->GetContext()->GetHandle(*view),
                        [sink](std::stop_token stop)
                        {
                            for (int i = 0; i < 300 && !stop.stop_requested(); ++i)
                            {
                                sink.Append(
                                    {"Element batch " + std::to_string(i), "Mesher", FeedbackSeverity::Information});
                            }
                        },
                        [](LogView&) {});
                }
            }
        });
    clear.Clicked().Connect(
        [weak]
        {
            if (const auto live = weak.lock())
            {
                if (auto* view = dynamic_cast<LogView*>(live->m_Log.Get()))
                {
                    view->Clear();
                }
            }
        });
    pause.Toggled().Connect(
        [weak](bool paused)
        {
            if (const auto live = weak.lock())
            {
                if (auto* view = dynamic_cast<LogView*>(live->m_Log.Get()))
                {
                    view->SetFollowTail(!paused);
                }
            }
        });

    auto& controls = Body(controlsPage);
    Heading(controls, "Common controls / engineering inputs");
    auto& segments = controls.Add<SegmentedControl>();
    segments.SetItems({"Design", "Inspect", "Results"});
    segments.SetWidth(SizeRule::Fixed(460));
    auto& row = controls.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetWidth(SizeRule::Fill());
    auto& multi = row.Add<MultiSelectComboBox>();
    multi.SetWidth(SizeRule::Fill());
    multi.SetItems({"Displacement", "Stress", "Strain", "Temperature", "Reaction forces"});
    auto& tags = controls.Add<TagEditor>();
    tags.SetWidth(SizeRule::Fill());
    tags.SetSuggestions({"Steel", "Aluminum", "Load case A", "Load case B", "Validated"});
    data->m_Bindings.emplace_back(BindTags(tags, data->m_Tags));
    auto& rangeLabel = controls.Add<Label>("Visible result interval: 20–80");
    const WidgetRef rangeText(&rangeLabel);
    auto& range = controls.Add<RangeSlider>();
    range.SetWidth(SizeRule::Fill());
    range.SetHeight(SizeRule::Fixed(32));
    data->m_Range = WidgetRef(&range);
    data->m_Bindings.emplace_back(BindValue(range, data->m_Interval));
    range.ValuesChanged().Connect(
        [rangeText](RangeValues values)
        {
            if (auto* label = dynamic_cast<Label*>(rangeText.Get()))
            {
                label->SetText("Visible result interval: " + std::to_string(static_cast<int>(values.m_Lower)) + "–" +
                               std::to_string(static_cast<int>(values.m_Upper)));
            }
        });
    Heading(controls, "Dates, local time & selectable labels");
    auto& datetime = controls.Add<DateTimeEdit>();
    datetime.SetWidth(SizeRule::Fill());
    datetime.SetValue(DateTimeValue{{2026, 10, 24}, {9, 30, 0, 0}});
    data->m_DateTime = WidgetRef(&datetime);
    auto& selectable = controls.Add<SelectableLabel>("Select and copy this identifier: BEAM-Δ-0027");
    selectable.SetWidth(SizeRule::Fill());
    auto& link = controls.Add<LinkLabel>("Open project documentation", "project://documentation");
    link.SetWidth(SizeRule::Fill());
    link.Activated().Connect(
        [weak](std::string target)
        {
            if (const auto live = weak.lock())
            {
                if (auto* center = dynamic_cast<NotificationCenter*>(live->m_Notifications.Get()))
                {
                    center->Show("Application received: " + target);
                }
            }
        });
    Heading(controls, "Font selection & live GPU preview");
    auto& fontEditor = controls.Add<FontEditor>();
    fontEditor.SetWidth(SizeRule::Fill());
    fontEditor.RefreshFamilies();
    data->m_FontEditor = WidgetRef(&fontEditor);
    data->m_Bindings.emplace_back(BindFont(fontEditor, data->m_Font));
    auto& fontPicker = controls.Add<FontPicker>();
    fontPicker.SetWidth(SizeRule::Fill());
    data->m_Bindings.emplace_back(BindFont(fontPicker, data->m_Font));
}

ProductivityGallery::~ProductivityGallery() = default;

void ProductivityGallery::Verify(const std::filesystem::path& output)
{
    int checks = 0;
    const auto check = [&](bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            throw std::runtime_error(std::string("Productivity gallery: ") + message);
        }
    };
    WindowOptions options;
    options.m_Width = 1100;
    options.m_Height = 900;
    options.m_Title = "EStudioUI / Search and common controls";
    auto probe = m_Window.CreateOwnedWindow(options);
    AllowOversizedGpuProbe(*probe);
    auto& ui = probe->Ui();
    auto& tabs = ui.Root().Add<TabWidget>();
    tabs.SetWidth(SizeRule::Fill());
    tabs.SetHeight(SizeRule::Fill());
    auto& first = tabs.AddTab("Search & feedback");
    auto& second = tabs.AddTab("Common controls");
    ProductivityGallery gallery(*probe, first, second);
    auto data = gallery.m_Data;
    const auto frame = [&] { probe->RenderFrame(ui.GetTime()); };
    Theme theme;
    theme.m_Mode = ThemeMode::Light;
    probe->Themes().SetTheme(theme);
    frame();
    auto* search = dynamic_cast<SearchBox*>(data->m_Search.Get());
    ui.SetFocus(&search->GetEditor());
    search->SetText("MESH");
    ui.SetTime(ui.GetTime() + .2);
    ui.PumpPlatformServices();
    frame();
    auto* completions = dynamic_cast<CompletionPopup*>(ui.GetPopup());
    check(completions && completions->GetItems().size() == 1, "Production completion filters case-folded queries");
    probe->Graphics().SavePpm(output / "search-feedback-completion.ppm");
    ui.KeyEvent(Key::Enter, true);
    check(search->GetText() == "Generate mesh", "Keyboard acceptance updates the real input control");
    ui.SetFocus(nullptr);
    frame();
    auto* notifications = dynamic_cast<NotificationCenter*>(data->m_Notifications.Get());
    notifications->Show("Project saved", FeedbackSeverity::Success, 0);
    frame();
    probe->Graphics().SavePpm(output / "search-feedback-light.ppm");
    frame();
    check(probe->Graphics().GetStats().m_RepaintPixels == 0,
          "Static feedback retains all pixels on an unchanged frame");
    auto* palette = dynamic_cast<CommandPalette*>(data->m_Palette.Get());
    palette->Show();
    notifications->Show("Background analysis finished", FeedbackSeverity::Information, 0);
    frame();
    check(ui.GetPopup() == palette, "Notification overlay preserves the active modal command palette");
    probe->Graphics().SavePpm(output / "command-palette.ppm");
    ui.KeyEvent(Key::Enter, true);
    frame();
    check(!ui.GetPopup(), "Command executes after closing its modal surface");
    notifications->Clear();
    tabs.SetCurrentIndex(1);
    ui.SetFocus(nullptr);
    frame();
    data->m_Interval.Set({35, 92});
    frame();
    check(dynamic_cast<RangeSlider*>(data->m_Range.Get())->GetValues() == RangeValues{35, 92},
          "Bound interval updates the production dual-handle GPU control");
    probe->Graphics().SavePpm(output / "common-controls-light.ppm");
    for (const auto mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        theme.m_Mode = mode;
        probe->Themes().SetTheme(theme);
        frame();
        const auto prefix = mode == ThemeMode::Light ? "common-controls-light" : "common-controls-dark";
        probe->Graphics().SavePpm(output / (std::string(prefix) + "-retained.ppm"));
        probe->Graphics().SetPartialRedrawEnabled(false);
        frame();
        probe->Graphics().SavePpm(output / (std::string(prefix) + "-full.ppm"));
        check(Read(output / (std::string(prefix) + "-retained.ppm")) ==
                  Read(output / (std::string(prefix) + "-full.ppm")),
              "New controls match full-redraw GPU pixels in both theme modes");
        probe->Graphics().SetPartialRedrawEnabled(true);
        frame();
        frame();
        check(probe->Graphics().GetStats().m_RepaintPixels == 0, "Unchanged common controls submit no repaint work");
    }
    auto* datetime = dynamic_cast<DateTimeEdit*>(data->m_DateTime.Get());
    datetime->GetDateEditor().ShowCalendar();
    frame();
    probe->Graphics().SavePpm(output / "calendar-picker.ppm");
    static_cast<Popup*>(ui.GetPopup())->Close();
    probe->SetSize(640, 700);
    frame();
    probe->Graphics().SavePpm(output / "common-controls-narrow.ppm");
    check(!ui.HasAnimations(), "Idle search, notifications and common controls introduce no continuous animation work");
    std::cout << "Productivity gallery: " << checks << " checks passed\n";
}
} // namespace EStudioUI
