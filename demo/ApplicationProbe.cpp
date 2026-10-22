#include "ApplicationProbe.h"

#include <EStudioUI/Application.h>
#include <EStudioUI/Animation.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Window.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace EStudioUI
{
namespace
{
using namespace std::chrono_literals;

void Check(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error("Application probe: " + message);
}

struct PaintState
{
    std::mutex m_Mutex;
    std::condition_variable m_Changed;
    int m_First = 0;
    int m_Second = 0;

    int Count(bool first)
    {
        const std::lock_guard lock(m_Mutex);
        return first ? m_First : m_Second;
    }

    void WaitAfter(bool first, int count)
    {
        std::unique_lock lock(m_Mutex);
        Check(m_Changed.wait_for(lock, 5s, [&] { return (first ? m_First : m_Second) > count; }),
              "queued changes reach the scheduled framebuffer");
    }
};

class PaintCounter final : public Widget
{
  public:
    PaintCounter(PaintState& state, bool first) : m_State(state), m_First(first)
    {
        SetHeight(SizeRule::Fill());
    }

  protected:
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override
    {
        Widget::Paint(renderer, font, time, opacity);
        {
            const std::lock_guard lock(m_State.m_Mutex);
            ++(m_First ? m_State.m_First : m_State.m_Second);
        }
        m_State.m_Changed.notify_all();
    }

  private:
    PaintState& m_State;
    bool m_First;
};

class DestructionWitness final : public Widget
{
  public:
    explicit DestructionWitness(bool& destroyed) : m_Destroyed(destroyed)
    {
    }

    ~DestructionWitness() override
    {
        m_Destroyed = true;
    }

  private:
    bool& m_Destroyed;
};

void Delivered(const DispatchReceipt& receipt)
{
    Check(receipt.WaitFor(5s), "posted work wakes the sleeping application loop");
    receipt.RethrowIfFailed();
    Check(receipt.GetStatus() == DispatchStatus::Completed, "accepted native work completes");
}
} // namespace

void VerifyApplication(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless)
{
    std::filesystem::create_directories(output);
    Application application;
    WindowOptions options;
    options.m_Width = 320;
    options.m_Height = 180;
    options.m_Headless = headless;
    options.m_RenderWhenUnfocused = true;
    options.m_Title = "EStudioUI / execution foundation";
    auto first = std::make_unique<Window>(options, assets / "NotoSans.ttf");
    auto second = std::make_unique<Window>(options, assets / "NotoSans.ttf");
    auto transient = std::make_unique<Window>(options, assets / "NotoSans.ttf");
    auto reentrant = std::make_unique<Window>(options, assets / "NotoSans.ttf");
    const auto closingTarget = reentrant->GetHandle();
    std::stop_callback deleteOnClose(closingTarget.GetTargetStopToken(), [&] { reentrant.reset(); });
    reentrant->Close();
    Check(!reentrant && closingTarget.Post([](Window&) {}).GetStatus() == DispatchStatus::Rejected,
          "a cancellation observer may destroy a directly closing window");
    for (const bool initialSetter : {true, false})
    {
        bool treeDestroyed = false;
        auto direct = std::make_unique<Window>(options, assets / "NotoSans.ttf");
        const auto directTarget = direct->GetHandle();
        auto& box = direct->Ui().Root().Add<DestructionWitness>(treeDestroyed);
        Animation animation;
        animation.AddCustom(box, "destroy-owning-window",
                            [&](Widget&, double progress)
                            {
                                if (initialSetter || progress > 0)
                                    direct.reset();
                            });
        direct->Ui().Animations().Play(std::move(animation));
        if (!initialSetter)
            direct->Ui().SetTime(1);
        Check(!direct && treeDestroyed,
              "direct animation callbacks finish safely and release native ownership at the outer call boundary");
        Check(directTarget.Post([](Window&) {}).GetStatus() == DispatchStatus::Rejected,
              "direct animation self-deletion closes its producer endpoint immediately");
    }
    {
        bool treeDestroyed = false;
        auto direct = std::make_unique<Window>(options, assets / "NotoSans.ttf");
        auto& box = direct->Ui().Root().Add<DestructionWitness>(treeDestroyed);
        auto dispatcher = direct->GetDispatcher();
        const auto deletion = direct->Ui().GetHandle(box).Post(
            [&](DestructionWitness& target)
            {
                direct.reset();
                Check(!treeDestroyed, "manual draining retains the receiver until its active callback returns");
                target.SetId("safe-through-direct-dispatch");
            });
        dispatcher.Drain();
        deletion.RethrowIfFailed();
        Check(!direct && treeDestroyed && deletion.GetStatus() == DispatchStatus::Completed,
              "manual dispatcher draining releases native ownership after its callback and drain guards finish");
    }
    for (const bool clearChildren : {false, true})
    {
        bool treeDestroyed = false;
        auto direct = std::make_unique<Window>(options, assets / "NotoSans.ttf");
        direct->Ui().Root().Add<DestructionWitness>(treeDestroyed);
        auto& parent = direct->Ui().Root().Add<Widget>();
        auto& child = parent.Add<Widget>();
        const auto childTarget = direct->Ui().GetHandle(child);
        std::stop_callback deleteOnRemoval(childTarget.GetTargetStopToken(),
                                           [&]
                                           {
                                               direct.reset();
                                               Check(!treeDestroyed,
                                                     "child removal retains the containing UI through stop callbacks");
                                           });
        if (clearChildren)
            parent.ClearChildren();
        else
            parent.RemoveChild(child);
        Check(!direct && treeDestroyed, "child removal releases window services after structural cleanup");
    }
    {
        bool treeDestroyed = false;
        auto direct = std::make_unique<Window>(options, assets / "NotoSans.ttf");
        direct->Ui().Root().Add<DestructionWitness>(treeDestroyed);
        auto& numeric = direct->Ui().Root().Add<NumericInput>();
        numeric.GetEditor().TextChanged().Connect(
            [&](const std::string&)
            {
                direct.reset();
                Check(!treeDestroyed, "compound input keeps its services through nested editor signals");
            });
        numeric.SetValue(42);
        Check(!direct && treeDestroyed, "compound input releases window services at its outer setter boundary");
    }
    {
        Window clockProbe(options, assets / "NotoSans.ttf");
        std::string clockError;
        Check(clockProbe.Ui().SetStyleSheet("window { background: black; padding: 0; }", clockError), clockError);
        auto& box = clockProbe.Ui().Root().Add<Widget>();
        box.SetHeight(SizeRule::Fill());
        const auto boxTarget = clockProbe.Ui().GetHandle(box);
        const auto postedAnimation = boxTarget.Post(
            [other = second.get()](Widget& target)
            {
                AnimationOptions animationOptions;
                animationOptions.m_Duration = 1;
                animationOptions.m_Easing = EasingCurve(EasingCurve::Kind::Linear);
                Animation animation(animationOptions);
                animation.AddStyleProperty(target, StyleProperty::Background, Color{1, 0, 0, 1}, Color{0, 0, 1, 1});
                animation.AddCustom(target, "change-current-context",
                                    [other](Widget&, double) { other->RenderFrame(); });
                target.GetContext()->Animations().Play(std::move(animation));
            });
        clockProbe.RenderFrame(1);
        Check(postedAnimation.GetStatus() == DispatchStatus::Completed,
              "queued animation starts at the frame's current GUI time");
        const auto start = clockProbe.Graphics().ReadPixel({5, 5});
        Check(start.m_R > .9f && start.m_B < .1f, "an animation posted after idle starts at its initial endpoint");
        const auto contextAfterClock = boxTarget.Post(
            [&](Widget&)
            {
                Check(reinterpret_cast<std::uintptr_t>(glfwGetCurrentContext()) == clockProbe.GetContextIdentity(),
                      "clock callbacks cannot redirect queued UI work to another window's GL context");
            });
        clockProbe.RenderFrame(1.5);
        contextAfterClock.RethrowIfFailed();
        Check(contextAfterClock.GetStatus() == DispatchStatus::Completed,
              "queued work runs after the window restores its GL context");
        const auto middle = clockProbe.Graphics().ReadPixel({5, 5});
        Check(middle.m_R > .45f && middle.m_R < .55f && middle.m_B > .45f && middle.m_B < .55f,
              "posted animation timing produces the actual half-duration framebuffer blend");
        clockProbe.RenderFrame(2);
        Check(clockProbe.Graphics().ReadPixel({5, 5}).m_B > .9f, "posted animation reaches its exact final endpoint");
    }
    PaintState painted;
    first->Ui().Root().Add<PaintCounter>(painted, true);
    auto& firstLabel = first->Ui().Root().Add<Label>("Waiting for worker");
    second->Ui().Root().Add<PaintCounter>(painted, false);
    auto& secondLabel = second->Ui().Root().Add<Label>("Second window stays responsive");
    std::string error;
    Check(first->Ui().SetStyleSheet("window { background: red; color: white; padding: 0; } label { height: 24px; }",
                                    error),
          error);
    Check(second->Ui().SetStyleSheet("window { background: green; color: white; padding: 0; } label { height: 24px; }",
                                     error),
          error);
    first->RenderFrame(20);
    first->SetSize(340, 190);
    first->RenderFrame();
    Check(first->Ui().GetTime() >= 20, "native callbacks do not move a deterministic GUI clock backwards");
    second->RenderFrame();
    Check(application.GetWindowCount() == 3, "one application observes all three windows");
    const auto firstTarget = first->GetHandle();
    const auto secondTarget = second->GetHandle();
    const auto transientTarget = transient->GetHandle();
    const auto firstText = first->Ui().GetHandle(firstLabel);
    const auto secondText = second->Ui().GetHandle(secondLabel);
    auto& removed = first->Ui().Root().Add<Label>("Removed before worker delivery");
    const auto removedTarget = first->Ui().GetHandle(removed);
    first->Ui().Root().RemoveChild(removed);
    std::atomic<bool> ownerDiagnostic = false;
    std::atomic<bool> survivedFirstClose = false;
    std::atomic<bool> nestedLoopDiagnostic = false;
    std::exception_ptr workerError;
    std::promise<void> taskDelivered;
    auto taskReady = taskDelivered.get_future();
    const auto guiThread = std::this_thread::get_id();
    const auto pooledTask = application.Tasks().Run(
        firstText,
        [guiThread]
        {
            Check(std::this_thread::get_id() != guiThread, "application-owned tasks compute on a worker");
            return std::string("Application-owned background result");
        },
        [&](Label& label, std::string result)
        {
            label.SetText(std::move(result));
            taskDelivered.set_value();
        });

    std::jthread worker(
        [&]
        {
            try
            {
                try
                {
                    second->SetSize(100, 100);
                }
                catch (const std::logic_error&)
                {
                    ownerDiagnostic.store(true);
                }
                const auto expired =
                    removedTarget.Post([](Label&) { throw std::logic_error("Dead receiver executed."); });
                Check(expired.WaitFor(5s) && expired.GetStatus() == DispatchStatus::TargetExpired,
                      "queued target resolution rejects a removed widget");
                Delivered(transientTarget.Post([&](Window&) { transient.reset(); }));
                Check(transientTarget.Post([](Window&) {}).GetStatus() == DispatchStatus::Rejected,
                      "a window endpoint rejects work after callback-driven destruction");
                int firstBefore = 0;
                const auto taskOutcome = taskReady.wait_for(5s);
                pooledTask.RethrowIfFailed();
                Check(taskOutcome == std::future_status::ready,
                      "application-owned tasks deliver while the native event loop waits");
                Delivered(firstTarget.Post(
                    [&](Window& window)
                    {
                        Check(pooledTask.GetStatus() == TaskStatus::Completed &&
                                  firstLabel.GetText() == "Application-owned background result",
                              "application-owned task completion updates its live UI target");
                        firstBefore = painted.Count(true);
                        Check(application.GetWindowCount() == 2, "deferred native retirement removes only its target");
                        std::string cssError;
                        Check(window.Ui().SetStyleSheet("window { background: blue; color: white; padding: 0; } "
                                                        "label { height: 24px; }",
                                                        cssError),
                              cssError);
                        try
                        {
                            application.Run();
                        }
                        catch (const std::logic_error&)
                        {
                            nestedLoopDiagnostic.store(true);
                        }
                    }));
                painted.WaitAfter(true, firstBefore);
                Delivered(firstTarget.Post(
                    [&](Window& window)
                    {
                        const auto pixel = window.Graphics().ReadPixel({5, 5});
                        Check(pixel.m_B > .9f && pixel.m_R < .1f,
                              "first queued model update reaches actual GPU pixels");
                        window.Graphics().SavePpm(output / "foundation-window-first.ppm");
                        window.Close();
                    }));
                int secondBefore = 0;
                Delivered(secondText.Post([](Label& label) { label.SetText("Still responsive after first closes"); }));
                Delivered(secondTarget.Post(
                    [&](Window& window)
                    {
                        secondBefore = painted.Count(false);
                        survivedFirstClose.store(first->IsClosed() && application.GetWindowCount() == 1);
                        std::string cssError;
                        Check(window.Ui().SetStyleSheet("window { background: #ff00ff; color: white; padding: 0; } "
                                                        "label { height: 24px; }",
                                                        cssError),
                              cssError);
                    }));
                painted.WaitAfter(false, secondBefore);
                Delivered(secondTarget.Post(
                    [&](Window& window)
                    {
                        const auto pixel = window.Graphics().ReadPixel({5, 5});
                        Check(pixel.m_R > .9f && pixel.m_B > .9f && pixel.m_G < .1f,
                              "remaining window continues changing its actual framebuffer");
                        window.Graphics().SavePpm(output / "foundation-window-second.ppm");
                        window.Close();
                    }));
            }
            catch (...)
            {
                workerError = std::current_exception();
                firstTarget.Post([](Window& window) { window.Close(); });
                secondTarget.Post([](Window& window) { window.Close(); });
                transientTarget.Post([](Window& window) { window.Close(); });
                glfwPostEmptyEvent();
            }
        });
    application.Run();
    worker.join();
    if (workerError)
        std::rethrow_exception(workerError);
    Check(ownerDiagnostic.load(), "wrong-thread native mutation fails before calling GLFW");
    Check(nestedLoopDiagnostic.load(), "nested application loops are rejected");
    Check(survivedFirstClose.load(), "closing one window leaves the other interactive");
    Check(application.GetWindowCount() == 0, "the application exits when its final window closes");
    std::ofstream report(output / "application-foundation.txt");
    report << "Application foundation: PASS\nDriver: " << second->Graphics().GetDriverDescription() << '\n'
           << "One native loop serviced two independently changing UI windows plus a transient window.\n"
           << "Worker publication woke idle delivery; removed widget delivery expired safely.\n"
           << "The lazy application-owned task pool computed off-thread and delivered its owned result.\n"
           << "Callback-driven window destruction was deferred; its endpoint rejected later work.\n"
           << "A cancellation observer safely destroyed its directly closing native window.\n"
           << "Direct animation setters safely destroyed their owning window without postponing tree cleanup.\n"
           << "Manual dispatcher draining retained deleted-window receivers through callback completion.\n"
           << "First-window closure preserved second-window responsiveness and actual framebuffer updates.\n"
           << "Owner-thread diagnostics, nested-loop rejection, deterministic-clock continuation passed.\n";
}
} // namespace EStudioUI
