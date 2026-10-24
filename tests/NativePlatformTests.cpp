#include "NativeAccessibility.h"
#include "NativePlatformServices.h"
#include "WindowsSupport.h"

#include <EStudioUI/Button.h>
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/MenuBar.h>
#include <EStudioUI/InputDialog.h>
#include <EStudioUI/ImageView.h>
#include <EStudioUI/ScrollBar.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/ProgressBar.h>
#include <EStudioUI/RadioButton.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/UiContext.h>

#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
using Microsoft::WRL::ComPtr;
int s_Checks = 0;

void Trace(const char* message)
{
    std::cout << "Native probe: " << message << std::endl;
}

class ProbeDeadline
{
  public:
    ProbeDeadline()
        : m_Thread(
              [this](std::stop_token stop)
              {
                  std::unique_lock lock(m_Mutex);
                  m_Changed.wait_for(lock, stop, std::chrono::seconds(70), [] { return false; });
                  if (!stop.stop_requested())
                  {
                      std::fputs("Native probe exceeded 70 seconds; see the last completed stage.\n", stderr);
                      std::fflush(stderr);
                      TerminateProcess(GetCurrentProcess(), 2);
                  }
              })
    {
    }

  private:
    std::mutex m_Mutex;
    std::condition_variable_any m_Changed;
    std::jthread m_Thread;
};

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
        throw std::runtime_error(message);
}

void Require(HRESULT code, const char* message)
{
    if (FAILED(code))
        throw std::runtime_error(std::string(message) + " HRESULT=" + std::to_string(code));
}

struct NativeOwner
{
    HWND m_Window = nullptr;

    NativeOwner()
    {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"EStudioUI.NativePlatform.Test";
        RegisterClassW(&type);
        m_Window = CreateWindowExW(0, type.lpszClassName, L"EStudioUI native platform test", WS_OVERLAPPEDWINDOW, 40,
                                   40, 820, 640, nullptr, nullptr, type.hInstance, nullptr);
        if (!m_Window)
            throw std::runtime_error("Create native platform test owner");
    }

    ~NativeOwner()
    {
        if (m_Window)
            DestroyWindow(m_Window);
    }
};

void Pump(UiContext& ui, Detail::NativeAccessibility* accessibility = nullptr)
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    ui.GetDispatcher().Drain();
    ui.PumpPlatformServices();
    if (accessibility)
        accessibility->Flush();
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
}

template <typename Predicate>
bool Until(UiContext& ui, Predicate predicate, int milliseconds = 10000,
           Detail::NativeAccessibility* accessibility = nullptr)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        Pump(ui, accessibility);
    return predicate();
}

PlatformHost Host(UiContext& ui, HWND window)
{
    PlatformHost host;
    host.m_Dispatcher = ui.GetDispatcher();
    host.m_NativeWindow = reinterpret_cast<std::uintptr_t>(window);
    host.m_WakeNativeLoop = [window] { PostMessageW(window, WM_NULL, 0, 0); };
    host.m_GetMetrics = [window]
    {
        RECT client{};
        POINT origin{};
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
        WindowMetrics metrics;
        metrics.m_LogicalSize = {800, 600};
        metrics.m_FramebufferSize = {static_cast<float>(client.right), static_cast<float>(client.bottom)};
        metrics.m_ScreenOrigin = {static_cast<float>(origin.x), static_cast<float>(origin.y)};
        metrics.m_HasScreenCoordinates = true;
        return metrics;
    };
    host.m_FocusWindow = [] { return PlatformResult{}; };
    return host;
}

ComPtr<IUIAutomationElement> Find(IUIAutomation& automation, IUIAutomationElement& root, const wchar_t* id)
{
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition;
    const auto code = automation.CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value);
    Require(code, "Create native automation condition");
    ComPtr<IUIAutomationElement> element;
    Require(root.FindFirst(TreeScope_Children, condition.Get(), &element), "Find native semantic child");
    if (!element)
        throw std::runtime_error("Native semantic child was not exposed");
    return element;
}

class ProbeEvents final : public IUIAutomationPropertyChangedEventHandler,
                          public IUIAutomationEventHandler,
                          public IUIAutomationStructureChangedEventHandler
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IUIAutomationPropertyChangedEventHandler))
            *result = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        else if (iid == __uuidof(IUIAutomationEventHandler))
            *result = static_cast<IUIAutomationEventHandler*>(this);
        else if (iid == __uuidof(IUIAutomationStructureChangedEventHandler))
            *result = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
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

    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID property,
                                                         VARIANT value) override
    {
        if (property == UIA_ValueValuePropertyId && value.vt == VT_BSTR && value.bstrVal &&
            std::wstring_view(value.bstrVal) == L"After \u03a9")
            ++m_Values;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID event) override
    {
        if (event == UIA_Text_TextChangedEventId)
            ++m_Text;
        else if (event == UIA_Text_TextSelectionChangedEventId)
            ++m_Selection;
        else if (event == UIA_SelectionItem_ElementSelectedEventId)
            ++m_Items;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType type,
                                                          SAFEARRAY*) override
    {
        if (type == StructureChangeType_ChildrenInvalidated || type == StructureChangeType_ChildRemoved)
            ++m_Structure;
        return S_OK;
    }

    std::atomic<int> m_Values = 0;
    std::atomic<int> m_Text = 0;
    std::atomic<int> m_Selection = 0;
    std::atomic<int> m_Structure = 0;
    std::atomic<int> m_Items = 0;

  private:
    std::atomic<ULONG> m_References = 1;
};

class EventSubscriptions
{
  public:
    explicit EventSubscriptions(IUIAutomation& automation) : m_Automation(automation)
    {
        m_Events.Attach(new ProbeEvents());
    }

    ~EventSubscriptions()
    {
        if (!m_Stopped)
            m_Automation.RemoveAllEventHandlers();
    }

    void WatchEditor(IUIAutomationElement& editor)
    {
        PROPERTYID properties[] = {UIA_ValueValuePropertyId};
        Require(m_Automation.AddPropertyChangedEventHandlerNativeArray(&editor, TreeScope_Element, nullptr,
                                                                       m_Events.Get(), properties, 1),
                "Subscribe to native value-property changes");
        for (const auto event : {UIA_Text_TextChangedEventId, UIA_Text_TextSelectionChangedEventId})
            Require(m_Automation.AddAutomationEventHandler(event, &editor, TreeScope_Element, nullptr, m_Events.Get()),
                    "Subscribe to native text and selection changes");
    }

    void WatchStructure(IUIAutomationElement& list)
    {
        Require(m_Automation.AddStructureChangedEventHandler(&list, TreeScope_Element, nullptr, m_Events.Get()),
                "Subscribe to native virtual-child structure changes");
    }

    void WatchItem(IUIAutomationElement& item)
    {
        Require(m_Automation.AddAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, &item,
                                                       TreeScope_Element, nullptr, m_Events.Get()),
                "Subscribe to native item-selection changes");
    }

    ProbeEvents& Events() const
    {
        return *m_Events.Get();
    }

    void Stop()
    {
        Require(m_Automation.RemoveAllEventHandlers(), "Unsubscribe native events on the registering MTA thread");
        m_Stopped = true;
    }

  private:
    IUIAutomation& m_Automation;
    ComPtr<ProbeEvents> m_Events;
    bool m_Stopped = false;
};

