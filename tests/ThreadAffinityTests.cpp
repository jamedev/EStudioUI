#include <EStudioUI/Label.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/ThreadAffinity.h>
#include <EStudioUI/UiContext.h>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const std::string& message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Callback> bool Rejects(Callback&& callback)
{
    try
    {
        callback();
        return false;
    }
    catch (const std::logic_error&)
    {
        return true;
    }
}

template <typename Callback> bool RejectsWorker(Callback&& callback)
{
    bool rejected = false;
    std::exception_ptr unexpected;
    std::thread worker(
        [&]
        {
            try
            {
                rejected = Rejects(callback);
            }
            catch (...)
            {
                unexpected = std::current_exception();
            }
        });
    worker.join();
    if (unexpected)
        std::rethrow_exception(unexpected);
    return rejected;
}

class LayoutProbe final : public Widget
{
  public:
    LayoutProbe()
    {
        SetHeight(SizeRule::Fixed(60));
        m_Child = &Add<Widget>();
    }

    UiPhase m_Observed = UiPhase::Idle;
    bool m_StructuralRejected = false;
    bool m_DrainRejected = false;
    DispatchReceipt m_Deferred;

  protected:
    void OnLayout() override
    {
        if (!m_First)
            return;
        m_First = false;
        auto& context = *GetContext();
        m_Observed = context.GetPhase();
        m_StructuralRejected = Rejects([&] { GetParent()->Add<Widget>(); });
        m_DrainRejected = Rejects([&] { context.GetDispatcher().Drain(); });
        m_Child->SetFloatingBounds(GetContentBounds());
        m_Deferred = context.Defer(
            [](UiContext& live)
            {
                auto& child = live.Root().Add<Widget>();
                child.SetId("deferred-child");
                child.SetHeight(SizeRule::Fixed(10));
            });
    }

  private:
    Widget* m_Child = nullptr;
    bool m_First = true;
};

class ThrowingMeasure final : public Widget
{
  public:
    mutable bool m_First = true;
    mutable UiPhase m_Observed = UiPhase::Idle;

  protected:
    Point Measure(FontFace&) const override
    {
        if (m_First)
        {
            m_First = false;
            m_Observed = GetContext()->GetPhase();
            GetParent()->Add<Widget>();
        }
        return {20, 20};
    }
};

class SurfaceProbe final : public Widget
{
  public:
    bool m_Throw = true;
    UiPhase m_PreparePhase = UiPhase::Idle;
    UiPhase m_SubmitPhase = UiPhase::Idle;
    bool m_PrepareRejected = false;
    bool m_SubmitRejected = false;
    mutable bool m_QueryRejected = false;
    mutable UiPhase m_QueryPhase = UiPhase::Idle;

  protected:
    void PrepareFrame(double, float, float) override
    {
        m_PreparePhase = GetContext()->GetPhase();
        m_PrepareRejected = Rejects([&] { GetParent()->ClearChildren(); });
        if (m_Throw)
        {
            m_Throw = false;
            throw std::runtime_error("scene preparation failure");
        }
    }

    void FrameSubmitted() override
    {
        m_SubmitPhase = GetContext()->GetPhase();
        m_SubmitRejected = Rejects([&] { GetParent()->ClearChildren(); });
    }

    bool NeedsContinuousRendering() const override
    {
        m_QueryPhase = GetContext()->GetPhase();
        m_QueryRejected = Rejects([&] { GetParent()->ClearChildren(); });
        return false;
    }
};

void DirectAccess(FontFace& font)
{
    ThreadAffinity affinity;
    Check(affinity.IsOwnerThread() && affinity.GetOwnerThreadId() == std::this_thread::get_id(),
          "thread affinity captures its creating owner");
    Check(RejectsWorker([&] { affinity.VerifyAccess(); }), "owner diagnostics reject worker access");
    UiContext ui(font);
    auto& widget = ui.Root().Add<Widget>();
    widget.SetId("owner");
    const WidgetRef reference(&widget);
    ui.Prepare(400, 200);
    const auto bounds = widget.GetBounds();
    Check(RejectsWorker([&] { widget.SetId("worker"); }), "widget mutation rejects before touching owner state");
    Check(widget.GetId() == "owner", "rejected mutation retains the original value");
    Check(RejectsWorker([&] { (void)widget.GetBounds(); }), "widget getters enforce owner access");
    Check(widget.GetBounds() == bounds, "rejected reads do not change layout");
    Check(RejectsWorker([&] { ui.RequestRepaint(); }), "repaint is not an implicit cross-thread dispatch API");
    Check(RejectsWorker([&] { ui.PointerDown({20, 20}); }), "input dispatch rejects the wrong thread at entry");
    Check(RejectsWorker([&] { (void)ui.GetDispatcher(); }), "producer endpoints are captured before leaving the owner");
    Check(RejectsWorker([&] { (void)reference.Get(); }), "weak observers cannot expose raw widgets to workers");
    Check(RejectsWorker([&] { WidgetRef wrong(&widget); }), "observer creation does not read a foreign widget");
    ui.Root().RemoveChild(widget);
    Check(!reference.Get(), "owner-side weak observation expires after deletion");
    Check(RejectsWorker([&] { (void)reference.Get(); }),
          "expired observers reject foreign dereference using copied identity only");
    const WidgetRef empty;
    bool emptyNull = false;
    std::thread emptyWorker([&] { emptyNull = !empty.Get(); });
    emptyWorker.join();
    Check(emptyNull, "empty observers remain harmless on any thread");
}

