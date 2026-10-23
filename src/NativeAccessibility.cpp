#include "NativeAccessibility.h"
#include "NativeAccessibilityText.h"
#include "WindowsSupport.h"

#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/ClipboardData.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

#ifdef _WIN32
#include <ole2.h>
#include <UIAutomation.h>
#include <commctrl.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <limits>
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
    case SemanticRole::Dialog:
    case SemanticRole::Window:
        return UIA_WindowControlTypeId;
    case SemanticRole::Text:
        return UIA_TextControlTypeId;
    case SemanticRole::Image:
        return UIA_ImageControlTypeId;
    case SemanticRole::Button:
        return UIA_ButtonControlTypeId;
    case SemanticRole::RadioButton:
        return UIA_RadioButtonControlTypeId;
    case SemanticRole::ComboBox:
        return UIA_ComboBoxControlTypeId;
    case SemanticRole::ProgressBar:
        return UIA_ProgressBarControlTypeId;
    case SemanticRole::Splitter:
        return UIA_ThumbControlTypeId;
    case SemanticRole::Separator:
        return UIA_SeparatorControlTypeId;
    case SemanticRole::ToolTip:
        return UIA_ToolTipControlTypeId;
    case SemanticRole::CheckBox:
        return UIA_CheckBoxControlTypeId;
    case SemanticRole::TextBox:
        return UIA_EditControlTypeId;
    case SemanticRole::ScrollBar:
        return UIA_ScrollBarControlTypeId;
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
    case SemanticRole::MenuBar:
        return UIA_MenuBarControlTypeId;
    case SemanticRole::Menu:
        return UIA_MenuControlTypeId;
    case SemanticRole::MenuItem:
        return UIA_MenuItemControlTypeId;
    case SemanticRole::Canvas:
        return UIA_PaneControlTypeId;
    case SemanticRole::ListItem:
    case SemanticRole::CanvasItem:
        return UIA_ListItemControlTypeId;
    case SemanticRole::TreeItem:
        return UIA_TreeItemControlTypeId;
    case SemanticRole::Row:
        return UIA_DataItemControlTypeId;
    case SemanticRole::Cell:
        return UIA_DataItemControlTypeId;
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

VARIANT Integer(LONG value)
{
    VARIANT result{};
    result.vt = VT_I4;
    result.lVal = value;
    return result;
}

VARIANT Rectangle(const std::optional<Rect>& bounds)
{
    VARIANT result{};
    if (!bounds)
        return result;
    auto* array = SafeArrayCreateVector(VT_R8, 0, 4);
    if (!array)
        return result;
    double values[] = {bounds->m_X, bounds->m_Y, bounds->m_Width, bounds->m_Height};
    for (LONG index = 0; index < 4; ++index)
    {
        if (FAILED(SafeArrayPutElement(array, &index, &values[index])))
        {
            SafeArrayDestroy(array);
            return result;
        }
    }
    result.vt = VT_ARRAY | VT_R8;
    result.parray = array;
    return result;
}