void AwaitEvent(const std::atomic<int>& counter, int before, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (counter.load() <= before && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(counter.load() > before, message);
}

void TraceElement(IUIAutomationElement& element, const char* label)
{
    BSTR name = nullptr;
    Require(element.get_CurrentName(&name), "Read native diagnostic element name");
    std::cout << "Native element " << label << " name=";
    if (name)
        std::cout << Detail::FromWindowsText({name, SysStringLen(name)});
    SysFreeString(name);
    SAFEARRAY* runtime = nullptr;
    Require(element.GetRuntimeId(&runtime), "Read native diagnostic runtime identity");
    LONG first = 0, last = -1;
    if (runtime)
    {
        Require(SafeArrayGetLBound(runtime, 1, &first), "Read native runtime identity lower bound");
        Require(SafeArrayGetUBound(runtime, 1, &last), "Read native runtime identity upper bound");
        std::cout << " runtime=";
        for (LONG index = first; index <= last && index - first < 32; ++index)
        {
            LONG value = 0;
            const auto code = SafeArrayGetElement(runtime, &index, &value);
            if (SUCCEEDED(code))
                std::cout << value << ',';
        }
        SafeArrayDestroy(runtime);
    }
    std::cout << std::endl;
}

struct SelectionSnapshot
{
    bool m_First = false;
    bool m_Second = false;
    int m_Count = 0;
    bool m_MatchesFirst = false;
    bool m_MatchesSecond = false;
    int m_Current = -1;
};

SelectionSnapshot TraceSelection(IUIAutomation& automation, IUIAutomationSelectionPattern& selection,
                                 IUIAutomationElement& first, IUIAutomationElement& second,
                                 IUIAutomationSelectionItemPattern& firstPattern,
                                 IUIAutomationSelectionItemPattern& secondPattern,
                                 const std::atomic<int>& canonicalCurrent, const char* label)
{
    BOOL firstSelected = FALSE, secondSelected = FALSE;
    Require(firstPattern.get_CurrentIsSelected(&firstSelected), "Read first diagnostic selected state");
    Require(secondPattern.get_CurrentIsSelected(&secondSelected), "Read second diagnostic selected state");
    ComPtr<IUIAutomationElementArray> selected;
    Require(selection.GetCurrentSelection(&selected), "Read diagnostic selected identities");
    int count = 0;
    Require(selected->get_Length(&count), "Count diagnostic selected identities");
    BOOL matchesFirst = FALSE, matchesSecond = FALSE;
    if (count == 1)
    {
        ComPtr<IUIAutomationElement> current;
        Require(selected->GetElement(0, &current), "Read diagnostic current selection identity");
        Require(automation.CompareElements(current.Get(), &first, &matchesFirst), "Compare first selected identity");
        Require(automation.CompareElements(current.Get(), &second, &matchesSecond), "Compare second selected identity");
    }
    std::cout << "Native selection " << label << ": first=" << firstSelected << " second=" << secondSelected
              << " count=" << count << " matchesFirst=" << matchesFirst << " matchesSecond=" << matchesSecond
              << " canonicalCurrent=" << canonicalCurrent.load() << std::endl;
    return {firstSelected != FALSE, secondSelected != FALSE, count,
            matchesFirst != FALSE,  matchesSecond != FALSE,  canonicalCurrent.load()};
}

class ScopedAutoFocus
{
  public:
    explicit ScopedAutoFocus(IUIAutomation& automation)
    {
        Require(automation.QueryInterface(IID_PPV_ARGS(&m_Automation)), "Obtain native client focus policy");
        Require(m_Automation->get_AutoSetFocus(&m_Previous), "Read native automatic focus policy");
        Require(m_Automation->put_AutoSetFocus(FALSE), "Isolate native pattern actions from automatic focus");
    }

    ~ScopedAutoFocus()
    {
        m_Automation->put_AutoSetFocus(m_Previous);
    }

    ScopedAutoFocus(const ScopedAutoFocus&) = delete;
    ScopedAutoFocus& operator=(const ScopedAutoFocus&) = delete;

  private:
    ComPtr<IUIAutomation2> m_Automation;
    BOOL m_Previous = TRUE;
};

void NativeDefaultFocus(IUIAutomation& automation, IUIAutomationElement& root, const std::atomic<int>& canonicalCurrent)
{
    Trace("UI Automation default automatic focus");
    ComPtr<IUIAutomation2> options;
    Require(automation.QueryInterface(IID_PPV_ARGS(&options)), "Obtain native client automatic focus options");
    BOOL autoFocus = FALSE;
    Require(options->get_AutoSetFocus(&autoFocus), "Read default native client automatic focus");
    Check(autoFocus, "Native client starts with its documented automatic focus behavior");
    auto list = Find(automation, root, L"list");
    ComPtr<IUIAutomationTreeWalker> walker;
    Require(automation.get_ControlViewWalker(&walker), "Obtain default-focus tree walker");
    ComPtr<IUIAutomationElement> first, second;
    Require(walker->GetFirstChildElement(list.Get(), &first), "Read first default-focus row");
    Require(walker->GetNextSiblingElement(first.Get(), &second), "Read second default-focus row");
    ComPtr<IUIAutomationSelectionItemPattern> firstSelection, secondSelection;
    Require(first->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&firstSelection)),
            "Obtain first default-focus selection pattern");
    Require(second->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&secondSelection)),
            "Obtain second default-focus selection pattern");
    ComPtr<IUIAutomationSelectionPattern> selection;
    Require(list->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection)),
            "Obtain default-focus selection container");
    Require(firstSelection->Select(), "Select first row using default automatic focus");
    Require(secondSelection->AddToSelection(), "Default client focuses the second row before its additive action");
    const auto state = TraceSelection(automation, *selection.Get(), *first.Get(), *second.Get(), *firstSelection.Get(),
                                      *secondSelection.Get(), canonicalCurrent, "with default automatic focus");
    Check(!state.m_First && state.m_Second && state.m_Count == 1 && state.m_MatchesSecond && state.m_Current == 1,
          "Default native automatic focus updates canonical current selection before the pattern action");
}

