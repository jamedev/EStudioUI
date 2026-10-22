#pragma once

#ifdef _WIN32
#include "WindowsSupport.h"

#include <ole2.h>
#include <UIAutomation.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace EStudioUI::Detail
{
//! An owning single-line editor snapshot obtained through the owner dispatcher.
struct NativeTextSnapshot
{
    //! Committed UTF-8 text, independent of any temporary composition.
    std::string m_Text{};
    //! Selection anchor as a grapheme-aligned UTF-8 byte offset.
    std::size_t m_Anchor = 0;
    //! Active selection endpoint as a grapheme-aligned UTF-8 byte offset.
    std::size_t m_Caret = 0;
    //! First partially visible grapheme; defaults to the document start.
    std::size_t m_VisibleStart = 0;
    //! Last partially visible grapheme endpoint; absent means document end.
    std::optional<std::size_t> m_VisibleEnd{};
    //! Prevents editing while preserving text selection and reading.
    bool m_ReadOnly = false;
    //! Reports whether this editor owns the keyboard caret.
    bool m_Focused = false;
    //! Reports whether any of the editor's viewport is visible.
    bool m_Visible = true;
};

//! Owner-marshaled operations retained by native text and range providers.
struct NativeTextCallbacks
{
    //! Identifies the containing semantic bridge across separate pattern queries.
    std::shared_ptr<void> m_Domain{};
    //! Lifetime-unique semantic identity within the bridge.
    std::uint64_t m_Identity = 0;
    //! Queries committed text, selection, and visibility without changing layout.
    std::function<HRESULT(NativeTextSnapshot&)> m_Query{};
    //! Returns a clipped physical desktop rectangle for a UTF-8 byte range.
    std::function<HRESULT(std::size_t, std::size_t, Rect&)> m_Bounds{};
    //! Replaces only the selection through the canonical editor operation.
    std::function<HRESULT(std::size_t, std::size_t)> m_Select{};
    //! Maps physical desktop coordinates to the nearest UTF-8 grapheme boundary.
    std::function<HRESULT(double, double, std::size_t&)> m_Point{};
    //! Returns the enclosing semantic provider with one owned COM reference.
    std::function<HRESULT(IRawElementProviderSimple**)> m_Enclosing{};
    //! Reveals a UTF-8 byte range without changing selection; optional for static editors.
    std::function<HRESULT(std::size_t, std::size_t, bool)> m_Scroll{};
};

//! Creates a single-line Text/Text2 pattern provider, initially owning one reference.
HRESULT CreateNativeTextProvider(NativeTextCallbacks callbacks, IUnknown** result) noexcept;
} // namespace EStudioUI::Detail
#endif