bool SameBounds(const std::optional<Rect>& left, const std::optional<Rect>& right)
{
    return left.has_value() == right.has_value() &&
           (!left || (left->m_X == right->m_X && left->m_Y == right->m_Y && left->m_Width == right->m_Width &&
                      left->m_Height == right->m_Height));
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
    std::deque<SemanticId> m_ObservationOrder;
    std::unordered_map<SemanticId, std::pair<std::size_t, std::size_t>> m_TextSelections;
    std::uint64_t m_LastRevision = 0;
    SemanticId m_LastFocus = 0;
    RECT m_LastWindowBounds{};
    bool m_LastHostEnabled = true;
    bool m_LastHostOffscreen = true;
    bool m_TraceSelections = false;

    ProviderState(UiContext& ui, HWND window, std::weak_ptr<void> owner)
        : m_Window(window), m_Ui(&ui), m_Target(ui.GetHandle(ui.Root())), m_Owner(std::move(owner))
    {
        wchar_t enabled[2]{};
        m_TraceSelections =
            GetEnvironmentVariableW(L"ESTUDIOUI_NATIVE_PLATFORM_TESTS", enabled, 2) == 1 && enabled[0] == L'1';
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
                    const auto owner = self->m_Owner.lock();
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
        node.m_Offscreen = node.m_Offscreen || !IsWindowVisible(m_Window) || IsIconic(m_Window);
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
                                   if (value && !self->m_Observed.contains(id))
                                   {
                                       while (self->m_Observed.size() >= 64)
                                       {
                                           while (!self->m_ObservationOrder.empty() &&
                                                  !self->m_Observed.contains(self->m_ObservationOrder.front()))
                                               self->m_ObservationOrder.pop_front();
                                           const auto oldest = self->m_ObservationOrder.front();
                                           self->m_ObservationOrder.pop_front();
                                           self->m_Observed.erase(oldest);
                                           self->m_TextSelections.erase(oldest);
                                       }
                                       self->m_Observed.emplace(id, *value);
                                       std::erase(self->m_ObservationOrder, id);
                                       self->m_ObservationOrder.push_back(id);
                                   }
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
                        const auto owner = self->m_Owner.lock();
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

    HRESULT TraceSelection(const char* operation, SemanticId id, HRESULT result)
    {
        if (!m_TraceSelections)
            return result;
        std::pair<bool, std::size_t> snapshot{};
        const auto queried =
            Call(snapshot,
                 [id](UiContext& ui)
                 {
                     auto& tree = ui.Semantics();
                     const auto node = tree.GetNode(id);
                     return std::pair{node && node->m_Info.m_Selected,
                                      node ? tree.GetSelectionCount(node->m_SelectionContainer) : std::size_t{0}};
                 });
        std::fprintf(stderr,
                     "Native selection: %s id=%llu/%llu/%llu/%llu selected=%d count=%zu query=%08lx result=%08lx\n",
                     operation, static_cast<unsigned long long>(id.m_Widget),
                     static_cast<unsigned long long>(id.m_Key.m_Item), static_cast<unsigned long long>(id.m_Key.m_Part),
                     static_cast<unsigned long long>(id.m_Key.m_Generation), snapshot.first ? 1 : 0, snapshot.second,
                     static_cast<unsigned long>(queried), static_cast<unsigned long>(result));
        std::fflush(stderr);
        return result;
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
                       public IToggleProvider,
                       public ISelectionProvider,
                       public ISelectionItemProvider,
                       public IGridProvider,
                       public IGridItemProvider,
                       public IExpandCollapseProvider,
                       public IScrollItemProvider,
                       public IVirtualizedItemProvider
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
        else if (iid == __uuidof(ISelectionProvider))
            *result = static_cast<ISelectionProvider*>(this);
        else if (iid == __uuidof(ISelectionItemProvider))
            *result = static_cast<ISelectionItemProvider*>(this);
        else if (iid == __uuidof(IGridProvider))
            *result = static_cast<IGridProvider*>(this);
        else if (iid == __uuidof(IGridItemProvider))
            *result = static_cast<IGridItemProvider*>(this);
        else if (iid == __uuidof(IExpandCollapseProvider))
            *result = static_cast<IExpandCollapseProvider*>(this);
        else if (iid == __uuidof(IScrollItemProvider))
            *result = static_cast<IScrollItemProvider*>(this);
        else if (iid == __uuidof(IVirtualizedItemProvider))
            *result = static_cast<IVirtualizedItemProvider*>(this);
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
        else if (pattern == UIA_ValuePatternId && IsValueControl(node))
            *result = static_cast<IValueProvider*>(this);
        else if (pattern == UIA_RangeValuePatternId && node.m_Info.m_Number && node.m_Info.m_Minimum &&
                 node.m_Info.m_Maximum)
            *result = static_cast<IRangeValueProvider*>(this);
        else if (pattern == UIA_TogglePatternId && HasSemanticAction(actions, SemanticAction::Toggle))
            *result = static_cast<IToggleProvider*>(this);
        else if (pattern == UIA_SelectionPatternId && IsSelectionContainer(node))
            *result = static_cast<ISelectionProvider*>(this);
        else if (pattern == UIA_SelectionItemPatternId &&
                 (node.m_SelectionContainer || node.m_Info.m_Role == SemanticRole::RadioButton))
            *result = static_cast<ISelectionItemProvider*>(this);
        else if (pattern == UIA_GridPatternId && node.m_Info.m_RowCount && node.m_Info.m_ColumnCount)
            *result = static_cast<IGridProvider*>(this);
        else if (pattern == UIA_GridItemPatternId && node.m_GridContainer && node.m_Info.m_Row && node.m_Info.m_Column)
            *result = static_cast<IGridItemProvider*>(this);
        else if (pattern == UIA_ExpandCollapsePatternId &&
                 (node.m_Info.m_Role == SemanticRole::TreeItem || node.m_Info.m_Role == SemanticRole::ComboBox ||
                  (node.m_Info.m_Role == SemanticRole::MenuItem && node.m_Info.m_Expandable)))
            *result = static_cast<IExpandCollapseProvider*>(this);
        else if (pattern == UIA_ScrollItemPatternId && HasSemanticAction(actions, SemanticAction::ScrollIntoView))
            *result = static_cast<IScrollItemProvider*>(this);
        else if (pattern == UIA_VirtualizedItemPatternId && m_Id.m_Key.m_Item &&
                 HasSemanticAction(actions, SemanticAction::ScrollIntoView))
            *result = static_cast<IVirtualizedItemProvider*>(this);
        else if ((pattern == UIA_TextPatternId || pattern == UIA_TextPattern2Id) &&
                 node.m_Info.m_Role == SemanticRole::TextBox)
            return TextPattern(result);
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
            case UIA_IsValuePatternAvailablePropertyId:
                *result = Boolean(IsValueControl(node));
                return S_OK;
            case UIA_ValueValuePropertyId:
                return IsValueControl(node) ? StringValue(node.m_Info.m_Value, result) : S_OK;
            case UIA_ValueIsReadOnlyPropertyId:
                if (IsValueControl(node))
                    *result = Boolean(node.m_Info.m_ReadOnly);
                return S_OK;
            case UIA_BoundingRectanglePropertyId:
                *result = Rectangle(node.m_ScreenBounds);
                return S_OK;
            case UIA_SelectionItemIsSelectedPropertyId:
                if (node.m_SelectionContainer)
                    *result = Boolean(node.m_Info.m_Selected);
                return S_OK;
            case UIA_ExpandCollapseExpandCollapseStatePropertyId:
                if (node.m_Info.m_Role == SemanticRole::TreeItem || node.m_Info.m_Expandable)
                    *result = Integer(!node.m_Info.m_Expandable ? ExpandCollapseState_LeafNode
                                      : node.m_Info.m_Expanded  ? ExpandCollapseState_Expanded
                                                                : ExpandCollapseState_Collapsed);
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
                                            if (direction == NavigateDirection_FirstChild)
                                                return tree.GetChildAt(id, 0);
                                            if (direction == NavigateDirection_LastChild)
                                            {
                                                const auto count = tree.GetChildCount(id);
                                                return count ? tree.GetChildAt(id, count - 1) : SemanticId{};
                                            }
                                            if (direction == NavigateDirection_NextSibling ||
                                                direction == NavigateDirection_PreviousSibling)
                                                return tree.GetSibling(id, direction == NavigateDirection_NextSibling);
                                            return 0;
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
        auto* array = SafeArrayCreateVector(VT_I4, 0, 9);
        if (!array)
            return E_OUTOFMEMORY;
        const std::uint64_t parts[] = {m_Id.m_Widget, m_Id.m_Key.m_Item, m_Id.m_Key.m_Part, m_Id.m_Key.m_Generation};
        LONG values[9] = {UiaAppendRuntimeId};
        for (std::size_t index = 0; index < 4; ++index)
        {
            values[index * 2 + 1] = static_cast<LONG>(parts[index] & 0xffffffffULL);
            values[index * 2 + 2] = static_cast<LONG>(parts[index] >> 32);
        }
        for (LONG index = 0; index < 9; ++index)
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
        SemanticNode node;
        const auto available = m_State->Node(m_Id, node);
        if (FAILED(available))
            return available;
        if (!node.m_Enabled)
            return UIA_E_ELEMENTNOTENABLED;
        if (node.m_Info.m_ReadOnly)
            return UIA_E_INVALIDOPERATION;
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
        const auto code = Number(result, 3);
        if (SUCCEEDED(code))
            *result *= 10;
        return code;
    }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* result) override
    {
        if (!result)
            return E_POINTER;
        *result = ToggleState_Off;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (SUCCEEDED(code))
            *result = node.m_Info.m_Indeterminate ? ToggleState_Indeterminate
                      : node.m_Info.m_Checked     ? ToggleState_On
                                                  : ToggleState_Off;
        return code;
    }

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;

        struct Selection
        {
            SemanticStatus m_Status = SemanticStatus::Unavailable;
            std::vector<SemanticId> m_Ids;
        };

        Selection selection;
        const auto code = m_State->Call(selection,
                                        [id = m_Id](UiContext& ui)
                                        {
                                            Selection value;
                                            auto& tree = ui.Semantics();
                                            const auto node = tree.GetNode(id);
                                            if (!node || !IsSelectionContainer(*node))
                                                return value;
                                            const auto count = tree.GetSelectionCount(id);
                                            // Native arrays are necessarily materialized. Reject an oversized
                                            // request explicitly rather than silently returning a partial selection.
                                            constexpr std::size_t maximum = 4096;
                                            if (count > maximum)
                                            {
                                                value.m_Status = SemanticStatus::Unsupported;
                                                return value;
                                            }
                                            value.m_Ids.reserve(count);
                                            for (std::size_t offset = 0; offset < count; offset += 256)
                                            {
                                                const auto page = tree.GetSelection(
                                                    id, offset, std::min<std::size_t>(256, count - offset));
                                                value.m_Ids.insert(value.m_Ids.end(), page.begin(), page.end());
                                            }
                                            value.m_Status = SemanticStatus::Succeeded;
                                            return value;
                                        });
        if (FAILED(code))
            return code;
        if (selection.m_Status != SemanticStatus::Succeeded)
            return StatusCode(selection.m_Status);
        return ProviderArray(selection.m_Ids, result);
    }

    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* result) override
    {
        return SelectionFlag(result, false);
    }

    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* result) override
    {
        return SelectionFlag(result, true);
    }

    HRESULT STDMETHODCALLTYPE Select() override
    {
        return m_State->TraceSelection("Select", m_Id, m_State->Action(m_Id, {SemanticAction::Select}));
    }

    HRESULT STDMETHODCALLTYPE AddToSelection() override
    {
        const auto finish = [this](HRESULT result) -> HRESULT { return m_State->TraceSelection("Add", m_Id, result); };
        SemanticNode item;
        auto code = m_State->Node(m_Id, item);
        if (FAILED(code))
            return finish(code);
        if (item.m_Info.m_Selected)
            return finish(S_OK);
        SemanticNode container;
        code = m_State->Node(item.m_SelectionContainer, container);
        if (FAILED(code))
            return finish(code);
        if (!container.m_Info.m_MultiSelectable)
        {
            std::size_t selected = 0;
            code = m_State->Call(selected, [id = item.m_SelectionContainer](UiContext& ui)
                                 { return ui.Semantics().GetSelectionCount(id); });
            if (FAILED(code))
                return finish(code);
            if (selected)
                return finish(UIA_E_INVALIDOPERATION);
        }
        return finish(m_State->Action(m_Id, {SemanticAction::AddToSelection}));
    }

    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override
    {
        const auto finish = [this](HRESULT result) -> HRESULT
        { return m_State->TraceSelection("Remove", m_Id, result); };
        SemanticNode item;
        auto code = m_State->Node(m_Id, item);
        if (FAILED(code))
            return finish(code);
        SemanticNode container;
        code = m_State->Node(item.m_SelectionContainer, container);
        if (FAILED(code))
            return finish(code);
        if (item.m_Info.m_Selected && container.m_Info.m_SelectionRequired)
        {
            std::size_t selected = 0;
            code = m_State->Call(selected, [id = item.m_SelectionContainer](UiContext& ui)
                                 { return ui.Semantics().GetSelectionCount(id); });
            if (FAILED(code))
                return finish(code);
            if (selected <= 1)
                return finish(UIA_E_INVALIDOPERATION);
        }
        return finish(m_State->Action(m_Id, {SemanticAction::RemoveFromSelection}));
    }

    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* result) override
    {
        if (!result)
            return E_POINTER;
        *result = FALSE;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (SUCCEEDED(code))
            *result = node.m_Info.m_Selected ? TRUE : FALSE;
        return code;
    }

    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        return FAILED(code) ? code : Simple(node.m_SelectionContainer, result);
    }

    HRESULT STDMETHODCALLTYPE GetItem(int row, int column, IRawElementProviderSimple** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (row < 0 || column < 0)
            return E_INVALIDARG;
        SemanticId item;
        const auto code = m_State->Call(item,
                                        [id = m_Id, row, column](UiContext& ui)
                                        {
                                            return ui.Semantics().GetGridItem(id, static_cast<std::size_t>(row),
                                                                              static_cast<std::size_t>(column));
                                        });
        if (FAILED(code))
            return code;
        return item ? Simple(item, result) : E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE get_RowCount(int* result) override
    {
        return GridNumber(result, 0);
    }

    HRESULT STDMETHODCALLTYPE get_ColumnCount(int* result) override
    {
        return GridNumber(result, 1);
    }

    HRESULT STDMETHODCALLTYPE get_Row(int* result) override
    {
        return GridNumber(result, 2);
    }

    HRESULT STDMETHODCALLTYPE get_Column(int* result) override
    {
        return GridNumber(result, 3);
    }

    HRESULT STDMETHODCALLTYPE get_RowSpan(int* result) override
    {
        if (!result)
            return E_POINTER;
        *result = 1;
        SemanticNode node;
        return m_State->Node(m_Id, node);
    }

    HRESULT STDMETHODCALLTYPE get_ColumnSpan(int* result) override
    {
        return get_RowSpan(result);
    }

    HRESULT STDMETHODCALLTYPE get_ContainingGrid(IRawElementProviderSimple** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        return FAILED(code) ? code : Simple(node.m_GridContainer, result);
    }

    HRESULT STDMETHODCALLTYPE Expand() override
    {
        return ExpandBranch(true);
    }

    HRESULT STDMETHODCALLTYPE Collapse() override
    {
        return ExpandBranch(false);
    }

    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* result) override
    {
        if (!result)
            return E_POINTER;
        *result = ExpandCollapseState_LeafNode;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (SUCCEEDED(code) && node.m_Info.m_Expandable)
            *result = node.m_Info.m_Expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return code;
    }

    HRESULT STDMETHODCALLTYPE ScrollIntoView() override
    {
        return m_State->Action(m_Id, {SemanticAction::ScrollIntoView});
    }

    HRESULT STDMETHODCALLTYPE Realize() override
    {
        return ScrollIntoView();
    }

  private:
    static bool IsValueControl(const SemanticNode& node)
    {
        return node.m_Info.m_Role == SemanticRole::TextBox || node.m_Info.m_Role == SemanticRole::Cell ||
               node.m_Info.m_Role == SemanticRole::ComboBox ||
               HasSemanticAction(node.m_Info.m_Actions, SemanticAction::SetText);
    }

    static bool IsSelectionContainer(const SemanticNode& node)
    {
        const auto role = node.m_Info.m_Role;
        return role == SemanticRole::List || role == SemanticRole::Tree || role == SemanticRole::Table ||
               role == SemanticRole::Canvas || role == SemanticRole::TabList;
    }

    bool IsRoot() const
    {
        return m_Id == m_State->m_Target.GetTargetKey();
    }

    HRESULT Simple(SemanticId id, IRawElementProviderSimple** result)
    {
        if (!id)
            return S_OK;
        auto* provider = new (std::nothrow) Provider(m_State, id);
        if (!provider)
            return E_OUTOFMEMORY;
        *result = static_cast<IRawElementProviderSimple*>(provider);
        return S_OK;
    }

    HRESULT ProviderArray(const std::vector<SemanticId>& ids, SAFEARRAY** result)
    {
        auto* array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(ids.size()));
        if (!array)
            return E_OUTOFMEMORY;
        for (LONG index = 0; index < static_cast<LONG>(ids.size()); ++index)
        {
            IRawElementProviderSimple* provider = nullptr;
            const auto created = Simple(ids[static_cast<std::size_t>(index)], &provider);
            const auto inserted = FAILED(created) ? created : SafeArrayPutElement(array, &index, provider);
            if (provider)
                provider->Release();
            if (FAILED(inserted))
            {
                SafeArrayDestroy(array);
                return inserted;
            }
        }
        *result = array;
        return S_OK;
    }

    HRESULT SelectionFlag(BOOL* result, bool required)
    {
        if (!result)
            return E_POINTER;
        *result = FALSE;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (SUCCEEDED(code))
            *result = (required ? node.m_Info.m_SelectionRequired : node.m_Info.m_MultiSelectable) ? TRUE : FALSE;
        return code;
    }

    HRESULT GridNumber(int* result, int field)
    {
        if (!result)
            return E_POINTER;
        *result = 0;
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (FAILED(code))
            return code;
        const auto value = field == 0   ? node.m_Info.m_RowCount
                           : field == 1 ? node.m_Info.m_ColumnCount
                           : field == 2 ? node.m_Info.m_Row
                                        : node.m_Info.m_Column;
        if (!value || *value > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return UIA_E_NOTSUPPORTED;
        *result = static_cast<int>(*value);
        return S_OK;
    }

    HRESULT ExpandBranch(bool expand)
    {
        SemanticNode node;
        const auto code = m_State->Node(m_Id, node);
        if (FAILED(code))
            return code;
        if (!node.m_Info.m_Expandable)
            return UIA_E_INVALIDOPERATION;
        if (node.m_Info.m_Expanded == expand)
            return S_OK;
        return m_State->Action(m_Id, {expand ? SemanticAction::Expand : SemanticAction::Collapse});
    }

    static TextBox* TextEditor(UiContext& ui, SemanticId id)
    {
        return dynamic_cast<TextBox*>(ui.Semantics().GetWidget(id).Get());
    }

    static Rect TextScreenBounds(UiContext& ui, const TextBox& editor, Rect local)
    {
        local = local.Intersect(editor.GetContentBounds());
        if (local.m_Width <= 0 || local.m_Height <= 0)
            return {};
        const Point points[] = {{local.m_X, local.m_Y},
                                {local.m_X + local.m_Width, local.m_Y},
                                {local.m_X, local.m_Y + local.m_Height},
                                {local.m_X + local.m_Width, local.m_Y + local.m_Height}};
        auto first = ui.ToWindowPoint(editor, points[0]);
        float left = first.m_X, top = first.m_Y, right = left, bottom = top;
        for (const auto point : points)
        {
            const auto transformed = ui.ToWindowPoint(editor, point);
            left = std::min(left, transformed.m_X);
            top = std::min(top, transformed.m_Y);
            right = std::max(right, transformed.m_X);
            bottom = std::max(bottom, transformed.m_Y);
        }
        auto bounds = Rect{left, top, right - left, bottom - top};
        if (const auto node = ui.Semantics().GetNode(ui.Semantics().GetId(editor)))
            bounds = bounds.Intersect(node->m_Bounds);
        return ui.Platform().GetWindowMetrics().ToScreen(bounds);
    }

    HRESULT TextPattern(IUnknown** result)
    {
        try
        {
            bool supported = false;
            auto code = m_State->Call(supported, [id = m_Id](UiContext& ui) { return TextEditor(ui, id) != nullptr; });
            if (FAILED(code) || !supported)
                return FAILED(code) ? code : S_OK;
            const auto state = m_State;
            const auto id = m_Id;
            NativeTextCallbacks callbacks;
            callbacks.m_Domain = state;
            callbacks.m_Identity = id.m_Widget;
            callbacks.m_Query = [state, id](NativeTextSnapshot& snapshot) -> HRESULT
            {
                std::optional<NativeTextSnapshot> value;
                const auto status =
                    state->Call(value,
                                [state, id](UiContext& ui) -> std::optional<NativeTextSnapshot>
                                {
                                    const auto* editor = TextEditor(ui, id);
                                    auto node = ui.Semantics().GetNode(id);
                                    if (!editor || !node)
                                        return {};
                                    state->ApplyHostState(*node);
                                    NativeTextSnapshot current;
                                    current.m_Text = editor->GetText();
                                    current.m_Anchor = editor->GetSelectionAnchor();
                                    current.m_Caret = editor->GetSelectionCaret();
                                    current.m_ReadOnly = editor->IsReadOnly();
                                    current.m_Focused = ui.GetFocus() == editor;
                                    const auto content = editor->GetContentBounds();
                                    current.m_Visible =
                                        !node->m_Offscreen && content.m_Width > 0 && content.m_Height > 0;
                                    current.m_VisibleRanges = editor->GetVisibleTextRanges();
                                    if (!current.m_VisibleRanges.empty())
                                    {
                                        current.m_VisibleStart = current.m_VisibleRanges.front().first;
                                        current.m_VisibleEnd = current.m_VisibleRanges.back().second;
                                    }
                                    if (state->m_Observed.contains(id))
                                        state->m_TextSelections.try_emplace(id, current.m_Anchor, current.m_Caret);
                                    return current;
                                });
                if (FAILED(status))
                    return status;
                if (!value)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                snapshot = std::move(*value);
                return S_OK;
            };
            callbacks.m_Bounds = [state, id](std::size_t start, std::size_t end, std::vector<Rect>& bounds) -> HRESULT
            {
                std::optional<std::vector<Rect>> value;
                const auto status =
                    state->Call(value,
                                [id, start, end](UiContext& ui) -> std::optional<std::vector<Rect>>
                                {
                                    const auto* editor = TextEditor(ui, id);
                                    if (!editor || !ui.Platform().GetWindowMetrics().m_HasScreenCoordinates)
                                        return {};
                                    auto rectangles = editor->GetTextRangeRectangles(start, end);
                                    for (auto& rectangle : rectangles)
                                        rectangle = TextScreenBounds(ui, *editor, rectangle);
                                    return rectangles;
                                });
                if (FAILED(status))
                    return status;
                if (!value)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                bounds = std::move(*value);
                return S_OK;
            };
            callbacks.m_Select = [state, id](std::size_t anchor, std::size_t caret) -> HRESULT
            {
                SemanticStatus value = SemanticStatus::Unavailable;
                const auto status =
                    state->Call(value,
                                [state, id, anchor, caret](UiContext& ui)
                                {
                                    auto* editor = TextEditor(ui, id);
                                    const auto node = ui.Semantics().GetNode(id);
                                    if (!editor || !node)
                                        return SemanticStatus::Unavailable;
                                    if (!node->m_Enabled || !IsWindowEnabled(state->m_Window))
                                        return SemanticStatus::Disabled;
                                    if (anchor > editor->GetText().size() || caret > editor->GetText().size())
                                        return SemanticStatus::InvalidValue;
                                    editor->SetSelection(anchor, caret);
                                    return SemanticStatus::Succeeded;
                                });
                return FAILED(status) ? status : StatusCode(value);
            };
            callbacks.m_Point = [state, id](double x, double y, std::size_t& offset) -> HRESULT
            {
                std::optional<std::size_t> value;
                const auto status = state->Call(value,
                                                [id, x, y](UiContext& ui) -> std::optional<std::size_t>
                                                {
                                                    const auto* editor = TextEditor(ui, id);
                                                    if (!editor)
                                                        return {};
                                                    const auto point = ui.Platform().GetWindowMetrics().FromScreen(
                                                        {static_cast<float>(x), static_cast<float>(y)});
                                                    return editor->TextOffsetAt(ui.ToLayoutPoint(*editor, point));
                                                });
                if (FAILED(status))
                    return status;
                if (!value)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                offset = *value;
                return S_OK;
            };
            callbacks.m_Enclosing = [state, id](IRawElementProviderSimple** enclosing) -> HRESULT
            {
                if (!enclosing)
                    return E_POINTER;
                *enclosing = nullptr;
                SemanticNode node;
                const auto status = state->Node(id, node);
                if (FAILED(status))
                    return status;
                auto* provider = new (std::nothrow) Provider(state, id);
                if (!provider)
                    return E_OUTOFMEMORY;
                *enclosing = static_cast<IRawElementProviderSimple*>(provider);
                return S_OK;
            };
            callbacks.m_Scroll = [state, id](std::size_t start, std::size_t end, bool) -> HRESULT
            {
                SemanticStatus value = SemanticStatus::Unavailable;
                const auto status = state->Call(value,
                                                [state, id, start, end](UiContext& ui)
                                                {
                                                    auto* editor = TextEditor(ui, id);
                                                    const auto node = ui.Semantics().GetNode(id);
                                                    if (!editor || !node)
                                                        return SemanticStatus::Unavailable;
                                                    if (!node->m_Enabled || !IsWindowEnabled(state->m_Window))
                                                        return SemanticStatus::Disabled;
                                                    editor->RevealTextRange(start, end);
                                                    return SemanticStatus::Succeeded;
                                                });
                return FAILED(status) ? status : StatusCode(value);
            };
            return CreateNativeTextProvider(std::move(callbacks), result);
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
        // Native notification delivery can reenter application callbacks. Keep
        // the containing UiContext alive until this bounded pass has unwound.
        const auto owner = state->m_Owner.lock();
        if (state->m_Closed || state->m_Observed.empty() || !UiaClientsAreListening())
            return;
        auto& tree = state->m_Ui->Semantics();
        RECT windowBounds{};
        GetWindowRect(state->m_Window, &windowBounds);
        const bool enabled = IsWindowEnabled(state->m_Window) != FALSE;
        const bool offscreen = !IsWindowVisible(state->m_Window) || IsIconic(state->m_Window);
        const bool hostChanged = !EqualRect(&windowBounds, &state->m_LastWindowBounds) ||
                                 enabled != state->m_LastHostEnabled || offscreen != state->m_LastHostOffscreen;
        if (tree.GetRevision() == state->m_LastRevision && !hostChanged)
            return;
        state->m_LastRevision = tree.GetRevision();
        state->m_LastWindowBounds = windowBounds;
        state->m_LastHostEnabled = enabled;
        state->m_LastHostOffscreen = offscreen;
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
            // A native event can query another node and evict a later entry in
            // this snapshot. Do not resurrect evicted entries during this flush.
            if (!state->m_Observed.contains(id))
                continue;
            auto node = tree.GetNode(id);
            if (!node)
            {
                state->m_Observed.erase(id);
                state->m_TextSelections.erase(id);
                std::erase(state->m_ObservationOrder, id);
                continue;
            }
            state->ApplyHostState(*node);
            state->m_Observed.at(id) = *node;
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
            textChanged(UIA_HelpTextPropertyId, old.m_Info.m_Description, node->m_Info.m_Description);
            textChanged(UIA_ValueValuePropertyId, old.m_Info.m_Value, node->m_Info.m_Value);
            if (old.m_Info.m_Value != node->m_Info.m_Value && node->m_Info.m_Role == SemanticRole::TextBox)
                UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider), UIA_Text_TextChangedEventId);
            if (!SameBounds(old.m_ScreenBounds, node->m_ScreenBounds))
                changed(UIA_BoundingRectanglePropertyId, Rectangle(old.m_ScreenBounds),
                        Rectangle(node->m_ScreenBounds));
            if (old.m_Enabled != node->m_Enabled)
                changed(UIA_IsEnabledPropertyId, Boolean(old.m_Enabled), Boolean(node->m_Enabled));
            if (old.m_Focused != node->m_Focused)
                changed(UIA_HasKeyboardFocusPropertyId, Boolean(old.m_Focused), Boolean(node->m_Focused));
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
            if ((old.m_Info.m_Checked != node->m_Info.m_Checked ||
                 old.m_Info.m_Indeterminate != node->m_Info.m_Indeterminate))
            {
                VARIANT before{}, after{};
                before.vt = after.vt = VT_I4;
                before.lVal = old.m_Info.m_Indeterminate ? ToggleState_Indeterminate
                              : old.m_Info.m_Checked     ? ToggleState_On
                                                         : ToggleState_Off;
                after.lVal = node->m_Info.m_Indeterminate ? ToggleState_Indeterminate
                             : node->m_Info.m_Checked     ? ToggleState_On
                                                          : ToggleState_Off;
                changed(UIA_ToggleToggleStatePropertyId, before, after);
            }
            if (old.m_Info.m_Selected != node->m_Info.m_Selected)
            {
                changed(UIA_SelectionItemIsSelectedPropertyId, Boolean(old.m_Info.m_Selected),
                        Boolean(node->m_Info.m_Selected));
                UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider),
                                        node->m_Info.m_Selected ? UIA_SelectionItem_ElementSelectedEventId
                                                                : UIA_SelectionItem_ElementRemovedFromSelectionEventId);
            }
            if (old.m_Info.m_Expandable != node->m_Info.m_Expandable ||
                old.m_Info.m_Expanded != node->m_Info.m_Expanded)
            {
                const auto expansion = [](const SemanticInfo& info)
                {
                    return !info.m_Expandable ? ExpandCollapseState_LeafNode
                           : info.m_Expanded  ? ExpandCollapseState_Expanded
                                              : ExpandCollapseState_Collapsed;
                };
                changed(UIA_ExpandCollapseExpandCollapseStatePropertyId, Integer(expansion(old.m_Info)),
                        Integer(expansion(node->m_Info)));
            }
            if (old.m_Info.m_RowCount != node->m_Info.m_RowCount && old.m_Info.m_RowCount && node->m_Info.m_RowCount &&
                *old.m_Info.m_RowCount <= static_cast<std::size_t>(std::numeric_limits<LONG>::max()) &&
                *node->m_Info.m_RowCount <= static_cast<std::size_t>(std::numeric_limits<LONG>::max()))
                changed(UIA_GridRowCountPropertyId, Integer(static_cast<LONG>(*old.m_Info.m_RowCount)),
                        Integer(static_cast<LONG>(*node->m_Info.m_RowCount)));
            if (old.m_Info.m_ColumnCount != node->m_Info.m_ColumnCount && old.m_Info.m_ColumnCount &&
                node->m_Info.m_ColumnCount &&
                *old.m_Info.m_ColumnCount <= static_cast<std::size_t>(std::numeric_limits<LONG>::max()) &&
                *node->m_Info.m_ColumnCount <= static_cast<std::size_t>(std::numeric_limits<LONG>::max()))
                changed(UIA_GridColumnCountPropertyId, Integer(static_cast<LONG>(*old.m_Info.m_ColumnCount)),
                        Integer(static_cast<LONG>(*node->m_Info.m_ColumnCount)));
            if (old.m_Info.m_StructureRevision != node->m_Info.m_StructureRevision ||
                old.m_TreeStructureRevision != node->m_TreeStructureRevision || old.m_Parent != node->m_Parent)
                UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(provider),
                                              StructureChangeType_ChildrenInvalidated, nullptr, 0);
            if (!state->m_Closed)
            {
                const auto selected = state->m_TextSelections.find(id);
                if (selected != state->m_TextSelections.end())
                {
                    if (const auto* editor = dynamic_cast<TextBox*>(tree.GetWidget(id).Get()))
                    {
                        const auto current = std::pair(editor->GetSelectionAnchor(), editor->GetSelectionCaret());
                        if (current != selected->second)
                        {
                            selected->second = current;
                            UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider),
                                                    UIA_Text_TextSelectionChangedEventId);
                        }
                    }
                }
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