void NativeCollections(IUIAutomation& automation, IUIAutomationElement& root, EventSubscriptions& subscriptions,
                       const std::atomic<int>& canonicalCurrent)
{
    Trace("UI Automation virtual list");
    // Native automatic focus canonically selects a row before AddToSelection.
    // Obtain fresh collection elements under the isolated client policy so the
    // following assertions exercise each provider action's own preconditions.
    const ScopedAutoFocus focusPolicy(automation);
    ComPtr<IUIAutomationTreeWalker> walker;
    Require(automation.get_ControlViewWalker(&walker), "Obtain native control tree walker");
    auto list = Find(automation, root, L"list");
    ComPtr<IUIAutomationSelectionPattern> selection;
    Require(list->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection)),
            "Obtain virtual list selection pattern");
    BOOL multiple = TRUE;
    Require(selection->get_CurrentCanSelectMultiple(&multiple), "Read list selection policy");
    Check(!multiple, "Convenience list exposes its single-selection policy");
    ComPtr<IUIAutomationElement> first, second, last;
    Require(walker->GetFirstChildElement(list.Get(), &first), "Read first virtual list row");
    Require(walker->GetNextSiblingElement(first.Get(), &second), "Read next virtual list row");
    Require(walker->GetLastChildElement(list.Get(), &last), "Read last virtual list row without enumerating all rows");
    Check(first && second && last, "Virtual list rows are native semantic children");
    TraceElement(*first.Get(), "first list row");
    TraceElement(*second.Get(), "second list row");
    BOOL sameRows = TRUE;
    Require(automation.CompareElements(first.Get(), second.Get(), &sameRows), "Compare adjacent native row identities");
    Check(!sameRows, "Adjacent native rows have distinct identities");
    ComPtr<IUIAutomationSelectionItemPattern> firstSelection, secondSelection;
    Require(first->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&firstSelection)),
            "Obtain first virtual row selection pattern");
    Require(second->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&secondSelection)),
            "Obtain second virtual row selection pattern");
    Require(firstSelection->Select(), "Select first virtual row");
    Require(secondSelection->Select(), "Select second virtual row");
    BOOL selected = FALSE;
    Require(secondSelection->get_CurrentIsSelected(&selected), "Read virtual row selected state");
    ComPtr<IUIAutomationElementArray> selectedItems;
    Require(selection->GetCurrentSelection(&selectedItems), "Query selected virtual list rows");
    int count = 0;
    Require(selectedItems->get_Length(&count), "Count selected virtual list rows");
    Check(selected && count == 1, "Native selection reads the canonical selected row only");
    TraceSelection(automation, *selection.Get(), *first.Get(), *second.Get(), *firstSelection.Get(),
                   *secondSelection.Get(), canonicalCurrent, "after Select(second)");
    Require(secondSelection->RemoveFromSelection(), "Clear virtual row selection");
    const auto removed =
        TraceSelection(automation, *selection.Get(), *first.Get(), *second.Get(), *firstSelection.Get(),
                       *secondSelection.Get(), canonicalCurrent, "after Remove(second)");
    Check(!removed.m_First && !removed.m_Second && removed.m_Count == 0 && removed.m_Current == -1,
          "Pattern-only removal clears the canonical single selection");
    Require(firstSelection->AddToSelection(), "Add a virtual row to an empty selection");
    const auto firstAdded =
        TraceSelection(automation, *selection.Get(), *first.Get(), *second.Get(), *firstSelection.Get(),
                       *secondSelection.Get(), canonicalCurrent, "after Add(first)");
    Check(firstAdded.m_First && !firstAdded.m_Second && firstAdded.m_Count == 1 && firstAdded.m_MatchesFirst &&
              firstAdded.m_Current == 0,
          "Pattern-only addition selects the first row without preselecting another target");
    const auto addition = secondSelection->AddToSelection();
    std::cout << "Native Add(second) HRESULT=" << addition << std::endl;
    const auto secondAdded =
        TraceSelection(automation, *selection.Get(), *first.Get(), *second.Get(), *firstSelection.Get(),
                       *secondSelection.Get(), canonicalCurrent, "after Add(second)");
    const bool additionRejected = addition == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && secondAdded.m_First &&
                                  !secondAdded.m_Second && secondAdded.m_Count == 1 && secondAdded.m_MatchesFirst &&
                                  secondAdded.m_Current == 0;
    ComPtr<IUIAutomationVirtualizedItemPattern> realization;
    Require(last->GetCurrentPatternAs(UIA_VirtualizedItemPatternId, IID_PPV_ARGS(&realization)),
            "Obtain offscreen virtual row realization pattern");
    Require(realization->Realize(), "Realize last virtual list row");
    ComPtr<IUIAutomationScrollItemPattern> reveal;
    Require(last->GetCurrentPatternAs(UIA_ScrollItemPatternId, IID_PPV_ARGS(&reveal)),
            "Obtain offscreen virtual row scroll pattern");
    Require(reveal->ScrollIntoView(), "Reveal last virtual list row");
    RECT bounds{};
    Require(last->get_CurrentBoundingRectangle(&bounds), "Read revealed virtual row bounds");
    Check(bounds.bottom > bounds.top && bounds.right > bounds.left,
          "Native scroll reveals actual clipped virtual row geometry");
    ComPtr<IUIAutomationElement> neighbor;
    Require(walker->GetPreviousSiblingElement(last.Get(), &neighbor), "Read another already visible virtual row");
    Check(neighbor != nullptr, "Last virtual row has an already visible neighbor");
    ComPtr<IUIAutomationSelectionItemPattern> lastSelection, neighborSelection;
    Require(last->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&lastSelection)),
            "Obtain revealed virtual row selection pattern");
    Require(neighbor->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&neighborSelection)),
            "Obtain visible neighboring row selection pattern");
    subscriptions.WatchItem(*last.Get());
    subscriptions.WatchItem(*neighbor.Get());
    auto itemEvents = subscriptions.Events().m_Items.load();
    Require(lastSelection->Select(), "Select the already revealed last virtual row");
    AwaitEvent(subscriptions.Events().m_Items, itemEvents,
               "Native client receives the first visible-row selection event");
    itemEvents = subscriptions.Events().m_Items.load();
    Require(neighborSelection->Select(), "Select another row without moving the viewport");
    AwaitEvent(subscriptions.Events().m_Items, itemEvents,
               "Native client receives a repeated selection event without layout or scrolling");
    auto remove = Find(automation, root, L"remove-row");
    ComPtr<IUIAutomationInvokePattern> removeAction;
    Require(remove->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&removeAction)),
            "Obtain canonical row-removal command");
    subscriptions.WatchStructure(*list.Get());
    const auto structureEvents = subscriptions.Events().m_Structure.load();
    Require(removeAction->Invoke(), "Remove a row while its native provider remains retained");
    AwaitEvent(subscriptions.Events().m_Structure, structureEvents,
               "Registered native client receives virtual-child structure invalidation");
    Check(FAILED(firstSelection->Select()), "Retained provider rejects selection after its row is removed");
    Require(secondSelection->Select(), "Retained neighboring row identity survives a preceding removal");

    Trace("UI Automation virtual tree");
    auto tree = Find(automation, root, L"tree");
    ComPtr<IUIAutomationElement> branch;
    Require(walker->GetFirstChildElement(tree.Get(), &branch), "Read virtual tree branch");
    Check(branch != nullptr, "Native tree exposes its collapsed root branch");
    ComPtr<IUIAutomationExpandCollapsePattern> expansion;
    Require(branch->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expansion)),
            "Obtain virtual tree expansion pattern");
    ExpandCollapseState state{};
    Require(expansion->get_CurrentExpandCollapseState(&state), "Read collapsed branch state");
    Check(state == ExpandCollapseState_Collapsed, "Native branch starts collapsed");
    Require(expansion->Expand(), "Expand virtual tree branch");
    Require(expansion->get_CurrentExpandCollapseState(&state), "Read expanded branch state");
    ComPtr<IUIAutomationElement> child;
    Require(walker->GetFirstChildElement(branch.Get(), &child), "Navigate expanded virtual tree child");
    Check(state == ExpandCollapseState_Expanded && child, "Native expansion reveals logical child nodes");
    Require(expansion->Collapse(), "Collapse virtual tree branch");
    child.Reset();
    Require(walker->GetFirstChildElement(branch.Get(), &child), "Navigate collapsed virtual tree branch");
    Check(!child, "Collapsed descendants leave native child navigation");

    Trace("UI Automation virtual grid");
    auto table = Find(automation, root, L"table");
    ComPtr<IUIAutomationGridPattern> grid;
    Require(table->GetCurrentPatternAs(UIA_GridPatternId, IID_PPV_ARGS(&grid)), "Obtain virtual grid pattern");
    int rowCount = 0, columnCount = 0;
    Require(grid->get_CurrentRowCount(&rowCount), "Read native grid row count");
    Require(grid->get_CurrentColumnCount(&columnCount), "Read native grid column count");
    Check(rowCount == 100 && columnCount == 2, "Native grid reports actual model dimensions");
    ComPtr<IUIAutomationElement> cell;
    Require(grid->GetItem(99, 1, &cell), "Obtain offscreen grid cell directly");
    ComPtr<IUIAutomationGridItemPattern> gridItem;
    Require(cell->GetCurrentPatternAs(UIA_GridItemPatternId, IID_PPV_ARGS(&gridItem)),
            "Obtain virtual cell grid-item pattern");
    int row = -1, column = -1;
    Require(gridItem->get_CurrentRow(&row), "Read native cell row");
    Require(gridItem->get_CurrentColumn(&column), "Read native cell column");
    ComPtr<IUIAutomationElement> containingGrid;
    Require(gridItem->get_CurrentContainingGrid(&containingGrid), "Read native cell containing grid");
    BOOL same = FALSE;
    Require(automation.CompareElements(table.Get(), containingGrid.Get(), &same), "Compare native grid identity");
    Check(row == 99 && column == 1 && same, "Native cell coordinates and containing grid are coherent");
    ComPtr<IUIAutomationSelectionItemPattern> cellSelection;
    Require(cell->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&cellSelection)),
            "Obtain virtual cell selection pattern");
    Require(cellSelection->Select(), "Select offscreen native grid cell");
    ComPtr<IUIAutomationScrollItemPattern> cellReveal;
    Require(cell->GetCurrentPatternAs(UIA_ScrollItemPatternId, IID_PPV_ARGS(&cellReveal)),
            "Obtain virtual grid cell scroll pattern");
    Require(cellReveal->ScrollIntoView(), "Reveal selected native grid cell");
    Require(cell->get_CurrentBoundingRectangle(&bounds), "Read revealed native cell bounds");
    Check(bounds.bottom > bounds.top && bounds.right > bounds.left,
          "Native grid reveal uses actual cell viewport geometry");
    ComPtr<IUIAutomationValuePattern> cellValue;
    Require(cell->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&cellValue)),
            "Obtain editable native cell value pattern");
    BSTR replacement = SysAllocString(L"Edited cell \u03a9");
    if (!replacement)
        throw std::bad_alloc();
    const auto edited = cellValue->SetValue(replacement);
    SysFreeString(replacement);
    Require(edited, "Edit native grid cell through canonical model validation");
    BSTR contents = nullptr;
    Require(cellValue->get_CurrentValue(&contents), "Read edited native grid cell");
    const bool cellEdited = contents && std::wstring_view(contents) == L"Edited cell \u03a9";
    SysFreeString(contents);
    Check(cellEdited, "Native cell value reflects the committed model edit");
    auto lockTable = Find(automation, root, L"lock-table");
    ComPtr<IUIAutomationInvokePattern> lockAction;
    Require(lockTable->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&lockAction)),
            "Obtain canonical table read-only command");
    Require(lockAction->Invoke(), "Make native table read-only");
    BOOL readOnly = FALSE;
    Require(cellValue->get_CurrentIsReadOnly(&readOnly), "Read native cell read-only policy");
    replacement = SysAllocString(L"Denied cell edit");
    if (!replacement)
        throw std::bad_alloc();
    const auto denied = cellValue->SetValue(replacement);
    SysFreeString(replacement);
    Check(readOnly && FAILED(denied), "Native cell pattern rejects edits after its canonical table becomes read-only");
    Check(additionRejected, "Single-selection list rejects a second additive selection");
}

