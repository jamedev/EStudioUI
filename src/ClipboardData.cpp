#include <EStudioUI/ClipboardData.h>

#include <utf8proc.h>

#include <algorithm>
#include <stdexcept>
#include <set>

namespace EStudioUI
{
void ValidatePlatformText(std::string_view text)
{
    if (text.size() > ClipboardData::s_MaxBytes)
        throw std::length_error("Native-service text exceeds the 8 MiB payload budget.");
    std::size_t offset = 0;
    while (offset < text.size())
    {
        utf8proc_int32_t codepoint = 0;
        const auto count = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
                                            static_cast<utf8proc_ssize_t>(text.size() - offset), &codepoint);
        if (count <= 0 || codepoint == 0)
            throw std::invalid_argument("Native-service strings require valid UTF-8 without embedded NULs.");
        offset += static_cast<std::size_t>(count);
    }
}

void ClipboardData::Validate() const
{
    std::size_t remaining = s_MaxBytes;
    const auto consume = [&](std::size_t bytes)
    {
        if (bytes > remaining)
            throw std::length_error("Clipboard data exceeds the 8 MiB payload budget.");
        remaining -= bytes;
    };
    for (const auto* value : {&m_Text, &m_Html})
        if (*value)
        {
            consume((*value)->size());
            ValidatePlatformText(**value);
        }
    if (m_Files.size() > 512 || m_Custom.size() > 32)
        throw std::length_error("Clipboard data supports at most 512 paths and 32 custom formats.");
    for (const auto& path : m_Files)
    {
        const auto encoded = path.u8string();
        if (encoded.empty())
            throw std::invalid_argument("A clipboard file path cannot be empty.");
        consume(encoded.size());
        ValidatePlatformText({reinterpret_cast<const char*>(encoded.data()), encoded.size()});
    }
    std::set<std::string> names;
    for (const auto& [format, bytes] : m_Custom)
    {
        const auto slash = format.find('/');
        const auto token = [](unsigned char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string_view::npos || c == '/';
        };
        if (format.size() > 128 || slash == std::string::npos || slash == 0 || slash + 1 == format.size() ||
            format.find('/', slash + 1) != std::string::npos || !std::all_of(format.begin(), format.end(), token))
            throw std::invalid_argument(
                "Custom clipboard formats require an ASCII MIME type/subtype without parameters.");
        auto canonical = format;
        for (auto& c : canonical)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        if (canonical == "text/plain" || canonical == "text/html" || !names.insert(canonical).second)
            throw std::invalid_argument(
                "Custom clipboard MIME names must be distinct ignoring ASCII case and cannot replace text formats.");
        consume(format.size());
        consume(bytes.size());
    }
}

bool ClipboardData::IsEmpty() const
{
    return !m_Text && !m_Html && m_Files.empty() && m_Custom.empty();
}
} // namespace EStudioUI
