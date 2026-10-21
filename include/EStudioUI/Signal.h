#pragma once

#include <algorithm>
#include <functional>
#include <memory>
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

    //! Disconnects the callback; safe during signal delivery.
    void Disconnect() const
    {
        if (const auto active = m_Active.lock())
            *active = false;
    }

    //! Returns whether both the signal and callback are still connected.
    bool IsConnected() const
    {
        const auto active = m_Active.lock();
        return active && *active;
    }

  private:
    template <typename...> friend class Signal;

    //! Creates a handle to a signal-owned activation flag.
    explicit Connection(const std::shared_ptr<bool>& active) : m_Active(active)
    {
    }

    //! Observes activation without retaining the signal or callback.
    std::weak_ptr<bool> m_Active;
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

//! Delivers typed callbacks on the GUI thread, without allocations per emission.
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
        Prune(*m_State);
        auto slot = std::make_shared<Slot>();
        slot->m_Callback = std::forward<Callback>(callback);
        m_State->m_Slots.push_back(slot);
        return Connection(slot->m_Active);
    }

    //! Invokes active callbacks, including when a callback destroys the sender.
    void Emit(Args... args) const
    {
        const auto state = m_State;

        struct Emission
        {
            State& m_State;

            explicit Emission(State& state) : m_State(state)
            {
                ++m_State.m_Depth;
            }

            ~Emission()
            {
                --m_State.m_Depth;
                Signal::Prune(m_State);
            }
        } emission(*state);

        const auto count = state->m_Slots.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto slot = state->m_Slots[i];
            if (*slot->m_Active)
                slot->m_Callback(args...);
        }
    }

    //! Disconnects all slots, including callbacks pending in an active emission.
    void DisconnectAll()
    {
        for (const auto& slot : m_State->m_Slots)
            *slot->m_Active = false;
        Prune(*m_State);
    }

  private:
    //! Stores a callback separately from the vector for stable delivery lifetime.
    struct Slot
    {
        //! Controls whether the callback participates in future delivery.
        std::shared_ptr<bool> m_Active = std::make_shared<bool>(true);

        //! Holds the user's typed callback.
        std::function<void(Args...)> m_Callback;
    };

    //! Survives destruction of the sender during delivery.
    struct State
    {
        //! Preserves callback registration order.
        std::vector<std::shared_ptr<Slot>> m_Slots;

        //! Defers removal until nested emissions finish.
        unsigned m_Depth = 0;
    };

    //! Releases disconnected callbacks when no delivery indexes are active.
    static void Prune(State& state)
    {
        if (state.m_Depth == 0)
            std::erase_if(state.m_Slots, [](const auto& slot) { return !*slot->m_Active; });
    }

    //! Retains delivery state independently of the sender's lifetime.
    std::shared_ptr<State> m_State;
};
} // namespace EStudioUI
