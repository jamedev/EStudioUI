#include "NativeAccessibility.h"
#include "WindowsSupport.h"

#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/ClipboardData.h>
#include <EStudioUI/UiContext.h>

#ifdef _WIN32
#include <ole2.h>
#include <UIAutomation.h>
#include <commctrl.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace EStudioUI::Detail
{
namespace
{
class Provider;

HRESULT StatusCode(SemanticStatus status)
{
    switch (status)
    {
    case SemanticStatus::Succeeded:
        return S_OK;
    case SemanticStatus::Unavailable:
        return UIA_E_ELEMENTNOTAVAILABLE;
    case SemanticStatus::Disabled:
        return UIA_E_ELEMENTNOTENABLED;
    case SemanticStatus::Unsupported:
        return UIA_E_NOTSUPPORTED;
    case SemanticStatus::InvalidValue:
        return E_INVALIDARG;
    }
    return E_FAIL;
}

CONTROLTYPEID ControlType(SemanticRole role)
{
    switch (role)
    {
    case SemanticRole::Window:
        return UIA_WindowControlTypeId;
    case SemanticRole::Text:
        return UIA_TextControlTypeId;
    case SemanticRole::Button:
        return UIA_ButtonControlTypeId;
    case SemanticRole::CheckBox:
        return UIA_CheckBoxControlTypeId;
    case SemanticRole::TextBox:
        return UIA_EditControlTypeId;
    case SemanticRole::Slider:
        return UIA_SliderControlTypeId;
    case SemanticRole::SpinBox:
        return UIA_SpinnerControlTypeId;
    case SemanticRole::TabList:
        return UIA_TabControlTypeId;
    case SemanticRole::Tab:
        return UIA_TabItemControlTypeId;
    case SemanticRole::ToolBar:
        return UIA_ToolBarControlTypeId;
    case SemanticRole::List:
        return UIA_ListControlTypeId;
    case SemanticRole::Tree:
        return UIA_TreeControlTypeId;
    case SemanticRole::Table:
        return UIA_TableControlTypeId;
    case SemanticRole::Menu:
        return UIA_MenuControlTypeId;
    case SemanticRole::MenuItem:
        return UIA_MenuItemControlTypeId;
    case SemanticRole::Canvas:
        return UIA_PaneControlTypeId;
    case SemanticRole::Group:
        return UIA_GroupControlTypeId;
    }
    return UIA_CustomControlTypeId;
}

VARIANT Boolean(bool value)
{
    VARIANT result{};
    result.vt = VT_BOOL;
    result.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    return result;
}

HRESULT StringValue(std::string_view value, VARIANT* result)
{
    const auto wide = ToWindowsText(value);
    result->bstrVal = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
    if (!result->bstrVal)
        return E_OUTOFMEMORY;
    result->vt = VT_BSTR;
    return S_OK;
}

struct ProviderState : std::enable_shared_from_this<ProviderState>
{
    HWND m_Window;
    UiContext* m_Ui;
    UiHandle<Widget> m_Target;
    std::weak_ptr<void> m_Owner;
    const std::thread::id m_Thread = std::this_thread::get_id();
    std::atomic<bool> m_Closed = false;
    std::unordered_map<SemanticId, SemanticNode> m_Observed;
    std::uint64_t m_LastRevision = 0;
    SemanticId m_LastFocus = 0;

    ProviderState(UiContext& ui, HWND window, std::weak_ptr<void> owner)
        : m_Window(window), m_Ui(&ui), m_Target(ui.GetHandle(ui.Root())), m_Owner(std::move(owner))
    {
    }

    template <typename Result, typename Function> HRESULT Call(Result& result, Function function)
    {
        if (m_Closed || !m_Target.IsConnected())
            return UIA_E_ELEMENTNOTAVAILABLE;
        try
        {
            if (std::this_thread::get_id() == m_Thread)
            {
                const auto owner = m_Owner.lock();
                result = function(*m_Ui);
                return m_Closed ? UIA_E_ELEMENTNOTAVAILABLE : S_OK;
            }

            struct Reply
            {
                std::mutex m_Mutex;
                std::optional<Result> m_Result;
            };

            auto reply = std::make_shared<Reply>();
            const auto self = shared_from_this();
            auto receipt = m_Target.Post(
                [reply, self, function = std::move(function)](Widget& root)
                {
                    if (self->m_Closed)
                        return;
                    auto value = function(*root.GetContext());
                    if (self->m_Closed)
                        return;
                    std::lock_guard lock(reply->m_Mutex);
                    reply->m_Result = std::move(value);
                });
            if (!receipt.WaitFor(std::chrono::milliseconds(1000)))
            {
                receipt.Cancel();
                return UIA_E_TIMEOUT;
            }
            if (receipt.GetStatus() != DispatchStatus::Completed)
                return UIA_E_ELEMENTNOTAVAILABLE;
            std::lock_guard lock(reply->m_Mutex);
            if (!reply->m_Result)
                return UIA_E_ELEMENTNOTAVAILABLE;
            result = std::move(*reply->m_Result);
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        catch (const std::invalid_argument&)
        {
            return E_INVALIDARG;
        }
        catch (...)
        {
            return UIA_E_INVALIDOPERATION;
        }
    }

    void ApplyHostState(SemanticNode& node) const
    {
        node.m_Enabled = node.m_Enabled && IsWindowEnabled(m_Window);
        node.m_Offscreen = node.m_Offscreen || !IsWindowVisible(m_Window);
    }

    HRESULT Node(SemanticId id, SemanticNode& node)
    {
        std::optional<SemanticNode> result;
        const auto self = shared_from_this();
        const auto code = Call(result,
                               [self, id](UiContext& ui)
                               {
                                   auto value = ui.Semantics().GetNode(id);
                                   if (value)
                                       self->ApplyHostState(*value);
                                   if (value && (self->m_Observed.size() < 64 || self->m_Observed.contains(id)))
                                       self->m_Observed.insert_or_assign(id, *value);
                                   return value;
                               });
        if (FAILED(code))
            return code;
        if (!result)
            return UIA_E_ELEMENTNOTAVAILABLE;
        node = std::move(*result);
        return S_OK;
    }

    HRESULT Action(SemanticId id, SemanticCommand command, bool asynchronous = false)
    {
        SemanticNode node;
        auto code = Node(id, node);
        if (FAILED(code))
            return code;
        if (!node.m_Enabled)
            return UIA_E_ELEMENTNOTENABLED;
        if (!HasSemanticAction(node.m_Info.m_Actions, command.m_Action))
            return UIA_E_NOTSUPPORTED;
        if (asynchronous)
        {
            try
            {
                const auto self = shared_from_this();
                auto receipt = m_Target.Post(
                    [self, id, command = std::move(command)](Widget& root)
                    {
                        if (!self->m_Closed && IsWindowEnabled(self->m_Window))
                            root.GetContext()->Semantics().Perform(id, command);
                    });
                return receipt.GetStatus() == DispatchStatus::Rejected ? UIA_E_ELEMENTNOTAVAILABLE : S_OK;
            }
            catch (...)
            {
                return E_OUTOFMEMORY;
            }
        }
        SemanticStatus result = SemanticStatus::Unavailable;
        code = Call(result,
                    [self = shared_from_this(), id, command = std::move(command)](UiContext& ui)
                    {
                        return !IsWindowEnabled(self->m_Window) ? SemanticStatus::Disabled
                                                                : ui.Semantics().Perform(id, command);
                    });
        return FAILED(code) ? code : StatusCode(result);
    }
};

// Providers keep only shared bridge state. Queries from native COM threads cross
// the existing owning dispatcher; there is no worker access to mutable widgets.
class Provider final : public IRawElementProviderSimple,
                       public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot,
                       public IInvokeProvider,
                       public IValueProvider,
                       public IRangeValueProvider,
                       public IToggleProvider
{
  public:
    Provider(std::shared_ptr<ProviderState> state, SemanticId id) : m_State(std::move(state)), m_Id(id)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
            *result = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment))
            *result = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == __uuidof(IRawElementProviderFragmentRoot) && IsRoot())
            *result = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (iid == __uuidof(IInvokeProvider))
            *result = static_cast<IInvokeProvider*>(this);
        else if (iid == __uuidof(IValueProvider))
            *result = static_cast<IValueProvider*>(this);
        else if (iid == __uuidof(IRangeValueProvider))
            *result = static_cast<IRangeValueProvider*>(this);
        else if (iid == __uuidof(IToggleProvider))
            *result = static_cast<IToggleProvider*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_References;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto count = --m_References;
        if (!count)
            delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override
    {
        if (!result)
            return E_POINTER;
        *result = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (FAILED(code))
            return code;
        const auto actions = node.m_Info.m_Actions;
        if (pattern == UIA_InvokePatternId && HasSemanticAction(actions, SemanticAction::Invoke))
            *result = static_cast<IInvokeProvider*>(this);
        else if (pattern == UIA_ValuePatternId && node.m_Info.m_Role == SemanticRole::TextBox)
            *result = static_cast<IValueProvider*>(this);
        else if (pattern == UIA_RangeValuePatternId && node.m_Info.m_Number && node.m_Info.m_Minimum &&
                 node.m_Info.m_Maximum)
            *result = static_cast<IRangeValueProvider*>(this);
        else if (pattern == UIA_TogglePatternId && HasSemanticAction(actions, SemanticAction::Toggle))
            *result = static_cast<IToggleProvider*>(this);
        if (*result)
            AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) override
    {
        if (!result)
            return E_POINTER;
        VariantInit(result);
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (FAILED(code))
            return code;
        try
        {
            switch (property)
            {
            case UIA_NamePropertyId:
                return StringValue(node.m_Info.m_Name, result);
            case UIA_HelpTextPropertyId:
                return StringValue(node.m_Info.m_Description, result);
            case UIA_AutomationIdPropertyId:
                return StringValue(node.m_AutomationId, result);
            case UIA_ClassNamePropertyId:
                return StringValue(node.m_ClassName, result);
            case UIA_FrameworkIdPropertyId:
                return StringValue("EStudioUI", result);
            case UIA_ControlTypePropertyId:
                result->vt = VT_I4;
                result->lVal = ControlType(node.m_Info.m_Role);
                return S_OK;
            case UIA_IsEnabledPropertyId:
                *result = Boolean(node.m_Enabled);
                return S_OK;
            case UIA_IsKeyboardFocusablePropertyId:
                *result = Boolean(node.m_Focusable);
                return S_OK;
            case UIA_HasKeyboardFocusPropertyId:
                *result = Boolean(node.m_Focused);
                return S_OK;
            case UIA_IsOffscreenPropertyId:
                *result = Boolean(node.m_Offscreen);
                return S_OK;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
                *result = Boolean(true);
                return S_OK;
            default:
                return S_OK;
            }
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        catch (...)
        {
            return E_FAIL;
        }
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (m_State->m_Closed)
            return UIA_E_ELEMENTNOTAVAILABLE;
        return IsRoot() ? UiaHostProviderFromHwnd(m_State->m_Window, result) : S_OK;
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        SemanticId destination = 0;
        const auto code = m_State->Call(destination,
                                        [id = m_Id, direction](UiContext& ui) -> SemanticId
                                        {
                                            auto& tree = ui.Semantics();
                                            const auto node = tree.GetNode(id);
                                            if (!node)
                                                return 0;
                                            if (direction == NavigateDirection_Parent)
                                                return node->m_Parent;
                                            const auto parent = direction == NavigateDirection_FirstChild ||
                                                                        direction == NavigateDirection_LastChild
                                                                    ? id
                                                                    : node->m_Parent;
                                            if (!parent)
                                                return 0;
                                            SemanticId previous = 0;
                                            bool next = false;
                                            for (std::size_t offset = 0;; offset += 256)
                                            {
                                                const auto children = tree.GetChildren(parent, offset);
                                                for (auto child : children)
                                                {
                                                    if (direction == NavigateDirection_FirstChild || next)
                                                        return child;
                                                    if (child == id)
                                                    {
                                                        if (direction == NavigateDirection_PreviousSibling)
                                                            return previous;
                                                        if (direction == NavigateDirection_NextSibling)
                                                            next = true;
                                                    }
                                                    previous = child;
                                                }
                                                if (children.size() < 256)
                                                    break;
                                            }
                                            return direction == NavigateDirection_LastChild ? previous : 0;
                                        });
        return FAILED(code) ? code : Fragment(destination, result);
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (m_State->m_Closed)
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (IsRoot())
            return S_OK;
        auto* array = SafeArrayCreateVector(VT_I4, 0, 3);
        if (!array)
            return E_OUTOFMEMORY;
        LONG values[3] = {UiaAppendRuntimeId, static_cast<LONG>(m_Id & 0xffffffffULL), static_cast<LONG>(m_Id >> 32)};
        for (LONG index = 0; index < 3; ++index)
            SafeArrayPutElement(array, &index, &values[index]);
        *result = array;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override
    {
        if (!result)
            return E_POINTER;
        *result = {};
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (FAILED(code))
            return code;
        if (!node.m_ScreenBounds)
            return UIA_E_NOTSUPPORTED;
        const auto bounds = *node.m_ScreenBounds;
        *result = {bounds.m_X, bounds.m_Y, bounds.m_Width, bounds.m_Height};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return m_State->m_Closed ? UIA_E_ELEMENTNOTAVAILABLE : S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override
    {
        return m_State->Action(m_Id, {SemanticAction::Focus});
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (m_State->m_Closed)
            return UIA_E_ELEMENTNOTAVAILABLE;
        auto* provider = new (std::nothrow) Provider(m_State, m_State->m_Target.GetTargetKey());
        if (!provider)
            return E_OUTOFMEMORY;
        *result = static_cast<IRawElementProviderFragmentRoot*>(provider);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        SemanticId id = 0;
        const auto code = m_State->Call(id,
                                        [x, y](UiContext& ui)
                                        {
                                            return ui.Semantics().HitTest(ui.Platform().GetWindowMetrics().FromScreen(
                                                {static_cast<float>(x), static_cast<float>(y)}));
                                        });
        return FAILED(code) ? code : Fragment(id, result);
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        SemanticId id = 0;
        const auto code = m_State->Call(id, [](UiContext& ui) { return ui.Semantics().GetFocus(); });
        return FAILED(code) ? code : Fragment(id, result);
    }

    HRESULT STDMETHODCALLTYPE Invoke() override
    {
        return m_State->Action(m_Id, {SemanticAction::Invoke}, true);
    }

    HRESULT STDMETHODCALLTYPE Toggle() override
    {
        return m_State->Action(m_Id, {SemanticAction::Toggle});
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override
    {
        if (!value)
            return E_INVALIDARG;
        try
        {
            constexpr std::size_t limit = ClipboardData::s_MaxBytes;
            std::size_t size = 0;
            while (size <= limit && value[size])
                ++size;
            if (size > limit)
                return E_INVALIDARG;
            return m_State->Action(m_Id, {SemanticAction::SetText, FromWindowsText({value, size})});
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        catch (...)
        {
            return E_INVALIDARG;
        }
    }

    HRESULT STDMETHODCALLTYPE get_Value(BSTR* result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (FAILED(code))
            return code;
        try
        {
            VARIANT value{};
            const auto converted = StringValue(node.m_Info.m_Value, &value);
            if (SUCCEEDED(converted))
                *result = value.bstrVal;
            return converted;
        }
        catch (...)
        {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* result) override
    {
        if (!result)
            return E_POINTER;
        *result = TRUE;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (SUCCEEDED(code))
            *result = node.m_Info.m_ReadOnly ? TRUE : FALSE;
        return code;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double value) override
    {
        return m_State->Action(m_Id, {SemanticAction::SetNumber, {}, value});
    }

    HRESULT STDMETHODCALLTYPE get_Value(double* result) override
    {
        return Number(result, 0);
    }

    HRESULT STDMETHODCALLTYPE get_Minimum(double* result) override
    {
        return Number(result, 1);
    }

    HRESULT STDMETHODCALLTYPE get_Maximum(double* result) override
    {
        return Number(result, 2);
    }

    HRESULT STDMETHODCALLTYPE get_SmallChange(double* result) override
    {
        return Number(result, 3);
    }

    HRESULT STDMETHODCALLTYPE get_LargeChange(double* result) override
    {
        return Number(result, 3);
    }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* result) override
    {
        if (!result)
            return E_POINTER;
        *result = ToggleState_Off;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (SUCCEEDED(code))
            *result = node.m_Info.m_Checked ? ToggleState_On : ToggleState_Off;
        return code;
    }

  private:
    bool IsRoot() const
    {
        return m_Id == m_State->m_Target.GetTargetKey();
    }

    HRESULT Fragment(SemanticId id, IRawElementProviderFragment** result)
    {
        if (!id)
            return S_OK;
        auto* provider = new (std::nothrow) Provider(m_State, id);
        if (!provider)
            return E_OUTOFMEMORY;
        *result = static_cast<IRawElementProviderFragment*>(provider);
        return S_OK;
    }

    HRESULT Number(double* result, int field)
    {
        if (!result)
            return E_POINTER;
        *result = 0;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (FAILED(code))
            return code;
        const auto value = field == 0   ? node.m_Info.m_Number
                           : field == 1 ? node.m_Info.m_Minimum
                           : field == 2 ? node.m_Info.m_Maximum
                                        : node.m_Info.m_Step;
        if (!value)
            return UIA_E_NOTSUPPORTED;
        *result = *value;
        return S_OK;
    }

    std::atomic<ULONG> m_References = 1;
    std::shared_ptr<ProviderState> m_State;
    SemanticId m_Id;
};
} // namespace

struct NativeAccessibility::Impl
{
    std::shared_ptr<ProviderState> m_State;
    Provider* m_Root = nullptr;
    HWND m_Window;

    Impl(UiContext& ui, HWND window, std::weak_ptr<void> owner)
        : m_State(std::make_shared<ProviderState>(ui, window, std::move(owner))), m_Window(window)
    {
        m_Root = new Provider(m_State, m_State->m_Target.GetTargetKey());
        if (!SetWindowSubclass(window, Procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
        {
            m_Root->Release();
            throw std::runtime_error("Could not attach the Windows accessibility provider.");
        }
    }

    ~Impl()
    {
        Close();
        m_Root->Release();
    }

    void Close() noexcept
    {
        if (m_State->m_Closed.exchange(true))
            return;
        if (m_Window)
            RemoveWindowSubclass(m_Window, Procedure, reinterpret_cast<UINT_PTR>(this));
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(m_Root));
        m_Window = nullptr;
    }

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto& self = *reinterpret_cast<Impl*>(data);
        if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId && !self.m_State->m_Closed)
            return UiaReturnRawElementProvider(window, wParam, lParam,
                                               static_cast<IRawElementProviderSimple*>(self.m_Root));
        if (message == WM_NCDESTROY)
            self.Close();
        return DefSubclassProc(window, message, wParam, lParam);
    }

    void Flush()
    {
        auto state = m_State;
        if (state->m_Closed || state->m_Observed.empty() || !UiaClientsAreListening())
            return;
        auto& tree = state->m_Ui->Semantics();
        if (tree.GetRevision() == state->m_LastRevision)
            return;
        state->m_LastRevision = tree.GetRevision();
        const auto focus = tree.GetFocus();
        if (focus != state->m_LastFocus)
        {
            state->m_LastFocus = focus;
            if (focus)
            {
                auto* provider = new Provider(state, focus);
                UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider),
                                        UIA_AutomationFocusChangedEventId);
                provider->Release();
            }
        }
        // Copy the bounded observation set before native calls, which may reenter the provider.
        const auto observed = state->m_Observed;
        for (const auto& [id, old] : observed)
        {
            if (state->m_Closed)
                break;
            auto node = tree.GetNode(id);
            if (!node)
            {
                state->m_Observed.erase(id);
                continue;
            }
            state->ApplyHostState(*node);
            state->m_Observed.insert_or_assign(id, *node);
            auto* provider = new Provider(state, id);
            const auto changed = [&](PROPERTYID property, VARIANT before, VARIANT after)
            {
                UiaRaiseAutomationPropertyChangedEvent(static_cast<IRawElementProviderSimple*>(provider), property,
                                                       before, after);
                VariantClear(&before);
                VariantClear(&after);
            };
            const auto textChanged = [&](PROPERTYID property, const std::string& before, const std::string& after)
            {
                if (before == after)
                    return;
                VARIANT previous{}, current{};
                StringValue(before, &previous);
                StringValue(after, &current);
                changed(property, previous, current);
            };
            textChanged(UIA_NamePropertyId, old.m_Info.m_Name, node->m_Info.m_Name);
            textChanged(UIA_ValueValuePropertyId, old.m_Info.m_Value, node->m_Info.m_Value);
            if (old.m_Enabled != node->m_Enabled)
                changed(UIA_IsEnabledPropertyId, Boolean(old.m_Enabled), Boolean(node->m_Enabled));
            if (old.m_Offscreen != node->m_Offscreen)
                changed(UIA_IsOffscreenPropertyId, Boolean(old.m_Offscreen), Boolean(node->m_Offscreen));
            if (old.m_Info.m_Number != node->m_Info.m_Number && old.m_Info.m_Number && node->m_Info.m_Number)
            {
                VARIANT before{}, after{};
                before.vt = after.vt = VT_R8;
                before.dblVal = *old.m_Info.m_Number;
                after.dblVal = *node->m_Info.m_Number;
                changed(UIA_RangeValueValuePropertyId, before, after);
            }
            if (old.m_Info.m_Checked != node->m_Info.m_Checked)
            {
                VARIANT before{}, after{};
                before.vt = after.vt = VT_I4;
                before.lVal = old.m_Info.m_Checked ? ToggleState_On : ToggleState_Off;
                after.lVal = node->m_Info.m_Checked ? ToggleState_On : ToggleState_Off;
                changed(UIA_ToggleToggleStatePropertyId, before, after);
            }
            provider->Release();
        }
    }
};

NativeAccessibility::NativeAccessibility(UiContext& ui, void* nativeWindow, std::weak_ptr<void> owner)
    : m_Impl(std::make_unique<Impl>(ui, static_cast<HWND>(nativeWindow), std::move(owner)))
{
}

NativeAccessibility::~NativeAccessibility() = default;

void NativeAccessibility::Flush()
{
    m_Impl->Flush();
}

void NativeAccessibility::Close() noexcept
{
    if (m_Impl)
        m_Impl->Close();
}
} // namespace EStudioUI::Detail
#else
namespace EStudioUI::Detail
{
struct NativeAccessibility::Impl
{
};

NativeAccessibility::NativeAccessibility(UiContext&, void*, std::weak_ptr<void>)
{
    throw std::runtime_error("Native accessibility is not implemented on this backend.");
}

NativeAccessibility::~NativeAccessibility() = default;

void NativeAccessibility::Flush()
{
}

void NativeAccessibility::Close() noexcept
{
}
} // namespace EStudioUI::Detail
#endif
