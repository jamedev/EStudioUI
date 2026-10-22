#include <EStudioUI/Action.h>
#include <EStudioUI/InputEvent.h>

#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
        throw std::runtime_error(message);
}

template <class Callback> bool Rejects(Callback callback)
{
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        return true;
    }
    return false;
}

void QueuedCommands()
{
    UiDispatcher dispatcher;
    StateStore states(dispatcher);
    auto action = std::make_unique<Action>(states, "Compute");
    action->SetCheckable(true);
    const auto handle = action->GetHandle();
    std::vector<int> values;
    std::vector<DispatchReceipt> receipts;
    int executions = 0;
    action->Triggered().Connect(
        [&]
        {
            ++executions;
            Check(!action->Trigger(), "recursive command execution is suppressed");
        });
    std::thread producer(
        [&]
        {
            for (int i = 0; i < 64; ++i)
                receipts.push_back(handle.Post(
                    [&, i](Action& live)
                    {
                        values.push_back(i);
                        live.Trigger();
                    }));
        });
    producer.join();
    Check(values.empty() && executions == 0, "worker requests do not access owner state before delivery");
    while (dispatcher.Drain())
    {
    }
    Check(values.size() == 64 && executions == 64 && !action->GetSnapshot()->m_Checked,
          "all accepted ordered commands execute once and share canonical toggle state");
    for (int i = 0; i < 64; ++i)
        Check(values[static_cast<std::size_t>(i)] == i &&
                  receipts[static_cast<std::size_t>(i)].GetStatus() == DispatchStatus::Completed,
              "per-producer ordering and completion are preserved");
    bool rejected = false;
    std::thread wrongOwner([&] { rejected = Rejects([&] { action->Trigger(); }); });
    wrongOwner.join();
    Check(rejected, "direct worker execution is rejected before command state changes");
    auto pending = handle.Post([](Action&) { throw std::runtime_error("dead action executed"); });
    action.reset();
    while (dispatcher.Drain())
    {
    }
    Check(!handle.IsConnected() && pending.GetStatus() == DispatchStatus::TargetExpired,
          "queued action requests expire with the public receiver");
}

void DomainAndFailure()
{
    UiDispatcher dispatcher;
    auto states = std::make_unique<StateStore>(dispatcher);
    auto action = std::make_unique<Action>(*states, "Execute");
    const auto handle = action->GetHandle();
    auto connection = action->Triggered().Connect([] { throw std::runtime_error("command failure"); });
    auto failed = handle.Post([](Action& live) { live.Trigger(); });
    dispatcher.Drain();
    Check(failed.GetStatus() == DispatchStatus::Failed && failed.GetError(),
          "queued command errors remain on their receipt");
    connection.Disconnect();
    Check(action->Trigger(), "execution guard is restored after a failed callback");
    auto pending = handle.Post([](Action&) { throw std::runtime_error("closed domain executed"); });
    states.reset();
    dispatcher.Drain();
    Check(action->GetStopToken().stop_requested() && !handle.IsConnected() && !action->Trigger() &&
              pending.GetStatus() == DispatchStatus::TargetExpired,
          "domain shutdown expires every action endpoint");
    Check(Rejects([&] { (void)action->GetSnapshot(); }), "expired property reads are diagnosed");
    Check(!Detail::IsDeliveringInput(), "input boundary begins idle without a native runtime");
    {
        Detail::InputDispatchScope scope;
        Check(Detail::IsDeliveringInput(), "input boundary is visible to scheduler guards");
        Check(Rejects([] { Detail::InputDispatchScope nested; }), "nested input boundary is rejected");
    }
    Check(!Detail::IsDeliveringInput(), "input boundary restores after nested rejection");
}
} // namespace

int main()
{
    try
    {
        QueuedCommands();
        DomainAndFailure();
        std::cout << s_Checks << " headless action-domain checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Action domain: " << error.what() << '\n';
        return 1;
    }
}