void NativeText(IUIAutomationElement& editor, IUIAutomationValuePattern& value, ProbeEvents& events,
                ComPtr<IUIAutomationTextRange>& retainedRange)
{
    Trace("UI Automation text ranges");
    BSTR replacement = SysAllocString(L"Ae\u0301\U0001f642\u03a9");
    if (!replacement)
        throw std::bad_alloc();
    const auto textEvents = events.m_Text.load();
    const auto edited = value.SetValue(replacement);
    SysFreeString(replacement);
    Require(edited, "Set native text containing a combining grapheme");
    AwaitEvent(events.m_Text, textEvents, "Registered native client receives committed-text change events");
    ComPtr<IUIAutomationTextPattern> text;
    Require(editor.GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text)), "Obtain native text pattern");
    ComPtr<IUIAutomationTextRange> document;
    Require(text->get_DocumentRange(&document), "Read native document range");
    BSTR contents = nullptr;
    Require(document->GetText(-1, &contents), "Read native document text");
    const bool complete = contents && std::wstring_view(contents) == L"Ae\u0301\U0001f642\u03a9";
    SysFreeString(contents);
    Check(complete, "Native text range preserves complete UTF-16 combining text");
    ComPtr<IUIAutomationTextRange> cloned;
    Require(document->Clone(&cloned), "Clone native text range");
    BOOL equal = FALSE;
    Require(document->Compare(cloned.Get(), &equal), "Compare cloned native text range across COM");
    Check(equal, "Native text clone preserves its exact document and endpoints");
    Require(cloned->MoveEndpointByRange(TextPatternRangeEndpoint_Start, document.Get(), TextPatternRangeEndpoint_End),
            "Move native endpoint to a peer range endpoint across COM");
    int endpointComparison = -1;
    Require(cloned->CompareEndpoints(TextPatternRangeEndpoint_Start, document.Get(), TextPatternRangeEndpoint_End,
                                     &endpointComparison),
            "Compare native endpoints across COM");
    Check(endpointComparison == 0, "Native peer range movement preserves endpoint identity across COM");
    int moved = 0;
    Require(document->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, 2, &moved),
            "Move native text endpoint by graphemes");
    Require(document->GetText(-1, &contents), "Read native grapheme-moved range");
    const bool grapheme = contents && std::wstring_view(contents) == L"\U0001f642\u03a9";
    SysFreeString(contents);
    Check(moved == 2 && grapheme, "Native character navigation does not split a combining grapheme");
    Require(document->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, 1, &moved),
            "Move native text endpoint across a surrogate pair");
    Require(document->GetText(-1, &contents), "Read native surrogate-moved range");
    const bool surrogate = contents && std::wstring_view(contents) == L"\u03a9";
    SysFreeString(contents);
    Check(moved == 1 && surrogate, "Native character navigation treats a supplementary character as one grapheme");
    const auto selectionEvents = events.m_Selection.load();
    Require(document->Select(), "Select native text range");
    AwaitEvent(events.m_Selection, selectionEvents, "Registered native client receives text-selection change events");
    ComPtr<IUIAutomationTextRangeArray> ranges;
    Require(text->GetSelection(&ranges), "Read native editor selection");
    int count = 0;
    Require(ranges->get_Length(&count), "Count native editor selections");
    Check(count == 1, "Native single-line editor exposes one canonical selection");
    ComPtr<IUIAutomationTextRange> selectedRange;
    Require(ranges->GetElement(0, &selectedRange), "Read native selected text range");
    Require(selectedRange->GetText(-1, &contents), "Read native selected text");
    const bool selectedOmega = contents && std::wstring_view(contents) == L"\u03a9";
    SysFreeString(contents);
    Check(selectedOmega, "Native range selection updates the editor's canonical grapheme selection");
    SAFEARRAY* rectangles = nullptr;
    Require(document->GetBoundingRectangles(&rectangles), "Read native text range rectangles");
    LONG first = 0, last = -1;
    Check(rectangles && SafeArrayGetDim(rectangles) == 1, "Native text rectangles use a one-dimensional array");
    const auto firstBound = SafeArrayGetLBound(rectangles, 1, &first);
    const auto lastBound = SafeArrayGetUBound(rectangles, 1, &last);
    SafeArrayDestroy(rectangles);
    Require(firstBound, "Read text rectangle array lower bound");
    Require(lastBound, "Read text rectangle array upper bound");
    Check((last - first + 1) % 4 == 0, "Native text rectangles contain complete physical coordinate tuples");
    ComPtr<IUIAutomationTextPattern2> text2;
    Require(editor.GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text2)),
            "Obtain native text pattern version 2");
    BOOL active = FALSE;
    ComPtr<IUIAutomationTextRange> caret;
    Require(text2->GetCaretRange(&active, &caret), "Read native caret range");
    int comparison = -1;
    Require(
        caret->CompareEndpoints(TextPatternRangeEndpoint_Start, caret.Get(), TextPatternRangeEndpoint_End, &comparison),
        "Compare native caret endpoints");
    Check(comparison == 0, "Native caret range is degenerate at the canonical insertion position");
    replacement = SysAllocString(L"abc אבג XYZ");
    if (!replacement)
        throw std::bad_alloc();
    const auto mixedEdit = value.SetValue(replacement);
    SysFreeString(replacement);
    Require(mixedEdit, "Set native mixed-direction text");
    ComPtr<IUIAutomationTextRange> mixedRange;
    Require(text->get_DocumentRange(&mixedRange), "Read mixed-direction native range");
    Require(mixedRange->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, 1, &moved),
            "Move mixed native range start");
    Require(mixedRange->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, -6, &moved),
            "Move mixed native range end");
    rectangles = nullptr;
    Require(mixedRange->GetBoundingRectangles(&rectangles), "Read disjoint native bidi range rectangles");
    first = 0;
    last = -1;
    Require(SafeArrayGetLBound(rectangles, 1, &first), "Read bidi rectangle lower bound");
    Require(SafeArrayGetUBound(rectangles, 1, &last), "Read bidi rectangle upper bound");
    SafeArrayDestroy(rectangles);
    Check(last - first + 1 == 8, "One contiguous logical mixed-direction range exposes two actual native rectangles");
    Require(mixedRange->Select(), "Select native mixed-direction logical range");
    contents = nullptr;
    Require(mixedRange->GetText(-1, &contents), "Read selected logical mixed-direction text");
    const bool logicalMixed = contents && std::wstring_view(contents) == L"bc א";
    SysFreeString(contents);
    Check(logicalMixed, "Native bidi geometry preserves logical selected text order");
    ComPtr<IUIAutomationTextRangeArray> visibleMixed;
    Require(text->GetVisibleRanges(&visibleMixed), "Read native mixed-direction visible ranges");
    count = 0;
    Require(visibleMixed->get_Length(&count), "Count native mixed-direction visible ranges");
    Check(count >= 1, "Native visible ranges include the prepared mixed-script viewport");
    replacement = SysAllocString(L"After \u03a9");
    if (!replacement)
        throw std::bad_alloc();
    const auto restored = value.SetValue(replacement);
    SysFreeString(replacement);
    Require(restored, "Restore native edited text");
    Require(text->get_DocumentRange(&retainedRange), "Retain native text range for closure check");
}

