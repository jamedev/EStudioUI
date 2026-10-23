#include <EStudioUI/CheckBox.h>
#include <EStudioUI/Bindings.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/ToolButton.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace Detail
{
std::shared_ptr<void> BindingAccess::RetainOwner(const Widget& widget)
{
    return widget.RetainOwner();
}

void BindingAccess::VerifyTargetMutation(const Widget& widget)
{
    widget.VerifyAccess();
    const auto* context = widget.GetContext();
    if (!context || context->GetPhase() != UiPhase::Idle)
        throw std::logic_error("Binding propagation requires its target UiContext to be Idle.");
}

BindingData::BindingData(Widget& target, std::stop_token targetLifetime, std::stop_token stateLifetime)
    : m_Target(&target), m_TargetLifetime(std::move(targetLifetime)), m_StateLifetime(std::move(stateLifetime)),
      m_OwnerThread(std::this_thread::get_id())
{
}

void BindingData::Disconnect() noexcept
{
    if (!m_Connected.exchange(false, std::memory_order_acq_rel))
        return;
    for (const auto& connection : m_Connections)
        connection.Disconnect();
}

bool BindingData::IsConnected() const noexcept
{
    return m_Connected.load(std::memory_order_acquire) && !m_TargetLifetime.stop_requested() &&
           !m_StateLifetime.stop_requested();
}

Widget* BindingData::GetTarget() const
{
    ThreadAffinity::VerifyThread(m_OwnerThread, "Binding receiver access");
    return IsConnected() ? m_Target.Get() : nullptr;
}

void BindingData::Add(Connection connection)
{
    if (!IsConnected())
    {
        connection.Disconnect();
        return;
    }
    try
    {
        m_Connections.push_back(std::move(connection));
    }
    catch (...)
    {
        connection.Disconnect();
        throw;
    }
}

void BindingData::WatchLifetimes()
{
    const auto weak = weak_from_this();
    std::function<void()> stop = [weak]
    {
        if (const auto data = weak.lock())
            data->Disconnect();
    };
    m_Stops.push_back(std::make_unique<std::stop_callback<std::function<void()>>>(m_TargetLifetime, stop));
    m_Stops.push_back(std::make_unique<std::stop_callback<std::function<void()>>>(m_StateLifetime, std::move(stop)));
}

void BindingData::ReportError(std::exception_ptr error)
{
    ThreadAffinity::VerifyThread(m_OwnerThread, "Binding error delivery");
    {
        std::lock_guard lock(m_ErrorMutex);
        m_Error = error;
    }
    const auto handler = m_ErrorHandler;
    if (handler)
        handler(std::move(error));
}

std::exception_ptr BindingData::GetError() const
{
    std::lock_guard lock(m_ErrorMutex);
    return m_Error;
}

void BindingData::ClearError()
{
    std::lock_guard lock(m_ErrorMutex);
    m_Error = {};
}

void BindingData::SetErrorHandler(std::function<void(std::exception_ptr)> callback)
{
    ThreadAffinity::VerifyThread(m_OwnerThread, "Binding error handler replacement");
    m_ErrorHandler = std::move(callback);
}
} // namespace Detail

void Binding::Disconnect() const noexcept
{
    if (m_Data)
        m_Data->Disconnect();
}

bool Binding::IsConnected() const noexcept
{
    return m_Data && m_Data->IsConnected();
}

std::exception_ptr Binding::GetError() const
{
    return m_Data ? m_Data->GetError() : std::exception_ptr{};
}

void Binding::ClearError() const
{
    if (m_Data)
        m_Data->ClearError();
}

void Binding::SetErrorHandler(std::function<void(std::exception_ptr)> callback) const
{
    if (m_Data)
        m_Data->SetErrorHandler(std::move(callback));
}

ScopedBinding::ScopedBinding(Binding binding) : m_Binding(std::move(binding))
{
}

ScopedBinding::~ScopedBinding()
{
    m_Binding.Disconnect();
}

