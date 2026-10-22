#include "WindowsClipboard.h"

#ifdef _WIN32
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace EStudioUI::Detail
{
namespace
{
constexpr std::size_t s_NativeBudget = ClipboardData::s_MaxBytes * 4 + 4096;
constexpr std::string_view s_CustomPrefix = "EStudioUI.MIME:";
constexpr std::uint64_t s_CustomMagic = 0x31454d494d495545ULL;

HRESULT LastError()
{
    const auto error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
}

class ClipboardLease
{
  public:
    explicit ClipboardLease(HWND owner) : m_Open(OpenClipboard(owner) != FALSE)
    {
    }

    ~ClipboardLease()
    {
        if (m_Open)
            CloseClipboard();
    }

    bool m_Open;
};

class GlobalView
{
  public:
    explicit GlobalView(HGLOBAL handle) : m_Handle(handle), m_Size(GlobalSize(handle))
    {
        if (m_Size > s_NativeBudget)
            throw std::length_error("A native clipboard format exceeds its allocation budget.");
        m_Bytes = static_cast<const unsigned char*>(GlobalLock(handle));
        if (!m_Bytes && m_Size)
            throw std::runtime_error("A native clipboard format cannot be locked.");
    }

    ~GlobalView()
    {
        if (m_Bytes)
            GlobalUnlock(m_Handle);
    }

    HGLOBAL m_Handle;
    std::size_t m_Size;
    const unsigned char* m_Bytes = nullptr;
};

class GlobalBlock
{
  public:
    GlobalBlock(UINT format, const void* bytes, std::size_t size) : m_Format(format)
    {
        if (!format || !size || size > s_NativeBudget)
            throw std::invalid_argument("Invalid native clipboard publication size or format.");
        m_Handle = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!m_Handle)
            throw std::bad_alloc();
        auto* destination = GlobalLock(m_Handle);
        if (!destination)
        {
            GlobalFree(m_Handle);
            m_Handle = nullptr;
            throw std::runtime_error("Native clipboard publication cannot be locked.");
        }
        std::memcpy(destination, bytes, size);
        GlobalUnlock(m_Handle);
    }

    GlobalBlock(GlobalBlock&& other) noexcept
        : m_Format(other.m_Format), m_Handle(std::exchange(other.m_Handle, nullptr))
    {
    }

    GlobalBlock(const GlobalBlock&) = delete;
    GlobalBlock& operator=(const GlobalBlock&) = delete;

    ~GlobalBlock()
    {
        if (m_Handle)
            GlobalFree(m_Handle);
    }

    UINT m_Format;
    HGLOBAL m_Handle = nullptr;
};

UINT HtmlFormat()
{
    const UINT format = RegisterClipboardFormatW(L"HTML Format");
    if (!format)
        throw std::runtime_error("Windows could not register the HTML clipboard format.");
    return format;
}

std::string HtmlPayload(std::string_view fragment)
{
    std::string header = "Version:1.0\r\nStartHTML:0000000000\r\nEndHTML:0000000000\r\nStartFragment:"
                         "0000000000\r\nEndFragment:0000000000\r\n";
    const std::string prefix = "<html><body><!--StartFragment-->";
    const std::string suffix = "<!--EndFragment--></body></html>";
    const auto field = [&](std::string_view name, std::size_t offset)
    {
        const auto digits = std::to_string(offset);
        header.replace(header.find(name) + name.size(), 10, std::string(10 - digits.size(), '0') + digits);
    };
    field("StartHTML:", header.size());
    field("EndHTML:", header.size() + prefix.size() + fragment.size() + suffix.size());
    field("StartFragment:", header.size() + prefix.size());
    field("EndFragment:", header.size() + prefix.size() + fragment.size());
    return header + prefix + std::string(fragment) + suffix + '\0';
}

std::string HtmlFragment(std::string_view payload)
{
    const auto offset = [&](std::string_view field) -> std::optional<std::size_t>
    {
        const auto header = payload.substr(0, std::min<std::size_t>(payload.size(), 4096));
        const auto found = header.find(field);
        if (found == std::string_view::npos)
            return {};
        const auto first = header.data() + found + field.size();
        std::size_t value = 0;
        const auto parsed = std::from_chars(first, header.data() + header.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr == first)
            return {};
        return value;
    };
    const auto from = offset("StartFragment:"), to = offset("EndFragment:");
    if (from && to && *from <= *to && *to <= payload.size())
        return std::string(payload.substr(*from, *to - *from));
    constexpr std::string_view start = "<!--StartFragment-->", end = "<!--EndFragment-->";
    const auto begin = payload.find(start);
    const auto finish =
        begin == std::string_view::npos ? std::string_view::npos : payload.find(end, begin + start.size());
    if (finish == std::string_view::npos)
        throw std::invalid_argument("The native HTML clipboard format has invalid fragment offsets.");
    return std::string(payload.substr(begin + start.size(), finish - begin - start.size()));
}

std::wstring WindowsLines(std::string_view text)
{
    auto wide = ToWindowsText(text);
    std::wstring result;
    result.reserve(wide.size());
    for (const auto character : wide)
    {
        if (character == L'\n')
            result += L'\r';
        result += character;
    }
    return result;
}
} // namespace

