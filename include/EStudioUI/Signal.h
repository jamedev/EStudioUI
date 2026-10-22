#pragma once

#include "UiHandle.h"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace EStudioUI
{
//! A non-owning connection handle; destruction leaves the connection active.
class Connection
{
  public:
    //! Creates an empty connection.
    Connection() = default;

    //! Disconnects from any thread; a callback already selected for execution may finish.
    void Disconnect() const noexcept
    {
        if (const auto active = m_Active.lock())
        {
            active->store(false, std::memory_order_release);
            if (m_Prune)
            {
                try
                {
                    m_Prune();
                }
                catch (...)
                {
                    // Inactive slots remain safe if allocation for registry cleanup fails.
                }
            }
        }
    }

    //! Returns whether both the signal and callback are still connected.
    bool IsConnected() const
    {
        const auto active = m_Active.lock();
        return !m_Owner.expired() && active && active->load(std::memory_order_acquire);
    }

  private:
    template <typename...> friend class Signal;

    //! Creates a handle to a signal-owned activation flag.
    explicit Connection(const std::shared_ptr<std::atomic_bool>& active, const std::shared_ptr<void>& owner,
                        std::function<void()> prune)
        : m_Active(active), m_Owner(owner), m_Prune(std::move(prune))
    {
    }

    //! Observes activation without retaining the signal or callback.
    std::weak_ptr<std::atomic_bool> m_Active;

    //! Detects sender destruction even while a queued command retains its callback.
    std::weak_ptr<void> m_Owner;

    //! Prunes disconnected captures without retaining the sender or its registry.
    std::function<void()> m_Prune;
};

//! Disconnects a connection when its owner leaves scope.
class ScopedConnection
{
  public:
    //! Takes responsibility for a connection.
    explicit ScopedConnection(Connection connection) : m_Connection(std::move(connection))
    {
    }

    //! Disconnects the owned callback.
    ~ScopedConnection()
    {
        m_Connection.Disconnect();
    }

    //! Prevents duplicated ownership.
    ScopedConnection(const ScopedConnection&) = delete;

    //! Prevents duplicated ownership.
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    //! Transfers disconnection responsibility.
    ScopedConnection(ScopedConnection&& other) noexcept : m_Connection(std::exchange(other.m_Connection, Connection{}))
    {
    }

    //! Transfers ownership after disconnecting the previous callback.
    ScopedConnection& operator=(ScopedConnection&& other) noexcept
    {
        if (this != &other)
        {
            m_Connection.Disconnect();
            m_Connection = std::exchange(other.m_Connection, Connection{});
        }
        return *this;
    }

  private:
    //! Owns disconnection responsibility, without retaining the sender.
    Connection m_Connection;
};

namespace Detail
{
//! Identifies standard nonowning payload wrappers that cannot cross a queued boundary implicitly.
template <typename T> struct BorrowedSignalArgument : std::false_type
{
};

//! Rejects borrowed character ranges even when the source outlives Emit only briefly.
template <typename Character, typename Traits>
struct BorrowedSignalArgument<std::basic_string_view<Character, Traits>> : std::true_type
{
};

//! Rejects borrowed contiguous ranges.
template <typename T, std::size_t Extent> struct BorrowedSignalArgument<std::span<T, Extent>> : std::true_type
{
};

//! Rejects explicitly borrowed references.
template <typename T> struct BorrowedSignalArgument<std::reference_wrapper<T>> : std::true_type
{
};

//! Rejects initializer-list storage whose backing array may already have expired.
template <typename T> struct BorrowedSignalArgument<std::initializer_list<T>> : std::true_type
{
};

//! Requires copyable owning top-level values for broadcasting into queued commands.
template <typename T>
inline constexpr bool s_OwnedSignalArgument =
    std::is_copy_constructible_v<std::decay_t<T>> && !std::is_pointer_v<std::decay_t<T>> &&
    !BorrowedSignalArgument<std::decay_t<T>>::value;
} // namespace Detail

//! Publishes typed callbacks through an immutable registry; direct slots run in the emitter's thread.
template <typename... Args> class Signal
{
  public:
    //! Creates an independent signal.
    Signal() : m_State(std::make_shared<State>())
    {
    }

    //! Signals belong to their emitting objects.
    Signal(const Signal&) = delete;

    //! Signals belong to their emitting objects.
    Signal& operator=(const Signal&) = delete;

    //! Connects a callback; newly connected slots wait for the next emission.
    template <typename Callback> Connection Connect(Callback&& callback)
    {
        auto slot = std::make_shared<Slot>();
        slot->m_Callback = std::forward<Callback>(callback);
        return AddSlot(std::move(slot));
    }

    //! Queues owned argument copies to a live UI target; disconnecting suppresses pending delivery.
    template <typename T, typename Callback>
        requires((Detail::s_OwnedSignalArgument<Args> && ...) &&
                 std::invocable<std::decay_t<Callback>&, T&, std::decay_t<Args>...>)
    Connection Connect(UiHandle<T> target, Callback&& callback)
    {
        return ConnectQueued(std::move(target), std::forward<Callback>(callback));
    }

    //! Queues target delivery and reports its receipt to an emitter-thread observer.
    template <typename T, typename Callback>
        requires((Detail::s_OwnedSignalArgument<Args> && ...) &&
                 std::invocable<std::decay_t<Callback>&, T&, std::decay_t<Args>...>)
    Connection Connect(UiHandle<T> target, Callback&& callback, std::function<void(DispatchReceipt)> observer)
    {
        return ConnectQueued(std::move(target), std::forward<Callback>(callback), std::move(observer));
    }

    //! Always queues target delivery, including emissions from the target's owning thread.
    template <typename T, typename Callback>
        requires((Detail::s_OwnedSignalArgument<Args> && ...) &&
                 std::invocable<std::decay_t<Callback>&, T&, std::decay_t<Args>...>)
    Connection ConnectQueued(UiHandle<T> target, Callback&& callback)
    {
        return ConnectQueued(std::move(target), std::forward<Callback>(callback), {});
    }

    //! Reports each post receipt to an emitter-thread observer that owns rejection/error policy.
    template <typename T, typename Callback>
        requires((Detail::s_OwnedSignalArgument<Args> && ...) &&
                 std::invocable<std::decay_t<Callback>&, T&, std::decay_t<Args>...>)
    Connection ConnectQueued(UiHandle<T> target, Callback&& callback, std::function<void(DispatchReceipt)> observer)
    {
        using Payload = std::tuple<std::decay_t<Args>...>;
        auto slot = std::make_shared<Slot>();
        const std::weak_ptr<std::atomic_bool> active = slot->m_Active;
        const std::weak_ptr<void> owner = m_Lifetime;
        const std::weak_ptr<State> registry = m_State;
        std::function<void()> stop = [active]
        {
            if (const auto current = active.lock())
                current->store(false, std::memory_order_release);
        };
        slot->m_TargetStop = std::make_unique<StopRegistration>(target.GetTargetStopToken(), stop);
        if (target.GetTargetKey())
            slot->m_DispatcherStop = std::make_unique<StopRegistration>(target.GetDispatcher().GetStopToken(), stop);
        if (!target.IsConnected())
            slot->m_Active->store(false, std::memory_order_release);
        auto listener = std::make_shared<std::decay_t<Callback>>(std::forward<Callback>(callback));
        slot->m_Callback = [target = std::move(target), listener = std::move(listener), active, owner, registry,
                            observer = std::move(observer)](Args... args)
        {
            const auto connected = active.lock();
            if (owner.expired() || !connected || !connected->load(std::memory_order_acquire))
                return;
            auto payload = std::make_shared<Payload>(args...);
            auto receipt = target.Post(
                [listener, payload = std::move(payload), active, owner](T& receiver)
                {
                    const auto current = active.lock();
                    if (owner.expired() || !current || !current->load(std::memory_order_acquire))
                        return;
                    std::apply([&](auto&... values) { std::invoke(*listener, receiver, std::move(values)...); },
                               *payload);
                });
            receipt.OnFinished(
                [active, owner, registry, target](DispatchStatus status, std::exception_ptr)
                {
                    if (status == DispatchStatus::TargetExpired || owner.expired() || !target.IsConnected())
                        Retire(active, registry);
                });
            if (observer)
                observer(std::move(receipt));
            else
                CheckPost(receipt);
        };
        return AddSlot(std::move(slot));
    }

    //! Invokes one stable snapshot without registry allocation or callback locks; direct callbacks synchronize
    //! themselves.
    void Emit(Args... args) const
    {
        const auto state = m_State;

        const auto slots = state->m_Slots.load(std::memory_order_acquire);
        for (const auto& slot : *slots)
        {
            if (slot->m_Active->load(std::memory_order_acquire))
                slot->m_Callback(args...);
        }
    }

    //! Returns whether at least one listener is active.
    bool HasConnections() const
    {
        const auto slots = m_State->m_Slots.load(std::memory_order_acquire);
        return std::any_of(slots->begin(), slots->end(),
                           [](const auto& slot) { return slot->m_Active->load(std::memory_order_acquire); });
    }

    //! Disconnects all slots, including callbacks pending in an active emission.
    void DisconnectAll()
    {
        const auto state = m_State;
        std::shared_ptr<const SlotList> slots;
        {
            std::lock_guard lock(state->m_Mutex);
            slots = state->m_Slots.load(std::memory_order_acquire);
            for (const auto& slot : *slots)
                slot->m_Active->store(false, std::memory_order_release);
            state->m_Slots.store(state->m_Empty, std::memory_order_release);
        }
    }

  private:
    //! Retires logical delivery on target/endpoint shutdown without running application cleanup inside a stop callback.
    using StopRegistration = std::stop_callback<std::function<void()>>;

    //! Stores a callback separately from the vector for stable delivery lifetime.
    struct Slot
    {
        //! Controls whether the callback participates in future delivery.
        std::shared_ptr<std::atomic_bool> m_Active = std::make_shared<std::atomic_bool>(true);

        //! Holds the user's typed callback.
        std::function<void(Args...)> m_Callback;

        //! Stops delivery immediately when a receiver lifetime token expires.
        std::unique_ptr<StopRegistration> m_TargetStop;

        //! Stops delivery immediately when the UI dispatcher closes.
        std::unique_ptr<StopRegistration> m_DispatcherStop;
    };

    //! Shares immutable connection order among simultaneous emissions.
    using SlotList = std::vector<std::shared_ptr<Slot>>;

    //! Survives destruction of the sender during delivery.
    struct State
    {
        //! Serializes copy-on-write publication while callbacks execute without this lock.
        std::mutex m_Mutex;

        //! Reuses an allocation-free empty snapshot when all registrations are removed.
        std::shared_ptr<const SlotList> m_Empty = std::make_shared<const SlotList>();

        //! Publishes stable callback snapshots without allocating during direct emission.
        std::atomic<std::shared_ptr<const SlotList>> m_Slots = m_Empty;
    };

    //! Publishes a new slot and prunes inactive registrations outside all active snapshots.
    Connection AddSlot(std::shared_ptr<Slot> slot)
    {
        const auto state = m_State;
        std::shared_ptr<const SlotList> slots;
        {
            std::lock_guard lock(state->m_Mutex);
            slots = state->m_Slots.load(std::memory_order_acquire);
            auto next = std::make_shared<SlotList>();
            next->reserve(slots->size() + 1);
            for (const auto& current : *slots)
                if (current->m_Active->load(std::memory_order_acquire))
                    next->push_back(current);
            if (slot->m_Active->load(std::memory_order_acquire))
                next->push_back(slot);
            state->m_Slots.store(std::move(next), std::memory_order_release);
        }
        return Connection(slot->m_Active, m_Lifetime,
                          [weak = std::weak_ptr<State>(state)]
                          {
                              if (const auto live = weak.lock())
                                  Prune(live);
                          });
    }

    //! Retires inactive captures while active emission snapshots retain their own delivery lifetimes.
    static void Prune(const std::shared_ptr<State>& state)
    {
        std::shared_ptr<const SlotList> slots;
        {
            std::lock_guard lock(state->m_Mutex);
            slots = state->m_Slots.load(std::memory_order_acquire);
            const auto count =
                static_cast<std::size_t>(std::count_if(slots->begin(), slots->end(), [](const auto& slot)
                                                       { return slot->m_Active->load(std::memory_order_acquire); }));
            if (!count)
                state->m_Slots.store(state->m_Empty, std::memory_order_release);
            else if (count != slots->size())
            {
                auto next = std::make_shared<SlotList>();
                next->reserve(count);
                for (const auto& current : *slots)
                    if (current->m_Active->load(std::memory_order_acquire))
                        next->push_back(current);
                state->m_Slots.store(std::move(next), std::memory_order_release);
            }
        }
    }

    //! Ends a receiver-bound subscription from a receipt completion without touching its UI target.
    static void Retire(const std::weak_ptr<std::atomic_bool>& active, const std::weak_ptr<State>& registry) noexcept
    {
        if (const auto current = active.lock())
            if (current->exchange(false, std::memory_order_acq_rel))
                if (const auto state = registry.lock())
                {
                    try
                    {
                        Prune(state);
                    }
                    catch (...)
                    {
                        // Disconnection remains effective even if inactive-slot cleanup cannot allocate.
                    }
                }
    }

    //! Makes bounded-queue overload visible while normal target shutdown suppresses delivery.
    static void CheckPost(const DispatchReceipt& receipt)
    {
        if (receipt.GetStatus() == DispatchStatus::Rejected &&
            receipt.GetRejectionReason() == DispatchRejectionReason::Capacity)
            throw std::runtime_error("Queued signal delivery exceeded the UI queue capacity.");
    }

    //! Retains delivery state independently of the sender's lifetime.
    std::shared_ptr<State> m_State;

    //! Ends queued subscriptions immediately when the sender dies while active direct emissions finish.
    std::shared_ptr<void> m_Lifetime = std::make_shared<int>(0);
};
} // namespace EStudioUI
