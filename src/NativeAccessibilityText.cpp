#include "NativeAccessibilityText.h"

#ifdef _WIN32
#include <EStudioUI/ClipboardData.h>
#include <EStudioUI/TextBuffer.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace EStudioUI::Detail
{
namespace
{
template <typename Function> HRESULT Guard(Function&& function) noexcept
{
    try
    {
        return function();
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const std::invalid_argument&)
    {
        return E_INVALIDARG;
    }
    catch (const std::length_error&)
    {
        return E_INVALIDARG;
    }
    catch (...)
    {
        return UIA_E_INVALIDOPERATION;
    }
}

bool ValidEndpoint(TextPatternRangeEndpoint endpoint)
{
    return endpoint == TextPatternRangeEndpoint_Start || endpoint == TextPatternRangeEndpoint_End;
}

bool ValidUnit(TextUnit unit)
{
    return unit >= TextUnit_Character && unit <= TextUnit_Document;
}

std::size_t Snap(const std::vector<std::size_t>& boundaries, std::size_t offset)
{
    const auto end = std::upper_bound(boundaries.begin(), boundaries.end(), offset);
    return end == boundaries.begin() ? 0 : *std::prev(end);
}

HRESULT Query(const NativeTextCallbacks& callbacks, NativeTextSnapshot& snapshot)
{
    const auto code = callbacks.m_Query(snapshot);
    if (FAILED(code))
        return code;
    ValidatePlatformText(snapshot.m_Text);
    const auto boundaries = TextBuffer::Graphemes(snapshot.m_Text);
    snapshot.m_Anchor = Snap(boundaries, snapshot.m_Anchor);
    snapshot.m_Caret = Snap(boundaries, snapshot.m_Caret);
    snapshot.m_VisibleStart = Snap(boundaries, snapshot.m_VisibleStart);
    snapshot.m_VisibleEnd = Snap(boundaries, snapshot.m_VisibleEnd.value_or(snapshot.m_Text.size()));
    if (*snapshot.m_VisibleEnd < snapshot.m_VisibleStart)
        snapshot.m_VisibleEnd = snapshot.m_VisibleStart;
    return S_OK;
}

std::vector<std::size_t> UnitBoundaries(std::string_view text, TextUnit unit)
{
    if (unit == TextUnit_Character)
        return TextBuffer::Graphemes(text);
    if (unit != TextUnit_Word)
        return text.empty() ? std::vector<std::size_t>{0} : std::vector<std::size_t>{0, text.size()};

    TextBuffer buffer;
    buffer.SetMaxBytes(text.size());
    buffer.SetText(text);
    std::vector<std::size_t> result{0};
    while (result.back() < text.size())
    {
        buffer.SelectWord(result.back());
        const auto next = std::max(buffer.GetAnchor(), buffer.GetCaret());
        if (next <= result.back())
            throw std::logic_error("Single-line word navigation did not advance.");
        result.push_back(next);
    }
    return result;
}

HRESULT UnknownArray(IUnknown* element, SAFEARRAY** result)
{
    auto* array = SafeArrayCreateVector(VT_UNKNOWN, 0, element ? 1 : 0);
    if (!array)
        return E_OUTOFMEMORY;
    if (element)
    {
        LONG index = 0;
        const auto code = SafeArrayPutElement(array, &index, element);
        if (FAILED(code))
        {
            SafeArrayDestroy(array);
            return code;
        }
    }
    *result = array;
    return S_OK;
}

class TextRange final : public ITextRangeProvider
{
  public:
    TextRange(std::shared_ptr<const NativeTextCallbacks> callbacks, NativeTextSnapshot snapshot, std::size_t start,
              std::size_t end)
        : m_Callbacks(std::move(callbacks)), m_Text(std::move(snapshot.m_Text)), m_Start(start), m_End(end)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(ITextRangeProvider))
            return E_NOINTERFACE;
        *result = static_cast<ITextRangeProvider*>(this);
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

    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                return FAILED(code) ? code : Create(m_Callbacks, std::move(snapshot), m_Start, m_End, result);
            });
    }

    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* range, BOOL* result) override
    {
        if (!result)
            return E_POINTER;
        *result = FALSE;
        auto* other = Peer(range);
        if (!other)
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                return WithPeer(*other,
                                [&](const NativeTextSnapshot&)
                                {
                                    *result = m_Start == other->m_Start && m_End == other->m_End ? TRUE : FALSE;
                                    return S_OK;
                                });
            });
    }

    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider* range,
                                               TextPatternRangeEndpoint otherEndpoint, int* result) override
    {
        if (!result)
            return E_POINTER;
        *result = 0;
        auto* other = Peer(range);
        if (!other || !ValidEndpoint(endpoint) || !ValidEndpoint(otherEndpoint))
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                return WithPeer(*other,
                                [&](const NativeTextSnapshot&)
                                {
                                    const auto left = Endpoint(endpoint);
                                    const auto right = other->Endpoint(otherEndpoint);
                                    *result = left < right ? -1 : left > right ? 1 : 0;
                                    return S_OK;
                                });
            });
    }

    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override
    {
        if (!ValidUnit(unit))
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                if (FAILED(code))
                    return code;
                const auto boundaries = UnitBoundaries(m_Text, unit);
                if (boundaries.size() == 1)
                    m_Start = m_End = 0;
                else
                {
                    auto position = std::upper_bound(boundaries.begin(), boundaries.end(), m_Start);
                    if (position == boundaries.end())
                        --position;
                    m_Start = *std::prev(position);
                    m_End = *position;
                }
                return S_OK;
            });
    }

    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID attribute, VARIANT value, BOOL,
                                            ITextRangeProvider** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                if (FAILED(code))
                    return code;
                if (attribute != UIA_IsReadOnlyAttributeId || value.vt != VT_BOOL ||
                    (value.boolVal != VARIANT_FALSE) != snapshot.m_ReadOnly)
                    return S_OK;
                return Create(m_Callbacks, std::move(snapshot), m_Start, m_End, result);
            });
    }

    HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (!text || !SysStringLen(text))
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                auto code = Refresh(snapshot);
                if (FAILED(code))
                    return code;
                const auto needle = std::wstring_view(text, SysStringLen(text));
                // Conversion validates embedded NULs and malformed UTF-16 before any search.
                FromWindowsText(needle);
                const auto haystack = ToWindowsText(std::string_view(m_Text).substr(m_Start, m_End - m_Start));
                if (needle.size() > haystack.size())
                    return S_OK;
                const auto boundaries = TextBuffer::Graphemes(m_Text);
                std::size_t first = 0;
                std::size_t last = haystack.size();
                while (last - first >= needle.size())
                {
                    const auto found = FindStringOrdinal(backward ? FIND_FROMEND : FIND_FROMSTART,
                                                         haystack.data() + first, static_cast<int>(last - first),
                                                         needle.data(), static_cast<int>(needle.size()), ignoreCase);
                    if (found < 0)
                        return S_OK;
                    const auto offset = first + static_cast<std::size_t>(found);
                    const auto start = m_Start + FromWindowsText(std::wstring_view(haystack).substr(0, offset)).size();
                    const auto end =
                        start + FromWindowsText(std::wstring_view(haystack).substr(offset, needle.size())).size();
                    if (std::binary_search(boundaries.begin(), boundaries.end(), start) &&
                        std::binary_search(boundaries.begin(), boundaries.end(), end))
                        return Create(m_Callbacks, std::move(snapshot), start, end, result);
                    if (backward)
                        last = offset + needle.size() - 1;
                    else
                        first = offset + 1;
                }
                return S_OK;
            });
    }

    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT* result) override
    {
        if (!result)
            return E_POINTER;
        VariantInit(result);
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                if (FAILED(code))
                    return code;
                if (attribute == UIA_IsReadOnlyAttributeId)
                {
                    result->vt = VT_BOOL;
                    result->boolVal = snapshot.m_ReadOnly ? VARIANT_TRUE : VARIANT_FALSE;
                    return S_OK;
                }
                const auto reserved = UiaGetReservedNotSupportedValue(&result->punkVal);
                if (SUCCEEDED(reserved))
                    result->vt = VT_UNKNOWN;
                return reserved;
            });
    }

    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                auto code = Refresh(snapshot);
                if (FAILED(code))
                    return code;
                if (!snapshot.m_Visible)
                {
                    *result = SafeArrayCreateVector(VT_R8, 0, 0);
                    return *result ? S_OK : E_OUTOFMEMORY;
                }
                Rect bounds;
                code = m_Callbacks->m_Bounds(m_Start, m_End, bounds);
                if (FAILED(code))
                    return code;
                const auto visible = bounds.m_Width > 0 && bounds.m_Height > 0;
                auto* array = SafeArrayCreateVector(VT_R8, 0, visible ? 4 : 0);
                if (!array)
                    return E_OUTOFMEMORY;
                const double values[] = {bounds.m_X, bounds.m_Y, bounds.m_Width, bounds.m_Height};
                for (LONG index = 0; visible && index < 4; ++index)
                {
                    code = SafeArrayPutElement(array, &index, const_cast<double*>(&values[index]));
                    if (FAILED(code))
                    {
                        SafeArrayDestroy(array);
                        return code;
                    }
                }
                *result = array;
                return S_OK;
            });
    }

    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                return FAILED(code) ? code : m_Callbacks->m_Enclosing(result);
            });
    }

    HRESULT STDMETHODCALLTYPE GetText(int maxLength, BSTR* result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (maxLength < -1)
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                if (FAILED(code))
                    return code;
                auto text = ToWindowsText(std::string_view(m_Text).substr(m_Start, m_End - m_Start));
                if (maxLength >= 0 && text.size() > static_cast<std::size_t>(maxLength))
                {
                    text.resize(static_cast<std::size_t>(maxLength));
                    if (!text.empty() && text.back() >= 0xd800 && text.back() <= 0xdbff)
                        text.pop_back();
                }
                *result = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
                return *result ? S_OK : E_OUTOFMEMORY;
            });
    }

    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* moved) override
    {
        if (!moved)
            return E_POINTER;
        *moved = 0;
        if (!ValidUnit(unit))
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                if (FAILED(code) || !count)
                    return code;
                const auto boundaries = UnitBoundaries(m_Text, unit);
                if (boundaries.size() == 1)
                    return S_OK;
                const bool degenerate = m_Start == m_End;
                const auto position = degenerate && count < 0
                                          ? std::lower_bound(boundaries.begin(), boundaries.end(), m_Start)
                                          : std::upper_bound(boundaries.begin(), boundaries.end(), m_Start);
                auto first =
                    static_cast<std::ptrdiff_t>(position - boundaries.begin()) - (degenerate && count < 0 ? 0 : 1);
                const auto last = static_cast<std::ptrdiff_t>(boundaries.size()) - (degenerate ? 1 : 2);
                first = std::min(first, last);
                const auto target = std::clamp(first + static_cast<std::ptrdiff_t>(count), std::ptrdiff_t{0}, last);
                *moved = static_cast<int>(target - first);
                if (!*moved)
                    return S_OK;
                m_Start = boundaries[static_cast<std::size_t>(target)];
                m_End = degenerate ? m_Start : boundaries[static_cast<std::size_t>(target + 1)];
                return S_OK;
            });
    }

    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count,
                                                 int* moved) override
    {
        if (!moved)
            return E_POINTER;
        *moved = 0;
        if (!ValidUnit(unit) || !ValidEndpoint(endpoint))
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                if (FAILED(code) || !count)
                    return code;
                const auto boundaries = UnitBoundaries(m_Text, unit);
                auto& value = Endpoint(endpoint);
                const auto at = count > 0 ? std::upper_bound(boundaries.begin(), boundaries.end(), value)
                                          : std::lower_bound(boundaries.begin(), boundaries.end(), value);
                const auto initial = count > 0 ? static_cast<std::ptrdiff_t>(at - boundaries.begin()) - 1
                                               : static_cast<std::ptrdiff_t>(at - boundaries.begin());
                const auto last = static_cast<std::ptrdiff_t>(boundaries.size()) - 1;
                const auto target = std::clamp(initial + static_cast<std::ptrdiff_t>(count), std::ptrdiff_t{0}, last);
                *moved = static_cast<int>(target - initial);
                value = boundaries[static_cast<std::size_t>(target)];
                Normalize(endpoint);
                return S_OK;
            });
    }

    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider* range,
                                                  TextPatternRangeEndpoint otherEndpoint) override
    {
        auto* other = Peer(range);
        if (!other || !ValidEndpoint(endpoint) || !ValidEndpoint(otherEndpoint))
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                return WithPeer(*other,
                                [&](const NativeTextSnapshot&)
                                {
                                    Endpoint(endpoint) = other->Endpoint(otherEndpoint);
                                    Normalize(endpoint);
                                    return S_OK;
                                });
            });
    }

    HRESULT STDMETHODCALLTYPE Select() override
    {
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                return FAILED(code) ? code : m_Callbacks->m_Select(m_Start, m_End);
            });
    }

    HRESULT STDMETHODCALLTYPE AddToSelection() override
    {
        return ChangeSelection(true);
    }

    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override
    {
        return ChangeSelection(false);
    }

    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL alignTop) override
    {
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                if (FAILED(code))
                    return code;
                if (m_Callbacks->m_Scroll)
                    return m_Callbacks->m_Scroll(m_Start, m_End, alignTop != FALSE);
                if (m_Start >= snapshot.m_VisibleStart && m_End <= *snapshot.m_VisibleEnd)
                    return S_OK;
                return UIA_E_NOTSUPPORTED;
            });
    }

    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                return FAILED(code) ? code : UnknownArray(nullptr, result);
            });
    }

    static HRESULT Create(std::shared_ptr<const NativeTextCallbacks> callbacks, NativeTextSnapshot snapshot,
                          std::size_t start, std::size_t end, ITextRangeProvider** result)
    {
        const auto boundaries = TextBuffer::Graphemes(snapshot.m_Text);
        start = Snap(boundaries, start);
        end = Snap(boundaries, end);
        if (end < start)
            std::swap(start, end);
        *result = new TextRange(std::move(callbacks), std::move(snapshot), start, end);
        return S_OK;
    }

  private:
    HRESULT ChangeSelection(bool add)
    {
        return Guard(
            [&]
            {
                std::lock_guard lock(m_Mutex);
                NativeTextSnapshot snapshot;
                const auto code = Refresh(snapshot);
                if (FAILED(code))
                    return code;
                if (m_Start == m_End)
                    return m_Callbacks->m_Select(m_Start, m_Start);
                auto first = std::min(snapshot.m_Anchor, snapshot.m_Caret);
                auto last = std::max(snapshot.m_Anchor, snapshot.m_Caret);
                const bool forward = snapshot.m_Anchor <= snapshot.m_Caret;
                if (add)
                {
                    if (first == last)
                    {
                        first = m_Start;
                        last = m_End;
                    }
                    else
                    {
                        if (m_End < first || m_Start > last)
                            return UIA_E_INVALIDOPERATION;
                        first = std::min(first, m_Start);
                        last = std::max(last, m_End);
                    }
                }
                else
                {
                    if (m_End <= first || m_Start >= last)
                        return S_OK;
                    if (m_Start > first && m_End < last)
                        return UIA_E_INVALIDOPERATION;
                    if (m_Start <= first && m_End >= last)
                        last = first;
                    else if (m_Start <= first)
                        first = m_End;
                    else
                        last = m_Start;
                }
                return m_Callbacks->m_Select(forward ? first : last, forward ? last : first);
            });
    }

    HRESULT Refresh(NativeTextSnapshot& snapshot)
    {
        const auto code = Query(*m_Callbacks, snapshot);
        if (FAILED(code))
            return code;
        Rebase(snapshot.m_Text);
        return S_OK;
    }

    void Rebase(const std::string& text)
    {
        if (text == m_Text)
            return;
        std::size_t prefix = 0;
        while (prefix < text.size() && prefix < m_Text.size() && text[prefix] == m_Text[prefix])
            ++prefix;
        std::size_t suffix = 0;
        while (suffix < text.size() - prefix && suffix < m_Text.size() - prefix &&
               text[text.size() - suffix - 1] == m_Text[m_Text.size() - suffix - 1])
            ++suffix;
        const auto oldEnd = m_Text.size() - suffix;
        const auto newEnd = text.size() - suffix;
        const bool degenerate = m_Start == m_End;
        const auto adjust = [&](std::size_t value, bool end)
        {
            if (value < prefix)
                return value;
            if (value >= oldEnd)
                return newEnd + (value - oldEnd);
            return end ? newEnd : prefix;
        };
        m_Start = adjust(m_Start, degenerate);
        m_End = adjust(m_End, true);
        m_Text = text;
        const auto boundaries = TextBuffer::Graphemes(m_Text);
        m_Start = Snap(boundaries, m_Start);
        m_End = std::max(m_Start, Snap(boundaries, m_End));
    }

    TextRange* Peer(ITextRangeProvider* range) const
    {
        auto* other = dynamic_cast<TextRange*>(range);
        return other && other->m_Callbacks->m_Domain == m_Callbacks->m_Domain &&
                       other->m_Callbacks->m_Identity == m_Callbacks->m_Identity
                   ? other
                   : nullptr;
    }

    template <typename Function> HRESULT WithPeer(TextRange& other, Function&& function)
    {
        if (&other == this)
        {
            std::lock_guard lock(m_Mutex);
            NativeTextSnapshot snapshot;
            const auto code = Refresh(snapshot);
            return FAILED(code) ? code : function(snapshot);
        }
        std::scoped_lock lock(m_Mutex, other.m_Mutex);
        NativeTextSnapshot snapshot;
        const auto code = Refresh(snapshot);
        if (FAILED(code))
            return code;
        other.Rebase(snapshot.m_Text);
        return function(snapshot);
    }

    std::size_t& Endpoint(TextPatternRangeEndpoint endpoint)
    {
        return endpoint == TextPatternRangeEndpoint_Start ? m_Start : m_End;
    }

    void Normalize(TextPatternRangeEndpoint endpoint)
    {
        if (m_Start > m_End)
            (endpoint == TextPatternRangeEndpoint_Start ? m_End : m_Start) = Endpoint(endpoint);
    }

    std::atomic<ULONG> m_References = 1;
    std::shared_ptr<const NativeTextCallbacks> m_Callbacks;
    std::mutex m_Mutex;
    std::string m_Text;
    std::size_t m_Start;
    std::size_t m_End;
};

