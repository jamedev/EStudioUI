#pragma once

#include "Signal.h"
#include "StateStore.h"

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace EStudioUI
{
//! Owns the immutable before/after snapshots of one value in a coherent store revision.
template <typename T> class StateChange
{
  public:
    //! Captures two prepared immutable snapshots without copying the application value.
    StateChange(std::shared_ptr<const T> before, std::shared_ptr<const T> after, std::uint64_t revision)
        : m_Before(std::move(before)), m_After(std::move(after)), m_Revision(revision)
    {
    }

    //! Returns the value before this commit.
    const T& GetBefore() const noexcept
    {
        return *m_Before;
    }

    //! Returns the value after this commit, even when later queued commits have completed.
    const T& GetAfter() const noexcept
    {
        return *m_After;
    }

    //! Returns an owned immutable snapshot of the previous value.
    std::shared_ptr<const T> GetBeforeSnapshot() const noexcept
    {
        return m_Before;
    }

    //! Returns an owned immutable snapshot of the published value.
    std::shared_ptr<const T> GetAfterSnapshot() const noexcept
    {
        return m_After;
    }

    //! Returns the shared store revision of this commit.
    std::uint64_t GetRevision() const noexcept
    {
        return m_Revision;
    }

  private:
    //! Owns the value before publication.
    std::shared_ptr<const T> m_Before;

    //! Owns the value after publication.
    std::shared_ptr<const T> m_After;

    //! Identifies one coherent multi-value commit.
    std::uint64_t m_Revision;
};

//! A typed owner-thread value with validation, transactional updates, and lifetime-safe publication.
template <typename T> class State
{
    static_assert(!std::is_reference_v<T> && !std::is_pointer_v<std::remove_cv_t<T>> &&
                      !Detail::BorrowedSignalArgument<std::remove_cv_t<T>>::value,
                  "State<T> requires an owned value; replace borrowed pointers/views with owning values or smart pointers.");

  public:
    //! Rejects a proposed value by throwing before any value in the batch is published.
    using Validator = std::function<void(const T&)>;

    //! Defines equality for no-op suppression; the default uses operator==.
    using Equality = std::function<bool(const T&, const T&)>;

    //! Creates a validated value in one store without copying its ownership to callers.
    State(StateStore& store, T initial, Validator validator = {}, Equality equality = {})
    {
        const auto data = store.m_Data;
        auto lease = Detail::AcquireStateLease(data);
        Detail::StateUpdateScope scope(data);
        if (!equality)
        {
            if constexpr (requires(const T& left, const T& right) { { left == right } -> std::convertible_to<bool>; })
                equality = std::equal_to<T>{};
            else
                throw std::invalid_argument("A state value without operator== requires an equality callback.");
        }
        m_Node = std::make_shared<Node>(data, this, std::move(initial), std::move(validator), std::move(equality));
        Detail::ValidateInitialState(data, m_Node);
        Detail::RegisterState(data, m_Node);
    }

    //! Expires queued publications and all subscriptions; destruction must occur on the owner.
    ~State()
    {
        if (m_Node)
        {
            if (!m_Node->m_Dispatcher.IsOwnerThread())
                std::terminate();
            m_Node->Expire();
        }
    }

    //! State objects retain stable identity for outstanding handles and subscriptions.
    State(const State&) = delete;

    //! State objects retain stable identity for outstanding handles and subscriptions.
    State& operator=(const State&) = delete;

    //! Reads the owner value; references expire on its next update or when their enclosing batch ends.
    const T& Get() const
    {
        return *GetSnapshot();
    }

    //! Captures an immutable owned value snapshot for later reads on any thread.
    std::shared_ptr<const T> GetSnapshot() const
    {
        const auto node = m_Node;
        return std::static_pointer_cast<const T>(Detail::ReadState(node->LockStore(), node));
    }

    //! Stages an owned value, returning false when it equals the currently staged candidate.
    bool Set(T value)
    {
        const auto node = m_Node;
        const auto data = node->LockStore();
        Detail::VerifyStateMutationAccess(data);
        auto lease = Detail::AcquireStateLease(data);
        Detail::StateUpdateScope scope(data);
        auto prepared = std::make_shared<const T>(std::move(value));
        return Detail::WriteState(data, node, std::move(prepared));
    }

    //! Returns this value's most recent committed revision.
    std::uint64_t GetRevision() const
    {
        const auto node = m_Node;
        Detail::VerifyStateAccess(node->LockStore());
        return node->m_Revision;
    }

    //! Captures the store's producer endpoint after owner and lifetime checks.
    UiDispatcher GetDispatcher() const
    {
        return Detail::GetStateDispatcher(m_Node->LockStore());
    }

    //! Captures a copyable typed endpoint usable by workers and BackgroundTasks::Run.
    UiHandle<State<T>> GetHandle() const
    {
        const auto node = m_Node;
        Detail::VerifyStateAccess(node->LockStore());
        return UiHandle<State<T>>::ForTarget(
            node->m_Dispatcher, node->m_TargetKey,
            [weak = std::weak_ptr<Node>(node)]() -> State<T>*
            {
                const auto current = weak.lock();
                return current && current->IsAlive() ? current->m_Owner : nullptr;
            },
            node->m_Lifetime.get_token());
    }

    //! Connects an owner-side observer; callback errors are rethrown after the current notification drain.
    template <typename Callback>
        requires std::invocable<std::decay_t<Callback>&, const StateChange<T>&>
    Connection Subscribe(Callback&& callback)
    {
        const auto node = m_Node;
        Detail::VerifyStateAccess(node->LockStore());
        auto listener = std::make_shared<std::decay_t<Callback>>(std::forward<Callback>(callback));
        Detail::VerifyStateMutationAccess(node->LockStore());
        return node->m_Changed.Connect(
            [weak = node->m_Store, listener = std::move(listener)](const StateChange<T>& change)
            {
                try
                {
                    std::invoke(*listener, change);
                }
                catch (...)
                {
                    Detail::RecordStateError(weak, std::current_exception());
                }
            });
    }

    //! Reconciles discarded rollback or net-no-op candidates; callbacks are read-only and errors are contained.
    template <typename Callback>
        requires std::invocable<std::decay_t<Callback>&, const T&>
    Connection SubscribeReconcile(Callback&& callback)
    {
        const auto node = m_Node;
        Detail::VerifyStateMutationAccess(node->LockStore());
        auto listener = std::make_shared<std::decay_t<Callback>>(std::forward<Callback>(callback));
        return node->m_Reconcile.Connect(
            [weak = node->m_Store, listener = std::move(listener)](const T& restored)
            {
                try
                {
                    std::invoke(*listener, restored);
                }
                catch (...)
                {
                    Detail::RecordStateReconciliationError(weak, std::current_exception());
                }
            });
    }

    //! Adds a pure final-candidate validator; registration itself does not validate the current value.
    Connection AddValidator(Validator validator)
    {
        const auto node = m_Node;
        Detail::VerifyStateAccess(node->LockStore());
        Detail::VerifyStateMutationAccess(node->LockStore());
        if (!validator)
            throw std::invalid_argument("A state validator must be callable.");
        return node->m_Validation.Connect(std::move(validator));
    }

    //! Always queues owned change snapshots to a live receiver, following Signal's delivery contract.
    template <typename Target, typename Callback>
        requires std::invocable<std::decay_t<Callback>&, Target&, const StateChange<T>&>
    Connection Subscribe(UiHandle<Target> target, Callback&& callback)
    {
        const auto node = m_Node;
        Detail::VerifyStateAccess(node->LockStore());
        Detail::VerifyStateMutationAccess(node->LockStore());
        return node->m_Changed.Connect(
            std::move(target), std::forward<Callback>(callback),
            [weak = node->m_Store](DispatchReceipt receipt)
            {
                try
                {
                    if (receipt.GetRejectionReason() == DispatchRejectionReason::Capacity)
                        throw std::runtime_error("Queued state delivery exceeded the UI queue capacity.");
                }
                catch (...)
                {
                    Detail::RecordStateError(weak, std::current_exception());
                }
            });
    }

    //! Reports each queued receiver delivery so applications can inspect rejection or callback failure.
    template <typename Target, typename Callback>
        requires std::invocable<std::decay_t<Callback>&, Target&, const StateChange<T>&>
    Connection Subscribe(UiHandle<Target> target, Callback&& callback, std::function<void(DispatchReceipt)> observer)
    {
        const auto node = m_Node;
        Detail::VerifyStateAccess(node->LockStore());
        Detail::VerifyStateMutationAccess(node->LockStore());
        return node->m_Changed.Connect(
            std::move(target), std::forward<Callback>(callback),
            [weak = node->m_Store, observer = std::move(observer)](DispatchReceipt receipt)
            {
                try
                {
                    if (observer)
                        observer(std::move(receipt));
                    else if (receipt.GetRejectionReason() == DispatchRejectionReason::Capacity)
                        throw std::runtime_error("Queued state delivery exceeded the UI queue capacity.");
                }
                catch (...)
                {
                    Detail::RecordStateError(weak, std::current_exception());
                }
            });
    }

  private:
    //! Holds value and observers through destruction of the public state during a callback.
    class Node final : public Detail::StateNode
    {
      public:
        //! Owns all prepared operations and the unique publication endpoint identity.
        Node(const std::shared_ptr<Detail::StateStoreData>& store, State<T>* owner, T initial, Validator validator,
             Equality equality)
            : m_Store(store), m_Dispatcher(Detail::GetStateDispatcher(store)), m_Owner(owner),
              m_Value(std::make_shared<const T>(std::move(initial))), m_Validator(std::move(validator)),
              m_Equality(std::move(equality)), m_TargetKey(UiDispatcher::AllocateTargetKey())
        {
        }

        //! Obtains the live store before owner-side value access.
        std::shared_ptr<Detail::StateStoreData> LockStore() const
        {
            m_Dispatcher.VerifyAccess();
            const auto data = m_Store.lock();
            if (!data || !m_Owner)
                throw std::logic_error("State access requires its live owning StateStore.");
            return data;
        }

        //! Returns the committed immutable value.
        std::shared_ptr<const void> ReadCommitted() const noexcept override
        {
            return m_Value;
        }

        //! Runs application equality only during transaction preparation.
        bool Equal(const std::shared_ptr<const void>& left, const std::shared_ptr<const void>& right) const override
        {
            return std::invoke(m_Equality, *std::static_pointer_cast<const T>(left),
                               *std::static_pointer_cast<const T>(right));
        }

        //! Runs the optional application validator without changing the committed value.
        void Validate(const std::shared_ptr<const void>& value) const override
        {
            const auto typed = std::static_pointer_cast<const T>(value);
            if (m_Validator)
                std::invoke(m_Validator, *typed);
            m_Validation.Emit(*typed);
        }

        //! Publishes one prepared value without throwing.
        void Publish(std::shared_ptr<const void> value, std::uint64_t revision) noexcept override
        {
            m_Value = std::static_pointer_cast<const T>(std::move(value));
            m_Revision = revision;
        }

        //! Broadcasts immutable snapshots while the store controls reentrant edits.
        void Notify(const std::shared_ptr<const void>& before, const std::shared_ptr<const void>& after,
                    std::uint64_t revision) override
        {
            if (IsAlive())
                m_Changed.Emit(StateChange<T>(std::static_pointer_cast<const T>(before),
                                             std::static_pointer_cast<const T>(after), revision));
        }

        //! Reconciles eager views after every canonical value has been restored or published.
        void NotifyReconcile(const std::shared_ptr<const void>& restored) override
        {
            if (IsAlive())
                m_Reconcile.Emit(*std::static_pointer_cast<const T>(restored));
        }

        //! Disables resolvers before stop observers are notified.
        void Expire() noexcept override
        {
            m_Owner = nullptr;
            m_Lifetime.request_stop();
            try
            {
                m_Changed.DisconnectAll();
                m_Validation.DisconnectAll();
                m_Reconcile.DisconnectAll();
            }
            catch (...)
            {
                // The resolver and stop token already forbid further target delivery.
            }
        }

        //! Returns whether the public value still exists.
        bool IsAlive() const noexcept override
        {
            return m_Owner != nullptr;
        }

        //! Observes its domain without keeping a closed wrapper alive.
        std::weak_ptr<Detail::StateStoreData> m_Store;

        //! Retains immutable owner identity and producer queue independently of its store wrapper.
        UiDispatcher m_Dispatcher;

        //! Resolves the original public state only on its owner thread.
        State<T>* m_Owner;

        //! Owns the current immutable value.
        std::shared_ptr<const T> m_Value;

        //! Rejects invalid candidates during preparation.
        Validator m_Validator;

        //! Suppresses equality-only publications.
        Equality m_Equality;

        //! Stores ordered lifetime-safe subscriptions.
        Signal<StateChange<T>> m_Changed;

        //! Reconciles eager presentation after discarded candidates without a change notification.
        Signal<const T&> m_Reconcile;

        //! Owns additional pure commit validators, including bound-view value constraints.
        Signal<const T&> m_Validation;

        //! Expires pending publications and receiver-bound tasks.
        std::stop_source m_Lifetime;

        //! Distinguishes this value from replacement objects at the same address.
        std::uint64_t m_TargetKey;

        //! Records the most recent commit that changed this value.
        std::uint64_t m_Revision = 0;
    };

    //! Retains the typed node while public methods or active callbacks execute.
    std::shared_ptr<Node> m_Node;
};
} // namespace EStudioUI
