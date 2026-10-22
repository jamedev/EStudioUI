#include "StateGallery.h"

#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/ToolBar.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("State gallery: " + message);
}

template <typename T> T& Live(const WidgetRef& reference)
{
    auto* value = dynamic_cast<T*>(reference.Get());
    if (!value)
        throw std::logic_error("The state gallery receiving widget is no longer live.");
    return *value;
}

std::string Number(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(0) << value;
    return stream.str();
}

Label& Caption(Widget& parent, std::string text, float size = 14)
{
    auto& label = parent.Add<Label>(std::move(text));
    label.SetStyleProperty(StyleProperty::FontSize, size);
    label.SetHeight(SizeRule::Fixed(size > 20 ? 36 : 25));
    return label;
}

Widget& Pane(Widget& parent, std::string title, float height)
{
    auto& pane = parent.Add<Widget>();
    pane.AddClass("gallery-pane");
    pane.SetGap(9);
    pane.SetHeight(SizeRule::Fixed(height));
    auto& caption = Caption(pane, std::move(title), 13);
    caption.AddClass("pane-title");
    return pane;
}

Point Center(const Widget& widget)
{
    const auto bounds = widget.GetBounds();
    return {bounds.m_X + bounds.m_Width * .5f, bounds.m_Y + bounds.m_Height * .5f};
}

void Click(UiContext& ui, Widget& widget)
{
    const auto center = Center(widget);
    ui.PointerDown(center);
    ui.PointerUp(center);
}

//! Measures cache locality on an ordinary label without changing its visual behavior.
class LocalityLabel final : public Label
{
  public:
    //! Initializes the unrelated sibling text.
    explicit LocalityLabel(std::string text) : Label(std::move(text))
    {
    }

    //! Counts actual intrinsic measurement calls for the isolated sibling.
    mutable std::size_t m_Measurements = 0;

    //! Counts actual layout callbacks for the isolated sibling.
    std::size_t m_Arrangements = 0;

  protected:
    //! Preserves ordinary label measurement while recording actual cache misses.
    Point Measure(FontFace& font) const override
    {
        ++m_Measurements;
        return Label::Measure(font);
    }

    //! Records whether an unrelated edit causes this sibling's arrangement to run.
    void OnLayout() override
    {
        ++m_Arrangements;
        Label::OnLayout();
    }
};

//! Witnesses actual receiver teardown independently of its containing native window wrapper.
class LifetimeLabel final : public Label
{
  public:
    //! Publishes destruction to an external verification witness.
    explicit LifetimeLabel(bool& destroyed) : m_Destroyed(destroyed)
    {
    }

    //! Records teardown without accessing any retired application object.
    ~LifetimeLabel() override
    {
        m_Destroyed = true;
    }

  private:
    //! Outlives the short native regression probe.
    bool& m_Destroyed;
};

void VerifyNativeLifetime(bool headless, const std::filesystem::path& assets)
{
    WindowOptions options;
    options.m_Width = 360;
    options.m_Height = 160;
    options.m_Headless = headless;
    options.m_Title = "EStudioUI / state lifetime verification";
    {
        bool destroyed = false;
        bool retained = false;
        auto owner = std::make_unique<Window>(options, assets / "NotoSans.ttf");
        State<int> state(owner->Ui().States(), 1);
        auto& target = owner->Ui().Root().Add<LifetimeLabel>(destroyed);
        Bind(target, state,
             [&owner, &destroyed, &retained](LifetimeLabel& live, const int& value)
             {
                 live.SetText("Value " + std::to_string(value));
                 if (value == 2)
                 {
                     owner.reset();
                     retained = !destroyed && live.GetText() == "Value 2";
                 }
             });
        owner->RenderFrame();
        state.Set(2);
        Check(!owner && retained && destroyed, "native window deletion from a binding setter retains its active receiver");
    }
    {
        bool destroyed = false;
        bool coherent = false;
        auto owner = std::make_unique<Window>(options, assets / "NotoSans.ttf");
        auto& store = owner->Ui().States();
        State<int> first(store, 1);
        State<int> second(store, 3);
        auto& target = owner->Ui().Root().Add<LifetimeLabel>(destroyed);
        Bind(target, first,
             [&destroyed, &coherent, &second](LifetimeLabel& live, const int& value)
             {
                 live.SetText("Value " + std::to_string(value));
                 if (value == 2)
                     coherent = !destroyed && second.Get() == 4;
             });
        owner->RenderFrame();
        store.Batch(
            [&]
            {
                first.Set(2);
                second.Set(4);
                owner.reset();
                Check(!destroyed, "a batch body keeps its native domain alive after wrapper deletion");
            });
        Check(!owner && coherent && destroyed, "a closing native domain finishes its leased coherent batch before teardown");
    }
    {
        bool destroyed = false;
        bool nestedRunRejected = false;
        auto owner = std::make_unique<Window>(options, assets / "NotoSans.ttf");
        auto& store = owner->Ui().States();
        State<int> state(store, 1);
        auto& target = owner->Ui().Root().Add<LifetimeLabel>(destroyed);
        Bind(target, state,
             [](LifetimeLabel& live, const int& value) { live.SetText("Value " + std::to_string(value)); });
        owner->RenderFrame();
        store.Batch(
            [&]
            {
                state.Set(2);
                owner->SetSize(410, 190);
                try
                {
                    owner->Run();
                }
                catch (const std::logic_error&)
                {
                    nestedRunRejected = true;
                }
                Check(!owner->IsClosed(), "native resize notifications do not close a window during a batch");
            });
        owner->RenderFrame();
        Check(nestedRunRejected && !owner->IsClosed() && state.Get() == 2 && target.GetText() == "Value 2" &&
                  owner->Graphics().GetStats().m_DrawCalls > 0,
              "batch resize commits and renders; nested Window::Run is rejected before processing windows");
    }
}
} // namespace

