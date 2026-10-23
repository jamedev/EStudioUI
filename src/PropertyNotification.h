#pragma once

#include <EStudioUI/Widget.h>

namespace EStudioUI::Detail
{
// Restores internal feedback suppression even when observers throw or destroy their owner.
class PropertySyncScope
{
  public:
    PropertySyncScope(Widget& owner, bool& flag) : m_Owner(&owner), m_Flag(flag), m_Previous(flag)
    {
        flag = true;
    }

    ~PropertySyncScope()
    {
        Reset();
    }

    void Reset()
    {
        if (m_Active && m_Owner.Get())
        {
            m_Flag = m_Previous;
        }
        m_Active = false;
    }

  private:
    WidgetRef m_Owner;
    bool& m_Flag;
    bool m_Previous = false;
    bool m_Active = true;
};

// Controls may synchronously reenter through observers; own payloads and bound stack growth.
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
        WidgetRef m_Owner;
        unsigned& m_Depth;

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
