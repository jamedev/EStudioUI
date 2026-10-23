#pragma once

#include "State.h"
#include "Widget.h"
#include "UiContext.h"

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace EStudioUI
{
class Label;
class TextBox;
class NumericInput;
class Slider;
class ToolButton;
class CheckBox;

//! Selects whether a control also publishes its committed edits to the state.
enum class BindingMode
{
    //! Updates the control from state commits only.
    OneWay,

    //! Also validates committed control edits against the shared state.
    TwoWay
};

namespace Detail
{
//! Accesses the containing window lease without exposing ownership to applications.
class BindingAccess
{
  public:
    //! Keeps containing services alive across an arbitrary binding callback.
    static std::shared_ptr<void> RetainOwner(const Widget& widget);

    //! Rejects binding propagation before its target changes during a UI traversal.
    static void VerifyTargetMutation(const Widget& widget);
};

//! Shares binding lifetime without owning either its widget or state facade.
class BindingData : public std::enable_shared_from_this<BindingData>
{
  public:
    //! Captures a weak owner-side receiver and independent lifetime identities.
    BindingData(Widget& target, std::stop_token targetLifetime, std::stop_token stateLifetime);

    //! Retires both directions and any representability policy from any thread.
    void Disconnect() noexcept;

    //! Returns whether both endpoints still permit propagation.
    bool IsConnected() const noexcept;

    //! Resolves the weak receiver on its owner thread.
    Widget* GetTarget() const;

    //! Keeps one connection until the binding is explicitly retired.
    void Add(Connection connection);

    //! Retires subscriptions when either endpoint expires.
    void WatchLifetimes();

    //! Records a cancelled edit and invokes an optional owner-side error handler.
    void ReportError(std::exception_ptr error);

    //! Returns the most recently cancelled edit, usable from any thread.
    std::exception_ptr GetError() const;

    //! Clears the saved error without changing either endpoint.
    void ClearError();

    //! Replaces the owner-side handler for later cancelled edits.
    void SetErrorHandler(std::function<void(std::exception_ptr)> callback);

    //! Suppresses further edits while a failed candidate is being restored.
    std::size_t m_Restoring = 0;

  private:
    //! Observes the widget without retaining it.
    WidgetRef m_Target;

    //! Cancels delivery when the target is destroyed.
    std::stop_token m_TargetLifetime;

    //! Cancels delivery when the state or store is destroyed.
    std::stop_token m_StateLifetime;

    //! Supports explicit thread-safe disconnection.
    std::atomic_bool m_Connected = true;

    //! Keeps immutable connection endpoints after setup.
    std::vector<Connection> m_Connections;

    //! Ends subscriptions at target and state teardown.
    std::vector<std::unique_ptr<std::stop_callback<std::function<void()>>>> m_Stops;

    //! Serializes inspection of cancelled edit errors.
    mutable std::mutex m_ErrorMutex;

    //! Stores the last cancelled control edit.
    std::exception_ptr m_Error;

    //! Receives rejected edits after the canonical state is restored.
    std::function<void(std::exception_ptr)> m_ErrorHandler;

    //! Diagnoses owner-only handler changes before mutable access.
    std::thread::id m_OwnerThread;
};

//! Prevents rejected edits from recursively re-entering their canonical restoration.
class BindingRestoration
{
  public:
    //! Begins one restoration while retaining the caller-owned shared binding state.
    explicit BindingRestoration(BindingData& data) : m_Data(data)
    {
        ++m_Data.m_Restoring;
    }

    //! Re-enables later ordinary control edits.
    ~BindingRestoration()
    {
        --m_Data.m_Restoring;
    }

  private:
    //! Remains live through the restoration callback's shared state.
    BindingData& m_Data;
};
} // namespace Detail

//! A persistent binding handle; discarding it leaves the receiver-bound binding active.
class Binding
{
  public:
    //! Creates an empty handle.
    Binding() = default;

    //! Disconnects both propagation directions from any thread.
    void Disconnect() const noexcept;

    //! Returns whether both original endpoints remain live and connected.
    bool IsConnected() const noexcept;

    //! Returns the last cancelled control edit, or an empty exception pointer.
    std::exception_ptr GetError() const;

    //! Clears the last cancelled edit without changing its canonical value.
    void ClearError() const;

    //! Handles later rejected edits on the owner after restoring the canonical value.
    void SetErrorHandler(std::function<void(std::exception_ptr)> callback) const;

    //! Wraps shared internal subscription state without retaining a widget or state.
    explicit Binding(std::shared_ptr<Detail::BindingData> data) : m_Data(std::move(data))
    {
    }

  private:
    //! Shares explicit disconnection and error inspection.
    std::shared_ptr<Detail::BindingData> m_Data;
};

//! Disconnects a binding when the owning application scope ends.
class ScopedBinding
{
  public:
    //! Takes responsibility for a persistent binding.
    explicit ScopedBinding(Binding binding);

    //! Disconnects both directions.
    ~ScopedBinding();

    //! Gives scoped teardown one owner.
    ScopedBinding(const ScopedBinding&) = delete;

    //! Gives scoped teardown one owner.
    ScopedBinding& operator=(const ScopedBinding&) = delete;

    //! Transfers teardown responsibility.
    ScopedBinding(ScopedBinding&& other) noexcept;

    //! Disconnects the old binding and transfers teardown responsibility.
    ScopedBinding& operator=(ScopedBinding&& other) noexcept;

  private:
    //! Owns teardown responsibility without retaining either endpoint.
    Binding m_Binding;
};

namespace Detail
{
//! Creates direct owner-side propagation with an optional pure target representability check.
template <typename Target, typename T, typename Setter, typename Validator>
std::pair<Binding, std::shared_ptr<BindingData>> MakeBinding(Target& target, State<T>& state, Setter setter,
                                                             Validator validator)
{
    static_assert(std::is_base_of_v<Widget, Target>);
    target.VerifyAccess();
    state.GetDispatcher().VerifyAccess();
    auto* context = target.GetContext();
    if (!context)
        throw std::invalid_argument("A binding target must belong to a UiContext.");
    BindingAccess::VerifyTargetMutation(target);
    const auto targetHandle = context->GetHandle(target);
    const auto stateHandle = state.GetHandle();
    auto data =
        std::make_shared<BindingData>(target, targetHandle.GetTargetStopToken(), stateHandle.GetTargetStopToken());
    auto apply = [data, setter = std::move(setter)](const T& value) mutable
    {
        if (auto* live = dynamic_cast<Target*>(data->GetTarget()))
        {
            const auto ownerLease = BindingAccess::RetainOwner(*live);
            BindingAccess::VerifyTargetMutation(*live);
            std::invoke(setter, *live, value);
        }
    };
    auto validate = [data, validator = std::move(validator)](const T& value) mutable
    {
        if (auto* live = dynamic_cast<Target*>(data->GetTarget()))
        {
            const auto ownerLease = BindingAccess::RetainOwner(*live);
            BindingAccess::VerifyTargetMutation(*live);
            std::invoke(validator, *live, value);
        }
    };
    try
    {
        const auto initial = state.GetSnapshot();
        validate(*initial);
        if (!data->IsConnected())
            throw std::logic_error("A binding endpoint expired during initial validation.");
        data->Add(state.AddValidator(validate));
        data->Add(state.Subscribe([apply](const StateChange<T>& change) mutable { apply(change.GetAfter()); }));
        data->Add(state.SubscribeReconcile([apply](const T& restored) mutable { apply(restored); }));
        data->WatchLifetimes();
        apply(*state.GetSnapshot());
    }
    catch (...)
    {
        data->Disconnect();
        throw;
    }
    return {Binding(data), std::move(data)};
}

//! Adds a weak-state writer; rejected edits restore the canonical value and remain inspectable.
template <typename Target, typename T, typename SignalType, typename Setter, typename Getter>
void AddBindingWriter(const std::shared_ptr<BindingData>& data, Target&, State<T>& state, SignalType& signal,
                      Setter setter, Getter getter)
{
    const auto stateLifetime = state.GetHandle().GetTargetStopToken();
    auto* statePointer = &state;
    auto restore = [data, statePointer, stateLifetime, setter, getter](std::exception_ptr error) mutable
    {
        if (!data->IsConnected() && data->GetError())
            return;
        try
        {
            if (!stateLifetime.stop_requested())
            {
                const auto canonical = statePointer->GetSnapshot();
                if (auto* live = dynamic_cast<Target*>(data->GetTarget()))
                {
                    const auto ownerLease = BindingAccess::RetainOwner(*live);
                    BindingAccess::VerifyTargetMutation(*live);
                    BindingRestoration restoring(*data);
                    std::invoke(setter, *live, *canonical);
                    if (auto* remaining = dynamic_cast<Target*>(data->GetTarget());
                        remaining && std::invoke(getter, *remaining) != *canonical)
                    {
                        throw std::invalid_argument(
                            "Binding disconnected because the control cannot represent its canonical state.");
                    }
                }
            }
        }
        catch (...)
        {
            data->Disconnect();
            error = std::current_exception();
        }
        if (error)
            data->ReportError(std::move(error));
    };
    auto write = [data, statePointer, stateLifetime, getter, restore = std::move(restore)]() mutable
    {
        if (!data->IsConnected() || data->m_Restoring || stateLifetime.stop_requested())
            return;
        auto* live = dynamic_cast<Target*>(data->GetTarget());
        if (!live)
            return;
        const auto ownerLease = BindingAccess::RetainOwner(*live);
        try
        {
            BindingAccess::VerifyTargetMutation(*live);
            T candidate = std::invoke(getter, *live);
            if (candidate == statePointer->Get())
                return;
            const bool changed = statePointer->Set(std::move(candidate));
            if (!changed && !stateLifetime.stop_requested())
                restore({});
        }
        catch (...)
        {
            restore(std::current_exception());
        }
    };
    data->Add(signal.Connect([write](const T&) mutable { write(); }));
    // Initial setters can invoke application callbacks before this writer is installed.
    write();
}
} // namespace Detail

//! Binds a custom typed property; the setter receives a live receiver and must accept every state candidate.
template <typename Target, typename T, typename Setter> Binding Bind(Target& target, State<T>& state, Setter setter)
{
    return Detail::MakeBinding(target, state, std::move(setter), [](Target&, const T&) {}).first;
}

//! Binds a UTF-8 label's text and synchronizes its initial value immediately.
Binding BindText(Label& target, State<std::string>& state);

//! Binds editor text; rejected state validation restores canonical text and records Binding::GetError().
Binding BindText(TextBox& target, State<std::string>& state, BindingMode mode = BindingMode::TwoWay);

//! Binds committed numeric values, keeping intermediate editor text local until commit.
Binding BindValue(NumericInput& target, State<double>& state, BindingMode mode = BindingMode::TwoWay);

//! Binds scalar slider values with exact range validation before state commits.
Binding BindValue(Slider& target, State<double>& state, BindingMode mode = BindingMode::TwoWay);

//! Binds persistent checked state; configure ToolButton::SetCheckable for user activation.
Binding BindChecked(ToolButton& target, State<bool>& state, BindingMode mode = BindingMode::TwoWay);

//! Binds a checkbox boolean value; indeterminate maps to false.
Binding BindChecked(CheckBox& target, State<bool>& state, BindingMode mode = BindingMode::TwoWay);

//! Binds the widget's local programmatic enabled flag in one direction.
Binding BindEnabled(Widget& target, State<bool>& state);

//! Binds the widget's local programmatic visibility flag in one direction.
Binding BindVisible(Widget& target, State<bool>& state);
} // namespace EStudioUI