StateGallery::StateGallery(Window& window, Widget& page, std::filesystem::path assets)
    : m_Window(window), m_Assets(std::move(assets)), m_Store(window.Ui().States()),
      m_Level(m_Store, 42.0,
              [](const double& value)
              {
                  if (!std::isfinite(value) || value < 0 || value > 100)
                      throw std::invalid_argument("Level must be between 0 and 100.");
              }),
      m_Name(m_Store, std::string("Aster beam"),
             [](const std::string& value)
             {
                 if (value.size() > 24)
                     throw std::invalid_argument("The shared name is limited to 24 UTF-8 bytes.");
             }),
      m_Enabled(m_Store, true), m_Visible(m_Store, true), m_Progress(m_Store, 0.0)
{
    page.SetGap(9);
    Caption(page, "One value. Every view stays in sync.", 23);
    Caption(page, "Typed state, two-way controls, atomic batches and safe publication from a worker.");
    auto& commands = page.Add<ToolBar>();
    commands.AddButton("Apply coherent batch").Clicked().Connect([this] { ApplyPreset(); });
    commands.AddButton("Try invalid batch").Clicked().Connect([this] { RejectPreset(); });
    commands.AddSeparator();
    commands.AddButton("Publish worker progress").Clicked().Connect([this] { StartProgress(); });

    auto& columns = page.Add<Widget>();
    columns.SetLayoutDirection(Axis::Horizontal);
    columns.SetHeight(SizeRule::Fill(1, 400));
    columns.SetGap(14);
    auto& left = columns.Add<Widget>();
    left.SetWidth(SizeRule::Fill(1, 340));
    left.SetGap(12);
    auto& right = columns.Add<Widget>();
    right.SetWidth(SizeRule::Fill(1, 340));
    right.SetGap(12);

    auto& scalar = Pane(left, "01 / SHARED SCALAR", 215);
    Caption(scalar, "Drag the slider or edit the number.");
    auto& editors = scalar.Add<Widget>();
    editors.SetLayoutDirection(Axis::Horizontal);
    editors.SetGap(12);
    editors.SetHeight(SizeRule::Fixed(40));
    auto& slider = editors.Add<Slider>();
    slider.SetRange(0, 100);
    slider.SetWidth(SizeRule::Fill());
    auto& number = editors.Add<NumericInput>();
    number.SetRange(0, 100);
    number.SetDecimals(0);
    number.SetWidth(SizeRule::Fixed(118));
    auto& level = Caption(scalar, {}, 28);
    m_Slider = WidgetRef(&slider);
    m_Number = WidgetRef(&number);
    m_LevelReadout = WidgetRef(&level);
    BindValue(slider, m_Level);
    BindValue(number, m_Level);
    Bind(level, m_Level, [](Label& live, const double& value) { live.SetText(Number(value) + " / 100"); });
    BindEnabled(slider, m_Enabled);
    BindEnabled(number, m_Enabled);

    auto& text = Pane(left, "02 / SHARED TEXT", 173);
    auto& editor = text.Add<TextBox>();
    editor.SetHeight(SizeRule::Fixed(37));
    editor.SetPlaceholder("Name the shared value");
    auto& echo = Caption(text, {});
    m_Editor = WidgetRef(&editor);
    m_Echo = WidgetRef(&echo);
    m_TextBinding = BindText(editor, m_Name);
    BindText(echo, m_Name);
    Caption(text, "Validation belongs to the state: at most 24 UTF-8 bytes.", 12);

    auto& batch = Pane(right, "03 / ONE COHERENT REVISION", 215);
    auto& summary = Caption(batch, "", 16);
    summary.SetHeight(SizeRule::Fixed(55));
    auto& counters = Caption(batch, "", 12);
    auto& toggles = batch.Add<ToolBar>();
    auto& enabled = toggles.AddButton("Editors enabled");
    enabled.SetCheckable(true);
    auto& visible = toggles.AddButton("Readout visible");
    visible.SetCheckable(true);
    m_Summary = WidgetRef(&summary);
    m_Counters = WidgetRef(&counters);
    m_EnableToggle = WidgetRef(&enabled);
    m_VisibleToggle = WidgetRef(&visible);
    m_Optional = WidgetRef(&level);
    BindChecked(enabled, m_Enabled);
    BindChecked(visible, m_Visible);
    BindVisible(level, m_Visible);

    auto& producer = Pane(right, "04 / WORKER TO STATE TO VIEW", 173);
    auto& progress = producer.Add<Slider>();
    progress.SetRange(0, 100);
    progress.SetEnabled(false);
    progress.SetHeight(SizeRule::Fixed(32));
    Bind(progress, m_Progress, [](Slider& live, const double& value) { live.SetValue(value); });
    auto& progressText = Caption(producer, {});
    Bind(progressText, m_Progress,
         [](Label& live, const double& value) { live.SetText(Number(value) + "% / latest pending publication wins"); });
    auto& isolated = producer.Add<LocalityLabel>("Independent sibling / no state dependency");
    isolated.SetStyleProperty(StyleProperty::FontSize, 12.0f);
    isolated.SetHeight(SizeRule::Fixed(25));
    m_Isolated = WidgetRef(&isolated);

    const auto observe = [this](const auto& change)
    {
        ++m_Notifications;
        RefreshSummary(change.GetRevision());
    };
    m_Level.Subscribe(observe);
    m_Name.Subscribe(observe);
    RefreshSummary(m_Store.GetRevision());
    auto& status = Caption(page, "Ready / ignored binding handles stay connected until a receiver or state expires.", 13);
    m_Status = WidgetRef(&status);
}