class TextProvider final : public ITextProvider2
{
  public:
    explicit TextProvider(NativeTextCallbacks callbacks)
        : m_Callbacks(std::make_shared<NativeTextCallbacks>(std::move(callbacks)))
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(ITextProvider) && iid != __uuidof(ITextProvider2))
            return E_NOINTERFACE;
        *result = static_cast<ITextProvider2*>(this);
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

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) override
    {
        return Ranges(result, false);
    }

    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** result) override
    {
        return Ranges(result, true);
    }

    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return InvalidRelatedElement();
    }

    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point, ITextRangeProvider** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return E_INVALIDARG;
        return Guard(
            [&]
            {
                NativeTextSnapshot snapshot;
                auto code = Query(*m_Callbacks, snapshot);
                if (FAILED(code))
                    return code;
                std::size_t offset = 0;
                code = m_Callbacks->m_Point(point.x, point.y, offset);
                return FAILED(code) ? code
                                    : TextRange::Create(m_Callbacks, std::move(snapshot), offset, offset, result);
            });
    }

    HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return Guard(
            [&]
            {
                NativeTextSnapshot snapshot;
                const auto code = Query(*m_Callbacks, snapshot);
                const auto end = snapshot.m_Text.size();
                return FAILED(code) ? code : TextRange::Create(m_Callbacks, std::move(snapshot), 0, end, result);
            });
    }

    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* result) override
    {
        if (!result)
            return E_POINTER;
        *result = SupportedTextSelection_None;
        return Guard(
            [&]
            {
                NativeTextSnapshot snapshot;
                const auto code = Query(*m_Callbacks, snapshot);
                if (SUCCEEDED(code))
                    *result = SupportedTextSelection_Single;
                return code;
            });
    }

    HRESULT STDMETHODCALLTYPE RangeFromAnnotation(IRawElementProviderSimple*, ITextRangeProvider** result) override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return InvalidRelatedElement();
    }

    HRESULT STDMETHODCALLTYPE GetCaretRange(BOOL* active, ITextRangeProvider** result) override
    {
        if (!active || !result)
            return E_POINTER;
        *active = FALSE;
        *result = nullptr;
        return Guard(
            [&]
            {
                NativeTextSnapshot snapshot;
                const auto code = Query(*m_Callbacks, snapshot);
                if (FAILED(code))
                    return code;
                *active = snapshot.m_Focused ? TRUE : FALSE;
                const auto caret = snapshot.m_Caret;
                return TextRange::Create(m_Callbacks, std::move(snapshot), caret, caret, result);
            });
    }

  private:
    HRESULT InvalidRelatedElement()
    {
        return Guard(
            [&]
            {
                NativeTextSnapshot snapshot;
                const auto code = Query(*m_Callbacks, snapshot);
                // A plain single-line editor has no embedded or annotation elements.
                return FAILED(code) ? code : E_INVALIDARG;
            });
    }

    HRESULT Ranges(SAFEARRAY** result, bool visible)
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return Guard(
            [&]
            {
                NativeTextSnapshot snapshot;
                const auto code = Query(*m_Callbacks, snapshot);
                if (FAILED(code))
                    return code;
                if (visible && !snapshot.m_Visible)
                    return UnknownArray(nullptr, result);
                const auto first = visible ? snapshot.m_VisibleStart : std::min(snapshot.m_Anchor, snapshot.m_Caret);
                const auto last = visible ? *snapshot.m_VisibleEnd : std::max(snapshot.m_Anchor, snapshot.m_Caret);
                ITextRangeProvider* range = nullptr;
                const auto created = TextRange::Create(m_Callbacks, std::move(snapshot), first, last, &range);
                if (FAILED(created))
                    return created;
                const auto array = UnknownArray(range, result);
                range->Release();
                return array;
            });
    }

    std::atomic<ULONG> m_References = 1;
    std::shared_ptr<const NativeTextCallbacks> m_Callbacks;
};
} // namespace

HRESULT CreateNativeTextProvider(NativeTextCallbacks callbacks, IUnknown** result) noexcept
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    return Guard(
        [&]
        {
            if (!callbacks.m_Domain || !callbacks.m_Identity || !callbacks.m_Query || !callbacks.m_Bounds ||
                !callbacks.m_Select || !callbacks.m_Point || !callbacks.m_Enclosing)
                return E_INVALIDARG;
            *result = static_cast<ITextProvider2*>(new TextProvider(std::move(callbacks)));
            return S_OK;
        });
}
} // namespace EStudioUI::Detail
#endif
