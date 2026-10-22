#include "FoundationGallery.h"

#include <EStudioUI/Button.h>
#include <EStudioUI/PaintWidget.h>
#include <EStudioUI/ToolBar.h>

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace EStudioUI
{
namespace
{
void Check(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error("Foundation gallery: " + message);
}

void Status(CanvasWidget& canvas, std::string message)
{
    if (auto* label = dynamic_cast<Label*>(canvas.GetContext()->Root().FindById("foundation-status")))
        label->SetText(std::move(message));
}

//! Exercises mutation deferral from an actual GPU paint callback.
class PaintMutationProbe final : public PaintWidget
{
  public:
    //! Observes external verification results while the probe owns no callback target.
    PaintMutationProbe(bool& rejected, bool& deferred) : m_Rejected(rejected), m_Deferred(deferred)
    {
    }

  protected:
    //! Requests structural work safely while direct mutation is rejected before touching ownership.
    void PaintContent(Painter& painter) const override
    {
        if (!m_Attempted)
        {
            m_Attempted = true;
            auto* self = const_cast<PaintMutationProbe*>(this);
            try
            {
                self->Add<Widget>();
            }
            catch (const std::logic_error&)
            {
                m_Rejected = true;
            }
            const WidgetRef target(self);
            auto* completed = &m_Deferred;
            GetContext()->Defer(
                [target, completed](UiContext&)
                {
                    if (auto* live = target.Get())
                    {
                        live->Add<Widget>();
                        *completed = true;
                    }
                });
        }
        painter.SetBrush(Color{.15f, .7f, .5f, 1});
        painter.SetPen(Color{0, 0, 0, 0}, 0);
        painter.DrawRect(GetPaintBounds());
    }

  private:
    //! Reports rejection of the synchronous unsafe structural operation.
    bool& m_Rejected;
    //! Reports successful owner-side deferred delivery.
    bool& m_Deferred;
    //! Ensures only one deferred publication is produced.
    mutable bool m_Attempted = false;
};
} // namespace

FoundationGallery::FoundationGallery(Window& window, Widget& page, std::filesystem::path assets)
    : m_Window(window), m_Page(page), m_Assets(std::move(assets))
{
    page.SetGap(12);
    auto& title = page.Add<Label>("Background work. Predictable UI updates.");
    title.SetStyleProperty(StyleProperty::FontSize, 23.0f);
    title.SetHeight(SizeRule::Fixed(34));
    page.Add<Label>("A worker builds the scene and its BVH. A typed handle publishes one coherent result.");
    auto& commands = page.Add<ToolBar>();
    commands.AddButton("Generate 200,000 items").Clicked().Connect([this] { Start(200000); });
    commands.AddButton("Replace with 20,000").Clicked().Connect([this] { Start(20000); });
    commands.AddButton("Cancel").Clicked().Connect([this] { Cancel(); });
    commands.AddSeparator();
    commands.AddButton("Delete target").Clicked().Connect([this] { DeleteTarget(); });
    commands.AddButton("New target").Clicked().Connect([this] { CreateTarget(); });
    commands.AddButton("Second window").Clicked().Connect([this] { OpenWindow(); });
    m_Status = &page.Add<Label>("Ready / drag the canvas to pan; scroll to zoom.");
    m_Status->SetId("foundation-status");
    m_Status->SetHeight(SizeRule::Fixed(28));
    m_Surface = &page.Add<Widget>();
    m_Surface->SetHeight(SizeRule::Fill(1, 180));
    CreateTarget();
    auto& note =
        page.Add<Label>("Latest request wins. Deleted targets cancel delivery. Settled controls return to idle.");
    note.SetHeight(SizeRule::Fixed(25));
}

FoundationGallery::~FoundationGallery()
{
    m_Requests.Cancel();
    m_Tasks.Shutdown();
}

void FoundationGallery::CreateTarget()
{
    if (m_Canvas)
        return;
    m_Canvas = &m_Surface->Add<CanvasWidget>();
    m_Canvas->SetHeight(SizeRule::Fill());
    m_Canvas->SetPanMode(true);
    m_Canvas->SetCamera({280, 180}, 1);
    std::string error;
    if (!m_Canvas->SetItemStyleSheet("canvas-item { background: #48cbb299; border: 1px solid #86edd0; "
                                     "border-radius: 2px; color: #a8d1ff; }",
                                     error))
        throw std::runtime_error(error);
    m_Status->SetText("Ready / each new target has its own lifetime and identity.");
}

void FoundationGallery::Start(std::size_t count)
{
    if (!m_Canvas)
        CreateTarget();
    const auto generation = ++m_Generation;
    m_Status->SetText("Request " + std::to_string(generation) + " / building " + std::to_string(count) + " items...");
    UiTaskOptions<CanvasWidget> options;
    options.m_Progress = [generation](CanvasWidget& canvas, double fraction)
    {
        Status(canvas, "Request " + std::to_string(generation) + " / " +
                           std::to_string(static_cast<int>(fraction * 100)) + "% / the UI remains interactive");
    };
    options.m_Error = [](CanvasWidget& canvas, std::exception_ptr error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception& failure)
        {
            Status(canvas, std::string("Background operation failed: ") + failure.what());
        }
    };
    m_Task = m_Tasks.RunLatest(
        m_Requests, m_Window.Ui().GetHandle(*m_Canvas),
        [count](TaskContext& task)
        {
            auto scene = std::make_unique<CanvasScene>();
            scene->Reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                if (index % 1024 == 0)
                {
                    task.ThrowIfCancellationRequested();
                    task.ReportProgress(static_cast<double>(index) / static_cast<double>(count));
                }
                const double x = static_cast<double>(index % 500) * 18;
                const double y = static_cast<double>(index / 500) * 18;
                scene->Add({CanvasPrimitive::Rectangle, {x, y}, {x + 11, y + 11}});
            }
            task.ThrowIfCancellationRequested();
            task.ReportProgress(1);
            return scene;
        },
        [count, generation, completions = m_Completions](CanvasWidget& canvas, std::unique_ptr<CanvasScene> scene)
        {
            canvas.GetScene().ReplaceWith(*scene);
            canvas.SceneChanged();
            completions->fetch_add(1);
            Status(canvas, "Request " + std::to_string(generation) + " / " + std::to_string(count) +
                               " items committed / pan and zoom while the next request builds");
        },
        std::move(options));
}