ClipboardResult ReadWindowsClipboard(HWND owner)
{
    ClipboardLease clipboard(owner);
    if (!clipboard.m_Open)
    {
        auto error = WindowsFailure(LastError(), "Open clipboard");
        error.m_Status = PlatformStatus::Busy;
        return {std::move(error), {}};
    }
    ClipboardData result;
    try
    {
        if (const auto handle = GetClipboardData(CF_UNICODETEXT))
        {
            const GlobalView view(handle);
            if (view.m_Size < sizeof(wchar_t) || view.m_Size % sizeof(wchar_t))
                throw std::invalid_argument("The native Unicode clipboard text has invalid storage.");
            const auto* first = reinterpret_cast<const wchar_t*>(view.m_Bytes);
            const auto* last = first + view.m_Size / sizeof(wchar_t);
            const auto* end = std::find(first, last, L'\0');
            if (end == last)
                throw std::invalid_argument("The native Unicode clipboard text is unterminated.");
            std::wstring canonical;
            canonical.reserve(std::min<std::size_t>(static_cast<std::size_t>(end - first), ClipboardData::s_MaxBytes));
            for (auto* current = first; current != end; ++current)
            {
                if (*current == L'\r')
                {
                    canonical += L'\n';
                    if (current + 1 != end && current[1] == L'\n')
                        ++current;
                }
                else
                    canonical += *current;
                if (canonical.size() > ClipboardData::s_MaxBytes)
                    throw std::length_error("Native clipboard text exceeds its canonical character budget.");
            }
            result.m_Text = FromWindowsText(canonical);
        }
        if (const auto handle = GetClipboardData(HtmlFormat()))
        {
            const GlobalView view(handle);
            if (!view.m_Size)
                throw std::invalid_argument("The native HTML clipboard format is empty.");
            result.m_Html = HtmlFragment({reinterpret_cast<const char*>(view.m_Bytes), view.m_Size});
        }
        if (const auto handle = GetClipboardData(CF_HDROP))
        {
            const GlobalView bounds(handle);
            DROPFILES header{};
            if (bounds.m_Size < sizeof(header))
                throw std::invalid_argument("The native clipboard file list has an incomplete header.");
            std::memcpy(&header, bounds.m_Bytes, sizeof(header));
            if (!header.fWide || header.pFiles < sizeof(header) || header.pFiles >= bounds.m_Size ||
                header.pFiles % sizeof(wchar_t) || (bounds.m_Size - header.pFiles) % sizeof(wchar_t))
                throw std::invalid_argument("The native clipboard requires a bounded UTF-16 file list.");
            const auto* current = reinterpret_cast<const wchar_t*>(bounds.m_Bytes + header.pFiles);
            const auto* end = current + (bounds.m_Size - header.pFiles) / sizeof(wchar_t);
            while (current != end && *current)
            {
                const auto* terminator = std::find(current, end, L'\0');
                if (terminator == end || terminator - current > 32768 || result.m_Files.size() >= 512)
                    throw std::length_error("A native clipboard file list is unterminated or exceeds its budget.");
                result.m_Files.emplace_back(std::wstring(current, terminator));
                current = terminator + 1;
            }
            if (current == end)
                throw std::invalid_argument("The native clipboard file list has no final terminator.");
        }
        UINT format = 0;
        while ((format = EnumClipboardFormats(format)) != 0)
        {
            wchar_t name[256]{};
            const int count = GetClipboardFormatNameW(format, name, 256);
            if (count <= 0)
                continue;
            const std::wstring_view nativeName(name, static_cast<std::size_t>(count));
            if (!nativeName.starts_with(L"EStudioUI.MIME:"))
                continue;
            const auto encoded = FromWindowsText(nativeName);
            if (result.m_Custom.size() >= 32)
                throw std::length_error("The native clipboard contains too many custom formats.");
            const GlobalView view(GetClipboardData(format));
            if (view.m_Size < 16)
                throw std::invalid_argument("A native custom clipboard payload is incomplete.");
            std::uint64_t magic = 0, length = 0;
            std::memcpy(&magic, view.m_Bytes, sizeof(magic));
            std::memcpy(&length, view.m_Bytes + 8, sizeof(length));
            if (magic != s_CustomMagic || length > ClipboardData::s_MaxBytes || length > view.m_Size - 16)
                throw std::invalid_argument("A native custom clipboard payload has an invalid envelope.");
            result.m_Custom.emplace(
                encoded.substr(s_CustomPrefix.size()),
                std::vector<std::uint8_t>(view.m_Bytes + 16, view.m_Bytes + 16 + static_cast<std::size_t>(length)));
        }
        result.Validate();
        return {{}, std::move(result)};
    }
    catch (const std::exception& error)
    {
        return {{PlatformStatus::Failed, error.what()}, {}};
    }
}