void Accessibility(UiContext& ui, HWND window)
{
    auto& button = ui.Root().Add<Button>("Native invoke");
    button.SetId("invoke");
    auto& text = ui.Root().Add<TextBox>("Before");
    text.SetId("value");
    text.SetAccessibleName("Project title");
    auto& slider = ui.Root().Add<Slider>();
    slider.SetId("range");
    slider.SetRange(-5, 50);
    auto& toggle = ui.Root().Add<ToolButton>("Grid");
    toggle.SetId("toggle");
    toggle.SetCheckable(true);
    auto& tri = ui.Root().Add<CheckBox>("Mixed native choice");
    tri.SetId("tri-state");
    tri.SetTristate(true);
    tri.SetCheckState(CheckState::Indeterminate);
    auto& combo = ui.Root().Add<ComboBox>();
    combo.SetId("native-combo");
    combo.SetItems({"First", "Second", "Third"});
    auto& progress = ui.Root().Add<ProgressBar>();
    progress.SetId("native-progress");
    progress.SetValue(65);
    auto radioGroup = std::make_shared<RadioGroup>();
    auto& radio = ui.Root().Add<RadioButton>("Native radio", radioGroup);
    radio.SetId("native-radio");
    auto& menuBar = ui.Root().Add<MenuBar>();
    menuBar.SetId("native-menubar");
    // Keep independent command probes outside the collection fixture's fixed viewport budget.
    menuBar.SetFloatingBounds({590, 0, 180, 40});
    auto& commandMenu = menuBar.AddMenu("Commands");
    menuBar.GetButton(0).SetId("native-menu-trigger");
    auto& menuToggle = commandMenu.AddAction("Native menu grid");
    menuToggle.SetId("native-menu-toggle");
    menuToggle.SetCheckable(true);
    auto& promptButton = ui.Root().Add<Button>("Open native prompt");
    promptButton.SetId("native-open-prompt");
    promptButton.SetFloatingBounds({590, 48, 180, 36});
    auto& prompt = InputDialog::Create(promptButton, "Native prompt", "Project name", "Before");
    prompt.SetId("native-prompt");
    prompt.Editor().SetId("native-prompt-value");
    prompt.Buttons().GetButton(StandardButton::Ok)->SetId("native-prompt-ok");
    std::string acceptedPrompt;
    const ScopedConnection promptConnection(promptButton.Clicked().Connect(
        [&]
        {
            prompt.Open().OnFinished(
                [&](InputDialogResult result)
                {
                    if (result.m_Text)
                        acceptedPrompt = *result.m_Text;
                });
        }));
    auto& list = ui.Root().Add<ListWidget>();
    list.SetId("list");
    list.SetHeight(SizeRule::Fixed(100));
    std::vector<std::string> items;
    for (int row = 0; row < 1000; ++row)
        items.push_back("List row " + std::to_string(row));
    list.SetItems(std::move(items));
    Point lastSelectionScroll;
    std::atomic<int> canonicalListCurrent = -1;
    bool sawLastSelection = false;
    bool sawNeighborSelection = false;
    bool selectionScrollStable = false;
    const ScopedConnection selectionConnection(list.CurrentChanged().Connect(
        [&](int row)
        {
            canonicalListCurrent = row;
            if (row == 999)
            {
                lastSelectionScroll = list.GetScrollOffset();
                sawLastSelection = true;
            }
            else if (row == 998 && sawLastSelection)
            {
                sawNeighborSelection = true;
                selectionScrollStable = lastSelectionScroll == list.GetScrollOffset();
            }
        }));
    auto& tree = ui.Root().Add<TreeWidget>();
    tree.SetId("tree");
    tree.SetHeight(SizeRule::Fixed(100));
    const auto branch = tree.AddItem(0, "Branch");
    tree.AddItem(branch, "Child");
    auto& table = ui.Root().Add<TableWidget>();
    table.SetId("table");
    table.SetHeight(SizeRule::Fixed(100));
    table.SetEditable(true);
    table.SetColumns({{"Name"}, {"Value"}});
    std::vector<std::vector<std::string>> rows;
    for (int row = 0; row < 100; ++row)
        rows.push_back({"Record " + std::to_string(row), "Value " + std::to_string(row)});
    table.SetRows(std::move(rows));
    auto& removeRow = ui.Root().Add<Button>("Remove first row");
    removeRow.SetId("remove-row");
    removeRow.Clicked().Connect([&list] { list.RemoveItem(0); });
    auto& lockTable = ui.Root().Add<Button>("Lock table");
    lockTable.SetId("lock-table");
    lockTable.Clicked().Connect([&table] { table.SetEditable(false); });
    auto& image = ui.Root().Add<ImageView>();
    image.SetId("native-image");
    image.SetAccessibleName("Sketch preview");
    image.SetFloatingBounds({600, 20, 32, 32});
    auto& scrollbar = ui.Root().Add<ScrollBar>();
    scrollbar.SetId("native-scrollbar");
    scrollbar.SetAccessibleName("Drawing vertical position");
    scrollbar.SetRange(0, 900);
    scrollbar.SetPageStep(100);
    scrollbar.SetFloatingBounds({650, 20, 18, 220});
    ui.Prepare(800, 600);
    int invocations = 0;
    const ScopedConnection invocationConnection(button.Clicked().Connect([&] { ++invocations; }));
    // Native range geometry intentionally excludes hidden HWNDs.
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    Trace("attach UI Automation provider");
    Detail::NativeAccessibility bridge(ui, window);
    std::atomic<bool> done = false;
    std::atomic<bool> readyToClose = false;
    std::atomic<bool> closed = false;
    std::exception_ptr error;
    // UI Automation clients run on an MTA thread; the owner continues its normal queue/message processing.
    std::jthread client(
        [&]
        {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            try
            {
                Require(initialized, "Initialize native automation client MTA");
                ComPtr<IUIAutomation> automation;
                Require(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&automation)),
                        "Create real Windows automation client");
                ComPtr<IUIAutomationElement> root;
                Require(automation->ElementFromHandle(window, &root), "Obtain native HWND automation root");
                Trace("UI Automation root connected");
                auto invokeElement = Find(*automation.Get(), *root.Get(), L"invoke");
                BSTR name = nullptr;
                Require(invokeElement->get_CurrentName(&name), "Read native accessible name");
                const bool named = name && std::wstring_view(name) == L"Native invoke";
                SysFreeString(name);
                if (!named)
                    throw std::runtime_error("Native button caption was not exposed");
                ComPtr<IUIAutomationInvokePattern> invoke;
                Require(invokeElement->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)),
                        "Obtain invoke pattern");
                Require(invoke->Invoke(), "Native invoke action");
                Trace("UI Automation invoke requested");
                auto barElement = Find(*automation.Get(), *root.Get(), L"native-menubar");
                CONTROLTYPEID controlType = 0;
                Require(barElement->get_CurrentControlType(&controlType), "Read native menu-bar control type");
                Check(controlType == UIA_MenuBarControlTypeId, "Native provider exposes a menu-bar role");
                auto imageElement = Find(*automation.Get(), *root.Get(), L"native-image");
                Require(imageElement->get_CurrentControlType(&controlType), "Read native image control type");
                Check(controlType == UIA_ImageControlTypeId, "Native provider exposes an image role");
                BOOL keyboardFocusable = TRUE;
                Require(imageElement->get_CurrentIsKeyboardFocusable(&keyboardFocusable),
                        "Read native image keyboard focus policy");
                Check(!keyboardFocusable, "Image presentation adds no native keyboard focus stop");
                auto scrollElement = Find(*automation.Get(), *root.Get(), L"native-scrollbar");
                Require(scrollElement->get_CurrentControlType(&controlType), "Read native scrollbar control type");
                Check(controlType == UIA_ScrollBarControlTypeId, "Native provider exposes a scrollbar role");
                ComPtr<IUIAutomationRangeValuePattern> scrollRange;
                Require(scrollElement->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&scrollRange)),
                        "Obtain native scrollbar numeric range");
                Require(scrollRange->SetValue(120), "Set native scrollbar value");
                double scrollValue = 0;
                Require(scrollRange->get_CurrentValue(&scrollValue), "Read native scrollbar value");
                Check(scrollValue == 120, "Native scrollbar range requests update canonical state");
                auto triggerElement = Find(*automation.Get(), *barElement.Get(), L"native-menu-trigger");
                ComPtr<IUIAutomationExpandCollapsePattern> menuExpansion;
                Require(triggerElement->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&menuExpansion)),
                        "Obtain native top-level menu expansion");
                Require(menuExpansion->Expand(), "Expand native top-level menu");
                ExpandCollapseState expandedMenu = ExpandCollapseState_LeafNode;
                Require(menuExpansion->get_CurrentExpandCollapseState(&expandedMenu), "Read native menu expansion");
                Check(expandedMenu == ExpandCollapseState_Expanded, "Native menu expansion uses canonical popup state");
                Require(menuExpansion->Collapse(), "Collapse native menu");
                auto promptOpener = Find(*automation.Get(), *root.Get(), L"native-open-prompt");
                ComPtr<IUIAutomationInvokePattern> openPrompt;
                Require(promptOpener->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&openPrompt)),
                        "Obtain native prompt opener");
                Require(openPrompt->Invoke(), "Open native input dialog through canonical button");
                auto promptElement = Find(*automation.Get(), *promptOpener.Get(), L"native-prompt");
                Require(promptElement->get_CurrentControlType(&controlType), "Read native dialog control type");
                Check(controlType == UIA_WindowControlTypeId, "In-window dialog maps its semantic window type");
                BOOL allowed = TRUE;
                Require(invokeElement->get_CurrentIsEnabled(&allowed), "Read underlying modal availability");
                Check(!allowed, "Native modal dialog disables underlying semantic controls");
                ComPtr<IUIAutomationCondition> promptValueCondition;
                VARIANT promptValueId{};
                promptValueId.vt = VT_BSTR;
                promptValueId.bstrVal = SysAllocString(L"native-prompt-value");
                const auto conditionCode = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId,
                                                                               promptValueId, &promptValueCondition);
                VariantClear(&promptValueId);
                Require(conditionCode, "Create nested prompt editor condition");
                ComPtr<IUIAutomationElement> promptValueElement;
                Require(
                    promptElement->FindFirst(TreeScope_Descendants, promptValueCondition.Get(), &promptValueElement),
                    "Find nested native prompt editor");
                if (!promptValueElement)
                    throw std::runtime_error("Native prompt editor was not exposed");
                ComPtr<IUIAutomationValuePattern> promptValue;
                Require(promptValueElement->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&promptValue)),
                        "Obtain native prompt value");
                BSTR promptText = SysAllocString(L"Accepted prompt");
                if (!promptText)
                    throw std::bad_alloc();
                const auto promptEdit = promptValue->SetValue(promptText);
                SysFreeString(promptText);
                Require(promptEdit, "Set canonical native prompt text");
                VARIANT promptOkId{};
                promptOkId.vt = VT_BSTR;
                promptOkId.bstrVal = SysAllocString(L"native-prompt-ok");
                ComPtr<IUIAutomationCondition> promptOkCondition;
                const auto okCode =
                    automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, promptOkId, &promptOkCondition);
                VariantClear(&promptOkId);
                Require(okCode, "Create nested prompt response condition");
                ComPtr<IUIAutomationElement> promptOkElement;
                Require(promptElement->FindFirst(TreeScope_Descendants, promptOkCondition.Get(), &promptOkElement),
                        "Find native prompt OK");
                if (!promptOkElement)
                    throw std::runtime_error("Native prompt OK was not exposed");
                ComPtr<IUIAutomationInvokePattern> acceptPrompt;
                Require(promptOkElement->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&acceptPrompt)),
                        "Obtain native prompt acceptance");
                Require(acceptPrompt->Invoke(), "Accept native prompt through its ordinary response");
                Require(invokeElement->get_CurrentIsEnabled(&allowed), "Read restored modal availability");
                Check(allowed, "Closing native prompt restores underlying semantic availability");
                auto valueElement = Find(*automation.Get(), *root.Get(), L"value");
                EventSubscriptions subscriptions(*automation.Get());
                subscriptions.WatchEditor(*valueElement.Get());
                ComPtr<IUIAutomationValuePattern> value;
                Require(valueElement->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value)),
                        "Obtain value pattern");
                BSTR replacement = SysAllocString(L"After Ω");
                if (!replacement)
                    throw std::bad_alloc();
                const auto editedValue = value->SetValue(replacement);
                SysFreeString(replacement);
                Require(editedValue, "Native Unicode value edit");
                AwaitEvent(subscriptions.Events().m_Values, 0,
                           "Registered native client receives the committed value-property event");
                AwaitEvent(subscriptions.Events().m_Text, 0,
                           "Registered native client receives the initial committed-text event");
                BSTR contents = nullptr;
                Require(value->get_CurrentValue(&contents), "Read native edited value");
                const bool edited = contents && std::wstring_view(contents) == L"After Ω";
                SysFreeString(contents);
                if (!edited)
                    throw std::runtime_error("Native value edit did not commit");
                Trace("UI Automation value committed");
                auto rangeElement = Find(*automation.Get(), *root.Get(), L"range");
                ComPtr<IUIAutomationRangeValuePattern> range;
                Require(rangeElement->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range)),
                        "Obtain range pattern");
                Require(range->SetValue(12.5), "Native range edit");
                if (SUCCEEDED(range->SetValue(100)))
                    throw std::runtime_error("Out of range native value was accepted");
                Trace("UI Automation range committed");
                auto triElement = Find(*automation.Get(), *root.Get(), L"tri-state");
                ComPtr<IUIAutomationTogglePattern> triPattern;
                Require(triElement->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&triPattern)),
                        "Obtain native tri-state toggle pattern");
                ToggleState triState = ToggleState_Off;
                Require(triPattern->get_CurrentToggleState(&triState), "Read native indeterminate state");
                Check(triState == ToggleState_Indeterminate, "Native checkbox exposes the third toggle state");
                Require(triPattern->Toggle(), "Toggle native indeterminate checkbox");
                Require(triPattern->get_CurrentToggleState(&triState), "Read native checkbox after activation");
                Check(triState == ToggleState_Off, "Native toggle uses the canonical three-state cycle");
                auto comboElement = Find(*automation.Get(), *root.Get(), L"native-combo");
                ComPtr<IUIAutomationExpandCollapsePattern> comboPattern;
                Require(comboElement->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&comboPattern)),
                        "Obtain native combo expansion pattern");
                Require(comboPattern->Expand(), "Expand native virtual combo popup");
                ExpandCollapseState comboState = ExpandCollapseState_LeafNode;
                Require(comboPattern->get_CurrentExpandCollapseState(&comboState), "Read native combo expansion");
                Check(comboState == ExpandCollapseState_Expanded, "Native combo expansion shares popup state");
                Require(comboPattern->Collapse(), "Collapse native combo popup");
                ComPtr<IUIAutomationValuePattern> comboValue;
                Require(comboElement->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&comboValue)),
                        "Obtain read-only native combo value");
                BSTR comboText = nullptr;
                Require(comboValue->get_CurrentValue(&comboText), "Read native combo value");
                const bool firstChoice = comboText && std::wstring_view(comboText) == L"First";
                SysFreeString(comboText);
                Check(firstChoice, "Read-only combo still exposes its textual value");
                auto progressElement = Find(*automation.Get(), *root.Get(), L"native-progress");
                ComPtr<IUIAutomationRangeValuePattern> progressPattern;
                Require(progressElement->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&progressPattern)),
                        "Obtain native progress range");
                BOOL progressReadOnly = FALSE;
                Require(progressPattern->get_CurrentIsReadOnly(&progressReadOnly), "Read native progress policy");
                Check(progressReadOnly, "Native progress reports a read-only range");
                auto radioElement = Find(*automation.Get(), *root.Get(), L"native-radio");
                ComPtr<IUIAutomationSelectionItemPattern> radioPattern;
                Require(radioElement->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&radioPattern)),
                        "Obtain native radio selection pattern");
                Require(radioPattern->Select(), "Select native radio through its group");
                BOOL radioSelected = FALSE;
                Require(radioPattern->get_CurrentIsSelected(&radioSelected), "Read native radio selection");
                Check(radioSelected, "Native radio shares canonical group selection");
                auto toggleElement = Find(*automation.Get(), *root.Get(), L"toggle");
                ComPtr<IUIAutomationTogglePattern> toggler;
                Require(toggleElement->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggler)),
                        "Obtain toggle pattern");
                Require(toggler->Toggle(), "Native toggle action");
                ToggleState checked{};
                Require(toggler->get_CurrentToggleState(&checked), "Read native toggle state");
                if (checked != ToggleState_On)
                    throw std::runtime_error("Native toggle did not commit");
                std::string failures;
                const auto probe = [&](const char* label, auto function)
                {
                    try
                    {
                        function();
                    }
                    catch (const std::exception& failure)
                    {
                        std::cerr << "Native " << label << " scenario failed: " << failure.what() << std::endl;
                        if (!failures.empty())
                            failures += "; ";
                        failures += std::string(label) + ": " + failure.what();
                    }
                };
                probe("default focus",
                      [&] { NativeDefaultFocus(*automation.Get(), *root.Get(), canonicalListCurrent); });
                probe("collections",
                      [&] { NativeCollections(*automation.Get(), *root.Get(), subscriptions, canonicalListCurrent); });
                ComPtr<IUIAutomation2> options;
                Require(automation.As(&options), "Read restored native client focus options");
                BOOL automaticFocus = FALSE;
                Require(options->get_AutoSetFocus(&automaticFocus), "Read restored native client automatic focus");
                Check(automaticFocus, "Pattern-only collection scope restores default focus before text probes");
                ComPtr<IUIAutomationTextRange> retainedRange;
                probe("text",
                      [&] { NativeText(*valueElement.Get(), *value.Get(), subscriptions.Events(), retainedRange); });
                subscriptions.Stop();
                Trace("UI Automation patterns committed");
                readyToClose = true;
                while (!closed)
                    SwitchToThread();
                BSTR denied = SysAllocString(L"Denied after closure");
                if (!denied)
                    throw std::bad_alloc();
                const auto expired = value->SetValue(denied);
                SysFreeString(denied);
                if (SUCCEEDED(expired))
                    throw std::runtime_error("Retained native provider accepted an edit after bridge closure");
                if (retainedRange)
                {
                    BSTR staleText = nullptr;
                    const auto expiredRange = retainedRange->GetText(-1, &staleText);
                    SysFreeString(staleText);
                    if (SUCCEEDED(expiredRange))
                        throw std::runtime_error("Retained native text range queried the UI after bridge closure");
                }
                Trace("UI Automation retained edit rejected");
                if (!failures.empty())
                    throw std::runtime_error(failures);
            }
            catch (...)
            {
                error = std::current_exception();
            }
            Trace("UI Automation client releases COM");
            if (SUCCEEDED(initialized))
                CoUninitialize();
            done = true;
            Trace("UI Automation client stopped");
        });
    const bool connected = Until(ui, [&] { return done.load() || readyToClose.load(); }, 20000, &bridge);
    Trace("flush UI Automation events");
    bridge.Flush();
    Trace("disconnect UI Automation provider");
    bridge.Close();
    Trace("provider disconnected");
    closed = true;
    // COM teardown may still call the owner's STA. Keep messages and the queue
    // moving until the client has actually stopped; the process watchdog bounds failure.
    const bool promptlyStopped = Until(ui, [&] { return done.load(); }, 10000, &bridge);
    while (!done)
        Pump(ui, &bridge);
    client.join();
    Check(connected, "Native automation client timed out");
    Check(promptlyStopped, "Native automation client cleanup exceeded ten seconds");
    if (error)
        std::rethrow_exception(error);
    Check(acceptedPrompt == "Accepted prompt", "Native dialog result carries owned canonical input");
    Check(invocations == 1 && text.GetText() == "After Ω" && slider.GetValue() == 12.5 && toggle.IsChecked(),
          "Real UI Automation client uses canonical control actions");
    Check(list.GetCurrentIndex() == 0 && table.GetCurrentCell() == std::pair{99, 1} && !tree.IsExpanded(branch),
          "Native collection patterns use canonical selection and expansion");
    Check(sawLastSelection && sawNeighborSelection && selectionScrollStable,
          "Repeated native selection events were tested with an unchanged visible viewport");
    Check(table.GetCell(99, 1) == "Edited cell Ω", "Rejected native cell edit leaves the canonical model unchanged");
    Check(true, "Retained native value provider expires before HWND teardown");
}