void Handles(FontFace& font)
{
    UiHandle<Label> surviving;
    DispatchReceipt pending;
    UiDispatcher retained;
    {
        UiContext ui(font);
        auto& label = ui.Root().Add<Label>("before");
        surviving = ui.GetHandle(label);
        retained = ui.GetDispatcher();
        bool stoppedAfterExpiry = false;
        const WidgetRef lifetime(&label);
        std::stop_callback cancellation(surviving.GetTargetStopToken(), [&] { stoppedAfterExpiry = !lifetime.Get(); });
        DispatchReceipt delivered;
        std::thread worker(
            [&]
            {
                delivered =
                    surviving.Post([value = std::string("owned worker result")](Label& live) { live.SetText(value); });
            });
        worker.join();
        Check(label.GetText() == "before" && delivered.GetStatus() == DispatchStatus::Pending,
              "worker publication remains queued until an explicit owner boundary");
        retained.Drain();
        Check(label.GetText() == "owned worker result" && delivered.GetStatus() == DispatchStatus::Completed,
              "a typed target receives owned data on its UI owner");
        const auto firstKey = surviving.GetTargetKey();
        const auto expired = surviving.Post([](Label&) { throw std::runtime_error("dead target executed"); });
        ui.Root().RemoveChild(label);
        Check(surviving.GetTargetStopToken().stop_requested() && stoppedAfterExpiry,
              "receiver cancellation starts only after weak resolution has expired");
        auto& replacement = ui.Root().Add<Label>("replacement");
        Check(ui.GetHandle(replacement).GetTargetKey() != firstKey,
              "replacement widgets receive a distinct generation-independent key");
        retained.Drain();
        Check(expired.GetStatus() == DispatchStatus::TargetExpired && replacement.GetText() == "replacement",
              "old posts never target replacement widgets");
        pending = ui.GetHandle(replacement).Post([](Label& live) { live.SetText("too late"); });
        Widget detached;
        Check(Rejects([&] { (void)ui.GetHandle(detached); }), "detached handles require explicit context ownership");
        UiContext other(font);
        Check(Rejects([&] { (void)other.GetHandle(replacement); }), "foreign-context targets cannot be captured");
    }
    Check(retained.IsClosed() && pending.GetStatus() == DispatchStatus::Cancelled,
          "context shutdown closes producers and cancels pending delivery before tree teardown");
    Check(retained.Drain() == 0, "closed endpoints do not invoke a dead context's phase guard");
    Check(surviving.Post([](Label&) {}).GetStatus() == DispatchStatus::Rejected,
          "handles retained after context shutdown reject new work");
}

void TraversalPhases(FontFace& font)
{
    UiContext ui(font);
    auto& probe = ui.Root().Add<LayoutProbe>();
    ui.Prepare(400, 200);
    Check(probe.m_Observed == UiPhase::Layout && probe.m_StructuralRejected,
          "layout hooks reject tree mutation before it can invalidate traversal");
    Check(probe.m_DrainRejected, "queued commits cannot be drained inside layout hooks");
    Check(probe.GetChildren()[0]->GetBounds().m_Width > 0,
          "layout hooks can still assign geometry to existing child controls");
    Check(probe.m_Deferred.GetStatus() == DispatchStatus::Pending && !ui.Root().FindById("deferred-child"),
          "a deferred structural commit waits until layout ends");
    ui.GetDispatcher().Drain();
    Check(probe.m_Deferred.GetStatus() == DispatchStatus::Completed && ui.Root().FindById("deferred-child"),
          "deferred commits execute safely at the next owner boundary");
    Check(ui.GetPhase() == UiPhase::Idle, "the owner returns to Idle after layout and dispatch");
    int committed = 0;
    auto owned = std::make_unique<int>(7);
    const auto moved = ui.Defer([value = std::move(owned), &committed] { committed = *value; });
    ui.GetDispatcher().Drain();
    Check(!owned && committed == 7 && moved.GetStatus() == DispatchStatus::Completed,
          "deferred commits accept simple no-argument work and move-only owned inputs");
    ui.Root().ClearChildren();
    auto& throwing = ui.Root().Add<ThrowingMeasure>();
    Check(Rejects([&] { ui.Prepare(400, 200); }), "measurement cannot mutate the traversed tree");
    Check(throwing.m_Observed == UiPhase::Measure && ui.GetPhase() == UiPhase::Idle,
          "measurement failure restores the preceding phase");
    ui.Prepare(400, 200);
    Check(ui.GetPhase() == UiPhase::Idle, "a later preparation succeeds after a hook failure");
    ui.Root().ClearChildren();
    auto& surface = ui.Root().Add<SurfaceProbe>();
    surface.SetHeight(SizeRule::Fixed(50));
    ui.Prepare(400, 200);
    bool threw = false;
    try
    {
        ui.PrepareSurfaces(0, 1, 1);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    Check(threw && surface.m_PreparePhase == UiPhase::SurfacePreparation && surface.m_PrepareRejected &&
              ui.GetPhase() == UiPhase::Idle,
          "surface callbacks are structurally guarded and restore the phase on failure");
    ui.PrepareSurfaces(0, 1, 1);
    ui.FrameSubmitted();
    Check(surface.m_SubmitPhase == UiPhase::Submission && surface.m_SubmitRejected && ui.GetPhase() == UiPhase::Idle,
          "surface-consumer submission preserves tree traversal and the idle boundary");
    Check(!ui.HasAnimations() && surface.m_QueryRejected && surface.m_QueryPhase == UiPhase::Inspection &&
              ui.GetPhase() == UiPhase::Idle,
          "read-only surface queries cannot invalidate their traversal");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        DirectAccess(font);
        Handles(font);
        TraversalPhases(font);
        std::cout << "Thread affinity and UI lifetime PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Thread affinity and UI lifetime FAIL / " << error.what() << '\n';
        return 1;
    }
}
