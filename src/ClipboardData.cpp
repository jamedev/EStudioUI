#include <EStudioUI/ClipboardData.h>

#include <utf8proc.h>

#include <algorithm>
#include <stdexcept>

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
    for (const auto& [format, bytes] : m_Custom)
    {
        if (format.empty() || format.size() > 128 || format.find('/') == std::string::npos ||
            !std::all_of(format.begin(), format.end(), [](unsigned char c) { return c > 32 && c < 127; }) ||
            format == "text/plain" || format == "text/html")
            throw std::invalid_argument("Custom clipboard formats require a distinct printable ASCII MIME name.");
        consume(format.size());
        consume(bytes.size());
    }
}

bool ClipboardData::IsEmpty() const
{
    return !m_Text && !m_Html && m_Files.empty() && m_Custom.empty();
}
} // namespace EStudioUI