void PublishNativeBytes(UINT format, const void* bytes, std::size_t count)
{
    const auto block = GlobalAlloc(GMEM_MOVEABLE, count);
    if (!block)
        throw std::bad_alloc();
    auto* destination = GlobalLock(block);
    if (!destination)
    {
        GlobalFree(block);
        throw std::runtime_error("Lock native clipboard test block");
    }
    std::memcpy(destination, bytes, count);
    GlobalUnlock(block);
    if (!SetClipboardData(format, block))
    {
        GlobalFree(block);
        throw std::runtime_error("Publish native clipboard test block");
    }
}

void Clipboard(UiContext& ui, HWND window)
{
    Trace("native clipboard formats");
    auto& services = ui.Platform();
    ClipboardData data;
    data.m_Text = "Windows Ω\nsecond line";
    data.m_Html = "<b>Windows Ω</b>";
    data.m_Files = {std::filesystem::temp_directory_path() / L"EStudioUI Ω.txt"};
    data.m_Custom["application/x-estudioui-test"] = {0, 1, 0, 255};
    data.m_Custom["application/x-estudioui-empty"] = {};
    Check(RegisterClipboardFormatW(L"estudioui.mime:APPLICATION/X-ESTUDIOUI-TEST") != 0,
          "External native application can register equivalent format spelling first");
    Check(static_cast<bool>(services.WriteClipboard(data)), "Publish real Windows clipboard formats");
    const auto read = services.ReadClipboard();
    Check(read.m_Result && read.m_Data.m_Text == data.m_Text && read.m_Data.m_Html == data.m_Html &&
              read.m_Data.m_Files == data.m_Files && read.m_Data.m_Custom == data.m_Custom,
          "Native Unicode, HTML, CF_HDROP and binary custom formats round trip");
    Check(OpenClipboard(nullptr) != FALSE, "Inspect real native clipboard");
    const auto block = GetClipboardData(CF_UNICODETEXT);
    const auto* wide = static_cast<const wchar_t*>(GlobalLock(block));
    const bool crlf = wide && std::wstring_view(wide).find(L"\r\n") != std::wstring_view::npos;
    if (wide)
        GlobalUnlock(block);
    CloseClipboard();
    Check(crlf, "Native Unicode clipboard uses Windows CRLF without changing logical LF values");
    std::atomic<bool> held = false, release = false;
    std::jthread lock(
        [&]
        {
            held = OpenClipboard(nullptr) != FALSE;
            while (!release)
                SwitchToThread();
            if (held)
                CloseClipboard();
        });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!held && std::chrono::steady_clock::now() < deadline)
        SwitchToThread();
    const bool locked = held;
    const auto busy = locked ? services.ReadClipboard().m_Result.m_Status : PlatformStatus::Failed;
    release = true;
    lock.join();
    Check(locked && busy == PlatformStatus::Busy,
          "Busy native clipboard is explicit and does not wait on the UI owner");
    Trace("malformed external clipboard format");
    Check(OpenClipboard(window) != FALSE, "Open native clipboard for malformed format probe");
    EmptyClipboard();
    const auto malformed = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 32);
    SetClipboardData(RegisterClipboardFormatW(L"HTML Format"), malformed);
    CloseClipboard();
    Check(services.ReadClipboard().m_Result.m_Status == PlatformStatus::Failed,
          "Malformed external HTML is bounded and diagnosed");
    Trace("aggregate external clipboard budget");
    Check(OpenClipboard(window) != FALSE, "Open native clipboard for aggregate budget probe");
    EmptyClipboard();
    {
        const std::wstring text(5 * 1024 * 1024, L'x');
        const std::string html = "<!--StartFragment-->" + std::string(5 * 1024 * 1024, 'h') + "<!--EndFragment-->";
        PublishNativeBytes(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        PublishNativeBytes(RegisterClipboardFormatW(L"HTML Format"), html.c_str(), html.size() + 1);
    }
    CloseClipboard();
    Check(services.ReadClipboard().m_Result.m_Status == PlatformStatus::Failed,
          "Combined external formats obey the aggregate native payload budget");
    services.SetClipboardText("");
    Trace("native clipboard checks complete");
}