StateGallery::~StateGallery()
{
    m_Worker.request_stop();
    if (m_Worker.joinable())
        m_Worker.join();
}

void StateGallery::SetStatus(std::string message)
{
    if (auto* label = dynamic_cast<Label*>(m_Status.Get()))
        label->SetText(std::move(message));
}

void StateGallery::RefreshSummary(std::uint64_t revision)
{
    if (m_SummaryRevision != revision || m_DerivedUpdates == 0)
    {
        m_SummaryRevision = revision;
        ++m_DerivedUpdates;
        if (auto* label = dynamic_cast<Label*>(m_Summary.Get()))
            label->SetText(m_Name.Get() + "\nLevel " + Number(m_Level.Get()) + " / shared revision " + std::to_string(revision));
    }
    if (auto* label = dynamic_cast<Label*>(m_Counters.Get()))
        label->SetText("Derived updates " + std::to_string(m_DerivedUpdates) + " / value notifications " +
                       std::to_string(m_Notifications));
}

void StateGallery::ApplyPreset()
{
    const bool alternate = m_Name.Get() != "Orion frame";
    m_Store.Batch(
        [this, alternate]
        {
            m_Level.Set(alternate ? 73.0 : 42.0);
            m_Name.Set(alternate ? "Orion frame" : "Aster beam");
            m_Enabled.Set(true);
            m_Visible.Set(true);
        });
    SetStatus("Committed together / both values are readable before the first notification; derived work runs once.");
}

void StateGallery::RejectPreset()
{
    try
    {
        m_Store.Batch(
            [this]
            {
                m_Name.Set("Should never appear");
                m_Level.Set(150);
            });
    }
    catch (const std::invalid_argument& error)
    {
        SetStatus(std::string("Batch rolled back / ") + error.what() + " No partial values or notifications.");
    }
}