ScopedBinding::ScopedBinding(ScopedBinding&& other) noexcept : m_Binding(std::exchange(other.m_Binding, Binding{}))
{
}

ScopedBinding& ScopedBinding::operator=(ScopedBinding&& other) noexcept
{
    if (this != &other)
    {
        m_Binding.Disconnect();
        m_Binding = std::exchange(other.m_Binding, Binding{});
    }
    return *this;
}

Binding BindText(Label& target, State<std::string>& state)
{
    return Bind(target, state, [](Label& live, const std::string& text) { live.SetText(text); });
}

Binding BindText(TextBox& target, State<std::string>& state, BindingMode mode)
{
    const auto setter = [](TextBox& live, const std::string& text) { live.SetText(text); };
    auto [binding, data] = Detail::MakeBinding(
        target, state, setter,
        [](TextBox& live, const std::string& text)
        {
            if (!live.CanSetText(text))
                throw std::invalid_argument("Bound text must satisfy the editor's validator and exact text limits.");
        });
    if (mode == BindingMode::TwoWay && data->IsConnected())
        Detail::AddBindingWriter(data, target, state, target.TextChanged(), setter,
                                 [](const TextBox& live) { return live.GetText(); });
    return binding;
}

Binding BindValue(NumericInput& target, State<double>& state, BindingMode mode)
{
    const auto setter = [](NumericInput& live, double value) { live.SetValue(value); };
    auto [binding, data] = Detail::MakeBinding(
        target, state, setter,
        [](NumericInput& live, double value)
        {
            if (!std::isfinite(value) || value < live.GetMinimum() || value > live.GetMaximum())
                throw std::invalid_argument("Bound numeric value must be finite and inside the control's range.");
        });
    if (mode == BindingMode::TwoWay && data->IsConnected())
        Detail::AddBindingWriter(data, target, state, target.ValueChanged(), setter,
                                 [](const NumericInput& live) { return live.GetValue(); });
    return binding;
}

Binding BindValue(Slider& target, State<double>& state, BindingMode mode)
{
    const auto setter = [](Slider& live, double value) { live.SetValue(value); };
    auto [binding, data] = Detail::MakeBinding(
        target, state, setter,
        [](Slider& live, double value)
        {
            if (!std::isfinite(value) || value < live.GetMinimum() || value > live.GetMaximum())
                throw std::invalid_argument("Bound slider value must be finite and inside the control's range.");
        });
    if (mode == BindingMode::TwoWay && data->IsConnected())
        Detail::AddBindingWriter(data, target, state, target.ValueChanged(), setter,
                                 [](const Slider& live) { return live.GetValue(); });
    return binding;
}

Binding BindChecked(ToolButton& target, State<bool>& state, BindingMode mode)
{
    const auto setter = [](ToolButton& live, bool checked) { live.SetChecked(checked); };
    auto [binding, data] = Detail::MakeBinding(target, state, setter, [](ToolButton&, bool) {});
    if (mode == BindingMode::TwoWay && data->IsConnected())
        Detail::AddBindingWriter(data, target, state, target.Toggled(), setter,
                                 [](const ToolButton& live) { return live.IsChecked(); });
    return binding;
}

Binding BindChecked(CheckBox& target, State<bool>& state, BindingMode mode)
{
    const auto setter = [](CheckBox& live, bool checked) { live.SetChecked(checked); };
    auto [binding, data] = Detail::MakeBinding(target, state, setter, [](CheckBox&, bool) {});
    if (mode == BindingMode::TwoWay && data->IsConnected())
        Detail::AddBindingWriter(data, target, state, target.Toggled(), setter,
                                 [](const CheckBox& live) { return live.IsChecked(); });
    return binding;
}

Binding BindEnabled(Widget& target, State<bool>& state)
{
    return Bind(target, state, [](Widget& live, bool enabled) { live.SetEnabled(enabled); });
}

Binding BindVisible(Widget& target, State<bool>& state)
{
    return Bind(target, state, [](Widget& live, bool visible) { live.SetVisible(visible); });
}
} // namespace EStudioUI