BOOL CALLBACK FindPicker(HWND window, LPARAM data)
{
    auto* owner = reinterpret_cast<HWND*>(data);
    if (GetWindow(window, GW_OWNER) == owner[0] && IsWindowVisible(window))
    {
        owner[1] = window;
        return FALSE;
    }
    return TRUE;
}

void Pickers(UiContext& ui, HWND window)
{
    for (const auto kind : {FileDialogKind::OpenFile, FileDialogKind::SaveFile, FileDialogKind::SelectFolder})
    {
        Trace(kind == FileDialogKind::OpenFile   ? "open native file picker"
              : kind == FileDialogKind::SaveFile ? "open native save picker"
                                                 : "open native folder picker");
        FileDialogOptions options;
        options.m_Kind = kind;
        options.m_Title = "EStudioUI automatic cancellation probe";
        options.m_InitialFolder = std::filesystem::temp_directory_path();
        if (kind == FileDialogKind::SaveFile)
        {
            options.m_FileName = "probe.txt";
            options.m_DefaultExtension = "txt";
        }
        if (kind != FileDialogKind::SelectFolder)
            options.m_Filters = {{"Text", "*.txt"}};
        const auto begin = std::chrono::steady_clock::now();
        auto session = ui.Platform().ShowFileDialog(options);
        int completions = 0;
        session.OnFinished([&](const FileDialogResult&) { ++completions; });
        Check(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(500),
              "Native picker opens asynchronously");
        HWND windows[2] = {window, nullptr};
        const bool shown = Until(ui,
                                 [&]
                                 {
                                     EnumWindows(FindPicker, reinterpret_cast<LPARAM>(windows));
                                     return windows[1] || !session.IsPending();
                                 });
        const bool actual = shown && windows[1];
        Trace("cancel native picker");
        Check(session.Cancel() && !session.Cancel(), "Native picker cancellation settles only once");
        Check(Until(ui, [&] { return !ui.HasPendingPlatformWork(); }),
              "Cancelled native STA unwinds while owner messages continue");
        Check(actual, "Actual native IFileDialog was shown before cancellation");
        Check(session.GetResult()->m_Status == FileDialogStatus::Cancelled && completions == 1,
              "Native cancellation delivers exactly one result after the STA unwinds");
    }
    Trace("retire native picker adapter");
    FileDialogOptions options;
    options.m_InitialFolder = std::filesystem::temp_directory_path();
    auto session = ui.Platform().ShowFileDialog(options);
    int completions = 0;
    session.OnFinished([&](const FileDialogResult&) { ++completions; });
    HWND windows[2] = {window, nullptr};
    const bool shown = Until(ui,
                             [&]
                             {
                                 EnumWindows(FindPicker, reinterpret_cast<LPARAM>(windows));
                                 return windows[1] || !session.IsPending();
                             });
    Check(shown && windows[1], "Native picker is visible before its adapter is retired");
    ui.SetPlatformServices(std::make_unique<MemoryPlatformServices>(ui.GetDispatcher()));
    Check(session.GetResult()->m_Status == FileDialogStatus::OwnerClosed && completions == 1,
          "Replacing native adapter settles its pending picker");
    Check(Until(ui, [&] { return !ui.HasPendingPlatformWork(); }),
          "Replaced native adapter is retired without blocking UI teardown");
    Check(completions == 1 && IsWindow(window),
          "Late native completion cannot redeliver a result or destroy its retained owner");
    Trace("cancel native picker during startup");
    ui.SetPlatformServices(Detail::CreateNativePlatformServices(Host(ui, window)));
    session = ui.Platform().ShowFileDialog(options);
    Check(session.Cancel(), "Native picker can be cancelled before showing its window");
    Check(Until(ui, [&] { return !ui.HasPendingPlatformWork(); }),
          "Startup cancellation unwinds the native STA without a visible-dialog requirement");
    Check(session.GetResult()->m_Status == FileDialogStatus::Cancelled,
          "Late native startup cannot replace the cancelled session result");
}
} // namespace

