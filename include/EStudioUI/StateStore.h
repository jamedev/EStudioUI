#pragma once

#include "UiDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

namespace EStudioUI
{
class UiContext;
template <typename T> class State;

namespace Detail
{
//! Keeps frame presentation outside a complete state computation, validation, and publication operation.
class StateUpdateScope
{
  public:
    //! Checks mutation access before entering the owner-thread state update scope.
    explicit StateUpdateScope(const std::shared_ptr<class StateStoreData>& data);

    //! Leaves the owner-thread state update scope after all callbacks complete.
    ~StateUpdateScope();

    //! A scope has exactly one matching exit.
    StateUpdateScope(const StateUpdateScope&) = delete;

    //! A scope has exactly one matching exit.
    StateUpdateScope& operator=(const StateUpdateScope&) = delete;
};

//! Owns transaction and notification bookkeeping independently of the public store wrapper.
class StateStoreData;

//! Type-erases immutable value snapshots for atomic multi-value publication.
class StateNode
{
  public:
    //! Releases a typed value node through its erased interface.
    virtual ~StateNode() = default;

    //! Returns the currently committed immutable snapshot without allocation.
    virtual std::shared_ptr<const void> ReadCommitted() const noexcept = 0;

    //! Tests candidate equality before any committed value is changed.
    virtual bool Equal(const std::shared_ptr<const void>& left, const std::shared_ptr<const void>& right) const = 0;

    //! Validates one final candidate while all staged candidates are readable.
    virtual void Validate(const std::shared_ptr<const void>& value) const = 0;

    //! Installs a prepared snapshot and revision without allocation or application code.
    virtual void Publish(std::shared_ptr<const void> value, std::uint64_t revision) noexcept = 0;

    //! Delivers a stable snapshot to the still-connected observers.
    virtual void Notify(const std::shared_ptr<const void>& before, const std::shared_ptr<const void>& after,
                        std::uint64_t revision) = 0;

    //! Reconciles one discarded candidate after all canonical values have been restored or published.
    virtual void NotifyReconcile(const std::shared_ptr<const void>& restored) = 0;

    //! Expires the value's target endpoint and subscriptions on the owner.
    virtual void Expire() noexcept = 0;

    //! Returns whether the original state object and store are still live.
    virtual bool IsAlive() const noexcept = 0;
};

//! Diagnoses owner and closed-domain access before reading owner state.
void VerifyStateAccess(const std::shared_ptr<StateStoreData>& data);

//! Additionally rejects mutation or registration while application validators are executing.
void VerifyStateMutationAccess(const std::shared_ptr<StateStoreData>& data);

//! Retains an optional enclosing UI owner for a complete state operation.
std::shared_ptr<void> AcquireStateLease(const std::shared_ptr<StateStoreData>& data);

//! Returns the producer dispatcher without granting cross-thread value access.
UiDispatcher GetStateDispatcher(const std::shared_ptr<StateStoreData>& data);

//! Validates a newly constructed value with the same no-mutation guard used by commits.
void ValidateInitialState(const std::shared_ptr<StateStoreData>& data, const std::shared_ptr<StateNode>& node);

//! Registers a weak node lifetime with its store on the owner.
void RegisterState(const std::shared_ptr<StateStoreData>& data, const std::shared_ptr<StateNode>& node);

//! Returns a candidate visible to the active explicit batch or the committed current notification round.
std::shared_ptr<const void> ReadState(const std::shared_ptr<StateStoreData>& data,
                                      const std::shared_ptr<StateNode>& node);

//! Stages a value and synchronously commits when outside a batch or notification round.
bool WriteState(const std::shared_ptr<StateStoreData>& data, const std::shared_ptr<StateNode>& node,
                std::shared_ptr<const void> value);

//! Contains direct observer errors until all remaining current-round observers have run.
void RecordStateError(const std::weak_ptr<StateStoreData>& data, std::exception_ptr error) noexcept;

//! Contains reconciliation observer errors without replacing the failed transaction exception.
void RecordStateReconciliationError(const std::weak_ptr<StateStoreData>& data, std::exception_ptr error) noexcept;
} // namespace Detail

//! Groups typed application values into coherent owner-thread revisions.
class StateStore
{
    friend class Action;

  public:
    //! Uses one owner dispatcher and bounds callback feedback to a positive number of notification rounds.
    explicit StateStore(UiDispatcher dispatcher, std::size_t maxNotificationRounds = 64);

    //! Expires state handles and subscriptions; destruction must occur on the owner.
    ~StateStore();

    //! Stores have stable identity and cannot transfer active transactions.
    StateStore(const StateStore&) = delete;

    //! Stores have stable identity and cannot transfer active transactions.
    StateStore& operator=(const StateStore&) = delete;

    //! Stages an explicit savepoint; outer success validates and commits all values before notifying.
    template <typename Callback> void Batch(Callback&& callback)
    {
        const auto data = m_Data;
        auto lease = Detail::AcquireStateLease(data);
        Detail::StateUpdateScope scope(data);
        BeginBatch(data);
        try
        {
            std::invoke(std::forward<Callback>(callback));
        }
        catch (...)
        {
            RollbackBatch(data);
            throw;
        }
        CommitBatch(data);
    }

    //! Returns the last coherent committed revision; equality-only batches do not advance it.
    std::uint64_t GetRevision() const;

    //! Returns the first observer error from the most recent discarded-candidate reconciliation.
    std::exception_ptr GetReconciliationError() const;

    //! Captures a producer endpoint on the owner.
    UiDispatcher GetDispatcher() const;

    //! Verifies owner and live store access; owner-side reads are allowed during UI traversal.
    void VerifyAccess() const;

    //! Returns the configured feedback bound.
    std::size_t GetMaxNotificationRounds() const;

  private:
    friend class UiContext;
    friend class Application;
    friend class Window;
    template <typename T> friend class State;

    //! Starts one explicit savepoint after checking owner access.
    static void BeginBatch(const std::shared_ptr<Detail::StateStoreData>& data);

    //! Merges a nested savepoint or publishes the outer savepoint, rolling back preparation failures.
    static void CommitBatch(const std::shared_ptr<Detail::StateStoreData>& data);

    //! Discards only the current explicit savepoint without invoking observers.
    static void RollbackBatch(const std::shared_ptr<Detail::StateStoreData>& data) noexcept;

    //! Returns whether any state mutation, validation, reconciliation, or notification is active on this owner.
    static bool IsUpdatingOnThisThread() noexcept;

    //! Adds the containing context's phase precondition to state mutation and registration.
    void SetAccessGuard(std::function<void()> callback);

    //! Retains the containing window while transaction bodies and state observers execute.
    void SetOwnerLease(std::function<std::shared_ptr<void>()> callback);

    //! Keeps transaction state alive through sender or store destruction from callbacks.
    std::shared_ptr<Detail::StateStoreData> m_Data;
};
} // namespace EStudioUI