void FoundationGallery::Cancel()
{
    m_Task.Cancel();
    m_Status->SetText("Cancelled / already running computation observes cooperative stop.");
}

void FoundationGallery::DeleteTarget()
{
    if (!m_Canvas)
        return;
    auto* removed = std::exchange(m_Canvas, nullptr);
    m_Surface->RemoveChild(*removed);
    m_Status->SetText("Target deleted / pending delivery cannot resolve it; worker stop is requested.");
}

void FoundationGallery::OpenWindow()
{
    WindowOptions options;
    options.m_Width = 580;
    options.m_Height = 250;
    options.m_Title = "EStudioUI / one application loop";
    options.m_Headless = m_Window.IsHeadless();
    auto window = std::make_unique<Window>(options, m_Assets / "NotoSans.ttf");
    std::string error;
    window->Ui().SetStyleSheet("window { background: #172434; color: #def7ef; padding: 24px; gap: 15px; } "
                               "label { font-size: 17px; } button { background: #366c65; color: white; "
                               "padding: 10px; border-radius: 8px; height: 42px; }",
                               error);
    window->Ui().Root().Add<Label>("This window shares the application's event loop.");
    window->Ui().Root().Add<Label>("The main window keeps receiving input and worker results.");
    auto& close = window->Ui().Root().Add<Button>("Close this window");
    const auto target = window->GetHandle();
    close.Clicked().Connect([target] { target.Post([](Window& receiver) { receiver.Close(); }); });
    m_Windows.push_back(std::move(window));
}

void FoundationGallery::Await()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    auto dispatcher = m_Window.Ui().GetDispatcher();
    while (!m_Task.IsReady() && std::chrono::steady_clock::now() < deadline)
    {
        dispatcher.Drain();
        if (m_Window.Ui().NeedsRepaint())
            m_Window.RenderFrame(m_Window.Ui().GetTime());
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(m_Task.IsReady(), "background request settles before its deadline");
    m_Task.RethrowIfFailed();
}

void FoundationGallery::Verify(const std::filesystem::path& output)
{
    auto& ui = m_Window.Ui();
    ui.SetFocus(nullptr);
    Start(200000);
    Await();
    Check(m_Task.GetStatus() == TaskStatus::Completed && m_Canvas->GetScene().Size() == 200000,
          "worker-built scene publishes into its live typed target");
    Start(200000);
    const auto obsolete = m_Task;
    Start(20000);
    Await();
    Check(obsolete.GetStatus() == TaskStatus::Superseded && m_Canvas->GetScene().Size() == 20000,
          "latest request suppresses the obsolete scene");
    Start(200000);
    Check(m_Task.Cancel(), "pending request accepts cancellation");
    Await();
    Check(m_Task.GetStatus() == TaskStatus::Cancelled, "cancelled result never applies");
    Start(200000);
    DeleteTarget();
    Await();
    Check(m_Task.GetStatus() == TaskStatus::TargetExpired, "receiver deletion retires work safely");
    CreateTarget();
    Start(200000);
    Await();
    m_Window.RenderFrame(ui.GetTime());
    m_Window.Graphics().SavePpm(output / "foundation-async.ppm");
    m_Window.RenderFrame(ui.GetTime());
    Check(m_Window.Graphics().GetStats().m_DrawCalls == 0, "settled background example submits no GPU draw calls");
    bool rejected = false;
    bool deferred = false;
    auto& probe = ui.Root().Add<PaintMutationProbe>(rejected, deferred);
    probe.SetFloatingBounds({50, 50, 40, 40});
    m_Window.RenderFrame(ui.GetTime());
    Check(rejected && !deferred, "paint rejects direct structural mutation and queues safe work");
    m_Window.RenderFrame(ui.GetTime());
    Check(deferred && probe.GetChildren().size() == 1, "next frame commits deferred paint-side structural work");
    ui.Root().RemoveChild(probe);
    m_Window.RenderFrame(ui.GetTime());
    const auto stats = ui.GetDispatcher().GetStats();
    std::ofstream report(output / "foundation-gallery.txt");
    report << "Background scene generation and real GPU publication: PASS\n"
           << "200,000 items built with an isolated worker-owned scene and BVH.\n"
           << "Latest-request replacement, explicit cancellation, target deletion, and fresh target delivery passed.\n"
           << "Paint mutation rejected; deferred mutation delivered at the next owner boundary.\n"
           << "Successful results: " << m_Completions->load() << "; queue high-water: " << stats.m_HighWaterMark
           << "; coalesced publications: " << stats.m_Superseded << ".\n"
           << "Settled frame submits zero draw calls. Software GL establishes correctness, not hardware throughput.\n";
}
} // namespace EStudioUI
