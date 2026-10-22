#include <EStudioUI/Action.h>
#include <EStudioUI/ThreadAffinity.h>

#include <stdexcept>

namespace EStudioUI
{
struct Action::Data
{
    ThreadAffinity m_Affinity;
    State<ActionState> m_Properties;
    std::stop_source m_Lifetime;
    std::unique_ptr<std::stop_callback<std::function<void()>>> m_DomainStop;
    const std::uint64_t m_TargetKey = UiDispatcher::AllocateTargetKey();
    Action* m_Owner = nullptr;
    Signal<> m_Triggered;
    bool m_Triggering = false;

    Data(StateStore& store, std::string text)
        : m_Properties(store, ActionState{std::move(text)},
                       [](const ActionState& value)
                       {
                           if (value.m_Checked && !value.m_Checkable)
                               throw std::invalid_argument("Only a checkable action can be checked.");
                           for (const auto& shortcut : value.m_Shortcuts)
                               shortcut.Validate();
                       })
    {
        m_DomainStop = std::make_unique<std::stop_callback<std::function<void()>>>(
            m_Properties.GetHandle().GetTargetStopToken(),
            [lifetime = m_Lifetime]() mutable { lifetime.request_stop(); });
    }
};

Action::Action(StateStore& store, std::string text) : m_Data(std::make_shared<Data>(store, std::move(text)))
{
    m_Data->m_Owner = this;
}

Action::~Action()
{
    if (!m_Data->m_Affinity.IsOwnerThread())
        std::terminate();
    m_Data->m_Owner = nullptr;
    m_Data->m_Lifetime.request_stop();
}

State<ActionState>& Action::Properties()
{
    (void)m_Data->m_Properties.GetSnapshot();
    return m_Data->m_Properties;
}

std::shared_ptr<const ActionState> Action::GetSnapshot() const
{
    return m_Data->m_Properties.GetSnapshot();
}

std::stop_token Action::GetStopToken() const
{
    m_Data->m_Affinity.VerifyAccess("Action lifetime access");
    return m_Data->m_Lifetime.get_token();
}

UiHandle<Action> Action::GetHandle() const
{
    const auto data = m_Data;
    (void)data->m_Properties.GetSnapshot();
    return UiHandle<Action>::ForTarget(
        data->m_Properties.GetDispatcher(), data->m_TargetKey,
        [weak = std::weak_ptr<Data>(data)]() -> Action*
        {
            const auto current = weak.lock();
            return current && !current->m_Lifetime.stop_requested() ? current->m_Owner : nullptr;
        },
        data->m_Lifetime.get_token());
}

void Action::SetText(std::string text)
{
    auto value = *GetSnapshot();
    value.m_Text = std::move(text);
    m_Data->m_Properties.Set(std::move(value));
}

void Action::SetEnabled(bool enabled)
{
    auto value = *GetSnapshot();
    value.m_Enabled = enabled;
    m_Data->m_Properties.Set(std::move(value));
}

void Action::SetVisible(bool visible)
{
    auto value = *GetSnapshot();
    value.m_Visible = visible;
    m_Data->m_Properties.Set(std::move(value));
}

void Action::SetCheckable(bool checkable)
{
    auto value = *GetSnapshot();
    value.m_Checkable = checkable;
    if (!checkable)
        value.m_Checked = false;
    m_Data->m_Properties.Set(std::move(value));
}

void Action::SetChecked(bool checked)
{
    auto value = *GetSnapshot();
    value.m_Checked = checked;
    m_Data->m_Properties.Set(std::move(value));
}

void Action::SetShortcut(Shortcut shortcut)
{
    SetShortcuts({shortcut});
}

void Action::SetShortcuts(std::vector<Shortcut> shortcuts)
{
    auto value = *GetSnapshot();
    value.m_Shortcuts = std::move(shortcuts);
    m_Data->m_Properties.Set(std::move(value));
}

bool Action::Trigger()
{
    const auto data = m_Data;
    data->m_Affinity.VerifyAccess("Action execution");
    if (data->m_Lifetime.stop_requested())
        return false;
    const auto value = data->m_Properties.GetSnapshot();
    if (StateStore::IsUpdatingOnThisThread())
        throw std::logic_error("Commit state updates before executing an action.");
    if (!value->m_Enabled || !value->m_Visible || data->m_Triggering)
        return false;
    // Even noncheckable commands must obey their state domain's mutation-phase guard.
    data->m_Properties.Set(*value);

    struct Reset
    {
        bool& m_Value;

        ~Reset()
        {
            m_Value = false;
        }
    } reset{data->m_Triggering};

    data->m_Triggering = true;
    if (value->m_Checkable)
    {
        auto next = *value;
        next.m_Checked = !next.m_Checked;
        data->m_Properties.Set(std::move(next));
    }
    data->m_Triggered.Emit();
    return true;
}

Signal<>& Action::Triggered()
{
    (void)GetSnapshot();
    return m_Data->m_Triggered;
}
} // namespace EStudioUI
