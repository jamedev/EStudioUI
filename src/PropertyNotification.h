#pragma once

#include <EStudioUI/Widget.h>

namespace EStudioUI::Detail
{
//! Restores internal feedback suppression even when observers throw or destroy their owner.
class PropertySyncScope
{
  public:
    //! Begins a temporary feedback-suppression scope.
    PropertySyncScope(Widget& owner, bool& flag) : m_Owner(&owner), m_Flag(flag), m_Previous(flag)
    {
        flag = true;
    }

    //! Restores suppression if the owner still exists.
    ~PropertySyncScope()
    {
        Reset();
    }

    //! Restores the prior flag once, including after callback failures.
    void Reset()
    {
        if (m_Active && m_Owner.Get())
        {
            m_Flag = m_Previous;
        }
        m_Active = false;
    }

  private:
    //! Observes the owner without retaining its UI lifetime.
    WidgetRef m_Owner;
    //! Refers to the owner-side suppression flag.
    bool& m_Flag;
    //! Preserves nested suppression state.
    bool m_Previous = false;
    //! Prevents repeated restoration.
    bool m_Active = true;
};

//! Controls may synchronously reenter through observers; own payloads and bound stack growth.
template <typename SignalType, typename Value>
void EmitPropertyNotification(Widget& owner, unsigned& depth, SignalType& signal, const Value& value)
{
    if (depth >= 64)
    {
        throw std::logic_error("Property editor notifications exceeded 64 reentrant changes.");
    }
    const WidgetRef self(&owner);
    ++depth;

    struct Scope
    {
        //! Observes the notifying control.
        WidgetRef m_Owner;
        //! Refers to the control-owned delivery depth.
        unsigned& m_Depth;

        //! Retires one delivery level if the owner survived.
        ~Scope()
        {
            if (m_Owner.Get())
            {
                --m_Depth;
            }
        }
    } scope{self, depth};

    const Value snapshot = value;
    signal.Emit(snapshot);
}
} // namespace EStudioUI::Detail
