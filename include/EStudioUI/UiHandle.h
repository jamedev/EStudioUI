#pragma once

#include "UiDispatcher.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace EStudioUI
{
//! A copyable typed endpoint that resolves a live target only during owner-side delivery.
template <typename T> class UiHandle
{
  public:
    //! Creates an empty handle whose posts are rejected.
    UiHandle() = default;

    //! Copies target identity and resolver without transferring receiver ownership.
    UiHandle(const UiHandle&) = default;

    //! Replaces this endpoint with another non-owning typed target endpoint.
    UiHandle& operator=(const UiHandle&) = default;

    //! Creates an owner-validated endpoint with a globally unique nonzero target key and safe owner resolver.
    static UiHandle ForTarget(const UiDispatcher& dispatcher, std::uint64_t targetKey,
                              std::function<T*()> resolver, std::stop_token targetLifetime = {})
    {
        dispatcher.VerifyAccess();
        if (!targetKey || !resolver)
            throw std::invalid_argument("A UI target requires a unique nonzero key and an owner-side resolver.");
        UiHandle result;
        result.m_Dispatcher = dispatcher;
        result.m_TargetKey = targetKey;
        result.m_Resolver = std::move(resolver);
        result.m_TargetLifetime = std::move(targetLifetime);
        return result;
    }

    //! Queues an owning callback; an expired target settles as TargetExpired.
    template <typename Callback> DispatchReceipt Post(Callback&& callback) const
    {
        return Submit({}, std::forward<Callback>(callback));
    }

    //! Coalesces pending publications by this target and a nonempty logical property key.
    template <typename Callback> DispatchReceipt PostLatest(std::string key, Callback&& callback) const
    {
        if (key.empty())
            throw std::invalid_argument("A coalesced UI publication requires a nonempty key.");
        return Submit(std::move(key), std::forward<Callback>(callback));
    }

    //! Returns a producer endpoint; throws for an empty handle.
    UiDispatcher GetDispatcher() const
    {
        if (!m_Dispatcher)
            throw std::logic_error("An empty UI handle has no dispatcher.");
        return *m_Dispatcher;
    }

    //! Returns immutable target identity without accessing the target object.
    std::uint64_t GetTargetKey() const noexcept
    {
        return m_TargetKey;
    }

    //! Returns the receiver lifetime token without resolving its object.
    std::stop_token GetTargetStopToken() const noexcept
    {
        return m_TargetLifetime;
    }

    //! Returns whether the endpoint and optional receiver token permit delivery, without resolving the target.
    bool IsConnected() const noexcept
    {
        return m_Dispatcher && m_Dispatcher->IsOpen() && !m_TargetLifetime.stop_requested();
    }

  private:
    //! Owns a move-only callback through a copyable queue closure and resolves the target on delivery.
    template <typename Callback> DispatchReceipt Submit(std::optional<std::string> key, Callback&& callback) const
    {
        static_assert(std::is_invocable_v<std::decay_t<Callback>&, T&>,
                      "A UI callback must accept its live target by reference.");
        if (!m_Dispatcher)
            return {};
        auto work = std::make_shared<std::decay_t<Callback>>(std::forward<Callback>(callback));
        auto operation = [resolver = m_Resolver, lifetime = m_TargetLifetime, work = std::move(work)]
        {
            if (lifetime.stop_requested())
                return DispatchStatus::TargetExpired;
            auto* target = resolver();
            if (!target)
                return DispatchStatus::TargetExpired;
            std::invoke(*work, *target);
            return DispatchStatus::Completed;
        };
        std::optional<typename UiDispatcher::PublicationKey> publication;
        if (key)
            publication.emplace(m_TargetKey, std::move(*key));
        return m_Dispatcher->PostWork(std::move(operation), std::move(publication));
    }

    //! Retains the producer endpoint without requiring target access.
    std::optional<UiDispatcher> m_Dispatcher;

    //! Distinguishes targets even when their storage addresses are reused.
    std::uint64_t m_TargetKey = 0;

    //! Captures a lifetime-safe resolver whose body runs only on the owner.
    std::function<T*()> m_Resolver;

    //! Suppresses delivery when the receiver announces owner-side destruction.
    std::stop_token m_TargetLifetime;
};
} // namespace EStudioUI