void StateGallery::StartProgress(bool burst)
{
    m_Worker.request_stop();
    if (m_Worker.joinable())
        m_Worker.join();
    m_Progress.Set(0);
    m_Publications->store(0);
    SetStatus("Worker owns only a typed state handle and values / UI delivery remains queued and coalesced.");
    m_Worker = std::jthread(
        [handle = m_Progress.GetHandle(), count = burst ? 2000 : 100, burst,
         publications = m_Publications, last = m_LastPublication](std::stop_token stop)
        {
            for (int index = 1; index <= count && !stop.stop_requested(); ++index)
            {
                const double value = 100.0 * static_cast<double>(index) / static_cast<double>(count);
                *last = handle.PostLatest("progress", [value](State<double>& live) { live.Set(value); });
                publications->fetch_add(1);
                if (!burst)
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        });
}

void StateGallery::Verify(const std::filesystem::path& output)
{
    auto& ui = m_Window.Ui();
    auto dispatcher = ui.GetDispatcher();
    ui.SetFocus(nullptr);

    const auto canonicalText = m_Name.GetSnapshot();
    Live<TextBox>(m_Editor).SetText("This candidate exceeds the shared name limit");
    Check(m_TextBinding.GetError() && m_Name.Get() == *canonicalText &&
              Live<TextBox>(m_Editor).GetText() == *canonicalText,
          "rejected two-way edits restore canonical text and expose the validation error");
    m_TextBinding.ClearError();
    m_Window.RenderFrame(ui.GetTime());
    Check(Live<Slider>(m_Slider).GetValue() == 42 && Live<NumericInput>(m_Number).GetValue() == 42,
          "initial values apply immediately to all views");
    Live<Slider>(m_Slider).SetValue(61);
    Check(m_Level.Get() == 61 && Live<NumericInput>(m_Number).GetValue() == 61 &&
              Live<Label>(m_LevelReadout).GetText() == "61 / 100", "scalar controls share one value without feedback");
    Live<NumericInput>(m_Number).SetValue(64);
    Check(m_Level.Get() == 64 && Live<Slider>(m_Slider).GetValue() == 64, "numeric edits update the sibling slider");
    ui.SetFocus(&Live<TextBox>(m_Editor));
    Live<TextBox>(m_Editor).SelectAll();
    ui.TextInput("Vega strut");
    Check(m_Name.Get() == "Vega strut" && Live<Label>(m_Echo).GetText() == "Vega strut",
          "actual text input reaches state and the independent label");
    ui.SetFocus(nullptr);

    m_Window.RenderFrame(ui.GetTime());
    Click(ui, Live<ToolButton>(m_EnableToggle));
    Check(!m_Enabled.Get() && !Live<Slider>(m_Slider).IsEnabled() && !Live<NumericInput>(m_Number).IsEnabled(),
          "actual toggle input disables both editors");
    Click(ui, Live<ToolButton>(m_EnableToggle));
    Click(ui, Live<ToolButton>(m_VisibleToggle));
    Check(!m_Visible.Get() && !Live<Label>(m_Optional).IsVisible(), "visibility binding follows its checked control");
    Click(ui, Live<ToolButton>(m_VisibleToggle));

    const auto beforeDerived = m_DerivedUpdates;
    const auto beforeNotifications = m_Notifications;
    bool coherent = true;
    auto levelObserver = m_Level.Subscribe(
        [this, &coherent](const StateChange<double>& change)
        {
            coherent = coherent && change.GetBefore() == 64 && change.GetAfter() == 73 && m_Name.Get() == "Orion frame" &&
                       m_Name.GetRevision() == change.GetRevision();
        });
    ApplyPreset();
    levelObserver.Disconnect();
    Check(coherent && m_DerivedUpdates == beforeDerived + 1 && m_Notifications == beforeNotifications + 2 &&
              m_Level.GetRevision() == m_Name.GetRevision(), "a batch publishes a coherent revision and derives once");
    const auto committed = m_Store.GetRevision();
    const auto savedName = m_Name.GetSnapshot();
    const auto savedLevel = m_Level.GetSnapshot();
    const auto notified = m_Notifications;
    RejectPreset();
    Check(m_Name.Get() == *savedName && m_Level.Get() == *savedLevel && m_Store.GetRevision() == committed &&
              m_Notifications == notified, "an invalid batch rolls back every candidate without notification");
    Check(!m_Level.Set(*savedLevel) && m_Store.GetRevision() == committed, "equal values suppress revisions");

    m_Window.RenderFrame(ui.GetTime());
    const auto beforeStats = ui.GetStats();
    const auto isolatedBounds = Live<Label>(m_Isolated).GetBounds();
    const auto isolatedMeasurements = Live<LocalityLabel>(m_Isolated).m_Measurements;
    const auto isolatedArrangements = Live<LocalityLabel>(m_Isolated).m_Arrangements;
    m_Level.Set(74);
    m_Window.RenderFrame(ui.GetTime());
    const auto afterStats = ui.GetStats();
    Check(afterStats.m_StyleResolutions == beforeStats.m_StyleResolutions,
          "bound value changes reuse resolved CSS rather than globally restyling");
    const auto currentBounds = Live<Label>(m_Isolated).GetBounds();
    Check(currentBounds.m_X == isolatedBounds.m_X && currentBounds.m_Y == isolatedBounds.m_Y &&
              currentBounds.m_Width == isolatedBounds.m_Width && currentBounds.m_Height == isolatedBounds.m_Height,
          "an independent sibling retains its geometry across a bound edit");
    Check(Live<LocalityLabel>(m_Isolated).m_Measurements == isolatedMeasurements &&
              Live<LocalityLabel>(m_Isolated).m_Arrangements == isolatedArrangements,
          "an independent sibling reuses its measurement and arrangement caches (measure " +
              std::to_string(isolatedMeasurements) + " -> " +
              std::to_string(Live<LocalityLabel>(m_Isolated).m_Measurements) + ", arrange " +
              std::to_string(isolatedArrangements) + " -> " +
              std::to_string(Live<LocalityLabel>(m_Isolated).m_Arrangements) + ")");

    const auto beforeQueue = dispatcher.GetStats();
    StartProgress(true);
    m_Worker.join();
    Check(m_Progress.Get() == 0 && m_Publications->load() == 2000, "worker writes remain queued until owner drain");
    dispatcher.Drain();
    const auto afterQueue = dispatcher.GetStats();
    Check(m_Progress.Get() == 100 && m_LastPublication->GetStatus() == DispatchStatus::Completed &&
              afterQueue.m_Superseded >= beforeQueue.m_Superseded + 1999,
          "two thousand worker publications coalesce to the final live state update");

    auto& temporary = ui.Root().Add<Label>();
    BindText(temporary, m_Name);
    ui.Root().RemoveChild(temporary);
    m_Name.Set("Receiver safely retired");
    Check(*savedName == "Orion frame" && *savedLevel == 73 && m_Level.Get() == 74,
          "owned snapshots retain earlier values after later commits");
    DispatchReceipt expired;
    {
        State<int> transient(m_Store, 1);
        expired = transient.GetHandle().Post([](State<int>& live) { live.Set(2); });
    }
    dispatcher.Drain();
    Check(expired.GetStatus() == DispatchStatus::TargetExpired, "destroyed states expire pending typed delivery");

    VerifyNativeLifetime(m_Window.IsHeadless(), m_Assets);

    m_Store.Batch(
        [this]
        {
            m_Level.Set(73);
            m_Name.Set("Orion frame");
        });
    SetStatus("Verified / coherent commits, rollback, coalesced worker delivery and local invalidation.");
    m_Window.RenderFrame(ui.GetTime());
    m_Window.Graphics().SavePpm(output / "state-foundation.ppm");
    m_Window.RenderFrame(ui.GetTime());
    Check(m_Window.Graphics().GetStats().m_DrawCalls == 0, "settled bindings submit no GPU draw calls");
    std::ofstream report(output / "state-foundation.txt");
    report << "Observable state, bindings and actual GPU integration: PASS\n"
           << "Shared slider/numeric value, actual text input, checked/enabled/visible bindings passed.\n"
           << "Atomic batch: one coherent revision, two notifications and one derived recomputation.\n"
           << "Invalid final candidate: full rollback, unchanged revision and no notifications.\n"
           << "Immutable snapshots retain their owned values; equal writes produce no revision.\n"
           << "Worker publications: " << m_Publications->load() << "; superseded pending updates: "
           << afterQueue.m_Superseded - beforeQueue.m_Superseded << ".\n"
           << "Bound edit CSS resolutions: " << afterStats.m_StyleResolutions - beforeStats.m_StyleResolutions
           << "; measurements: " << afterStats.m_Measurements - beforeStats.m_Measurements
           << "; arrangements: " << afterStats.m_Arrangements - beforeStats.m_Arrangements << ".\n"
           << "Independent sibling: zero new measurements and zero arrangements; geometry reused.\n"
           << "Removed receiver and expired state delivery are safe.\n"
           << "Actual native window deletion from a binding setter and a coherent batch retains active receivers.\n"
           << "Native resize inside a batch stays open and renders after commit; nested Window::Run is rejected.\n"
           << "Settled frame submits zero draw calls. Software GL establishes correctness, not hardware throughput.\n";
}
} // namespace EStudioUI