int main()
{
    ProbeDeadline deadline;
    std::cout << std::unitbuf;
    Trace("initialize owner STA");
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try
    {
        Require(initialized, "Initialize native platform test STA");
        NativeOwner native;
        FontFace font(ESTUDIOUI_FONT_PATH);
        font.AddFont({std::filesystem::path(ESTUDIOUI_TEST_FONT_DIR) / "NotoSansHebrew.ttf"});
        UiContext ui(font);
        ui.GetDispatcher().SetWakeup([window = native.m_Window] { PostMessageW(window, WM_NULL, 0, 0); });
        ui.SetPlatformServices(Detail::CreateNativePlatformServices(Host(ui, native.m_Window)));
        Check(ui.Platform().GetCapabilities().Get(PlatformFeature::ClipboardHtml) == PlatformSupport::Native,
              "Windows adapter reports actual rich clipboard capability");
        const auto appearance = ui.Platform().GetSystemAppearance();
        appearance.Validate();
        Check(appearance.m_Support == PlatformSupport::Native &&
                  ui.Platform().GetCapabilities().Get(PlatformFeature::Appearance) == PlatformSupport::Native,
              "Windows exposes owned native appearance colors and capabilities");
        ui.PumpPlatformServices();
        int appearanceChanges = 0;
        auto appearanceConnection = ui.Platform().AppearanceChanged().Connect(
            [&](const SystemAppearance& snapshot)
            {
                snapshot.Validate();
                ++appearanceChanges;
            });
        SendMessageW(native.m_Window, WM_SETTINGCHANGE, 0, 0);
        SendMessageW(native.m_Window, WM_THEMECHANGED, 0, 0);
        Check(appearanceChanges == 0, "Native appearance messages never execute application callbacks");
        ui.PumpPlatformServices();
        Check(appearanceChanges == 1, "Appearance messages coalesce into one owned idle snapshot");
        ui.PumpPlatformServices();
        Check(appearanceChanges == 1, "An unchanged native turn performs no extra appearance publication");
        try
        {
            bool accessibilityPassed = true;
            try
            {
                Accessibility(ui, native.m_Window);
            }
            catch (const std::exception& error)
            {
                std::cerr << "Native accessibility scenario failed: " << error.what() << std::endl;
                accessibilityPassed = false;
            }
            wchar_t nativeFlag[2]{};
            if (GetEnvironmentVariableW(L"ESTUDIOUI_NATIVE_PLATFORM_TESTS", nativeFlag, 2) == 1 &&
                nativeFlag[0] == L'1')
            {
                Clipboard(ui, native.m_Window);
                Pickers(ui, native.m_Window);
            }
            else
                std::cout
                    << "Clipboard mutation and visible picker probes require ESTUDIOUI_NATIVE_PLATFORM_TESTS=1.\n";
            Check(accessibilityPassed, "Native accessibility scenario failed; see its original diagnostic");
        }
        catch (const std::exception& error)
        {
            std::cerr << "Native scenario failed: " << error.what() << std::endl;
            ui.Platform().Close();
            Trace("unwind native work after failed scenario");
            if (!Until(ui, [&] { return !ui.HasPendingPlatformWork(); }))
                Trace("native cleanup remains pending; process watchdog will fail this probe");
            // Preserve native-owner lifetime and STA service even on a failed assertion.
            // The watchdog makes a stuck worker an explicit process failure.
            while (ui.HasPendingPlatformWork())
                Pump(ui);
            throw;
        }
        std::cout << "Native platform: " << s_Checks << " checks passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    Trace("release owner COM");
    if (SUCCEEDED(initialized))
        CoUninitialize();
    Trace("native probe finished");
    return result;
}