PlatformResult WriteWindowsClipboard(HWND owner, const ClipboardData& data)
{
    std::vector<GlobalBlock> prepared;
    if (data.m_Text)
    {
        const auto text = WindowsLines(*data.m_Text);
        prepared.emplace_back(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    }
    if (data.m_Html)
    {
        const auto html = HtmlPayload(*data.m_Html);
        prepared.emplace_back(HtmlFormat(), html.data(), html.size());
    }
    if (!data.m_Files.empty())
    {
        std::wstring paths;
        for (const auto& path : data.m_Files)
        {
            if (!path.is_absolute())
                return {PlatformStatus::Failed, "Native clipboard file paths must be absolute."};
            paths += path.native();
            paths += L'\0';
        }
        paths += L'\0';
        DROPFILES header{};
        header.pFiles = static_cast<DWORD>(sizeof(header));
        header.fWide = TRUE;
        std::vector<unsigned char> bytes(sizeof(header) + paths.size() * sizeof(wchar_t));
        std::memcpy(bytes.data(), &header, sizeof(header));
        std::memcpy(bytes.data() + sizeof(header), paths.data(), paths.size() * sizeof(wchar_t));
        prepared.emplace_back(CF_HDROP, bytes.data(), bytes.size());
    }
    for (const auto& [name, bytes] : data.m_Custom)
    {
        const auto nativeName = ToWindowsText(std::string(s_CustomPrefix) + name);
        const UINT format = RegisterClipboardFormatW(nativeName.c_str());
        if (!format)
            return WindowsFailure(LastError(), "Register clipboard MIME format");
        std::vector<unsigned char> framed(16 + bytes.size());
        const std::uint64_t length = bytes.size();
        std::memcpy(framed.data(), &s_CustomMagic, sizeof(s_CustomMagic));
        std::memcpy(framed.data() + 8, &length, sizeof(length));
        std::copy(bytes.begin(), bytes.end(), framed.begin() + 16);
        prepared.emplace_back(format, framed.data(), framed.size());
    }
    ClipboardLease clipboard(owner);
    if (!clipboard.m_Open)
    {
        auto error = WindowsFailure(LastError(), "Open clipboard");
        error.m_Status = PlatformStatus::Busy;
        return error;
    }
    if (!EmptyClipboard())
        return WindowsFailure(LastError(), "Empty clipboard");
    for (auto& block : prepared)
    {
        if (!SetClipboardData(block.m_Format, block.m_Handle))
            return WindowsFailure(LastError(), "Publish clipboard format; earlier formats may already be present");
        block.m_Handle = nullptr;
    }
    return {};
}
} // namespace EStudioUI::Detail
#endif
